#include "fit.h"

#include <algorithm>
#include <limits>

namespace cp {

namespace {
constexpr double kPi = 3.14159265358979323846;

double wrapPi(double a) {
    while (a > kPi) a -= 2 * kPi;
    while (a <= -kPi) a += 2 * kPi;
    return a;
}

double lineError(const std::vector<Pt>& p, int i, int j) {
    double e = 0;
    for (int k = i + 1; k < j; ++k) e = std::max(e, pointSegmentDist(p[size_t(k)], p[size_t(i)], p[size_t(j)]));
    return e;
}

struct ArcFit {
    bool ok = false;
    Pt c;
    double r = 0, sweep = 0, err = 0;
};

// Best circle through p[i] and p[j] (centre on the chord's bisector) for the
// vertices between them. Closed-form least squares on the algebraic distance.
ArcFit arcThrough(const std::vector<Pt>& p, int i, int j, double maxR) {
    ArcFit f;
    if (j - i < 2) return f;
    const Pt a = p[size_t(i)], b = p[size_t(j)];
    const Pt ch = b - a;
    const double L = norm(ch);
    if (L < 1e-12) return f;
    const Pt n(-ch.y / L, ch.x / L);
    const Pt m = ch * 0.5;  // midpoint relative to a
    double sab = 0, sbb = 0;
    for (int k = i + 1; k < j; ++k) {
        const Pt q = p[size_t(k)] - a;
        const double alpha = dot(q, q) - 2 * dot(m, q);
        const double beta = 2 * dot(n, q);
        sab += alpha * beta;
        sbb += beta * beta;
    }
    if (sbb < 1e-18) return f;  // collinear
    const double t = sab / sbb;
    f.c = a + m + n * t;
    f.r = dist(a, f.c);
    if (!(f.r < maxR)) return f;
    // Sweep: unwrap the angles of the vertices; they must progress one way.
    double prev = std::atan2(a.y - f.c.y, a.x - f.c.x), acc = 0;
    int sign = 0;
    for (int k = i + 1; k <= j; ++k) {
        const Pt q = p[size_t(k)] - f.c;
        const double ang = std::atan2(q.y, q.x);
        const double d = wrapPi(ang - prev);
        const int s = d > 0 ? 1 : (d < 0 ? -1 : 0);
        if (s != 0) {
            if (sign != 0 && s != sign) return f;
            sign = s;
        }
        acc += d;
        prev = ang;
    }
    if (std::fabs(acc) < 1e-6 || std::fabs(acc) > 2 * kPi - 1e-3) return f;
    f.sweep = acc;
    for (int k = i + 1; k < j; ++k) f.err = std::max(f.err, std::fabs(dist(p[size_t(k)], f.c) - f.r));
    f.ok = true;
    return f;
}
}  // namespace

Pt Segment::pointAt(double t) const {
    if (!arc) return a + (b - a) * t;
    const double a0 = std::atan2(a.y - c.y, a.x - c.x) + sweep * t;
    return Pt(c.x + r * std::cos(a0), c.y + r * std::sin(a0));
}

std::vector<Segment> fitLinesArcs(const std::vector<Pt>& in, bool closed, const FitOptions& opt) {
    std::vector<Pt> p;
    for (const Pt& q : in)
        if (p.empty() || dist(p.back(), q) > 1e-9) p.push_back(q);
    if (closed && p.size() > 2 && dist(p.front(), p.back()) > 1e-9) p.push_back(p.front());
    const int n = int(p.size());
    std::vector<Segment> out;
    if (n < 2) return out;

    // Dynamic programming over break points: fewest segments within tolerance.
    const double inf = std::numeric_limits<double>::infinity();
    std::vector<double> best(size_t(n), inf);
    std::vector<int> from(size_t(n), -1);
    std::vector<Segment> via(static_cast<size_t>(n));
    best[0] = 0;
    for (int i = 0; i < n - 1; ++i) {
        if (best[size_t(i)] == inf) continue;
        int fails = 0;
        for (int j = i + 1; j < n; ++j) {
            Segment s;
            s.a = p[size_t(i)];
            s.b = p[size_t(j)];
            s.i0 = i;
            s.i1 = j;
            double cost = inf;
            const double le = lineError(p, i, j);
            if (le <= opt.tol) {
                cost = 1;
                s.maxErr = le;
            } else {
                const ArcFit af = arcThrough(p, i, j, opt.maxRadius);
                if (af.ok && af.err <= opt.tol) {
                    cost = 1 + opt.arcPenalty;
                    s.arc = true;
                    s.c = af.c;
                    s.r = af.r;
                    s.sweep = af.sweep;
                    s.maxErr = af.err;
                }
            }
            if (cost == inf) {
                if (++fails > 8) break;  // longer spans will not fit either
                continue;
            }
            fails = 0;
            // Among equally short solutions prefer the most accurate one.
            cost += 0.05 * s.maxErr / std::max(1e-12, opt.tol);
            if (best[size_t(i)] + cost < best[size_t(j)] - 1e-9) {
                best[size_t(j)] = best[size_t(i)] + cost;
                from[size_t(j)] = i;
                via[size_t(j)] = s;
            }
        }
    }
    for (int j = n - 1; j > 0; j = from[size_t(j)]) {
        if (from[size_t(j)] < 0) return {};
        out.push_back(via[size_t(j)]);
    }
    std::reverse(out.begin(), out.end());
    return out;
}

std::vector<Pt> sampleSegments(const std::vector<Segment>& segs, double maxStep) {
    std::vector<Pt> out;
    for (const Segment& s : segs) {
        if (out.empty()) out.push_back(s.a);
        if (!s.arc) {
            out.push_back(s.b);
            continue;
        }
        const int k = std::max(2, int(std::ceil(s.length() / std::max(1e-6, maxStep))));
        const int kk = std::max(k, int(std::ceil(std::fabs(s.sweep) / 0.05)));
        for (int i = 1; i <= kk; ++i) out.push_back(s.pointAt(double(i) / kk));
    }
    return out;
}

}  // namespace cp
