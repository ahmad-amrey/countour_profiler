// Lightweight image containers and the image-processing primitives used by
// grid and profile detection. Pixel centres are at integer coordinates.
#pragma once
#include <cstdint>
#include <vector>

#include "geom.h"

namespace cp {

struct ImageU8 {
    int w = 0, h = 0, c = 3;
    std::vector<uint8_t> d;
    void resize(int W, int H, int C) { w = W; h = H; c = C; d.assign(size_t(W) * H * C, 0); }
    bool empty() const { return w <= 0 || h <= 0; }
    uint8_t* px(int x, int y) { return &d[(size_t(y) * w + x) * c]; }
    const uint8_t* px(int x, int y) const { return &d[(size_t(y) * w + x) * c]; }
};

struct ImageF {
    int w = 0, h = 0, c = 1;
    std::vector<float> d;
    void resize(int W, int H, int C = 1) { w = W; h = H; c = C; d.assign(size_t(W) * H * C, 0.f); }
    bool empty() const { return w <= 0 || h <= 0; }
    float& at(int x, int y, int ch = 0) { return d[(size_t(y) * w + x) * c + ch]; }
    float at(int x, int y, int ch = 0) const { return d[(size_t(y) * w + x) * c + ch]; }
    float clampAt(int x, int y, int ch = 0) const;
    float bilinear(double x, double y, int ch = 0) const;
};

using Mask = std::vector<uint8_t>;

ImageF toGray(const ImageU8& img);
ImageF toFloat(const ImageU8& img);  // keeps channels
ImageF downscaleBlock(const ImageF& img, int k);
ImageF gaussianBlur(const ImageF& img, double sigma);
void sampleBilinear(const ImageU8& img, double x, double y, float out[3]);

// Renders the world rectangle `area` (mm) at `res` px/mm by sampling `src`
// through Hw2i (world -> image). `valid` (optional) marks pixels inside src.
void warpPerspective(const ImageU8& src, const Mat3& Hw2i, const RectD& area, double res, ImageU8& out,
                     Mask* valid = nullptr);

void dilate(Mask& m, int w, int h, int r);
void erode(Mask& m, int w, int h, int r);
// 8-connected labelling. labels: 0 = background, 1..n. areas[0] unused.
int labelComponents(const Mask& m, int w, int h, std::vector<int>& labels, std::vector<int>& areas);
void thinZhangSuen(Mask& m, int w, int h);
// Outer boundary (Moore tracing) starting at the top-left pixel (sx, sy).
std::vector<Pt> traceBoundary(const Mask& m, int w, int h, int sx, int sy);
// Longest geodesic path through a (thinned) skeleton, in pixel coordinates.
// Pieces whose endpoints are closer than maxGap pixels are linked first.
std::vector<Pt> longestSkeletonPath(const Mask& skel, int w, int h, double maxGap = 0);
double otsuThreshold(const std::vector<float>& values);

}  // namespace cp
