#include "calib.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace cp {

Calibration calibrate(const std::vector<Pt>& img, const std::vector<Pt>& world) {
    Calibration c;
    if (img.size() < 4 || img.size() != world.size()) return c;
    if (!computeHomography(world, img, c.Hw2i)) return c;
    bool ok = false;
    c.Hi2w = c.Hw2i.inverse(&ok);
    if (!ok) return c;
    double ss = 0;
    for (size_t i = 0; i < img.size(); ++i) {
        bool okp = false;
        const Pt w = c.Hi2w.apply(img[i], &okp);
        const double r = okp ? dist(w, world[i]) : std::numeric_limits<double>::infinity();
        c.residualsMM.push_back(r);
        ss += r * r;
    }
    c.rmsMM = std::sqrt(ss / img.size());
    c.ok = std::isfinite(c.rmsMM);
    return c;
}

RectD boundingBox(const std::vector<Pt>& pts, double margin) {
    RectD r;
    if (pts.empty()) return r;
    r.x0 = r.y0 = std::numeric_limits<double>::infinity();
    r.x1 = r.y1 = -std::numeric_limits<double>::infinity();
    for (const Pt& p : pts) {
        r.x0 = std::min(r.x0, p.x); r.y0 = std::min(r.y0, p.y);
        r.x1 = std::max(r.x1, p.x); r.y1 = std::max(r.y1, p.y);
    }
    r.x0 -= margin; r.y0 -= margin; r.x1 += margin; r.y1 += margin;
    return r;
}

double pxPerMM(const Mat3& Hw2i, Pt w) {
    const Pt a = Hw2i.apply(w), bx = Hw2i.apply(w + Pt(1, 0)), by = Hw2i.apply(w + Pt(0, 1));
    return std::sqrt(dist(a, bx) * dist(a, by));
}

}  // namespace cp
