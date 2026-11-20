#include "image.h"

#include <algorithm>
#include <cmath>
#include <deque>

#include "parallel.h"

namespace cp {

float ImageF::clampAt(int x, int y, int ch) const {
    x = std::clamp(x, 0, w - 1);
    y = std::clamp(y, 0, h - 1);
    return at(x, y, ch);
}

float ImageF::bilinear(double x, double y, int ch) const {
    x = std::clamp(x, 0.0, double(w - 1));
    y = std::clamp(y, 0.0, double(h - 1));
    const int x0 = std::min(int(x), w - 2 < 0 ? 0 : w - 2), y0 = std::min(int(y), h - 2 < 0 ? 0 : h - 2);
    const int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
    const double fx = x - x0, fy = y - y0;
    const double a = at(x0, y0, ch) * (1 - fx) + at(x1, y0, ch) * fx;
    const double b = at(x0, y1, ch) * (1 - fx) + at(x1, y1, ch) * fx;
    return float(a * (1 - fy) + b * fy);
}

ImageF toGray(const ImageU8& img) {
    ImageF g;
    g.resize(img.w, img.h, 1);
    parallelFor(img.h, [&](int y) {
        for (int x = 0; x < img.w; ++x) {
            const uint8_t* p = img.px(x, y);
            g.at(x, y) = img.c >= 3 ? 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2] : float(p[0]);
        }
    });
    return g;
}

ImageF toFloat(const ImageU8& img) {
    ImageF f;
    f.resize(img.w, img.h, img.c);
    for (size_t i = 0; i < img.d.size(); ++i) f.d[i] = img.d[i];
    return f;
}

ImageF downscaleBlock(const ImageF& img, int k) {
    if (k <= 1) return img;
    ImageF o;
    o.resize(std::max(1, img.w / k), std::max(1, img.h / k), img.c);
    const float inv = 1.f / float(k * k);
    parallelFor(o.h, [&](int y) {
        for (int x = 0; x < o.w; ++x)
            for (int ch = 0; ch < img.c; ++ch) {
                float s = 0;
                for (int dy = 0; dy < k; ++dy)
                    for (int dx = 0; dx < k; ++dx) s += img.at(x * k + dx, y * k + dy, ch);
                o.at(x, y, ch) = s * inv;
            }
    });
    return o;
}

ImageF gaussianBlur(const ImageF& img, double sigma) {
    if (sigma <= 0.05 || img.empty()) return img;
    const int r = std::max(1, int(std::ceil(3 * sigma)));
    std::vector<float> k(2 * r + 1);
    double sum = 0;
    for (int i = -r; i <= r; ++i) sum += (k[i + r] = float(std::exp(-0.5 * i * i / (sigma * sigma))));
    for (float& v : k) v = float(v / sum);
    ImageF tmp, out;
    tmp.resize(img.w, img.h, img.c);
    out.resize(img.w, img.h, img.c);
    const int w = img.w, h = img.h, c = img.c;
    parallelFor(h, [&](int y) {
        for (int x = 0; x < w; ++x)
            for (int ch = 0; ch < c; ++ch) {
                float s = 0;
                for (int i = -r; i <= r; ++i) s += k[i + r] * img.at(std::clamp(x + i, 0, w - 1), y, ch);
                tmp.at(x, y, ch) = s;
            }
    });
    parallelFor(h, [&](int y) {
        for (int x = 0; x < w; ++x)
            for (int ch = 0; ch < c; ++ch) {
                float s = 0;
                for (int i = -r; i <= r; ++i) s += k[i + r] * tmp.at(x, std::clamp(y + i, 0, h - 1), ch);
                out.at(x, y, ch) = s;
            }
    });
    return out;
}

void sampleBilinear(const ImageU8& img, double x, double y, float out[3]) {
    x = std::clamp(x, 0.0, double(img.w - 1));
    y = std::clamp(y, 0.0, double(img.h - 1));
    const int x0 = std::min(int(x), std::max(0, img.w - 2)), y0 = std::min(int(y), std::max(0, img.h - 2));
    const int x1 = std::min(x0 + 1, img.w - 1), y1 = std::min(y0 + 1, img.h - 1);
    const float fx = float(x - x0), fy = float(y - y0);
    const uint8_t *a = img.px(x0, y0), *b = img.px(x1, y0), *c = img.px(x0, y1), *d = img.px(x1, y1);
    for (int ch = 0; ch < 3; ++ch) {
        const int cc = std::min(ch, img.c - 1);
        out[ch] = (a[cc] * (1 - fx) + b[cc] * fx) * (1 - fy) + (c[cc] * (1 - fx) + d[cc] * fx) * fy;
    }
}

void warpPerspective(const ImageU8& src, const Mat3& Hw2i, const RectD& area, double res, ImageU8& out, Mask* valid) {
    const int W = std::max(1, int(std::ceil(area.w() * res)));
    const int H = std::max(1, int(std::ceil(area.h() * res)));
    out.resize(W, H, 3);
    if (valid) valid->assign(size_t(W) * H, 0);
    parallelFor(H, [&](int v) {
        for (int u = 0; u < W; ++u) {
            bool ok = false;
            const Pt p = Hw2i.apply({area.x0 + (u + 0.5) / res, area.y0 + (v + 0.5) / res}, &ok);
            uint8_t* o = out.px(u, v);
            if (!ok || p.x < 0 || p.y < 0 || p.x > src.w - 1 || p.y > src.h - 1) {
                o[0] = o[1] = o[2] = 0;
                continue;
            }
            float c[3];
            sampleBilinear(src, p.x, p.y, c);
            for (int ch = 0; ch < 3; ++ch) o[ch] = uint8_t(std::clamp(c[ch] + 0.5f, 0.f, 255.f));
            if (valid) (*valid)[size_t(v) * W + u] = 1;
        }
    });
}

namespace {
void morph(Mask& m, int w, int h, int r, bool isDilate) {
    if (r <= 0) return;
    Mask tmp(m.size());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uint8_t v = isDilate ? 0 : 1;
            for (int i = std::max(0, x - r); i <= std::min(w - 1, x + r); ++i) {
                const uint8_t s = m[size_t(y) * w + i] ? 1 : 0;
                if (isDilate ? s : !s) { v = isDilate ? 1 : 0; break; }
            }
            tmp[size_t(y) * w + x] = v;
        }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uint8_t v = isDilate ? 0 : 1;
            for (int j = std::max(0, y - r); j <= std::min(h - 1, y + r); ++j) {
                const uint8_t s = tmp[size_t(j) * w + x];
                if (isDilate ? s : !s) { v = isDilate ? 1 : 0; break; }
            }
            m[size_t(y) * w + x] = v;
        }
}
}  // namespace

void dilate(Mask& m, int w, int h, int r) { morph(m, w, h, r, true); }
void erode(Mask& m, int w, int h, int r) { morph(m, w, h, r, false); }

int labelComponents(const Mask& m, int w, int h, std::vector<int>& labels, std::vector<int>& areas) {
    labels.assign(size_t(w) * h, 0);
    areas.assign(1, 0);
    int n = 0;
    std::vector<int> stack;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const size_t i0 = size_t(y) * w + x;
            if (!m[i0] || labels[i0]) continue;
            ++n;
            int area = 0;
            labels[i0] = n;
            stack.push_back(int(i0));
            while (!stack.empty()) {
                const int i = stack.back();
                stack.pop_back();
                ++area;
                const int cx = i % w, cy = i / w;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = cx + dx, ny = cy + dy;
                        if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                        const size_t j = size_t(ny) * w + nx;
                        if (m[j] && !labels[j]) {
                            labels[j] = n;
                            stack.push_back(int(j));
                        }
                    }
            }
            areas.push_back(area);
        }
    return n;
}

void thinZhangSuen(Mask& m, int w, int h) {
    for (int x = 0; x < w; ++x) m[x] = m[size_t(h - 1) * w + x] = 0;
    for (int y = 0; y < h; ++y) m[size_t(y) * w] = m[size_t(y) * w + w - 1] = 0;
    std::vector<int> del;
    bool changed = true;
    while (changed) {
        changed = false;
        for (int pass = 0; pass < 2; ++pass) {
            del.clear();
            for (int y = 1; y < h - 1; ++y)
                for (int x = 1; x < w - 1; ++x) {
                    if (!m[size_t(y) * w + x]) continue;
                    auto P = [&](int dx, int dy) { return m[size_t(y + dy) * w + x + dx] ? 1 : 0; };
                    const int p2 = P(0, -1), p3 = P(1, -1), p4 = P(1, 0), p5 = P(1, 1);
                    const int p6 = P(0, 1), p7 = P(-1, 1), p8 = P(-1, 0), p9 = P(-1, -1);
                    const int B = p2 + p3 + p4 + p5 + p6 + p7 + p8 + p9;
                    if (B < 2 || B > 6) continue;
                    const int seq[9] = {p2, p3, p4, p5, p6, p7, p8, p9, p2};
                    int A = 0;
                    for (int i = 0; i < 8; ++i) A += (seq[i] == 0 && seq[i + 1] == 1);
                    if (A != 1) continue;
                    if (pass == 0 ? (p2 * p4 * p6 != 0 || p4 * p6 * p8 != 0)
                                  : (p2 * p4 * p8 != 0 || p2 * p6 * p8 != 0))
                        continue;
                    del.push_back(y * w + x);
                }
            for (int i : del) m[size_t(i)] = 0;
            if (!del.empty()) changed = true;
        }
    }
}

std::vector<Pt> traceBoundary(const Mask& m, int w, int h, int sx, int sy) {
    static const int dx[8] = {1, 1, 0, -1, -1, -1, 0, 1};
    static const int dy[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    auto fg = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h && m[size_t(y) * w + x]; };
    auto dirIndex = [&](int ddx, int ddy) {
        for (int i = 0; i < 8; ++i)
            if (dx[i] == ddx && dy[i] == ddy) return i;
        return 4;
    };
    std::vector<Pt> pts{Pt(sx, sy)};
    int cx = sx, cy = sy, back = 4, firstDir = -1;
    const size_t maxIter = size_t(w) * h * 4 + 16;
    for (size_t it = 0; it < maxIter; ++it) {
        int found = -1;
        for (int k = 1; k <= 8; ++k) {
            const int d = (back + k) % 8;
            if (fg(cx + dx[d], cy + dy[d])) { found = d; break; }
        }
        if (found < 0) break;
        if (cx == sx && cy == sy) {
            if (firstDir < 0) firstDir = found;
            else if (found == firstDir) break;
        }
        const int bx = cx + dx[(found + 7) % 8], by = cy + dy[(found + 7) % 8];
        cx += dx[found];
        cy += dy[found];
        back = dirIndex(bx - cx, by - cy);
        pts.emplace_back(cx, cy);
    }
    if (pts.size() > 1 && pts.back().x == sx && pts.back().y == sy) pts.pop_back();
    return pts;
}

std::vector<Pt> longestSkeletonPath(const Mask& skelIn, int w, int h, double maxGap) {
    Mask skel = skelIn;
    auto on = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h && skel[size_t(y) * w + x]; };
    // Link pieces: repeatedly join the closest pair of endpoints that belong
    // to different skeleton components, if closer than maxGap.
    if (maxGap > 0) {
        for (int iter = 0; iter < 200; ++iter) {
            std::vector<int> lab, ar;
            const int nc = labelComponents(skel, w, h, lab, ar);
            if (nc < 2) break;
            std::vector<int> ends;
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    if (!on(x, y)) continue;
                    int nb = 0;
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx)
                            if ((dx || dy) && on(x + dx, y + dy)) ++nb;
                    if (nb <= 1) ends.push_back(y * w + x);
                }
            double best = maxGap * maxGap;
            int ba = -1, bb = -1;
            for (size_t i = 0; i < ends.size(); ++i)
                for (size_t j = i + 1; j < ends.size(); ++j) {
                    if (lab[size_t(ends[i])] == lab[size_t(ends[j])]) continue;
                    const double dx = ends[i] % w - ends[j] % w, dy = ends[i] / w - ends[j] / w;
                    const double d2 = dx * dx + dy * dy;
                    if (d2 < best) { best = d2; ba = ends[i]; bb = ends[j]; }
                }
            if (ba < 0) break;
            const int ax = ba % w, ay = ba / w, bx = bb % w, by = bb / w;
            const int steps = std::max(std::abs(bx - ax), std::abs(by - ay));
            for (int k = 0; k <= steps; ++k) {
                const int x = ax + int(std::lround(double(bx - ax) * k / std::max(1, steps)));
                const int y = ay + int(std::lround(double(by - ay) * k / std::max(1, steps)));
                skel[size_t(y) * w + x] = 1;
            }
        }
    }
    // Keep the largest component.
    std::vector<int> lab, ar;
    const int nc = labelComponents(skel, w, h, lab, ar);
    if (nc == 0) return {};
    int bestL = 1;
    for (int l = 2; l <= nc; ++l)
        if (ar[size_t(l)] > ar[size_t(bestL)]) bestL = l;
    std::vector<int> nodes;
    for (int i = 0; i < w * h; ++i)
        if (lab[size_t(i)] == bestL) nodes.push_back(i);
    std::vector<int> distv(size_t(w) * h, -1), parent(size_t(w) * h, -1);
    auto bfs = [&](int start) {
        for (int i : nodes) { distv[size_t(i)] = -1; parent[size_t(i)] = -1; }
        std::deque<int> q{start};
        distv[size_t(start)] = 0;
        int last = start;
        while (!q.empty()) {
            const int i = q.front();
            q.pop_front();
            last = i;
            const int x = i % w, y = i / w;
            // 4-neighbours first so diagonal shortcuts do not dominate.
            static const int ox[8] = {1, -1, 0, 0, 1, 1, -1, -1};
            static const int oy[8] = {0, 0, 1, -1, 1, -1, 1, -1};
            for (int k = 0; k < 8; ++k) {
                const int nx = x + ox[k], ny = y + oy[k];
                if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                const int j = ny * w + nx;
                if (lab[size_t(j)] == bestL && distv[size_t(j)] < 0) {
                    distv[size_t(j)] = distv[size_t(i)] + 1;
                    parent[size_t(j)] = i;
                    q.push_back(j);
                }
            }
        }
        return last;
    };
    const int a = bfs(nodes.front());
    const int b = bfs(a);
    std::vector<Pt> path;
    for (int i = b; i >= 0; i = parent[size_t(i)]) path.emplace_back(i % w, i / w);
    return path;
}

double otsuThreshold(const std::vector<float>& values) {
    if (values.empty()) return 0;
    double hist[256] = {};
    for (float v : values) hist[std::clamp(int(v), 0, 255)] += 1;
    const double total = double(values.size());
    double sumAll = 0;
    for (int i = 0; i < 256; ++i) sumAll += i * hist[i];
    double wB = 0, sumB = 0, best = -1;
    int thr = 0;
    for (int t = 0; t < 256; ++t) {
        wB += hist[t];
        if (wB == 0) continue;
        const double wF = total - wB;
        if (wF == 0) break;
        sumB += t * hist[t];
        const double mB = sumB / wB, mF = (sumAll - sumB) / wF;
        const double between = wB * wF * (mB - mF) * (mB - mF);
        if (between > best) { best = between; thr = t; }
    }
    return thr + 0.5;
}

}  // namespace cp
