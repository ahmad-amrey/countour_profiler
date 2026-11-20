// Basic 2D geometry: points, 3x3 homographies, polyline utilities.
#pragma once
#include <cmath>
#include <vector>

namespace cp {

struct Pt {
    double x = 0, y = 0;
    Pt() = default;
    Pt(double x_, double y_) : x(x_), y(y_) {}
    Pt operator+(const Pt& o) const { return {x + o.x, y + o.y}; }
    Pt operator-(const Pt& o) const { return {x - o.x, y - o.y}; }
    Pt operator*(double s) const { return {x * s, y * s}; }
    Pt operator/(double s) const { return {x / s, y / s}; }
    Pt& operator+=(const Pt& o) { x += o.x; y += o.y; return *this; }
};

inline double dot(Pt a, Pt b) { return a.x * b.x + a.y * b.y; }
inline double cross(Pt a, Pt b) { return a.x * b.y - a.y * b.x; }
inline double norm(Pt a) { return std::sqrt(dot(a, a)); }
inline double dist(Pt a, Pt b) { return norm(a - b); }

struct RectD {
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    double w() const { return x1 - x0; }
    double h() const { return y1 - y0; }
    bool valid() const { return x1 > x0 && y1 > y0; }
};

// Row-major 3x3 matrix used as a planar projective transform.
struct Mat3 {
    double m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    // Applies the transform with perspective division. ok=false if the
    // point maps to infinity or behind the projection centre (w <= 0).
    Pt apply(Pt p, bool* ok = nullptr) const;
    Mat3 inverse(bool* ok = nullptr) const;
    Mat3 operator*(const Mat3& o) const;
};

// Least-squares homography H with dst ~ H * src (normalised DLT, >= 4 pts).
// Returns false if the configuration is degenerate (e.g. collinear points).
// H is scaled so points near the src centroid have positive w.
bool computeHomography(const std::vector<Pt>& src, const std::vector<Pt>& dst, Mat3& H);

double pointSegmentDist(Pt p, Pt a, Pt b, double* tOut = nullptr);
double pointPolylineDist(Pt p, const std::vector<Pt>& poly, bool closed);
double polylineLength(const std::vector<Pt>& pts, bool closed);

// Ramer-Douglas-Peucker simplification.
std::vector<Pt> simplifyDP(const std::vector<Pt>& pts, double tol, bool closed);
// Moving-average smoothing with half window `half` (endpoints kept for open lines).
std::vector<Pt> smoothMovingAverage(const std::vector<Pt>& pts, int half, bool closed);
// Catmull-Rom densification: `segs` sub-segments per original segment.
std::vector<Pt> catmullRom(const std::vector<Pt>& pts, int segs, bool closed);
// Resample at roughly uniform spacing.
std::vector<Pt> resampleUniform(const std::vector<Pt>& pts, double step, bool closed);

}  // namespace cp
