#include "geom.h"

#include <algorithm>
#include <limits>

namespace cp {

Pt Mat3::apply(Pt p, bool* ok) const {
    const double w = m[6] * p.x + m[7] * p.y + m[8];
    if (std::fabs(w) < 1e-12 || w < 0) {
        if (ok) *ok = false;
        if (std::fabs(w) < 1e-12) return {0, 0};
    } else if (ok) {
        *ok = true;
    }
    return {(m[0] * p.x + m[1] * p.y + m[2]) / w, (m[3] * p.x + m[4] * p.y + m[5]) / w};
}

Mat3 Mat3::operator*(const Mat3& o) const {
    Mat3 r;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            double s = 0;
            for (int k = 0; k < 3; ++k) s += m[i * 3 + k] * o.m[k * 3 + j];
            r.m[i * 3 + j] = s;
        }
    return r;
}

Mat3 Mat3::inverse(bool* ok) const {
    const double* a = m;
    Mat3 r;
    r.m[0] = a[4] * a[8] - a[5] * a[7];
    r.m[1] = a[2] * a[7] - a[1] * a[8];
    r.m[2] = a[1] * a[5] - a[2] * a[4];
    r.m[3] = a[5] * a[6] - a[3] * a[8];
    r.m[4] = a[0] * a[8] - a[2] * a[6];
    r.m[5] = a[2] * a[3] - a[0] * a[5];
    r.m[6] = a[3] * a[7] - a[4] * a[6];
    r.m[7] = a[1] * a[6] - a[0] * a[7];
    r.m[8] = a[0] * a[4] - a[1] * a[3];
    const double det = a[0] * r.m[0] + a[1] * r.m[3] + a[2] * r.m[6];
    if (std::fabs(det) < 1e-300) {
        if (ok) *ok = false;
        return Mat3{};
    }
    for (double& v : r.m) v /= det;
    if (ok) *ok = true;
    return r;
}

namespace {

// Cyclic Jacobi eigen-decomposition of a symmetric NxN matrix.
template <int N>
void jacobiEigen(double A[N][N], double V[N][N], double eig[N]) {
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j) V[i][j] = (i == j) ? 1.0 : 0.0;
    for (int sweep = 0; sweep < 100; ++sweep) {
        double off = 0, diag = 0;
        for (int p = 0; p < N; ++p) {
            diag += A[p][p] * A[p][p];
            for (int q = p + 1; q < N; ++q) off += A[p][q] * A[p][q];
        }
        if (off <= 1e-30 * (diag + 1e-300)) break;
        for (int p = 0; p < N; ++p)
            for (int q = p + 1; q < N; ++q) {
                if (std::fabs(A[p][q]) < 1e-300) continue;
                const double theta = (A[q][q] - A[p][p]) / (2 * A[p][q]);
                const double t = (theta >= 0 ? 1.0 : -1.0) / (std::fabs(theta) + std::sqrt(theta * theta + 1));
                const double c = 1 / std::sqrt(t * t + 1), s = t * c;
                for (int k = 0; k < N; ++k) {
                    const double akp = A[k][p], akq = A[k][q];
                    A[k][p] = c * akp - s * akq;
                    A[k][q] = s * akp + c * akq;
                }
                for (int k = 0; k < N; ++k) {
                    const double apk = A[p][k], aqk = A[q][k];
                    A[p][k] = c * apk - s * aqk;
                    A[q][k] = s * apk + c * aqk;
                }
                for (int k = 0; k < N; ++k) {
                    const double vkp = V[k][p], vkq = V[k][q];
                    V[k][p] = c * vkp - s * vkq;
                    V[k][q] = s * vkp + c * vkq;
                }
            }
    }
    for (int i = 0; i < N; ++i) eig[i] = A[i][i];
}

Mat3 normalisation(const std::vector<Pt>& p) {
    Pt c(0, 0);
    for (const Pt& q : p) c += q;
    c = c / double(p.size());
    double md = 0;
    for (const Pt& q : p) md += dist(q, c);
    md /= double(p.size());
    const double s = md > 1e-12 ? std::sqrt(2.0) / md : 1.0;
    Mat3 T;
    T.m[0] = s; T.m[1] = 0; T.m[2] = -s * c.x;
    T.m[3] = 0; T.m[4] = s; T.m[5] = -s * c.y;
    T.m[6] = 0; T.m[7] = 0; T.m[8] = 1;
    return T;
}

}  // namespace

bool computeHomography(const std::vector<Pt>& src, const std::vector<Pt>& dst, Mat3& H) {
    const size_t n = src.size();
    if (n < 4 || dst.size() != n) return false;
    const Mat3 Ts = normalisation(src), Td = normalisation(dst);
    double AtA[9][9] = {};
    for (size_t i = 0; i < n; ++i) {
        const Pt s = Ts.apply(src[i]), d = Td.apply(dst[i]);
        const double r1[9] = {-s.x, -s.y, -1, 0, 0, 0, d.x * s.x, d.x * s.y, d.x};
        const double r2[9] = {0, 0, 0, -s.x, -s.y, -1, d.y * s.x, d.y * s.y, d.y};
        for (int a = 0; a < 9; ++a)
            for (int b = 0; b < 9; ++b) AtA[a][b] += r1[a] * r1[b] + r2[a] * r2[b];
    }
    double V[9][9], eig[9];
    jacobiEigen<9>(AtA, V, eig);
    int idx[9];
    for (int i = 0; i < 9; ++i) idx[i] = i;
    std::sort(idx, idx + 9, [&](int a, int b) { return eig[a] < eig[b]; });
    // Degenerate if the null space is more than one-dimensional.
    if (eig[idx[1]] < 1e-10 * std::max(1e-300, eig[idx[8]])) return false;
    Mat3 Hn;
    for (int i = 0; i < 9; ++i) Hn.m[i] = V[i][idx[0]];
    bool ok = false;
    const Mat3 TdInv = Td.inverse(&ok);
    if (!ok) return false;
    Mat3 R = TdInv * Hn * Ts;
    if (std::fabs(R.m[8]) > 1e-12) {
        const double s = 1.0 / R.m[8];
        for (double& v : R.m) v *= s;
    }
    // Make w positive around the source centroid.
    Pt c(0, 0);
    for (const Pt& q : src) c += q;
    c = c / double(n);
    if (R.m[6] * c.x + R.m[7] * c.y + R.m[8] < 0)
        for (double& v : R.m) v = -v;
    R.inverse(&ok);
    if (!ok) return false;
    H = R;
    return true;
}

double pointSegmentDist(Pt p, Pt a, Pt b, double* tOut) {
    const Pt ab = b - a;
    const double L2 = dot(ab, ab);
    double t = L2 > 0 ? dot(p - a, ab) / L2 : 0;
    t = std::clamp(t, 0.0, 1.0);
    if (tOut) *tOut = t;
    return dist(p, a + ab * t);
}

double pointPolylineDist(Pt p, const std::vector<Pt>& poly, bool closed) {
    if (poly.empty()) return std::numeric_limits<double>::infinity();
    if (poly.size() == 1) return dist(p, poly[0]);
    double best = std::numeric_limits<double>::infinity();
    const size_t n = poly.size(), segs = closed ? n : n - 1;
    for (size_t i = 0; i < segs; ++i) best = std::min(best, pointSegmentDist(p, poly[i], poly[(i + 1) % n]));
    return best;
}

double polylineLength(const std::vector<Pt>& pts, bool closed) {
    double L = 0;
    for (size_t i = 1; i < pts.size(); ++i) L += dist(pts[i - 1], pts[i]);
    if (closed && pts.size() > 2) L += dist(pts.back(), pts.front());
    return L;
}

namespace {
void dpRecurse(const std::vector<Pt>& p, size_t a, size_t b, double tol, std::vector<char>& keep) {
    // Iterative to avoid deep recursion on long skeletons.
    std::vector<std::pair<size_t, size_t>> st{{a, b}};
    while (!st.empty()) {
        auto [i0, i1] = st.back();
        st.pop_back();
        if (i1 <= i0 + 1) continue;
        double dmax = -1;
        size_t imax = i0;
        for (size_t i = i0 + 1; i < i1; ++i) {
            const double d = pointSegmentDist(p[i], p[i0], p[i1]);
            if (d > dmax) { dmax = d; imax = i; }
        }
        if (dmax > tol) {
            keep[imax] = 1;
            st.push_back({i0, imax});
            st.push_back({imax, i1});
        }
    }
}
}  // namespace

std::vector<Pt> simplifyDP(const std::vector<Pt>& pts, double tol, bool closed) {
    if (pts.size() < 3 || tol <= 0) return pts;
    if (!closed) {
        std::vector<char> keep(pts.size(), 0);
        keep.front() = keep.back() = 1;
        dpRecurse(pts, 0, pts.size() - 1, tol, keep);
        std::vector<Pt> out;
        for (size_t i = 0; i < pts.size(); ++i)
            if (keep[i]) out.push_back(pts[i]);
        return out;
    }
    // Closed: split at the point farthest from pts[0].
    size_t far = 0;
    double dmax = -1;
    for (size_t i = 1; i < pts.size(); ++i) {
        const double d = dist(pts[i], pts[0]);
        if (d > dmax) { dmax = d; far = i; }
    }
    std::vector<Pt> ext(pts.begin(), pts.end());
    ext.push_back(pts[0]);
    std::vector<char> keep(ext.size(), 0);
    keep[0] = keep[far] = keep.back() = 1;
    dpRecurse(ext, 0, far, tol, keep);
    dpRecurse(ext, far, ext.size() - 1, tol, keep);
    std::vector<Pt> out;
    for (size_t i = 0; i + 1 < ext.size(); ++i)
        if (keep[i]) out.push_back(ext[i]);
    return out;
}

std::vector<Pt> smoothMovingAverage(const std::vector<Pt>& pts, int half, bool closed) {
    const int n = int(pts.size());
    if (half <= 0 || n < 3) return pts;
    std::vector<Pt> out(n);
    for (int i = 0; i < n; ++i) {
        int h = half;
        if (!closed) h = std::min({half, i, n - 1 - i});
        Pt s(0, 0);
        for (int k = -h; k <= h; ++k) s += pts[((i + k) % n + n) % n];
        out[i] = s / double(2 * h + 1);
    }
    return out;
}

std::vector<Pt> catmullRom(const std::vector<Pt>& pts, int segs, bool closed) {
    const int n = int(pts.size());
    if (n < 3 || segs < 2) return pts;
    auto at = [&](int i) -> Pt {
        if (closed) return pts[((i % n) + n) % n];
        return pts[std::clamp(i, 0, n - 1)];
    };
    std::vector<Pt> out;
    const int last = closed ? n : n - 1;
    for (int i = 0; i < last; ++i) {
        const Pt p0 = at(i - 1), p1 = at(i), p2 = at(i + 1), p3 = at(i + 2);
        for (int s = 0; s < segs; ++s) {
            const double t = double(s) / segs, t2 = t * t, t3 = t2 * t;
            out.push_back((p1 * 2 + (p2 - p0) * t + (p0 * 2 - p1 * 5 + p2 * 4 - p3) * t2 +
                           (p1 * 3 - p0 - p2 * 3 + p3) * t3) * 0.5);
        }
    }
    if (!closed) out.push_back(pts.back());
    return out;
}

std::vector<Pt> resampleUniform(const std::vector<Pt>& pts, double step, bool closed) {
    if (pts.size() < 2 || step <= 0) return pts;
    std::vector<Pt> src = pts;
    if (closed) src.push_back(pts.front());
    std::vector<Pt> out{src.front()};
    double carry = 0;
    for (size_t i = 1; i < src.size(); ++i) {
        const Pt a = src[i - 1], b = src[i];
        const double L = dist(a, b);
        double pos = step - carry;
        while (pos <= L) {
            out.push_back(a + (b - a) * (pos / L));
            pos += step;
        }
        carry = L - (pos - step);
    }
    if (!closed && dist(out.back(), src.back()) > 1e-9) out.push_back(src.back());
    if (closed && out.size() > 1 && dist(out.back(), out.front()) < step * 0.5) out.pop_back();
    return out;
}

}  // namespace cp
