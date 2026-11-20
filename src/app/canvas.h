// Zoomable image view with editable overlays (reference points, profile).
#pragma once
#include <QPixmap>
#include <QWidget>

#include "document.h"

class Canvas : public QWidget {
    Q_OBJECT
public:
    enum class View { Original, Rectified };
    enum class Tool { Select, AddRef, DrawProfile, PickObject, PickColor };

    explicit Canvas(Document* doc, QWidget* parent = nullptr);

    void setView(View v);
    View view() const { return view_; }
    void setTool(Tool t);
    Tool tool() const { return tool_; }

    void imageChanged();      // a new photo was loaded
    void rectifiedChanged();  // rectified rendering updated
    void fitToWindow();

    void setSnap(bool on) { snap_ = on; }
    void setShowGrid(bool on) { showGrid_ = on; update(); }
    void setShowMask(bool on) { showMask_ = on; update(); }
    void setShowCandidates(bool on) { showCand_ = on; update(); }
    int selectedRef() const { return selRef_; }
    void setSelectedRef(int i) { selRef_ = i; update(); }

signals:
    void aboutToModify();                 // take an undo snapshot
    void refsChanged(bool finished);      // reference points moved/added/removed
    void profileChanged();
    void addRefRequested(QPointF img);
    void pickObjectRequested(QPointF world);
    void pickColorRequested(QPointF img);
    void cursorInfo(const QString& text);
    void toolChanged(Canvas::Tool t);
    void refSelected(int index);

protected:
    void paintEvent(QPaintEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    QPointF toScreen(QPointF disp) const { return disp * zoom_ + pan_; }
    QPointF toDisp(QPointF screen) const { return (screen - pan_) / zoom_; }
    bool worldToDisp(cp::Pt w, QPointF& d) const;
    bool dispToWorld(QPointF d, cp::Pt& w) const;
    bool imgToDisp(cp::Pt p, QPointF& d) const;
    bool dispToImg(QPointF d, cp::Pt& p) const;
    bool screenToWorld(QPointF s, cp::Pt& w) const { return dispToWorld(toDisp(s), w); }

    int hitVertex(QPointF screen) const;
    int hitRef(QPointF screen) const;
    bool hitSegment(QPointF screen, int& seg, cp::Pt& world) const;
    cp::Pt snapImage(cp::Pt img, QPointF screen) const;
    QSizeF contentSize() const;
    void drawWorldGrid(QPainter& p);

    Document* doc_;
    View view_ = View::Original;
    Tool tool_ = Tool::Select;
    QPixmap origPix_, rectPix_;
    double zoom_ = 1;
    QPointF pan_;
    bool snap_ = true, showGrid_ = true, showMask_ = true, showCand_ = false;

    enum class Drag { None, Pan, Ref, Vertex };
    Drag drag_ = Drag::None;
    int dragIndex_ = -1;
    QPointF lastMouse_;
    int selRef_ = -1, selVertex_ = -1, hoverRef_ = -1, hoverVertex_ = -1;
};
