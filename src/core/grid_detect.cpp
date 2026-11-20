// Grid detection pipeline
// 1. Work on a down-scaled copy (long side <= 1600 px).
// 2. Multi-scale Hessian corner candidates:
//      checkerboard -> saddle points (det H < 0)
//      line grid    -> line crossings = blobs of the line polarity
// 3. From strong seeds, find a unit cell (4 corners, edges on lines) and grow
//    the lattice by extrapolation, validating every edge against the image.
// 4. Prune weakly connected nodes, fit a homography, fill holes / extend.
// 5. Back to full resolution, sub-pixel refinement (line fitting for line
//    grids, gradient saddle refinement for checkerboards), outlier rejection.
#include "grid_detect.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <cstdio>
#include <cstdlib>
#include <set>

#include "parallel.h"

namespace cp {
namespace {

using Key = std::pair<int, int>;

struct Cand {
    Pt p;
    float r;
};

// ---------------------------------------------------------------------------
std::vector<Cand> findCandidates(const ImageF& b, double sigma, PatternType type, int polarity) {
    const int w = b.w, h = b.h;
    ImageF R;
    R.resize(w, h, 1);
    const double s2 = sigma * sigma;
    parallelFor(h, [&](int y) {
        if (y < 1 || y >= h - 1) return;
        for (int x = 1; x < w - 1; ++x) {
            const double c = b.at(x, y);
            const double ixx = b.at(x + 1, y) - 2 * c + b.at(x - 1, y);
            const double iyy = b.at(x, y + 1) - 2 * c + b.at(x, y - 1);
            const double ixy = 0.25 * (b.at(x + 1, y + 1) - b.at(x + 1, y - 1) - b.at(x - 1, y + 1) + b.at(x - 1, y - 1));
            double r = 0;
            if (type == PatternType::Checkerboard) {
                // Saddle: curvatures of opposite sign, both strong (edges have one ~0).
                const double tr = 0.5 * (ixx + iyy);
                const double dd = std::sqrt(0.25 * (ixx - iyy) * (ixx - iyy) + ixy * ixy);
                const double l1 = tr - dd, l2 = tr + dd;
                r = (l1 < 0 && l2 > 0) ? std::min(-l1, l2) * s2 : 0;
            } else {
                const double tr = 0.5 * (ixx + iyy);
                const double dd = std::sqrt(0.25 * (ixx - iyy) * (ixx - iyy) + ixy * ixy);
                const double lmin = tr - dd, lmax = tr + dd;
                // A crossing of two lines curves (almost) equally in both
                // directions; a plain line has one curvature ~0.
                if (polarity > 0) r = (lmax < 0 && -lmax > 0.3 * -lmin) ? -lmax * s2 : 0;
                else r = (lmin > 0 && lmin > 0.3 * lmax) ? lmin * s2 : 0;
            }
            R.at(x, y) = float(r);
        }
    });
    const int rad = std::max(2, int(std::lround(1.5 * sigma)));
    const int border = std::max(rad + 1, int(std::ceil(2 * sigma)) + 2);
    std::vector<std::vector<Cand>> rows(static_cast<size_t>(h));
    parallelFor(h, [&](int y) {
        if (y < border || y >= h - border) return;
        for (int x = border; x < w - border; ++x) {
            const float v = R.at(x, y);
            if (v <= 0) continue;
            if (R.at(x - 1, y) > v || R.at(x + 1, y) > v || R.at(x, y - 1) > v || R.at(x, y + 1) > v) continue;
            bool isMax = true;
            for (int dy = -rad; dy <= rad && isMax; ++dy)
                for (int dx = -rad; dx <= rad; ++dx) {
                    if (!dx && !dy) continue;
                    const float u = R.at(x + dx, y + dy);
                    if (u > v || (u == v && (dy < 0 || (dy == 0 && dx < 0)))) { isMax = false; break; }
                }
            if (!isMax) continue;
            // Quadratic sub-pixel peak.
            const double l = R.at(x - 1, y), r = R.at(x + 1, y), u = R.at(x, y - 1), d = R.at(x, y + 1);
            double ox = 0, oy = 0;
            const double denx = 2 * v - l - r, deny = 2 * v - u - d;
            if (denx > 1e-12) ox = std::clamp(0.5 * (r - l) / denx, -0.5, 0.5);
            if (deny > 1e-12) oy = std::clamp(0.5 * (d - u) / deny, -0.5, 0.5);
            rows[size_t(y)].push_back({Pt(x + ox, y + oy), v});
        }
    });
    std::vector<Cand> peaks;
    for (auto& r : rows) peaks.insert(peaks.end(), r.begin(), r.end());
    if (peaks.empty()) return peaks;
    std::sort(peaks.begin(), peaks.end(), [](const Cand& a, const Cand& b) { return a.r > b.r; });
    const size_t refIdx = std::min<size_t>(peaks.size(), 40) / 2;
    const float thr = 0.12f * peaks[refIdx].r;
    size_t keep = 0;
    while (keep < peaks.size() && peaks[keep].r > thr) ++keep;
    peaks.resize(std::min<size_t>(keep, 6000));
    return peaks;
}

// ---------------------------------------------------------------------------
// Line-grid candidates from straight lines: multi-scale ridge detection,
// orientation-gated Hough transform, two direction families, intersections.
// Robust against textured backgrounds (cutting mats) where blob-like corner
// detectors fire everywhere.
std::vector<Cand> houghCandidates(const ImageF& work, int polarity, std::vector<std::pair<Pt, Pt>>* linesOut) {
    const int w = work.w, h = work.h;
    ImageF S, A;  // ridge strength and normal angle
    S.resize(w, h, 1);
    A.resize(w, h, 1);
    for (double sigma : {1.5, 2.5, 4.0, 6.0}) {
        if (sigma > std::min(w, h) / 40.0) continue;
        const ImageF b = gaussianBlur(work, sigma);
        const double s2 = sigma * sigma;
        parallelFor(h, [&](int y) {
            if (y < 1 || y >= h - 1) return;
            for (int x = 1; x < w - 1; ++x) {
                const double c = b.at(x, y);
                const double ixx = b.at(x + 1, y) - 2 * c + b.at(x - 1, y);
                const double iyy = b.at(x, y + 1) - 2 * c + b.at(x, y - 1);
                const double ixy = 0.25 * (b.at(x + 1, y + 1) - b.at(x + 1, y - 1) - b.at(x - 1, y + 1) + b.at(x - 1, y - 1));
                const double tr = 0.5 * (ixx + iyy);
                const double dd = std::sqrt(0.25 * (ixx - iyy) * (ixx - iyy) + ixy * ixy);
                const double l1 = tr - dd, l2 = tr + dd;
                double s;
                double ang = 0.5 * std::atan2(2 * ixy, ixx - iyy);  // eigenvector of l2
                if (polarity > 0) { s = -l1 - std::fabs(l2); ang += 1.5707963267948966; }
                else s = l2 - std::fabs(l1);
                s *= s2;
                if (s > S.at(x, y)) { S.at(x, y) = float(s); A.at(x, y) = float(ang); }
            }
        });
    }
    // Ridge pixels: non-maximum suppression across the line + threshold.
    std::vector<float> sv;
    for (float v : S.d)
        if (v > 0) sv.push_back(v);
    if (sv.size() < 100) return {};
    std::nth_element(sv.begin(), sv.begin() + size_t(sv.size() * 0.99), sv.end());
    const float thr = 0.2f * sv[size_t(sv.size() * 0.99)];
    // Votes measure line *length*: a very bright short ridge (a specular
    // highlight on the wire) must not outvote a long, dimmer grid line.
    const float cap = 1.5f * thr;
    const double D = 0.5 * std::hypot(w, h);
    const double cx = 0.5 * w, cy = 0.5 * h;
    const int NT = 360;  // 0.5 degree bins over [0, pi)
    const int NR = int(2 * D) + 3;
    std::vector<float> acc(size_t(NT) * NR, 0.f);
    const double dT = 3.14159265358979323846 / NT;
    for (int y = 2; y < h - 2; ++y)
        for (int x = 2; x < w - 2; ++x) {
            const float v = S.at(x, y);
            if (v < thr) continue;
            const double ang = A.at(x, y);
            const double nx = std::cos(ang), ny = std::sin(ang);
            if (S.bilinear(x + nx, y + ny) > v || S.bilinear(x - nx, y - ny) > v) continue;
            double a0 = std::fmod(ang, 3.14159265358979323846);
            if (a0 < 0) a0 += 3.14159265358979323846;
            const int t0 = int(std::lround(a0 / dT));
            for (int dt = -4; dt <= 4; ++dt) {
                const int t = ((t0 + dt) % NT + NT) % NT;
                // Wrapping past pi flips the sign of rho.
                const double th = t * dT;
                const double rho = (x - cx) * std::cos(th) + (y - cy) * std::sin(th);
                const int r = int(std::lround(rho + D + 1));
                if (r >= 0 && r < NR) acc[size_t(t) * NR + r] += std::min(v, cap) / cap;
            }
        }
    if (const char* dd = std::getenv("CP_DEBUG_DIR")) {
        const std::string fn = std::string(dd) + "/ridge_" + (polarity > 0 ? "b" : "d") + ".pgm";
        if (FILE* fp = std::fopen(fn.c_str(), "wb")) {
            std::fprintf(fp, "P5 %d %d 255\n", w, h);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const float v = S.at(x, y);
                    std::fputc(v >= thr ? 255 : int(std::min(127.f, 127.f * v / thr)), fp);
                }
            std::fclose(fp);
        }
    }
    // Lines rarely fall exactly on a (theta, rho) bin: sum over a small
    // neighbourhood so a long line's votes are not split across bins.
    {
        std::vector<float> tmp(acc.size(), 0.f);
        for (int t = 0; t < NT; ++t)
            for (int r = 2; r < NR - 2; ++r) {
                float s = 0;
                for (int dr = -2; dr <= 2; ++dr) s += acc[size_t(t) * NR + r + dr];
                tmp[size_t(t) * NR + r] = s;
            }
        for (int t = 0; t < NT; ++t)
            for (int r = 0; r < NR; ++r) {
                const int tp = (t + 1) % NT, tm = (t + NT - 1) % NT;
                // Neighbouring theta bins across the 0/pi seam use mirrored rho.
                const int rp = (t + 1 == NT) ? NR - 1 - r : r, rm = (t == 0) ? NR - 1 - r : r;
                acc[size_t(t) * NR + r] = tmp[size_t(t) * NR + r] + 0.5f * (tmp[size_t(tp) * NR + rp] + tmp[size_t(tm) * NR + rm]);
            }
    }
    // Peaks.
    struct Peak { int t, r; float v; };
    std::vector<Peak> peaks;
    const int wt = 4, wr = 10;
    for (int t = 0; t < NT; ++t)
        for (int r = 1; r < NR - 1; ++r) {
            const float v = acc[size_t(t) * NR + r];
            if (v <= 0) continue;
            bool isMax = true;
            for (int dt = -wt; dt <= wt && isMax; ++dt) {
                int tt = t + dt, rr0 = r;
                if (tt < 0) { tt += NT; rr0 = NR - 1 - r; }
                if (tt >= NT) { tt -= NT; rr0 = NR - 1 - r; }
                for (int dr = -wr; dr <= wr; ++dr) {
                    const int rr = rr0 + dr;
                    if (rr < 0 || rr >= NR || (!dt && !dr)) continue;
                    const float u = acc[size_t(tt) * NR + rr];
                    if (u > v || (u == v && (dt < 0 || (dt == 0 && dr < 0)))) { isMax = false; break; }
                }
            }
            if (isMax) peaks.push_back({t, r, v});
        }
    if (peaks.size() < 4) return {};
    std::sort(peaks.begin(), peaks.end(), [](const Peak& a, const Peak& b) { return a.v > b.v; });
    const float ref = peaks[std::min<size_t>(3, peaks.size() - 1)].v;
    std::vector<Peak> lines;
    for (const Peak& p : peaks)
        if (p.v > 0.25f * ref && p.v > 0.2f * std::min(w, h) && lines.size() < 80) lines.push_back(p);
    // Two direction families from a vote-weighted angle histogram.
    auto angDist = [&](int a, int b) { int d = std::abs(a - b) % NT; return std::min(d, NT - d); };
    std::vector<double> hist(size_t(NT), 0);
    for (const Peak& p : lines)
        for (int t = 0; t < NT; ++t)
            if (angDist(t, p.t) <= 16) hist[size_t(t)] += p.v;
    int f1 = int(std::max_element(hist.begin(), hist.end()) - hist.begin());
    int f2 = -1;
    for (int t = 0; t < NT; ++t)
        if (angDist(t, f1) >= 60 && (f2 < 0 || hist[size_t(t)] > hist[size_t(f2)])) f2 = t;
    if (f2 < 0) return {};
    std::vector<Peak> famA, famB;
    for (const Peak& p : lines) {
        if (angDist(p.t, f1) <= 30) famA.push_back(p);
        else if (angDist(p.t, f2) <= 30) famB.push_back(p);
    }
    if (std::getenv("CP_DEBUG")) {
        std::fprintf(stderr, "  hough pol %+d: %zu peaks, ref %.0f, f1 %.1f f2 %.1f\n", polarity, peaks.size(), ref, f1 * 0.5, f2 * 0.5);
        for (size_t i = 0; i < std::min<size_t>(peaks.size(), 40); ++i)
            std::fprintf(stderr, "    th %.1f rho %.0f v %.0f%s\n", peaks[i].t * 0.5, peaks[i].r - D - 1, peaks[i].v,
                         peaks[i].v > 0.25f * ref ? "" : " (weak)");
    }
    auto lineOf = [&](const Peak& p, Pt& n, double& rho) {
        const double th = p.t * dT;
        n = Pt(std::cos(th), std::sin(th));
        rho = p.r - D - 1;
    };
    // Per family: (1) keep lines consistent with one vanishing point (RANSAC
    // over line pairs) - removes fans of lines through bright blobs;
    // (2) drop the weaker of two lines much closer than the typical spacing.
    auto filterFamily = [&](std::vector<Peak>& fam) {
        if (fam.size() >= 3) {
            const double tol = 2.5 * 3.14159265358979323846 / 180;
            auto consistent = [&](const Peak& l, double vx, double vy, double vz) {
                Pt n;
                double rho;
                lineOf(l, n, rho);
                const Pt d(-n.y, n.x), p = n * rho;
                Pt dir;
                if (std::fabs(vz) < 1e-9 * std::hypot(vx, vy)) dir = Pt(vx, vy);
                else dir = Pt(vx / vz, vy / vz) - p;
                const double L = norm(dir);
                if (L < 1e-9) return true;
                return std::fabs(cross(d, dir)) / L < std::sin(tol);
            };
            double bestScore = -1;
            std::vector<char> bestIn;
            for (size_t i = 0; i < fam.size(); ++i)
                for (size_t j = i + 1; j < fam.size(); ++j) {
                    Pt ni, nj;
                    double ri, rj;
                    lineOf(fam[i], ni, ri);
                    lineOf(fam[j], nj, rj);
                    // v = li x lj with l = (nx, ny, -rho)
                    const double vx = ni.y * (-rj) - (-ri) * nj.y;
                    const double vy = (-ri) * nj.x - ni.x * (-rj);
                    const double vz = ni.x * nj.y - ni.y * nj.x;
                    double score = 0;
                    std::vector<char> in(fam.size(), 0);
                    for (size_t k = 0; k < fam.size(); ++k)
                        if (consistent(fam[k], vx, vy, vz)) { in[k] = 1; score += fam[k].v; }
                    if (score > bestScore) { bestScore = score; bestIn = in; }
                }
            std::vector<Peak> keep;
            for (size_t k = 0; k < fam.size(); ++k)
                if (bestIn[k]) keep.push_back(fam[k]);
            fam = keep;
        }
        if (fam.size() >= 3) {
            Pt mean(0, 0);
            for (const Peak& p : fam) { Pt n; double r; lineOf(p, n, r); if (dot(n, Pt(std::cos(fam[0].t * dT), std::sin(fam[0].t * dT))) < 0) n = n * -1; mean += n; }
            mean = mean / std::max(1e-9, norm(mean));
            bool changed = true;
            while (changed && fam.size() >= 3) {
                changed = false;
                std::vector<std::pair<double, size_t>> pos;
                for (size_t k = 0; k < fam.size(); ++k) {
                    Pt n;
                    double r;
                    lineOf(fam[k], n, r);
                    pos.push_back({r / dot(n, mean), k});
                }
                std::sort(pos.begin(), pos.end());
                std::vector<double> gaps;
                for (size_t k = 1; k < pos.size(); ++k) gaps.push_back(pos[k].first - pos[k - 1].first);
                std::vector<double> sg = gaps;
                std::sort(sg.begin(), sg.end());
                const double med = sg[sg.size() / 2];
                for (size_t k = 0; k < gaps.size(); ++k)
                    if (gaps[k] < 0.35 * med) {
                        const size_t a = pos[k].second, b = pos[k + 1].second;
                        fam.erase(fam.begin() + long(fam[a].v < fam[b].v ? a : b));
                        changed = true;
                        break;
                    }
            }
        }
    };
    filterFamily(famA);
    filterFamily(famB);
    if (std::getenv("CP_DEBUG")) {
        for (const auto* fam : {&famA, &famB}) {
            std::fprintf(stderr, "    family:");
            for (const Peak& p : *fam) std::fprintf(stderr, " (%.1f,%.0f,%.0f)", p.t * 0.5, p.r - D - 1, p.v);
            std::fprintf(stderr, "\n");
        }
    }
    if (famA.size() < 2 || famB.size() < 2) return {};
    std::vector<Cand> out;
    for (const Peak& a : famA)
        for (const Peak& b : famB) {
            Pt na, nb;
            double ra, rb;
            lineOf(a, na, ra);
            lineOf(b, nb, rb);
            const double det = na.x * nb.y - na.y * nb.x;
            if (std::fabs(det) < 0.3) continue;
            const double x = (ra * nb.y - rb * na.y) / det + cx;
            const double y = (na.x * rb - nb.x * ra) / det + cy;
            if (x < 3 || y < 3 || x > w - 4 || y > h - 4) continue;
            out.push_back({Pt(x, y), std::sqrt(a.v * b.v)});
        }
    if (linesOut) {
        for (const auto* fam : {&famA, &famB})
            for (const Peak& p : *fam) {
                Pt n;
                double rho;
                lineOf(p, n, rho);
                const Pt c = Pt(cx, cy) + n * rho, d(-n.y, n.x);
                linesOut->push_back({c - d * D * 2, c + d * D * 2});
            }
    }
    std::sort(out.begin(), out.end(), [](const Cand& a, const Cand& b) { return a.r > b.r; });
    return out;
}

// ---------------------------------------------------------------------------
struct SpatialIndex {
    const std::vector<Pt>* pts = nullptr;
    double cell = 24;
    int gw = 1, gh = 1;
    std::vector<std::vector<int>> buckets;

    SpatialIndex(const std::vector<Pt>& p, int w, int h, double c) : pts(&p), cell(c) {
        gw = std::max(1, int(std::ceil(w / cell)) + 1);
        gh = std::max(1, int(std::ceil(h / cell)) + 1);
        buckets.resize(size_t(gw) * gh);
        for (size_t i = 0; i < p.size(); ++i) {
            const int bx = std::clamp(int(p[i].x / cell), 0, gw - 1), by = std::clamp(int(p[i].y / cell), 0, gh - 1);
            buckets[size_t(by) * gw + bx].push_back(int(i));
        }
    }
    int nearest(Pt q, double maxD, const std::vector<char>* used = nullptr) const {
        const int r = int(std::ceil(maxD / cell));
        const int cx = int(std::floor(q.x / cell)), cy = int(std::floor(q.y / cell));
        int best = -1;
        double bd = maxD * maxD;
        for (int by = std::max(0, cy - r); by <= std::min(gh - 1, cy + r); ++by)
            for (int bx = std::max(0, cx - r); bx <= std::min(gw - 1, cx + r); ++bx)
                for (int i : buckets[size_t(by) * gw + bx]) {
                    if (used && (*used)[size_t(i)]) continue;
                    const Pt d = (*pts)[size_t(i)] - q;
                    const double d2 = dot(d, d);
                    if (d2 < bd) { bd = d2; best = i; }
                }
        return best;
    }
    // All points within maxD, nearest first.
    std::vector<int> within(Pt q, double maxD, const std::vector<char>* used = nullptr) const {
        const int r = int(std::ceil(maxD / cell));
        const int cx = int(std::floor(q.x / cell)), cy = int(std::floor(q.y / cell));
        std::vector<std::pair<double, int>> found;
        for (int by = std::max(0, cy - r); by <= std::min(gh - 1, cy + r); ++by)
            for (int bx = std::max(0, cx - r); bx <= std::min(gw - 1, cx + r); ++bx)
                for (int i : buckets[size_t(by) * gw + bx]) {
                    if (used && (*used)[size_t(i)]) continue;
                    const double d = dist((*pts)[size_t(i)], q);
                    if (d < maxD) found.push_back({d, i});
                }
        std::sort(found.begin(), found.end());
        std::vector<int> out;
        for (auto& f : found) out.push_back(f.second);
        return out;
    }
};

// Checks that the straight segment a-b runs along a drawn line of the given
// polarity (line grids only).
struct EdgeChecker {
    const ImageF* g = nullptr;
    int polarity = 1;
    double minContrast = 8;
    bool valid(Pt a, Pt b) const {
        const Pt d = b - a;
        const double L = norm(d);
        if (L < 4) return false;
        const Pt n(-d.y / L, d.x / L);
        const double off = 0.2 * L;
        int good = 0;
        for (int k = 0; k < 9; ++k) {
            const Pt p = a + d * (0.2 + 0.6 * k / 8.0);
            const double on = g->bilinear(p.x, p.y);
            const Pt p1 = p + n * off, p2 = p - n * off;
            const double o1 = g->bilinear(p1.x, p1.y), o2 = g->bilinear(p2.x, p2.y);
            if (polarity * (on - o1) > minContrast * 0.5 && polarity * (on - o2) > minContrast * 0.5 &&
                polarity * (on - 0.5 * (o1 + o2)) > minContrast)
                ++good;
        }
        return good >= 8;
    }
};

const Key kDirs[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

bool seedVectors(const std::vector<Pt>& pts, const SpatialIndex& si, int s, const EdgeChecker* ec, Pt& v1, Pt& v2) {
    const Pt P = pts[size_t(s)];
    std::vector<std::pair<double, int>> nb;
    nb.reserve(pts.size());
    for (size_t i = 0; i < pts.size(); ++i)
        if (int(i) != s) nb.push_back({dist(pts[i], P), int(i)});
    const size_t K = std::min<size_t>(nb.size(), 10);
    std::partial_sort(nb.begin(), nb.begin() + K, nb.end());
    double bestScore = 1e300;
    bool found = false;
    for (size_t a = 0; a < K; ++a) {
        const Pt va = pts[size_t(nb[a].second)] - P;
        const double la = norm(va);
        if (la < 3) continue;
        if (ec && !ec->valid(P, pts[size_t(nb[a].second)])) continue;
        for (size_t b = 0; b < K; ++b) {
            if (b == a) continue;
            const Pt vb = pts[size_t(nb[b].second)] - P;
            const double lb = norm(vb);
            if (lb < 3 || lb / la < 0.4 || lb / la > 2.5) continue;
            if (std::fabs(cross(va, vb)) / (la * lb) < 0.5) continue;
            const double score = la + lb;
            if (score >= bestScore) continue;
            if (ec && !ec->valid(P, pts[size_t(nb[b].second)])) continue;
            const int dgi = si.nearest(P + va + vb, 0.25 * std::max(la, lb));
            if (dgi < 0) continue;
            if (ec && (!ec->valid(pts[size_t(nb[a].second)], pts[size_t(dgi)]) ||
                       !ec->valid(pts[size_t(nb[b].second)], pts[size_t(dgi)])))
                continue;
            bestScore = score;
            v1 = va;
            v2 = vb;
            found = true;
        }
    }
    return found;
}

using NodeMap = std::map<Key, int>;

NodeMap grow(const std::vector<Pt>& pts, const SpatialIndex& si, int seed, Pt v1, Pt v2, const EdgeChecker* ec,
             std::vector<char>& used) {
    NodeMap nodes;
    used.assign(pts.size(), 0);
    nodes[{0, 0}] = seed;
    used[size_t(seed)] = 1;
    std::deque<Key> q{{0, 0}};
    const Pt base[4] = {v1, v1 * -1, v2, v2 * -1};
    while (!q.empty()) {
        const Key cur = q.front();
        q.pop_front();
        const Pt P = pts[size_t(nodes[cur])];
        for (int d = 0; d < 4; ++d) {
            const Key t{cur.first + kDirs[d].first, cur.second + kDirs[d].second};
            if (nodes.count(t)) continue;
            Pt step = base[d];
            const Key prev{cur.first - kDirs[d].first, cur.second - kDirs[d].second};
            auto ip = nodes.find(prev);
            if (ip != nodes.end()) {
                step = P - pts[size_t(ip->second)];
            } else {
                bool got = false;
                for (int a = -1; a <= 1 && !got; ++a)
                    for (int b = -1; b <= 1 && !got; ++b) {
                        if (!a && !b) continue;
                        const Key k0{cur.first + a, cur.second + b};
                        const Key k1{k0.first + kDirs[d].first, k0.second + kDirs[d].second};
                        auto i0 = nodes.find(k0), i1 = nodes.find(k1);
                        if (i0 != nodes.end() && i1 != nodes.end()) {
                            step = pts[size_t(i1->second)] - pts[size_t(i0->second)];
                            got = true;
                        }
                    }
            }
            const double L = norm(step);
            int k = -1;
            for (int c : si.within(P + step, 0.22 * L, &used)) {
                const double len = dist(pts[size_t(c)], P);
                if (len < 0.75 * L || len > 1.33 * L) continue;
                if (ec && !ec->valid(P, pts[size_t(c)])) continue;
                k = c;
                break;
            }
            if (k < 0) continue;
            nodes[t] = k;
            used[size_t(k)] = 1;
            q.push_back(t);
        }
    }
    return nodes;
}

// Line grids: drop nodes with fewer than two line-validated edges, then keep
// the largest connected component of the validated-edge graph.
void pruneLineGrid(NodeMap& nodes, const std::vector<Pt>& pts, const EdgeChecker& ec, std::vector<char>& used) {
    std::map<std::pair<Key, Key>, bool> cache;
    auto edgeOk = [&](const Key& a, const Key& b) {
        const auto key = a < b ? std::make_pair(a, b) : std::make_pair(b, a);
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;
        const bool v = ec.valid(pts[size_t(nodes[a])], pts[size_t(nodes[b])]);
        cache[key] = v;
        return v;
    };
    bool changed = true;
    while (changed) {
        changed = false;
        std::vector<Key> drop;
        for (auto& [k, idx] : nodes) {
            int deg = 0;
            for (const Key& d : kDirs) {
                const Key n{k.first + d.first, k.second + d.second};
                if (nodes.count(n) && edgeOk(k, n)) ++deg;
            }
            if (deg < 2) drop.push_back(k);
        }
        for (const Key& k : drop) {
            used[size_t(nodes[k])] = 0;
            nodes.erase(k);
            changed = true;
        }
    }
    // Largest connected component.
    std::set<Key> seen;
    std::vector<Key> bestComp;
    for (auto& [k, idx] : nodes) {
        if (seen.count(k)) continue;
        std::vector<Key> comp, st{k};
        seen.insert(k);
        while (!st.empty()) {
            const Key c = st.back();
            st.pop_back();
            comp.push_back(c);
            for (const Key& d : kDirs) {
                const Key n{c.first + d.first, c.second + d.second};
                if (nodes.count(n) && !seen.count(n) && edgeOk(c, n)) {
                    seen.insert(n);
                    st.push_back(n);
                }
            }
        }
        if (comp.size() > bestComp.size()) bestComp = comp;
    }
    std::set<Key> keepSet(bestComp.begin(), bestComp.end());
    for (auto it = nodes.begin(); it != nodes.end();) {
        if (!keepSet.count(it->first)) {
            used[size_t(it->second)] = 0;
            it = nodes.erase(it);
        } else {
            ++it;
        }
    }
}

bool spansTwoAxes(const std::vector<Key>& keys) {
    std::set<int> is, js;
    for (const Key& k : keys) { is.insert(k.first); js.insert(k.second); }
    return is.size() >= 2 && js.size() >= 2;
}

template <class Map, class GetPt>
bool fitLattice(const Map& nodes, GetPt get, Mat3& H) {
    std::vector<Pt> src, dst;
    std::vector<Key> keys;
    for (auto& kv : nodes) {
        src.emplace_back(kv.first.first, kv.first.second);
        dst.push_back(get(kv.second));
        keys.push_back(kv.first);
    }
    if (src.size() < 4 || !spansTwoAxes(keys)) return false;
    return computeHomography(src, dst, H);
}

double medianOf(std::vector<double> v) {
    if (v.empty()) return 0;
    std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
    return v[v.size() / 2];
}

// Removes nodes whose position disagrees with the lattice homography, worst
// first, refitting after every removal (robust to a minority of bad nodes).
// Threshold: max(minThr, relStep * median lattice step, 4 * median residual).
template <class Map, class GetPt, class OnErase>
void rejectOutliers(Map& nodes, GetPt get, double minThr, OnErase onErase, double relStep = 0.08) {
    for (int guard = 0; guard < 100000 && nodes.size() > 4; ++guard) {
        Mat3 H;
        if (!fitLattice(nodes, get, H)) return;
        std::vector<double> res, steps;
        double worst = -1;
        auto worstIt = nodes.end();
        for (auto it = nodes.begin(); it != nodes.end(); ++it) {
            const Pt l(it->first.first, it->first.second);
            const double r = dist(H.apply(l), get(it->second));
            res.push_back(r);
            steps.push_back(dist(H.apply(l), H.apply(l + Pt(1, 0))));
            steps.push_back(dist(H.apply(l), H.apply(l + Pt(0, 1))));
            if (r > worst) { worst = r; worstIt = it; }
        }
        const double thr = std::max({minThr, relStep * medianOf(steps), 4 * medianOf(res)});
        if (worst <= thr) return;
        onErase(worstIt->second);
        nodes.erase(worstIt);
    }
}

void fillExtend(NodeMap& nodes, const std::vector<Pt>& pts, const SpatialIndex& si, const EdgeChecker* ec,
                std::vector<char>& used, int imgW, int imgH) {
    auto get = [&](int idx) { return pts[size_t(idx)]; };
    auto onErase = [&](int idx) { used[size_t(idx)] = 0; };
    for (int iter = 0; iter < 12; ++iter) {
        rejectOutliers(nodes, get, 2.0, onErase);
        Mat3 H;
        if (!fitLattice(nodes, get, H)) return;
        int mini = 1 << 30, maxi = -(1 << 30), minj = 1 << 30, maxj = -(1 << 30);
        for (auto& kv : nodes) {
            mini = std::min(mini, kv.first.first); maxi = std::max(maxi, kv.first.first);
            minj = std::min(minj, kv.first.second); maxj = std::max(maxj, kv.first.second);
        }
        int added = 0;
        for (int i = mini - 1; i <= maxi + 1; ++i)
            for (int j = minj - 1; j <= maxj + 1; ++j) {
                const Key k{i, j};
                if (nodes.count(k)) continue;
                bool ok1 = false;
                const Pt pred = H.apply(Pt(i, j), &ok1);
                if (!ok1 || pred.x < 2 || pred.y < 2 || pred.x > imgW - 3 || pred.y > imgH - 3) continue;
                const double step = std::min(dist(H.apply(Pt(i + 1, j)), pred), dist(H.apply(Pt(i, j + 1)), pred));
                if (!(step > 3)) continue;
                std::vector<Key> nbrs;
                for (const Key& d : kDirs) {
                    const Key n{i + d.first, j + d.second};
                    if (nodes.count(n)) nbrs.push_back(n);
                }
                if (nbrs.empty()) continue;
                int c = -1;
                for (int cc : si.within(pred, 0.2 * step, &used)) {
                    bool anyOk = !ec;
                    for (size_t q = 0; q < nbrs.size() && !anyOk; ++q)
                        if (ec->valid(pts[size_t(nodes[nbrs[q]])], pts[size_t(cc)])) anyOk = true;
                    if (anyOk) { c = cc; break; }
                }
                if (c < 0) continue;
                nodes[k] = c;
                used[size_t(c)] = 1;
                ++added;
            }
        if (!added) break;
    }
    rejectOutliers(nodes, get, 2.0, onErase);
}

// Re-index so +i points roughly along image +x and the frame is not mirrored.
std::map<Key, Pt> normaliseOrientation(const std::map<Key, Pt>& in) {
    Pt di(0, 0), dj(0, 0);
    for (auto& [k, p] : in) {
        auto a = in.find({k.first + 1, k.second});
        if (a != in.end()) di += a->second - p;
        auto b = in.find({k.first, k.second + 1});
        if (b != in.end()) dj += b->second - p;
    }
    di = di / std::max(1e-9, norm(di));
    dj = dj / std::max(1e-9, norm(dj));
    int A, B, C, D;  // newI = A*i + B*j ; newJ = C*i + D*j
    const Pt cand[4] = {di, di * -1, dj, dj * -1};
    int bi = 0;
    for (int q = 1; q < 4; ++q)
        if (cand[q].x > cand[bi].x) bi = q;
    const Pt u = cand[bi];
    if (bi < 2) {
        const int s = bi == 0 ? 1 : -1;
        const int t = cross(u, dj) > 0 ? 1 : -1;
        A = s; B = 0; C = 0; D = t;
    } else {
        const int s = bi == 2 ? 1 : -1;
        const int t = cross(u, di) > 0 ? 1 : -1;
        A = 0; B = s; C = t; D = 0;
    }
    std::map<Key, Pt> tmp;
    int mi = 1 << 30, mj = 1 << 30;
    for (auto& [k, p] : in) {
        const Key nk{A * k.first + B * k.second, C * k.first + D * k.second};
        tmp[nk] = p;
        mi = std::min(mi, nk.first);
        mj = std::min(mj, nk.second);
    }
    std::map<Key, Pt> out;
    for (auto& [k, p] : tmp) out[{k.first - mi, k.second - mj}] = p;
    return out;
}

// Sub-pixel refinement of a line crossing by fitting both lines locally.
bool refineLineCrossing(const ImageF& g, Pt& p, Pt dirA, Pt dirB, double step, int polarity) {
    Pt cur = p;
    for (int pass = 0; pass < 2; ++pass) {
        Pt lineC[2], lineD[2];
        bool okLine[2] = {false, false};
        for (int f = 0; f < 2; ++f) {
            Pt dir = f == 0 ? dirA : dirB;
            dir = dir / norm(dir);
            const Pt n(-dir.y, dir.x);
            const double R = std::max(6.0, 0.1 * step);
            const int nu = std::max(12, int(R));
            std::vector<Pt> samples;
            std::vector<double> vals(size_t(2 * nu + 1));
            for (int side = -1; side <= 1; side += 2)
                for (int q = 0; q < 10; ++q) {
                    const double t = side * step * (0.1 + 0.32 * q / 9.0);
                    const Pt s = cur + dir * t;
                    for (int k = -nu; k <= nu; ++k) {
                        const Pt pp = s + n * (R * k / nu);
                        vals[size_t(k + nu)] = polarity * g.bilinear(pp.x, pp.y);
                    }
                    std::vector<double> sorted = vals;
                    std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
                    const double base = sorted[sorted.size() / 2];
                    double mx = -1e9;
                    int kmax = 0;
                    for (int k = 0; k <= 2 * nu; ++k)
                        if (vals[size_t(k)] > mx) { mx = vals[size_t(k)]; kmax = k; }
                    if (mx - base < 12) continue;
                    // Centroid of the peak region (above half contrast) around the maximum.
                    const double half = base + 0.5 * (mx - base);
                    int a = kmax, b = kmax;
                    while (a > 0 && vals[size_t(a - 1)] > half) --a;
                    while (b < 2 * nu && vals[size_t(b + 1)] > half) ++b;
                    if (a == 0 || b == 2 * nu) continue;  // peak not enclosed: unreliable
                    double sw = 0, su = 0;
                    for (int k = a; k <= b; ++k) {
                        const double wgt = vals[size_t(k)] - base;
                        sw += wgt;
                        su += wgt * (R * (k - nu) / nu);
                    }
                    if (sw <= 0) continue;
                    samples.push_back(s + n * (su / sw));
                }
            for (int rep = 0; rep < 2 && samples.size() >= 4; ++rep) {
                Pt c(0, 0);
                for (const Pt& s : samples) c += s;
                c = c / double(samples.size());
                double sxx = 0, sxy = 0, syy = 0;
                for (const Pt& s : samples) {
                    const Pt d = s - c;
                    sxx += d.x * d.x; sxy += d.x * d.y; syy += d.y * d.y;
                }
                const double ang = 0.5 * std::atan2(2 * sxy, sxx - syy);
                const Pt ld(std::cos(ang), std::sin(ang));
                lineC[f] = c;
                lineD[f] = ld;
                okLine[f] = true;
                // Drop outliers (e.g. where the wire crosses the line) and refit once.
                std::vector<double> res;
                for (const Pt& s : samples) res.push_back(std::fabs(cross(ld, s - c)));
                const double thr = std::max(1.0, 3 * medianOf(res));
                std::vector<Pt> keep;
                for (size_t i = 0; i < samples.size(); ++i)
                    if (res[i] <= thr) keep.push_back(samples[i]);
                if (keep.size() == samples.size()) break;
                samples = keep;
            }
        }
        if (!okLine[0] || !okLine[1]) return false;
        const double den = cross(lineD[0], lineD[1]);
        if (std::fabs(den) < 0.2) return false;
        const double t = cross(lineC[1] - lineC[0], lineD[1]) / den;
        const Pt x = lineC[0] + lineD[0] * t;
        if (dist(x, p) > 0.15 * step) return false;
        cur = x;
    }
    p = cur;
    return true;
}

// Gradient-based saddle refinement (checkerboard corners).
bool refineSaddle(const ImageF& g, Pt& p, int win) {
    Pt q = p;
    for (int it = 0; it < 12; ++it) {
        double a = 0, b = 0, c = 0, bx = 0, by = 0;
        const int ix = int(std::lround(q.x)), iy = int(std::lround(q.y));
        const double sg = win * 0.6;
        for (int dy = -win; dy <= win; ++dy)
            for (int dx = -win; dx <= win; ++dx) {
                const int x = ix + dx, y = iy + dy;
                if (x < 1 || y < 1 || x >= g.w - 1 || y >= g.h - 1) continue;
                const double gx = 0.5 * (g.at(x + 1, y) - g.at(x - 1, y));
                const double gy = 0.5 * (g.at(x, y + 1) - g.at(x, y - 1));
                const double wt = std::exp(-(dx * dx + dy * dy) / (2 * sg * sg));
                a += wt * gx * gx; b += wt * gx * gy; c += wt * gy * gy;
                bx += wt * (gx * gx * x + gx * gy * y);
                by += wt * (gx * gy * x + gy * gy * y);
            }
        const double det = a * c - b * b;
        if (det <= 1e-9) return false;
        const Pt nq((c * bx - b * by) / det, (a * by - b * bx) / det);
        if (dist(nq, p) > win) return false;
        const double mv = dist(nq, q);
        q = nq;
        if (mv < 0.01) break;
    }
    p = q;
    return true;
}

}  // namespace

GridResult detectGrid(const ImageF& full, PatternType type) {
    GridResult R;
    if (full.empty()) {
        R.message = "Empty image";
        return R;
    }
    const int k = std::max(1, int(std::ceil(std::max(full.w, full.h) / 1600.0)));
    const ImageF work = downscaleBlock(full, k);
    const ImageF work1 = gaussianBlur(work, 1.0);

    struct Best {
        NodeMap nodes;
        std::vector<Pt> pts;
        int polarity = 0;
        double sigma = 0;
    } best;

    static const bool dbg = std::getenv("CP_DEBUG") != nullptr;
    // Runs seeding / growing / pruning / filling on one candidate set and
    // keeps the largest consistent grid.
    auto tryCands = [&](const std::vector<Pt>& pts, int pol, double sigma) {
            if (pts.size() < 4) return;
            const SpatialIndex si(pts, work.w, work.h, 24);
            EdgeChecker ec{&work1, pol, 8.0};
            const EdgeChecker* pec = type == PatternType::LineGrid ? &ec : nullptr;

            // Seeds: strongest candidates, preferring the central region.
            std::vector<int> seeds;
            for (size_t i = 0; i < pts.size() && seeds.size() < 30; ++i) {
                const Pt& p = pts[i];
                if (p.x > 0.08 * work.w && p.x < 0.92 * work.w && p.y > 0.08 * work.h && p.y < 0.92 * work.h)
                    seeds.push_back(int(i));
            }
            NodeMap localBest;
            std::vector<char> used;
            for (int s : seeds) {
                Pt v1, v2;
                if (!seedVectors(pts, si, s, pec, v1, v2)) continue;
                NodeMap nodes = grow(pts, si, s, v1, v2, pec, used);
                if (pec) pruneLineGrid(nodes, pts, ec, used);
                if (nodes.size() > localBest.size()) localBest = nodes;
                if (localBest.size() > 0.5 * pts.size()) break;
            }
            if (dbg) {
                int nseedOk = 0;
                for (int s2 : seeds) { Pt a, b; if (seedVectors(pts, si, s2, pec, a, b)) ++nseedOk; }
                std::fprintf(stderr, "  sigma %.1f pol %+d: %zu cands, %zu seeds, %d with cells, best grow %zu\n", sigma, pol,
                             pts.size(), seeds.size(), nseedOk, localBest.size());
            }
            if (localBest.size() > best.nodes.size() && best.nodes.size() < 4) best.pts = pts;  // for diagnostics
            if (localBest.size() < 4) return;
            used.assign(pts.size(), 0);
            for (auto& kv : localBest) used[size_t(kv.second)] = 1;
            fillExtend(localBest, pts, si, pec, used, work.w, work.h);
            if (pec) pruneLineGrid(localBest, pts, ec, used);
            std::vector<Key> keys;
            for (auto& kv : localBest) keys.push_back(kv.first);
            if (localBest.size() < 4 || !spansTwoAxes(keys)) return;
            if (localBest.size() > best.nodes.size()) {
                best.nodes = localBest;
                best.pts = pts;
                best.polarity = pol;
                best.sigma = sigma;
            }
    };

    if (type == PatternType::LineGrid) {
        for (int pol : {1, -1}) {
            std::vector<Pt> pts;
            std::vector<std::pair<Pt, Pt>> lines;
            for (const Cand& c : houghCandidates(work, pol, &lines)) pts.push_back(c.p);
            const size_t before = best.nodes.size();
            tryCands(pts, pol, 0.0);
            if (best.nodes.size() > before || (R.lines.empty() && pol > 0)) {
                R.lines.clear();
                for (auto& l : lines)
                    R.lines.push_back({Pt((l.first.x + 0.5) * k - 0.5, (l.first.y + 0.5) * k - 0.5),
                                       Pt((l.second.x + 0.5) * k - 0.5, (l.second.y + 0.5) * k - 0.5)});
            }
        }
    }
    const double sigmas[] = {1.2, 2.0, 3.2, 5.0, 8.0, 12.0};
    const double maxSigma = std::min(work.w, work.h) / 30.0;
    for (double sigma : sigmas) {
        if (sigma > maxSigma) continue;
        // Pyramid: large scales are computed on a reduced image.
        const int oct = sigma >= 6 ? 4 : sigma >= 3 ? 2 : 1;
        const ImageF b = gaussianBlur(oct > 1 ? downscaleBlock(work, oct) : work, sigma / oct);
        std::vector<int> pols;
        if (type == PatternType::Checkerboard) pols = {0};
        else pols = {1, -1};
        for (int pol : pols) {
            const std::vector<Cand> cands = findCandidates(b, sigma / oct, type, pol);
            std::vector<Pt> pts;
            for (const Cand& c : cands) pts.push_back(Pt((c.p.x + 0.5) * oct - 0.5, (c.p.y + 0.5) * oct - 0.5));
            tryCands(pts, pol, sigma);
        }
    }

    if (best.nodes.size() < 4) {
        for (const Pt& p : best.pts) R.candidates.push_back(Pt((p.x + 0.5) * k - 0.5, (p.y + 0.5) * k - 0.5));
        R.message = "No grid found. Try the other pattern type, or place reference points manually.";
        return R;
    }

    // To full resolution.
    auto toFull = [k](Pt p) { return Pt((p.x + 0.5) * k - 0.5, (p.y + 0.5) * k - 0.5); };
    std::map<Key, Pt> nodes;
    for (auto& kv : best.nodes) nodes[kv.first] = toFull(best.pts[size_t(kv.second)]);
    nodes = normaliseOrientation(nodes);
    for (const Pt& p : best.pts) R.candidates.push_back(toFull(p));

    // Sub-pixel refinement at full resolution.
    const ImageF fullBlur = gaussianBlur(full, std::max(1.0, 0.5 * k));
    Mat3 H;
    auto getP = [](const Pt& p) { return p; };
    if (!fitLattice(nodes, getP, H)) {
        R.message = "Grid fit failed";
        return R;
    }
    std::map<Key, Pt> refined;
    for (auto& [key, p] : nodes) {
        const Pt c = H.apply(Pt(key.first, key.second));
        const Pt dA = H.apply(Pt(key.first + 0.5, key.second)) - H.apply(Pt(key.first - 0.5, key.second));
        const Pt dB = H.apply(Pt(key.first, key.second + 0.5)) - H.apply(Pt(key.first, key.second - 0.5));
        const double step = std::min(norm(dA), norm(dB));
        (void)c;
        Pt q = p;
        bool ok;
        if (type == PatternType::LineGrid) {
            ok = refineLineCrossing(fullBlur, q, dA, dB, step, best.polarity);
        } else {
            const int win = std::clamp(int(0.25 * step), 3, 30);
            ok = refineSaddle(fullBlur, q, win);
        }
        refined[key] = ok ? q : p;
    }
    rejectOutliers(refined, getP, 2.0, [](const Pt&) {}, 0.015);
    if (!fitLattice(refined, getP, H)) {
        R.message = "Grid fit failed after refinement";
        return R;
    }

    std::vector<double> res, steps;
    for (auto& [key, p] : refined) {
        GridNode n;
        n.i = key.first;
        n.j = key.second;
        n.img = p;
        R.nodes.push_back(n);
        res.push_back(dist(H.apply(Pt(key.first, key.second)), p));
        steps.push_back(dist(H.apply(Pt(key.first + 1, key.second)), H.apply(Pt(key.first, key.second))));
    }
    double ss = 0;
    for (double r : res) ss += r * r;
    R.rmsPx = std::sqrt(ss / res.size());
    R.stepPx = medianOf(steps);
    R.latticeToImage = H;
    R.polarity = best.polarity;
    R.ok = true;
    int maxi = 0, maxj = 0;
    for (auto& n : R.nodes) { maxi = std::max(maxi, n.i); maxj = std::max(maxj, n.j); }
    R.message = std::to_string(R.nodes.size()) + " grid corners (" + std::to_string(maxi + 1) + " x " +
                std::to_string(maxj + 1) + "), fit RMS " + std::to_string(R.rmsPx).substr(0, 4) + " px";
    return R;
}

}  // namespace cp
