// Profile extraction
// 1. Rectify the region of interest at an analysis resolution chosen so the
//    background pattern period is an integer number of pixels.
// 2. Build a local background model: for every pixel, the per-channel median
//    of the pixels one, two... pattern periods away (5x5 neighbourhood).
//    The pattern (lines, squares, ticks) cancels; the wire does not.
// 3. Threshold the difference, clean up, pick the object (largest elongated
//    component, or the one nearest a user-picked seed point).
// 4. Centre line via thinning + longest path, or outer outline, then smooth
//    and simplify in millimetres.
#include "profile_detect.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "parallel.h"

namespace cp {

ProfileResult detectProfile(const ImageU8& src, const Mat3& Hw2i, const RectD& detectRoi, double nativePxPerMM,
                            const ProfileParams& prm) {
    ProfileResult out;
    if (src.empty() || !detectRoi.valid()) {
        out.message = "Nothing to analyse (no image or empty region).";
        return out;
    }
    const double period = prm.pattern == PatternType::Checkerboard ? 2 * prm.squareMM : prm.squareMM;
    // The background model needs neighbouring pattern periods, so analyse a
    // region one period larger than the detection region on every side.
    RectD roi = detectRoi;
    if (prm.mode == DetectMode::PatternSubtraction) {
        roi.x0 -= period; roi.y0 -= period; roi.x1 += period; roi.y1 += period;
    }
    double rd = prm.resolution;
    if (rd <= 0) {
        rd = std::min({30.0, std::max(1.0, nativePxPerMM), std::sqrt(4.0e6 / (roi.w() * roi.h()))});
        rd = std::max(rd, 0.5);
    }
    int P = 0;
    if (prm.mode == DetectMode::PatternSubtraction) {
        P = std::max(6, int(std::lround(period * rd)));
        rd = P / period;
    }

    ImageU8 img;
    Mask valid;
    warpPerspective(src, Hw2i, roi, rd, img, &valid);
    const int W = img.w, H = img.h;
    const ImageF f = gaussianBlur(toFloat(img), 1.0);

    std::vector<float> A(size_t(W) * H, 0.f);
    if (prm.mode == DetectMode::PatternSubtraction) {
        // Pass 1: background model T (median of the same pattern phase in the
        // neighbouring periods) and its spread S (median absolute deviation).
        const int Rn = 2;
        ImageF T, S;
        T.resize(W, H, 3);
        S.resize(W, H, 3);
        Mask tValid(size_t(W) * H, 0);
        parallelFor(H, [&](int v) {
            float buf[(2 * Rn + 1) * (2 * Rn + 1)];
            for (int u = 0; u < W; ++u) {
                const size_t idx = size_t(v) * W + u;
                if (!valid[idx]) continue;
                bool okPix = true;
                for (int ch = 0; ch < 3 && okPix; ++ch) {
                    int n = 0;
                    for (int b = -Rn; b <= Rn; ++b) {
                        const int vv = v + b * P;
                        if (vv < 0 || vv >= H) continue;
                        for (int a = -Rn; a <= Rn; ++a) {
                            if (!a && !b) continue;
                            const int uu = u + a * P;
                            if (uu < 0 || uu >= W || !valid[size_t(vv) * W + uu]) continue;
                            buf[n++] = f.at(uu, vv, ch);
                        }
                    }
                    if (n < 2) { okPix = false; break; }
                    std::nth_element(buf, buf + n / 2, buf + n);
                    const float med = buf[n / 2];
                    // Where the background is not periodic (pattern border,
                    // printed digits) the neighbours disagree: large spread.
                    for (int i = 0; i < n; ++i) buf[i] = std::fabs(buf[i] - med);
                    std::nth_element(buf, buf + n / 2, buf + n);
                    T.at(u, v, ch) = med;
                    S.at(u, v, ch) = buf[n / 2];
                }
                tValid[idx] = okPix ? 1 : 0;
            }
        });
        // Pass 2: compare each pixel with the best-matching background value
        // within +-r pixels, so small registration errors along sharp pattern
        // edges (lens distortion, printing tolerance) are not reported. The
        // object is much wider than 2r, so its core still stands out.
        const int r = rd >= 10 ? 1 : 0;
        parallelFor(H, [&](int v) {
            for (int u = 0; u < W; ++u) {
                const size_t idx = size_t(v) * W + u;
                if (!tValid[idx]) continue;
                double best = 1e30;
                for (int dv = -r; dv <= r; ++dv) {
                    const int vv = v + dv;
                    if (vv < 0 || vv >= H) continue;
                    for (int du = -r; du <= r; ++du) {
                        const int uu = u + du;
                        if (uu < 0 || uu >= W || !tValid[size_t(vv) * W + uu]) continue;
                        double z = 0;
                        for (int ch = 0; ch < 3; ++ch)
                            z = std::max(z, std::fabs(f.at(u, v, ch) - T.at(uu, vv, ch)) /
                                                (1.4826 * S.at(uu, vv, ch) + 3.0));
                        best = std::min(best, z);
                    }
                }
                A[idx] = best < 1e29 ? float(std::min(255.0, 10.0 * best)) : 0.f;
            }
        });
    } else {
        const double tol = std::max(1.0, prm.colorTol);
        parallelFor(H, [&](int v) {
            for (int u = 0; u < W; ++u) {
                const size_t idx = size_t(v) * W + u;
                if (!valid[idx]) continue;
                double d2 = 0;
                for (int ch = 0; ch < 3; ++ch) {
                    const double d = f.at(u, v, ch) - prm.color[ch];
                    d2 += d * d;
                }
                A[idx] = float(255.0 * std::max(0.0, 1.0 - std::sqrt(d2) / (2 * tol)));
            }
        });
    }

    // Only the detection region takes part in thresholding and selection.
    for (int v = 0; v < H; ++v)
        for (int u = 0; u < W; ++u) {
            const double X = roi.x0 + (u + 0.5) / rd, Y = roi.y0 + (v + 0.5) / rd;
            if (X < detectRoi.x0 || X > detectRoi.x1 || Y < detectRoi.y0 || Y > detectRoi.y1) valid[size_t(v) * W + u] = 0;
        }
    if (const char* dd = std::getenv("CP_DEBUG_DIR")) {
        static int counter = 0;
        const std::string base = std::string(dd) + "/anom_" + std::to_string(counter++);
        if (FILE* fp = std::fopen((base + ".pgm").c_str(), "wb")) {
            std::fprintf(fp, "P5 %d %d 255\n", W, H);
            for (float a : A) std::fputc(int(std::min(255.f, a)), fp);
            std::fclose(fp);
        }
        if (FILE* fp = std::fopen((base + "_img.ppm").c_str(), "wb")) {
            std::fprintf(fp, "P6 %d %d 255\n", W, H);
            std::fwrite(img.d.data(), 1, img.d.size(), fp);
            std::fclose(fp);
        }
    }
    // Smooth at about the wire half-width: fuses the bright edges of a round,
    // reflective wire (whose centre mirrors the dark background) into one
    // band, and suppresses single-pixel texture speckle.
    if (prm.mode == DetectMode::PatternSubtraction) {
        ImageF a;
        a.w = W; a.h = H; a.c = 1;
        a.d = A;
        A = gaussianBlur(a, std::max(0.7, 0.08 * rd)).d;
        for (size_t i = 0; i < A.size(); ++i)
            if (!valid[i]) A[i] = 0;
    }
    // Threshold.
    double thr = prm.threshold;
    if (thr <= 0) {
        if (prm.mode == DetectMode::Color) {
            thr = 127.5;
        } else {
            std::vector<float> vals;
            vals.reserve(A.size());
            for (size_t i = 0; i < A.size(); ++i)
                if (valid[i]) vals.push_back(std::min(A[i], 255.f));
            if (vals.empty()) {
                out.message = "Region of interest lies outside the image.";
                return out;
            }
            // Heavy-tailed texture (cutting mats) is captured by the 90th
            // percentile; the object itself covers far less than 10 %.
            std::nth_element(vals.begin(), vals.begin() + size_t(vals.size() * 0.9), vals.end());
            const double p90 = vals[size_t(vals.size() * 0.9)];
            std::nth_element(vals.begin(), vals.begin() + vals.size() / 2, vals.end());
            const double med = vals[vals.size() / 2];
            for (float& v : vals) v = std::fabs(v - float(med));
            std::nth_element(vals.begin(), vals.begin() + vals.size() / 2, vals.end());
            const double mad = vals[vals.size() / 2];
            thr = std::max({med + 10 * 1.4826 * mad, 3.0 * p90, 25.0});
        }
    }
    out.thresholdUsed = thr;

    // Hysteresis: strong pixels (> thr) seed objects, connected pixels above
    // thr/2 extend them. Keeps dim parts of the wire attached to bright ones.
    Mask m(size_t(W) * H, 0);
    {
        Mask low(size_t(W) * H, 0);
        for (size_t i = 0; i < low.size(); ++i) low[i] = (valid[i] && A[i] > 0.5 * thr) ? 1 : 0;
        std::vector<int> lab, ar;
        const int nl = labelComponents(low, W, H, lab, ar);
        std::vector<char> strong(size_t(nl) + 1, 0);
        for (size_t i = 0; i < low.size(); ++i)
            if (lab[i] && A[i] > thr) strong[size_t(lab[i])] = 1;
        for (size_t i = 0; i < m.size(); ++i) m[i] = (lab[i] && strong[size_t(lab[i])]) ? 1 : 0;
    }
    // Opening removes 1-2 px slivers along pattern edges (only at high resolution).
    if (const int ro = int(0.04 * rd); ro > 0) { erode(m, W, H, ro); dilate(m, W, H, ro); }
    // Drop specks (dust, glitter in the mat) smaller than a ~0.45 mm disc.
    {
        std::vector<int> lab, ar;
        labelComponents(m, W, H, lab, ar);
        const double minArea = std::max(6.0, 0.16 * rd * rd);
        for (size_t i = 0; i < m.size(); ++i)
            if (m[i] && ar[size_t(lab[i])] < minArea) m[i] = 0;
    }
    out.mask.assign(m.begin(), m.end());
    out.maskW = W;
    out.maskH = H;
    out.maskArea = roi;
    out.maskRes = rd;

    // Group pieces that lie within `gap` of each other (a wire is often broken
    // by highlights, shadows or where it crosses a pattern line).
    const int g = std::max(1, int(std::lround(0.4 * rd)));
    Mask md = m;
    dilate(md, W, H, g);
    std::vector<int> labels, areasDil;
    const int n = labelComponents(md, W, H, labels, areasDil);
    if (n == 0) {
        out.message = "No object found. Lower the threshold or pick the object colour.";
        return out;
    }
    // Per-group statistics on the original (undilated) pixels.
    std::vector<int> area(size_t(n) + 1, 0);
    std::vector<char> touches(size_t(n) + 1, 0);  // reaches the region or photo border
    std::vector<int> x0(size_t(n) + 1, W), y0(size_t(n) + 1, H), x1(size_t(n) + 1, -1), y1(size_t(n) + 1, -1);
    for (int v = 0; v < H; ++v)
        for (int u = 0; u < W; ++u) {
            const size_t i = size_t(v) * W + u;
            const int l = labels[i];
            if (!l || !m[i]) continue;
            ++area[size_t(l)];
            if (u <= 1 || v <= 1 || u >= W - 2 || v >= H - 2 || !valid[i - 2] || !valid[i + 2] || !valid[i - 2 * size_t(W)] || !valid[i + 2 * size_t(W)])
                touches[size_t(l)] = 1;
            x0[size_t(l)] = std::min(x0[size_t(l)], u); x1[size_t(l)] = std::max(x1[size_t(l)], u);
            y0[size_t(l)] = std::min(y0[size_t(l)], v); y1[size_t(l)] = std::max(y1[size_t(l)], v);
        }
    int chosen = 0;
    if (prm.hasSeed) {
        const double su = (prm.seed.x - roi.x0) * rd - 0.5, sv = (prm.seed.y - roi.y0) * rd - 0.5;
        double bd = 1e300;
        for (int v = 0; v < H; ++v)
            for (int u = 0; u < W; ++u) {
                const size_t i = size_t(v) * W + u;
                if (!m[i]) continue;
                const double d = (u - su) * (u - su) + (v - sv) * (v - sv);
                if (d < bd) { bd = d; chosen = labels[i]; }
            }
    } else {
        // Prefer large AND elongated objects (a bent wire) over compact blobs
        // such as printed digits or dust.
        double bestScore = -1;
        for (int l = 1; l <= n; ++l) {
            if (area[size_t(l)] == 0) continue;
            const double diag = std::hypot(x1[size_t(l)] - x0[size_t(l)] + 1, y1[size_t(l)] - y0[size_t(l)] + 1);
            double score = double(area[size_t(l)]) * diag;
            // Objects running into the region border are usually the edge of
            // the pattern or clutter outside it, not the profile.
            if (touches[size_t(l)]) score *= 0.05;
            if (score > bestScore) { bestScore = score; chosen = l; }
        }
    }
    if (!chosen || area[size_t(chosen)] < 12) {
        out.message = "Detected object is too small.";
        return out;
    }
    // Object = original pixels of the group, gaps closed.
    Mask comp(size_t(W) * H, 0);
    for (size_t i = 0; i < comp.size(); ++i)
        if (m[i] && labels[i] == chosen) comp[i] = 1;
    dilate(comp, W, H, g);
    erode(comp, W, H, g);
    for (size_t i = 0; i < comp.size(); ++i)
        if (comp[i]) out.mask[i] = 2;
    std::vector<int> areas{0, 0};
    for (uint8_t c : comp) areas[1] += c;
    const int chosenArea = areas[1];

    auto toWorld = [&](const Pt& p) { return Pt(roi.x0 + (p.x + 0.5) / rd, roi.y0 + (p.y + 0.5) / rd); };
    std::vector<Pt> pts;
    if (prm.output == ProfileOutput::Outline) {
        int sx = -1, sy = -1;
        for (int v = 0; v < H && sx < 0; ++v)
            for (int u = 0; u < W; ++u)
                if (comp[size_t(v) * W + u]) { sx = u; sy = v; break; }
        for (const Pt& p : traceBoundary(comp, W, H, sx, sy)) pts.push_back(toWorld(p));
        pts = smoothMovingAverage(pts, int(std::lround(prm.smoothMM * rd)), true);
        pts = simplifyDP(pts, prm.simplifyMM, true);
        out.closed = true;
    } else {
        Mask skel = comp;
        thinZhangSuen(skel, W, H);
        std::vector<Pt> path = longestSkeletonPath(skel, W, H, 1.0 * rd);
        if (path.size() < 2) {
            out.message = "Could not trace the object's centre line.";
            return out;
        }
        const double pathLen = polylineLength(path, false);
        const double thickPx = chosenArea / std::max(1.0, pathLen);
        out.thicknessMM = thickPx / rd;
        for (const Pt& p : path) pts.push_back(toWorld(p));
        pts = resampleUniform(pts, 1.0 / rd, false);
        // Trim a sharp kink close to an end: the longest skeleton path can
        // take a side branch at the tip (shadow of the cut end, pattern edge).
        auto trimStartKink = [&](std::vector<Pt>& p) {
            const double tailMM = std::max(1.5, 4 * out.thicknessMM);
            const int win = std::max(3, int(std::lround(0.5 * rd)));
            const int tail = int(std::lround(tailMM * rd));
            if (int(p.size()) < 3 * tail + 2 * win) return;
            int bestC = -1;
            double bestAng = 0;
            for (int c = win; c <= tail; ++c) {
                const Pt a = p[size_t(c)] - p[size_t(c - win)], b = p[size_t(c + win)] - p[size_t(c)];
                const double ang = std::acos(std::clamp(dot(a, b) / (norm(a) * norm(b) + 1e-12), -1.0, 1.0));
                if (ang > bestAng) { bestAng = ang; bestC = c; }
            }
            if (bestC > 0 && bestAng > 55.0 * 3.14159265358979323846 / 180) p.erase(p.begin(), p.begin() + bestC);
        };
        trimStartKink(pts);
        std::reverse(pts.begin(), pts.end());
        trimStartKink(pts);
        std::reverse(pts.begin(), pts.end());
        pts = smoothMovingAverage(pts, int(std::lround(prm.smoothMM * rd)), false);
        // Thinning shortens the skeleton at each end. Extend each end along
        // its tangent up to the object's boundary, then step back by the
        // smoothing halo so the end sits on the physical tip.
        if (pts.size() >= 4) {
            const double halo = std::max(0.7, 0.08 * rd) / rd;  // mm
            const size_t kk = std::min<size_t>(pts.size() - 1, size_t(std::max(3.0, thickPx)));
            auto inside = [&](Pt w) {
                const int x = int(std::lround((w.x - roi.x0) * rd - 0.5)), y = int(std::lround((w.y - roi.y0) * rd - 0.5));
                return x >= 0 && y >= 0 && x < W && y < H && comp[size_t(y) * W + x];
            };
            auto extend = [&](Pt end, Pt ref) {
                Pt d = end - ref;
                if (norm(d) < 1e-9) return end;
                d = d / norm(d);
                const double stepMM = 0.25 / rd, maxMM = 3 * std::max(out.thicknessMM, 0.2);
                double t = 0;
                while (t < maxMM && inside(end + d * (t + stepMM))) t += stepMM;
                return end + d * std::max(0.0, t - halo);
            };
            const Pt e0 = extend(pts[0], pts[kk]);
            const Pt e1 = extend(pts.back(), pts[pts.size() - 1 - kk]);
            pts.insert(pts.begin(), e0);
            pts.push_back(e1);
        }
        pts = simplifyDP(pts, prm.simplifyMM, false);
        out.closed = false;
    }
    out.pts = pts;
    out.ok = pts.size() >= 2;
    char buf[160];
    std::snprintf(buf, sizeof buf, "Profile: %zu points, length %.2f mm%s", pts.size(), polylineLength(pts, out.closed),
                  out.thicknessMM > 0 ? "" : "");
    out.message = buf;
    if (out.thicknessMM > 0) {
        std::snprintf(buf, sizeof buf, ", wire ~%.2f mm thick", out.thicknessMM);
        out.message += buf;
    }
    return out;
}

}  // namespace cp
