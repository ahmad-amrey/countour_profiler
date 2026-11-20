// cp_selftest - headless pipeline runner
//
//   cp_selftest synth <outdir>
//       Renders synthetic perspective photos (line grid bright/dark,
//       checkerboard), runs the full pipeline and compares the recovered
//       profile with ground truth. Exit code != 0 on failure.
//
//   cp_selftest run <outdir> <square_mm> <image> [image...]
//       Runs the automatic pipeline on real photos, writes overlays,
//       rectified images and DXF files, and prints measurements.
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QString>
#include <QFileInfo>
#include <QDir>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#include <string>

#include "calib.h"
#include "calsheet.h"
#include "export.h"
#include "fit.h"
#include "grid_detect.h"
#include "image.h"
#include "profile_detect.h"
#include "synth.h"

using namespace cp;

static QImage toQImage(const ImageU8& im) {
    QImage q(im.w, im.h, QImage::Format_RGB888);
    for (int y = 0; y < im.h; ++y) memcpy(q.scanLine(y), im.px(0, y), size_t(im.w) * 3);
    return q;
}
static ImageU8 fromQImage(const QImage& in) {
    const QImage q = in.convertToFormat(QImage::Format_RGB888);
    ImageU8 im;
    im.resize(q.width(), q.height(), 3);
    for (int y = 0; y < im.h; ++y) memcpy(im.px(0, y), q.constScanLine(y), size_t(im.w) * 3);
    return im;
}

struct RunOut {
    bool ok = false;
    GridResult grid;
    Calibration cal;
    RectD roi;
    ProfileResult prof;
};

static RunOut runPipeline(const ImageU8& img, PatternType pattern, double square) {
    RunOut r;
    auto t0 = std::chrono::steady_clock::now();
    auto secs = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
    r.grid = detectGrid(toGray(img), pattern);
    std::printf("  grid: %s [%.2fs]\n", r.grid.message.c_str(), secs());
    t0 = std::chrono::steady_clock::now();
    if (!r.grid.ok) return r;
    std::vector<Pt> ip, wp;
    for (const GridNode& n : r.grid.nodes) { ip.push_back(n.img); wp.push_back(gridWorld(n, square)); }
    r.cal = calibrate(ip, wp);
    if (!r.cal.ok) { std::printf("  calibration failed\n"); return r; }
    // ROI: detected grid extent, clipped to the image footprint.
    // ROI: detected grid extent plus a quarter square, clipped to the image footprint.
    RectD roi = boundingBox(wp, 0.25 * square);
    std::vector<Pt> foot;
    for (Pt c : {Pt(0, 0), Pt(img.w - 1, 0), Pt(img.w - 1, img.h - 1), Pt(0, img.h - 1)}) {
        bool ok = false;
        const Pt w = r.cal.Hi2w.apply(c, &ok);
        if (ok) foot.push_back(w);
    }
    if (foot.size() == 4) {
        const RectD fb = boundingBox(foot, 0);
        roi.x0 = std::max(roi.x0, fb.x0); roi.y0 = std::max(roi.y0, fb.y0);
        roi.x1 = std::min(roi.x1, fb.x1); roi.y1 = std::min(roi.y1, fb.y1);
    }
    r.roi = roi;
    ProfileParams pp;
    pp.pattern = pattern;
    pp.squareMM = square;
    const Pt c((roi.x0 + roi.x1) / 2, (roi.y0 + roi.y1) / 2);
    r.prof = detectProfile(img, r.cal.Hw2i, roi, pxPerMM(r.cal.Hw2i, c), pp);
    std::printf("  calibration RMS %.3f mm, %.1f px/mm; ROI %.1f x %.1f mm\n", r.cal.rmsMM, pxPerMM(r.cal.Hw2i, c),
                roi.w(), roi.h());
    std::printf("  %s (threshold %.1f) [%.2fs]\n", r.prof.message.c_str(), r.prof.thresholdUsed, secs());
    r.ok = r.prof.ok;
    return r;
}

static void writeOutputs(const QString& base, const ImageU8& img, const RunOut& r) {
    // Overlay on the original photo.
    QImage ov = toQImage(img);
    {
        QPainter p(&ov);
        p.setRenderHint(QPainter::Antialiasing);
        const double s = std::max(img.w, img.h) / 1400.0;
        p.setPen(QPen(QColor(0, 200, 255, 160), 1.5 * s));
        for (const auto& l : r.grid.lines) p.drawLine(QPointF(l.first.x, l.first.y), QPointF(l.second.x, l.second.y));
        p.setPen(QPen(QColor(255, 255, 0), 2 * s));
        for (const Pt& c : r.grid.candidates) p.drawEllipse(QPointF(c.x, c.y), 3 * s, 3 * s);
        p.setPen(QPen(QColor(0, 255, 0), 3 * s));
        for (const GridNode& n : r.grid.nodes) p.drawEllipse(QPointF(n.img.x, n.img.y), 9 * s, 9 * s);
        if (r.cal.ok) {
            p.setPen(QPen(QColor(255, 0, 255), 2 * s));
            QPolygonF roi;
            for (Pt w : {Pt(r.roi.x0, r.roi.y0), Pt(r.roi.x1, r.roi.y0), Pt(r.roi.x1, r.roi.y1), Pt(r.roi.x0, r.roi.y1)}) {
                const Pt q = r.cal.Hw2i.apply(w);
                roi << QPointF(q.x, q.y);
            }
            p.drawPolygon(roi);
            p.setPen(QPen(QColor(255, 0, 0), 3 * s));
            QPolygonF pl;
            for (const Pt& w : r.prof.pts) {
                const Pt q = r.cal.Hw2i.apply(w);
                pl << QPointF(q.x, q.y);
            }
            p.drawPolyline(pl);
        }
    }
    ov.scaled(ov.width() * 1400 / std::max(ov.width(), ov.height()), ov.height() * 1400 / std::max(ov.width(), ov.height()),
              Qt::KeepAspectRatio, Qt::SmoothTransformation)
        .save(base + "_overlay.jpg", nullptr, 88);
    if (!r.cal.ok) return;
    // Rectified ROI with mask and profile.
    const double res = std::min(40.0, std::sqrt(4.0e6 / (r.roi.w() * r.roi.h())));
    ImageU8 rect;
    warpPerspective(img, r.cal.Hw2i, r.roi, res, rect);
    QImage rq = toQImage(rect).convertToFormat(QImage::Format_ARGB32);
    {
        QPainter p(&rq);
        p.setRenderHint(QPainter::Antialiasing);
        if (!r.prof.mask.empty()) {
            QImage m(r.prof.maskW, r.prof.maskH, QImage::Format_ARGB32);
            for (int y = 0; y < m.height(); ++y)
                for (int x = 0; x < m.width(); ++x) {
                    const uint8_t v = r.prof.mask[size_t(y) * m.width() + x];
                    m.setPixel(x, y, v == 2 ? qRgba(0, 255, 0, 90) : v == 1 ? qRgba(255, 0, 0, 110) : qRgba(0, 0, 0, 0));
                }
            const RectD& a = r.prof.maskArea;
            p.drawImage(QRectF((a.x0 - r.roi.x0) * res, (a.y0 - r.roi.y0) * res, a.w() * res, a.h() * res), m);
        }
        p.setPen(QPen(QColor(255, 255, 0), 1.5));
        QPolygonF pl;
        for (const Pt& w : r.prof.pts) pl << QPointF((w.x - r.roi.x0) * res, (w.y - r.roi.y0) * res);
        p.drawPolyline(pl);
        p.setPen(QPen(QColor(255, 0, 255), 3));
        for (const QPointF& q : pl) p.drawPoint(q);
    }
    rq.save(base + "_rectified.png");
    if (std::getenv("CP_CLEAN_OUTPUT")) {
        // Overlay-free top view plus the profile in its pixel coordinates.
        toQImage(rect).save(base + "_topview.png");
        std::ofstream px(QString(base + "_topview_profile.csv").toStdString());
        px << "u,v\n";
        for (const Pt& w : r.prof.pts) px << (w.x - r.roi.x0) * res << "," << (w.y - r.roi.y0) * res << "\n";
    }
    // DXF in CAD orientation.
    ExportPolyline e;
    for (const Pt& w : r.prof.pts) e.pts.emplace_back(w.x, -w.y);
    e.closed = r.prof.closed;
    std::ofstream(QString(base + ".dxf").toStdString()) << makeDXF({e});
    std::ofstream(QString(base + ".csv").toStdString()) << makeCSV({e});
}

static int cmdSynth(const QString& outdir) {
    QDir().mkpath(outdir);
    struct Case { const char* name; PatternType pt; bool bright; } cases[] = {
        {"linegrid_bright", PatternType::LineGrid, true},
        {"linegrid_dark", PatternType::LineGrid, false},
        {"checkerboard", PatternType::Checkerboard, true},
    };
    int failures = 0;
    for (const Case& c : cases) {
        SynthParams sp;
        sp.pattern = c.pt;
        sp.brightLines = c.bright;
        if (c.pt == PatternType::Checkerboard) {
            // A wire must contrast with BOTH squares: use copper, not silver.
            sp.squareMM = 20; sp.cols = 10; sp.rows = 8;
            sp.wireColor[0] = 190; sp.wireColor[1] = 105; sp.wireColor[2] = 45;
        }
        const SynthResult syn = renderSynthetic(sp);
        std::printf("[%s]\n", c.name);
        const RunOut r = runPipeline(syn.img, c.pt, sp.squareMM);
        const QString base = outdir + "/" + c.name;
        toQImage(syn.img).save(base + ".jpg", nullptr, 92);
        writeOutputs(base, syn.img, r);
        if (!r.ok) { ++failures; std::printf("  FAIL: pipeline\n"); continue; }
        // Map the estimated profile into the ground-truth frame through the image.
        bool okInv = false;
        const Mat3 Hi2wTrue = syn.Hw2i.inverse(&okInv);
        std::vector<Pt> est;
        for (const Pt& w : r.prof.pts) est.push_back(Hi2wTrue.apply(r.cal.Hw2i.apply(w)));
        // The physical wire extends by its radius beyond the centre-line ends.
        std::vector<Pt> truth = syn.truth;
        {
            const double rad = 0.5 * sp.wireDiameterMM;
            const Pt d0 = truth[0] - truth[3], d1 = truth.back() - truth[truth.size() - 4];
            truth.insert(truth.begin(), truth[0] + d0 / norm(d0) * rad);
            truth.push_back(truth.back() + d1 / norm(d1) * rad);
        }
        double maxE = 0, sumE = 0;
        const std::vector<Pt> dense = resampleUniform(est, 0.1, false);
        for (const Pt& q : dense) {
            const double d = pointPolylineDist(q, truth, false);
            maxE = std::max(maxE, d);
            sumE += d;
        }
        double maxCov = 0;
        for (const Pt& q : truth) maxCov = std::max(maxCov, pointPolylineDist(q, est, false));
        // Calibration accuracy: grid node world error (frame offset removed).
        double calErr = 0;
        {
            Pt off(0, 0);
            std::vector<Pt> d;
            for (const GridNode& n : r.grid.nodes) {
                d.push_back(Hi2wTrue.apply(n.img) - gridWorld(n, sp.squareMM));
                off += d.back();
            }
            off = off / double(d.size());
            for (const Pt& q : d) calErr = std::max(calErr, dist(q, off));
        }
        const double lenT = polylineLength(truth, false), lenE = polylineLength(est, false);
        std::printf("  grid max node error %.3f mm | profile mean %.3f / max %.3f mm, coverage gap %.3f mm, length %.2f vs %.2f mm\n",
                    calErr, sumE / dense.size(), maxE, maxCov, lenE, lenT);
        const bool pass = calErr < 0.15 && maxE < 0.35 && maxCov < 0.5;
        std::printf("  %s\n", pass ? "PASS" : "FAIL");
        if (!pass) ++failures;
    }
    return failures;
}

// Shape difference of two open polylines after optimal rigid alignment
// (arc-length correspondence, Kabsch). Tries both directions.
static double alignedRms(const std::vector<Pt>& a, const std::vector<Pt>& b) {
    const int N = 400;
    auto sample = [N](const std::vector<Pt>& p) {
        std::vector<Pt> out = resampleUniform(p, polylineLength(p, false) / (N - 1), false);
        out.resize(size_t(N), out.empty() ? Pt() : out.back());
        return out;
    };
    const std::vector<Pt> A = sample(a);
    double best = 1e300;
    for (int rev = 0; rev < 2; ++rev) {
        std::vector<Pt> B = sample(b);
        if (rev) std::reverse(B.begin(), B.end());
        Pt ca(0, 0), cb(0, 0);
        for (int i = 0; i < N; ++i) { ca += A[size_t(i)]; cb += B[size_t(i)]; }
        ca = ca / N;
        cb = cb / N;
        double sxx = 0, sxy = 0;  // rotation angle from cross-covariance
        for (int i = 0; i < N; ++i) {
            const Pt p = A[size_t(i)] - ca, q = B[size_t(i)] - cb;
            sxx += dot(p, q);
            sxy += cross(q, p);
        }
        const double th = std::atan2(sxy, sxx), c = std::cos(th), s = std::sin(th);
        // Initial alignment: B rotated about its centroid onto A's centroid.
        std::vector<Pt> Bt(B.size());
        for (size_t i = 0; i < B.size(); ++i) {
            const Pt q = B[i] - cb;
            Bt[i] = ca + Pt(c * q.x - s * q.y, s * q.x + c * q.y);
        }
        // ICP refinement: interior points of A (tips excluded) to nearest
        // point on B's polyline.
        const double tip = 1.0;  // mm
        const double La = polylineLength(A, false);
        std::vector<Pt> core;
        double acc = 0;
        for (int i = 0; i < N; ++i) {
            if (i) acc += dist(A[size_t(i)], A[size_t(i - 1)]);
            if (acc > tip && acc < La - tip) core.push_back(A[size_t(i)]);
        }
        double rms = 1e300;
        for (int it = 0; it < 40 && core.size() > 10; ++it) {
            std::vector<Pt> match(core.size());
            double ss = 0;
            for (size_t k = 0; k < core.size(); ++k) {
                double bd = 1e300;
                for (size_t j = 1; j < Bt.size(); ++j) {
                    double t;
                    const double d = pointSegmentDist(core[k], Bt[j - 1], Bt[j], &t);
                    if (d < bd) { bd = d; match[k] = Bt[j - 1] + (Bt[j] - Bt[j - 1]) * t; }
                }
                ss += bd * bd;
            }
            rms = std::sqrt(ss / core.size());
            // Rigid transform moving the matches onto the core points.
            Pt cm(0, 0), cc(0, 0);
            for (size_t k = 0; k < core.size(); ++k) { cm += match[k]; cc += core[k]; }
            cm = cm / double(core.size());
            cc = cc / double(core.size());
            double a = 0, b = 0;
            for (size_t k = 0; k < core.size(); ++k) {
                const Pt p = core[k] - cc, q = match[k] - cm;
                a += dot(q, p);
                b += cross(q, p);
            }
            const double t2 = std::atan2(b, a), c2 = std::cos(t2), s2 = std::sin(t2);
            for (Pt& q0 : Bt) {
                const Pt q = q0 - cm;
                q0 = cc + Pt(c2 * q.x - s2 * q.y, s2 * q.x + c2 * q.y);
            }
        }
        best = std::min(best, rms);
    }
    return best;
}

// Same physical wire photographed several times: every run must succeed and
// all profiles must agree in shape (rigid alignment) and length.
static int cmdConsistency(const QString& outdir, double square, const QStringList& files, double maxRms) {
    QDir().mkpath(outdir);
    std::vector<std::vector<Pt>> profiles;
    int failures = 0;
    for (const QString& f : files) {
        QImageReader rd(f);
        rd.setAutoTransform(true);
        const QImage q = rd.read();
        std::printf("[%s] %dx%d\n", QFileInfo(f).fileName().toLocal8Bit().constData(), q.width(), q.height());
        if (q.isNull()) { ++failures; continue; }
        const ImageU8 img = fromQImage(q);
        const RunOut r = runPipeline(img, PatternType::LineGrid, square);
        writeOutputs(outdir + "/" + QFileInfo(f).completeBaseName(), img, r);
        if (!r.ok) { ++failures; std::printf("  FAIL: no profile\n"); continue; }
        profiles.push_back(r.prof.pts);
        {
            FitOptions fo;
            fo.tol = 0.05;
            const auto segs = fitLinesArcs(r.prof.pts, false, fo);
            std::string desc;
            for (const auto& sg : segs)
                desc += sg.arc ? " arc(R" + std::to_string(sg.r).substr(0, 4) + ")" : " line(" + std::to_string(sg.length()).substr(0, 4) + ")";
            std::printf("  lines & arcs:%s\n", desc.c_str());
        }
    }
    if (profiles.size() >= 2) {
        std::vector<double> lens;
        for (const auto& p : profiles) lens.push_back(polylineLength(p, false));
        std::vector<double> sorted = lens;
        std::sort(sorted.begin(), sorted.end());
        const double med = sorted[sorted.size() / 2];
        for (size_t i = 0; i < profiles.size(); ++i) {
            const double dev = std::fabs(lens[i] - med) / med;
            const double rms = i ? alignedRms(profiles[0], profiles[i]) : 0.0;
            const bool ok = dev < 0.04 && rms < maxRms;
            std::printf("  #%zu length %.2f mm (median %.2f, %+.1f%%), shape RMS vs #1 %.3f mm  %s\n", i + 1, lens[i], med,
                        100 * (lens[i] - med) / med, rms, ok ? "ok" : "FAIL");
            if (!ok) ++failures;
        }
    }
    std::printf("%s\n", failures ? "FAIL" : "PASS");
    return failures;
}

// Line/arc fitting against known geometry: dense noisy trace and sparse clicks.
static int cmdFit() {
    const double kPi = 3.14159265358979323846;
    // Truth: line (0,0)->(20,0), arc R10 turning +90deg, line 15, arc R4 turning -60deg.
    std::vector<Segment> truth;
    auto addLine = [&](Pt a, Pt b) { Segment s; s.a = a; s.b = b; truth.push_back(s); };
    auto addArc = [&](Pt a, double dirAng, double R, double sweep) {
        Segment s; s.arc = true; s.a = a; s.r = R; s.sweep = sweep;
        const double side = sweep > 0 ? 1 : -1;  // centre to the left (+) of travel
        s.c = a + Pt(-std::sin(dirAng), std::cos(dirAng)) * (R * side);
        s.b = s.pointAt(1.0);
        truth.push_back(s);
        return s.b;
    };
    addLine(Pt(0, 0), Pt(20, 0));
    Pt e = addArc(Pt(20, 0), 0, 10, kPi / 2);
    const Pt e2 = e + Pt(0, 15);
    addLine(e, e2);
    addArc(e2, kPi / 2, 4, -kPi / 3);
    int failures = 0;
    auto report = [&](const char* name, const std::vector<Pt>& pts, double tol, int expLines, int expArcs) {
        FitOptions o; o.tol = tol;
        const auto segs = fitLinesArcs(pts, false, o);
        int nl = 0, na = 0; double maxErr = 0;
        std::string radii;
        for (const auto& s : segs) {
            (s.arc ? na : nl)++;
            maxErr = std::max(maxErr, s.maxErr);
            if (s.arc) radii += " R" + std::to_string(s.r).substr(0, 5);
        }
        const bool ok = nl == expLines && na == expArcs;
        std::printf("[fit %s] %zu points -> %d lines, %d arcs (%s), max dev %.3f mm  %s\n", name, pts.size(), nl, na,
                    radii.c_str(), maxErr, ok ? "PASS" : "FAIL");
        if (!ok) ++failures;
        return segs;
    };
    // Dense trace with noise (like a detected wire), 0.05 mm spacing.
    std::mt19937 rng(7);
    std::normal_distribution<double> noise(0, 0.01);
    std::vector<Pt> dense;
    for (const auto& s : truth) {
        const int k = std::max(2, int(s.length() / 0.05));
        for (int i = dense.empty() ? 0 : 1; i <= k; ++i) {
            const Pt q = s.pointAt(double(i) / k);
            dense.push_back(q + Pt(noise(rng), noise(rng)));
        }
    }
    // As in the app: simplify first, then fit.
    const auto segsDense = report("dense trace", dense, 0.05, 2, 2);
    report("simplified trace", simplifyDP(dense, 0.02, false), 0.05, 2, 2);
    for (const auto& s : segsDense)
        if (s.arc && std::fabs(s.r - 10) > 0.1 && std::fabs(s.r - 4) > 0.1) { std::printf("  radius off\n"); ++failures; }
    // Sparse clicks: a few points per feature, slightly off (hand placement).
    std::vector<Pt> clicks = {Pt(0, 0.05), Pt(10, -0.04), Pt(20, 0)};
    for (double f : {0.33, 0.66, 1.0}) clicks.push_back(truth[1].pointAt(f) + Pt(0.03, -0.03));
    clicks.push_back(truth[2].pointAt(0.5));
    clicks.push_back(truth[2].b);
    for (double f : {0.5, 1.0}) clicks.push_back(truth[3].pointAt(f));
    report("sparse clicks", clicks, 0.1, 2, 2);
    // Arcs must reach the DXF as bulges, and the bulge must reproduce the arc.
    {
        ExportPolyline e;
        int arcs = 0;
        for (const auto& s : segsDense) {
            e.pts.push_back(s.a);
            e.bulge.push_back(s.bulge());
            arcs += s.arc;
        }
        e.pts.push_back(segsDense.back().b);
        const std::string dxf = makeDXF({e});
        int n42 = 0;
        for (size_t pos = 0; (pos = dxf.find("\n42\n", pos)) != std::string::npos; ++pos) ++n42;
        double worst = 0;
        for (const auto& s : segsDense) {
            if (!s.arc) continue;
            const double chord = dist(s.a, s.b), b = std::fabs(s.bulge());
            worst = std::max(worst, std::fabs(chord * (1 + b * b) / (4 * b) - s.r));
        }
        const bool ok = n42 == arcs && worst < 1e-6;
        std::printf("[fit dxf] %d arcs -> %d bulge vertices, radius round-trip error %.2g mm  %s\n", arcs, n42, worst,
                    ok ? "PASS" : "FAIL");
        if (!ok) ++failures;
    }
    return failures;
}

// Printed calibration sheets: render each variant, draw a wire on it,
// photograph it in perspective, and require the pipeline to read it back.
static int cmdSheet(const QString& outdir) {
    QDir().mkpath(outdir);
    struct V { const char* name; bool checker, dark; QColor wire; };
    const V variants[] = {
        {"sheet_grid_white", false, false, QColor(150, 75, 35)},      // copper wire
        {"sheet_grid_dark", false, true, QColor(205, 205, 212)},      // silver wire
        {"sheet_checkerboard", true, false, QColor(190, 100, 40)},    // copper wire
    };
    const double kPx = 8.0;  // sheet pixels per mm
    int failures = 0;
    for (const V& v : variants) {
        SheetOptions o;
        o.checkerboard = v.checker;
        o.dark = v.dark;
        o.squareMM = 10;
        writeSheetPdf(outdir + "/" + v.name + ".pdf", o);
        QImage sheet = renderSheet(o, kPx);
        // Wire (truth in sheet mm), drawn round-capped 0.7 mm thick.
        std::vector<Pt> truth;
        for (int i = 0; i <= 300; ++i) {
            const double t = i / 300.0;
            truth.emplace_back(75 + 60 * t, 170 - 45 * std::sin(3.14159265358979 * t) + 6 * std::sin(3 * 3.14159265358979 * t));
        }
        {
            QPainter p(&sheet);
            p.setRenderHint(QPainter::Antialiasing);
            p.scale(kPx, kPx);
            p.setPen(QPen(v.wire, 0.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            QPolygonF poly;
            for (const Pt& q : truth) poly << QPointF(q.x, q.y);
            p.drawPolyline(poly);
        }
        sheet.save(outdir + "/" + v.name + "_flat.png");
        // Perspective photo of the sheet lying on a table.
        const ImageU8 sheetU8 = fromQImage(sheet);
        const double W = sheet.width(), H = sheet.height();
        Mat3 Hs2p;
        computeHomography({Pt(0, 0), Pt(W, 0), Pt(W, H), Pt(0, H)},
                          {Pt(520, 90), Pt(1480, 150), Pt(1760, 1440), Pt(230, 1380)}, Hs2p);
        bool okInv = false;
        const Mat3 Hp2s = Hs2p.inverse(&okInv);
        ImageU8 photo;
        warpPerspective(sheetU8, Hp2s, RectD{0, 0, 2000, 1500}, 1.0, photo);
        for (auto& b : photo.d) b = uint8_t(std::min(255, b + 18));  // table instead of black
        std::printf("[%s]\n", v.name);
        const RunOut r = runPipeline(photo, v.checker ? PatternType::Checkerboard : PatternType::LineGrid, 10);
        writeOutputs(outdir + "/" + v.name, photo, r);
        if (!r.ok) { ++failures; std::printf("  FAIL: no profile\n"); continue; }
        std::vector<Pt> truthMM = truth;
        const double lenT = polylineLength(truthMM, false) + 0.7, lenE = polylineLength(r.prof.pts, false);
        const double rms = alignedRms(truthMM, r.prof.pts);
        const bool ok = std::fabs(lenE - lenT) / lenT < 0.03 && rms < 0.15;
        std::printf("  length %.2f vs %.2f mm, shape RMS %.3f mm  %s\n", lenE, lenT, rms, ok ? "PASS" : "FAIL");
        if (!ok) ++failures;
    }
    return failures;
}

static int cmdRun(const QString& outdir, double square, const QStringList& files) {
    QDir().mkpath(outdir);
    int failures = 0;
    for (const QString& f : files) {
        QImageReader rd(f);
        rd.setAutoTransform(true);
        const QImage q = rd.read();
        std::printf("[%s] %dx%d\n", QFileInfo(f).fileName().toLocal8Bit().constData(), q.width(), q.height());
        if (q.isNull()) { ++failures; continue; }
        const ImageU8 img = fromQImage(q);
        const RunOut r = runPipeline(img, PatternType::LineGrid, square);
        if (r.ok) {
            double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
            for (const Pt& p : r.prof.pts) {
                x0 = std::min(x0, p.x); y0 = std::min(y0, p.y);
                x1 = std::max(x1, p.x); y1 = std::max(y1, p.y);
            }
            std::printf("  profile bbox %.2f x %.2f mm, end-to-end %.2f mm, length %.2f mm\n", x1 - x0, y1 - y0,
                        dist(r.prof.pts.front(), r.prof.pts.back()), polylineLength(r.prof.pts, false));
        } else {
            ++failures;
        }
        writeOutputs(outdir + "/" + QFileInfo(f).completeBaseName(), img, r);
    }
    return failures;
}

int main(int argc, char** argv) {
    if (argc >= 3 && QString(argv[1]) == "synth") return cmdSynth(argv[2]);
    if (argc >= 5 && QString(argv[1]) == "run") {
        QStringList files;
        for (int i = 4; i < argc; ++i) files << QString::fromLocal8Bit(argv[i]);
        return cmdRun(argv[2], QString(argv[3]).toDouble(), files);
    }
    if (argc >= 2 && QString(argv[1]) == "fit") return cmdFit();
    if (argc >= 3 && QString(argv[1]) == "sheet") {
        // Text rendering needs a GUI application; no window is ever shown.
#if !defined(_WIN32) && !defined(__APPLE__)
        if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
        QGuiApplication app(argc, argv);
        return cmdSheet(argv[2]);
    }
    if (argc >= 6 && QString(argv[1]) == "consistency") {
        QStringList files;
        for (int i = 4; i < argc; ++i) files << QString::fromLocal8Bit(argv[i]);
        return cmdConsistency(argv[2], QString(argv[3]).toDouble(), files, 0.15);
    }
    std::printf("usage:\n  cp_selftest synth <outdir>\n  cp_selftest run <outdir> <square_mm> <image>...\n"
                "  cp_selftest consistency <outdir> <square_mm> <image> <image>...\n");
    return 2;
}
