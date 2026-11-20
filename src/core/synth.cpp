#include "synth.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "parallel.h"

namespace cp {
namespace { constexpr double kPi = 3.14159265358979323846; }

SynthResult renderSynthetic(const SynthParams& p) {
    SynthResult R;
    const double s = p.squareMM, BW = p.cols * s, BH = p.rows * s;
    const std::vector<Pt> board = {{0, 0}, {BW, 0}, {BW, BH}, {0, BH}};
    std::vector<Pt> quad;
    for (int i = 0; i < 4; ++i) quad.emplace_back(p.quad[2 * i] * p.imgW, p.quad[2 * i + 1] * p.imgH);
    computeHomography(board, quad, R.Hw2i);
    bool ok = false;
    const Mat3 Hi2w = R.Hw2i.inverse(&ok);

    // Ground-truth wire: an asymmetric hook, like a bent profile gauge wire.
    const double cx = BW * 0.5, cy = BH * 0.5;
    for (int i = 0; i <= 400; ++i) {
        const double t = i / 400.0;
        R.truth.emplace_back(cx - 0.18 * BW + 0.36 * BW * t,
                             cy + 0.15 * BH - 0.32 * BH * std::sin(kPi * t) + 0.05 * BH * std::sin(3 * kPi * t));
    }
    // Distance raster around the wire (world space).
    const double rad = 0.5 * p.wireDiameterMM, rr = 30.0;  // px/mm
    double wx0 = 1e9, wy0 = 1e9, wx1 = -1e9, wy1 = -1e9;
    for (const Pt& q : R.truth) {
        wx0 = std::min(wx0, q.x); wy0 = std::min(wy0, q.y);
        wx1 = std::max(wx1, q.x); wy1 = std::max(wy1, q.y);
    }
    wx0 -= 2; wy0 -= 2; wx1 += 2; wy1 += 2;
    const int dw = int((wx1 - wx0) * rr) + 1, dh = int((wy1 - wy0) * rr) + 1;
    std::vector<float> dfield(size_t(dw) * dh, 1e9f);
    for (size_t i = 1; i < R.truth.size(); ++i) {
        const Pt a = R.truth[i - 1], b = R.truth[i];
        const int u0 = std::max(0, int((std::min(a.x, b.x) - rad - wx0) * rr) - 1);
        const int u1 = std::min(dw - 1, int((std::max(a.x, b.x) + rad - wx0) * rr) + 1);
        const int v0 = std::max(0, int((std::min(a.y, b.y) - rad - wy0) * rr) - 1);
        const int v1 = std::min(dh - 1, int((std::max(a.y, b.y) + rad - wy0) * rr) + 1);
        for (int v = v0; v <= v1; ++v)
            for (int u = u0; u <= u1; ++u) {
                const double d = pointSegmentDist(Pt(wx0 + u / rr, wy0 + v / rr), a, b);
                float& f = dfield[size_t(v) * dw + u];
                f = std::min(f, float(d));
            }
    }

    auto shade = [&](Pt w, float out[3]) {
        // Base surface.
        const bool onMat = w.x > -1.5 * s && w.y > -1.5 * s && w.x < BW + 1.5 * s && w.y < BH + 1.5 * s;
        float c[3] = {110, 90, 70};  // table
        if (onMat) {
            if (p.pattern == PatternType::Checkerboard) {
                c[0] = c[1] = c[2] = 232;
                if (w.x >= 0 && w.y >= 0 && w.x < BW && w.y < BH && ((int(w.x / s) + int(w.y / s)) & 1)) c[0] = c[1] = c[2] = 28;
            } else {
                const float bg[3] = {40, 95, 82}, ln[3] = {125, 165, 232};
                const float bgD[3] = {236, 236, 230}, lnD[3] = {55, 60, 70};
                const float* B = p.brightLines ? bg : bgD;
                const float* L = p.brightLines ? ln : lnD;
                std::copy(B, B + 3, c);
                const double hw = 0.25;  // half line width (mm)
                const double fx = std::fabs(w.x - s * std::round(w.x / s)), fy = std::fabs(w.y - s * std::round(w.y / s));
                const bool inside = w.x > -hw && w.y > -hw && w.x < BW + hw && w.y < BH + hw;
                if (inside && (fx < hw || fy < hw)) std::copy(L, L + 3, c);
            }
        }
        // Wire.
        const int u = int((w.x - wx0) * rr + 0.5), v = int((w.y - wy0) * rr + 0.5);
        if (u >= 0 && v >= 0 && u < dw && v < dh) {
            const float d = dfield[size_t(v) * dw + u];
            if (d < rad) {
                const double q = d / rad;
                const double b = 0.55 + 0.45 * (1 - q * q);
                c[0] = float(p.wireColor[0] * b); c[1] = float(p.wireColor[1] * b); c[2] = float(p.wireColor[2] * b);
            }
        }
        std::copy(c, c + 3, out);
    };

    R.img.resize(p.imgW, p.imgH, 3);
    parallelFor(p.imgH, [&](int y) {
        std::mt19937 rng(p.seed * 7919u + unsigned(y));
        std::normal_distribution<float> noise(0.f, 4.f);
        for (int x = 0; x < p.imgW; ++x) {
            float acc[3] = {0, 0, 0};
            for (int sy = 0; sy < 2; ++sy)
                for (int sx = 0; sx < 2; ++sx) {
                    bool okw = false;
                    const Pt w = Hi2w.apply(Pt(x - 0.25 + 0.5 * sx, y - 0.25 + 0.5 * sy), &okw);
                    float c[3];
                    shade(w, c);
                    for (int ch = 0; ch < 3; ++ch) acc[ch] += 0.25f * c[ch];
                }
            const double lx = double(x) / p.imgW - 0.5, ly = double(y) / p.imgH - 0.5;
            const float light = float((0.85 + 0.25 * lx) * (1.0 - 0.35 * (lx * lx + ly * ly)));
            uint8_t* o = R.img.px(x, y);
            for (int ch = 0; ch < 3; ++ch) o[ch] = uint8_t(std::clamp(acc[ch] * light + noise(rng), 0.f, 255.f));
        }
    });
    return R;
}

}  // namespace cp
