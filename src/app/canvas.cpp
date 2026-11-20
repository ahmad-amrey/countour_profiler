#include "canvas.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace {
constexpr double kHit = 9.0;  // screen pixels
QColor errColor(double e) {
    if (e < 0.1) return QColor(60, 220, 90);
    if (e < 0.3) return QColor(255, 190, 40);
    return QColor(255, 70, 60);
}
}  // namespace

Canvas::Canvas(Document* doc, QWidget* parent) : QWidget(parent), doc_(doc) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(400, 300);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void Canvas::setView(View v) {
    if (v == view_) return;
    view_ = v;
    drag_ = Drag::None;
    fitToWindow();
}

void Canvas::setTool(Tool t) {
    tool_ = t;
    setCursor(t == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
    update();
}

void Canvas::imageChanged() {
    origPix_ = doc_->hasImage() ? QPixmap::fromImage(doc_->image) : QPixmap();
    rectPix_ = QPixmap();
    selRef_ = selVertex_ = hoverRef_ = hoverVertex_ = -1;
    fitToWindow();
}

void Canvas::rectifiedChanged() {
    const bool hadPix = !rectPix_.isNull();
    rectPix_ = doc_->rectified.isNull() ? QPixmap() : QPixmap::fromImage(doc_->rectified);
    if (view_ == View::Rectified && !hadPix) fitToWindow();
    update();
}

QSizeF Canvas::contentSize() const {
    const QPixmap& pm = view_ == View::Original ? origPix_ : rectPix_;
    return pm.isNull() ? QSizeF(1, 1) : QSizeF(pm.size());
}

void Canvas::fitToWindow() {
    const QSizeF c = contentSize();
    if (c.width() <= 1 || width() < 10) { update(); return; }
    zoom_ = std::min((width() - 20) / c.width(), (height() - 20) / c.height());
    pan_ = QPointF((width() - c.width() * zoom_) / 2, (height() - c.height() * zoom_) / 2);
    update();
}

void Canvas::resizeEvent(QResizeEvent*) { fitToWindow(); }

// --- coordinate mapping ------------------------------------------------------
bool Canvas::worldToDisp(cp::Pt w, QPointF& d) const {
    if (view_ == View::Rectified) {
        if (doc_->rectRes <= 0) return false;
        d = QPointF((w.x - doc_->rectArea.x0) * doc_->rectRes, (w.y - doc_->rectArea.y0) * doc_->rectRes);
        return true;
    }
    if (!doc_->cal.ok) return false;
    bool ok = false;
    const cp::Pt p = doc_->cal.Hw2i.apply(w, &ok);
    d = QPointF(p.x + 0.5, p.y + 0.5);
    return ok;
}

bool Canvas::dispToWorld(QPointF d, cp::Pt& w) const {
    if (view_ == View::Rectified) {
        if (doc_->rectRes <= 0) return false;
        w = cp::Pt(doc_->rectArea.x0 + d.x() / doc_->rectRes, doc_->rectArea.y0 + d.y() / doc_->rectRes);
        return true;
    }
    if (!doc_->cal.ok) return false;
    bool ok = false;
    w = doc_->cal.Hi2w.apply(cp::Pt(d.x() - 0.5, d.y() - 0.5), &ok);
    return ok;
}

bool Canvas::imgToDisp(cp::Pt p, QPointF& d) const {
    if (view_ == View::Original) {
        d = QPointF(p.x + 0.5, p.y + 0.5);
        return true;
    }
    if (!doc_->cal.ok) return false;
    bool ok = false;
    const cp::Pt w = doc_->cal.Hi2w.apply(p, &ok);
    return ok && worldToDisp(w, d);
}

bool Canvas::dispToImg(QPointF d, cp::Pt& p) const {
    if (view_ == View::Original) {
        p = cp::Pt(d.x() - 0.5, d.y() - 0.5);
        return true;
    }
    cp::Pt w;
    if (!dispToWorld(d, w) || !doc_->cal.ok) return false;
    bool ok = false;
    p = doc_->cal.Hw2i.apply(w, &ok);
    return ok;
}

// --- hit testing -------------------------------------------------------------
int Canvas::hitVertex(QPointF s) const {
    int best = -1;
    double bd = kHit;
    for (size_t i = 0; i < doc_->profile.size(); ++i) {
        QPointF d;
        if (!worldToDisp(doc_->profile[i], d)) continue;
        const double dd = std::hypot(toScreen(d).x() - s.x(), toScreen(d).y() - s.y());
        if (dd < bd) { bd = dd; best = int(i); }
    }
    return best;
}

int Canvas::hitRef(QPointF s) const {
    int best = -1;
    double bd = kHit;
    for (size_t i = 0; i < doc_->refs.size(); ++i) {
        QPointF d;
        if (!imgToDisp(doc_->refs[i].img, d)) continue;
        const double dd = std::hypot(toScreen(d).x() - s.x(), toScreen(d).y() - s.y());
        if (dd < bd) { bd = dd; best = int(i); }
    }
    return best;
}

bool Canvas::hitSegment(QPointF s, int& seg, cp::Pt& world) const {
    const auto& P = doc_->profile;
    if (P.size() < 2) return false;
    const size_t n = P.size(), segs = doc_->closed ? n : n - 1;
    double bd = kHit;
    seg = -1;
    for (size_t i = 0; i < segs; ++i) {
        QPointF a, b;
        if (!worldToDisp(P[i], a) || !worldToDisp(P[(i + 1) % n], b)) continue;
        a = toScreen(a);
        b = toScreen(b);
        double t = 0;
        const double d = cp::pointSegmentDist(cp::Pt(s.x(), s.y()), cp::Pt(a.x(), a.y()), cp::Pt(b.x(), b.y()), &t);
        if (d < bd) {
            bd = d;
            seg = int(i);
            world = P[i] + (P[(i + 1) % n] - P[i]) * t;
        }
    }
    return seg >= 0;
}

cp::Pt Canvas::snapImage(cp::Pt img, QPointF screen) const {
    if (!snap_ || view_ != View::Original) return img;
    cp::Pt best = img;
    double bd = 12.0;  // screen px
    auto consider = [&](const cp::Pt& c) {
        const QPointF s = toScreen(QPointF(c.x + 0.5, c.y + 0.5));
        const double d = std::hypot(s.x() - screen.x(), s.y() - screen.y());
        if (d < bd) { bd = d; best = c; }
    };
    for (const auto& n : doc_->grid.nodes) consider(n.img);
    for (const auto& c : doc_->grid.candidates) consider(c);
    return best;
}

// --- painting ----------------------------------------------------------------
void Canvas::drawWorldGrid(QPainter& p) {
    if (!showGrid_ || !doc_->cal.ok || doc_->refs.empty()) return;
    const double s = doc_->squareMM;
    if (s <= 0) return;
    cp::RectD r = doc_->rectArea.valid() ? doc_->rectArea : doc_->detectRoi;
    if (!r.valid()) return;
    p.setPen(QPen(QColor(0, 200, 255, 110), 1.0));
    const int i0 = int(std::ceil(r.x0 / s)), i1 = int(std::floor(r.x1 / s));
    const int j0 = int(std::ceil(r.y0 / s)), j1 = int(std::floor(r.y1 / s));
    if (i1 - i0 > 400 || j1 - j0 > 400) return;
    for (int i = i0; i <= i1; ++i) {
        QPointF a, b;
        if (worldToDisp(cp::Pt(i * s, r.y0), a) && worldToDisp(cp::Pt(i * s, r.y1), b)) p.drawLine(toScreen(a), toScreen(b));
    }
    for (int j = j0; j <= j1; ++j) {
        QPointF a, b;
        if (worldToDisp(cp::Pt(r.x0, j * s), a) && worldToDisp(cp::Pt(r.x1, j * s), b)) p.drawLine(toScreen(a), toScreen(b));
    }
}

void Canvas::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(38, 40, 44));
    const QPixmap& pm = view_ == View::Original ? origPix_ : rectPix_;
    if (pm.isNull()) {
        p.setPen(QColor(170, 170, 170));
        const QString msg = !doc_->hasImage()
                                ? tr("Open a photo (File > Open, or drag && drop an image here).")
                                : tr("No calibration yet: detect the grid or add at least 4 reference points.");
        p.drawText(rect(), Qt::AlignCenter, msg);
        if (pm.isNull() && view_ == View::Rectified) return;
        if (!doc_->hasImage()) return;
    }
    p.setRenderHint(QPainter::SmoothPixmapTransform, zoom_ < 2.0);
    p.save();
    p.translate(pan_);
    p.scale(zoom_, zoom_);
    p.drawPixmap(0, 0, pm);
    if (view_ == View::Rectified && showMask_ && !doc_->maskImage.isNull() && doc_->rectRes > 0) {
        const cp::RectD& a = doc_->maskArea;
        const double r = doc_->rectRes;
        p.drawImage(QRectF((a.x0 - doc_->rectArea.x0) * r, (a.y0 - doc_->rectArea.y0) * r, a.w() * r, a.h() * r),
                    doc_->maskImage);
    }
    p.restore();
    p.setRenderHint(QPainter::Antialiasing, true);

    drawWorldGrid(p);

    // Grid corner candidates.
    if (showCand_ && view_ == View::Original) {
        p.setPen(QPen(QColor(255, 255, 0, 160), 1));
        for (const auto& c : doc_->grid.candidates) {
            const QPointF s = toScreen(QPointF(c.x + 0.5, c.y + 0.5));
            p.drawEllipse(s, 2.5, 2.5);
        }
    }

    // Detection region.
    if (doc_->detectRoi.valid() && doc_->cal.ok) {
        const cp::RectD& r = doc_->detectRoi;
        QPolygonF poly;
        bool ok = true;
        for (cp::Pt w : {cp::Pt(r.x0, r.y0), cp::Pt(r.x1, r.y0), cp::Pt(r.x1, r.y1), cp::Pt(r.x0, r.y1)}) {
            QPointF d;
            ok = ok && worldToDisp(w, d);
            poly << toScreen(d);
        }
        if (ok) {
            p.setPen(QPen(QColor(255, 0, 255, 170), 1.2, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawPolygon(poly);
        }
    }

    // Reference points.
    QFont f = p.font();
    f.setPointSizeF(8);
    p.setFont(f);
    const bool manyRefs = doc_->refs.size() > 40;
    for (size_t i = 0; i < doc_->refs.size(); ++i) {
        QPointF d;
        if (!imgToDisp(doc_->refs[i].img, d)) continue;
        const QPointF s = toScreen(d);
        const bool sel = int(i) == selRef_, hov = int(i) == hoverRef_;
        const QColor c = doc_->cal.ok ? errColor(doc_->refs[i].errMM) : QColor(80, 200, 255);
        p.setPen(QPen(sel ? QColor(255, 255, 255) : c, sel || hov ? 2.5 : 1.6));
        p.setBrush(Qt::NoBrush);
        const double rr = sel || hov ? 7 : 5;
        p.drawEllipse(s, rr, rr);
        p.drawLine(s - QPointF(rr + 3, 0), s - QPointF(rr - 2, 0));
        p.drawLine(s + QPointF(rr - 2, 0), s + QPointF(rr + 3, 0));
        if (!manyRefs || sel || hov) {
            p.setPen(QColor(255, 255, 255));
            p.drawText(s + QPointF(8, -6), QString::number(i + 1));
        }
    }

    // Profile.
    if (!doc_->profile.empty()) {
        QPolygonF poly;
        for (const auto& w : doc_->profile) {
            QPointF d;
            if (worldToDisp(w, d)) poly << toScreen(d);
        }
        p.setPen(QPen(QColor(0, 0, 0, 160), 4.5));
        if (doc_->closed) p.drawPolygon(poly); else p.drawPolyline(poly);
        p.setPen(QPen(QColor(255, 140, 0), 2.0));
        if (doc_->closed) p.drawPolygon(poly); else p.drawPolyline(poly);
        const bool showVerts = poly.size() < 400 || zoom_ > 1.5;
        for (int i = 0; i < poly.size() && showVerts; ++i) {
            const bool sel = i == selVertex_, hov = i == hoverVertex_;
            const double r = sel || hov ? 4.5 : 2.8;
            p.setPen(QPen(Qt::black, 1));
            p.setBrush(sel ? QColor(255, 255, 255) : (i == 0 ? QColor(80, 255, 80) : QColor(255, 200, 0)));
            p.drawRect(QRectF(poly[i].x() - r, poly[i].y() - r, 2 * r, 2 * r));
        }
    }

    // Scale bar (rectified view).
    if (view_ == View::Rectified && doc_->rectRes > 0) {
        double mm = 1;
        for (double c : {1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0})
            if (c * doc_->rectRes * zoom_ < 160) mm = c;
        const double L = mm * doc_->rectRes * zoom_;
        const QPointF a(20, height() - 24);
        p.setPen(QPen(Qt::black, 5));
        p.drawLine(a, a + QPointF(L, 0));
        p.setPen(QPen(Qt::white, 3));
        p.drawLine(a, a + QPointF(L, 0));
        p.drawText(a + QPointF(0, -8), QString("%1 mm").arg(mm));
    }

    // Tool hint.
    QString hint;
    switch (tool_) {
        case Tool::Select: hint = tr("Edit: drag points/vertices, Ctrl+click or double-click a segment to insert, right-click to delete"); break;
        case Tool::AddRef: hint = tr("Add reference point: click a known mark (snaps to grid corners), then set its mm coordinates"); break;
        case Tool::DrawProfile: hint = tr("Draw profile: click to add vertices, Backspace removes the last, Enter/Esc finishes"); break;
        case Tool::PickObject: hint = tr("Click on the wire / metal piece to select it"); break;
        case Tool::PickColor: hint = tr("Click on the wire to sample its colour"); break;
    }
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 150));
    const QRectF hr(8, 8, std::min(width() - 16.0, p.fontMetrics().horizontalAdvance(hint) + 16.0), 22);
    p.drawRoundedRect(hr, 4, 4);
    p.setPen(QColor(235, 235, 235));
    p.drawText(hr, Qt::AlignCenter, hint);
}

// --- interaction -------------------------------------------------------------
void Canvas::wheelEvent(QWheelEvent* e) {
    const double f = std::pow(1.0015, e->angleDelta().y());
    const QPointF c = e->position();
    const QPointF d = toDisp(c);
    zoom_ = std::clamp(zoom_ * f, 0.02, 80.0);
    pan_ = c - d * zoom_;
    update();
}

void Canvas::mousePressEvent(QMouseEvent* e) {
    const QPointF s = e->position();
    lastMouse_ = s;
    setFocus();
    if (e->button() == Qt::MiddleButton || (e->button() == Qt::LeftButton && (e->modifiers() & Qt::AltModifier))) {
        drag_ = Drag::Pan;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (e->button() == Qt::RightButton) {
        const int v = hitVertex(s);
        if (v >= 0) {
            emit aboutToModify();
            doc_->profile.erase(doc_->profile.begin() + v);
            selVertex_ = -1;
            emit profileChanged();
            update();
            return;
        }
        const int r = view_ == View::Original ? hitRef(s) : -1;
        if (r >= 0) {
            emit aboutToModify();
            doc_->refs.erase(doc_->refs.begin() + r);
            selRef_ = -1;
            emit refsChanged(true);
            update();
        }
        return;
    }
    if (e->button() != Qt::LeftButton) return;

    switch (tool_) {
        case Tool::AddRef: {
            if (view_ != View::Original) {
                emit cursorInfo(tr("Reference points are placed in the Original view."));
                return;
            }
            cp::Pt img;
            if (!dispToImg(toDisp(s), img)) return;
            img = snapImage(img, s);
            emit addRefRequested(QPointF(img.x, img.y));
            return;
        }
        case Tool::DrawProfile: {
            cp::Pt w;
            if (!screenToWorld(s, w)) {
                emit cursorInfo(tr("Calibrate first (detect the grid or add 4 reference points)."));
                return;
            }
            emit aboutToModify();
            doc_->profile.push_back(w);
            selVertex_ = int(doc_->profile.size()) - 1;
            emit profileChanged();
            update();
            return;
        }
        case Tool::PickObject: {
            cp::Pt w;
            if (screenToWorld(s, w)) emit pickObjectRequested(QPointF(w.x, w.y));
            return;
        }
        case Tool::PickColor: {
            cp::Pt img;
            if (dispToImg(toDisp(s), img)) emit pickColorRequested(QPointF(img.x, img.y));
            return;
        }
        case Tool::Select: break;
    }

    // Select tool.
    const int v = hitVertex(s);
    if (v >= 0) {
        emit aboutToModify();
        drag_ = Drag::Vertex;
        dragIndex_ = selVertex_ = v;
        update();
        return;
    }
    const int r = view_ == View::Original ? hitRef(s) : -1;
    if (r >= 0) {
        emit aboutToModify();
        drag_ = Drag::Ref;
        dragIndex_ = selRef_ = r;
        emit refSelected(r);
        update();
        return;
    }
    int seg;
    cp::Pt w;
    if ((e->modifiers() & Qt::ControlModifier) && hitSegment(s, seg, w)) {
        emit aboutToModify();
        doc_->profile.insert(doc_->profile.begin() + seg + 1, w);
        drag_ = Drag::Vertex;
        dragIndex_ = selVertex_ = seg + 1;
        emit profileChanged();
        update();
        return;
    }
    selVertex_ = -1;
    drag_ = Drag::Pan;
    setCursor(Qt::ClosedHandCursor);
}

void Canvas::mouseDoubleClickEvent(QMouseEvent* e) {
    if (tool_ != Tool::Select || e->button() != Qt::LeftButton) return;
    int seg;
    cp::Pt w;
    if (hitVertex(e->position()) < 0 && hitSegment(e->position(), seg, w)) {
        emit aboutToModify();
        doc_->profile.insert(doc_->profile.begin() + seg + 1, w);
        selVertex_ = seg + 1;
        emit profileChanged();
        update();
    }
}

void Canvas::mouseMoveEvent(QMouseEvent* e) {
    const QPointF s = e->position();
    switch (drag_) {
        case Drag::Pan:
            pan_ += s - lastMouse_;
            lastMouse_ = s;
            update();
            break;
        case Drag::Vertex: {
            cp::Pt w;
            if (dragIndex_ >= 0 && dragIndex_ < int(doc_->profile.size()) && screenToWorld(s, w)) {
                doc_->profile[size_t(dragIndex_)] = w;
                emit profileChanged();
                update();
            }
            break;
        }
        case Drag::Ref: {
            cp::Pt img;
            if (dragIndex_ >= 0 && dragIndex_ < int(doc_->refs.size()) && dispToImg(toDisp(s), img)) {
                doc_->refs[size_t(dragIndex_)].img = snapImage(img, s);
                emit refsChanged(false);
                update();
            }
            break;
        }
        case Drag::None: {
            const int hv = hitVertex(s), hr = (view_ == View::Original && tool_ == Tool::Select) ? hitRef(s) : -1;
            if (hv != hoverVertex_ || hr != hoverRef_) {
                hoverVertex_ = hv;
                hoverRef_ = hr;
                update();
            }
            break;
        }
    }
    // Cursor read-out.
    QString txt;
    cp::Pt img, w;
    if (dispToImg(toDisp(s), img)) txt = QString("image %1, %2 px").arg(img.x, 0, 'f', 1).arg(img.y, 0, 'f', 1);
    if (screenToWorld(s, w)) txt += QString("    world %1, %2 mm").arg(w.x, 0, 'f', 2).arg(w.y, 0, 'f', 2);
    emit cursorInfo(txt);
}

void Canvas::mouseReleaseEvent(QMouseEvent*) {
    if (drag_ == Drag::Ref) emit refsChanged(true);
    drag_ = Drag::None;
    dragIndex_ = -1;
    setCursor(tool_ == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
}

void Canvas::keyPressEvent(QKeyEvent* e) {
    switch (e->key()) {
        case Qt::Key_Delete:
        case Qt::Key_Backspace:
            if (tool_ == Tool::DrawProfile && !doc_->profile.empty() && e->key() == Qt::Key_Backspace) {
                emit aboutToModify();
                doc_->profile.pop_back();
                emit profileChanged();
            } else if (selVertex_ >= 0 && selVertex_ < int(doc_->profile.size())) {
                emit aboutToModify();
                doc_->profile.erase(doc_->profile.begin() + selVertex_);
                selVertex_ = -1;
                emit profileChanged();
            } else if (selRef_ >= 0 && selRef_ < int(doc_->refs.size())) {
                emit aboutToModify();
                doc_->refs.erase(doc_->refs.begin() + selRef_);
                selRef_ = -1;
                emit refsChanged(true);
            }
            update();
            break;
        case Qt::Key_Escape:
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (tool_ != Tool::Select) {
                setTool(Tool::Select);
                emit toolChanged(Tool::Select);
            }
            break;
        case Qt::Key_F: fitToWindow(); break;
        default: QWidget::keyPressEvent(e);
    }
}
