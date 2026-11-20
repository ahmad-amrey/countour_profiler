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
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QString>
#include <QFileInfo>
#include <QDir>
#include <chrono>
#include <cstdio>
#include <fstream>

#include "calib.h"
#include "export.h"
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
    std::printf("usage:\n  cp_selftest synth <outdir>\n  cp_selftest run <outdir> <square_mm> <image>...\n");
    return 2;
}
