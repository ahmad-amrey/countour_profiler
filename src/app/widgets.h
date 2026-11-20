// Small custom widgets of the Graphite UI.
#pragma once
#include <QDialog>
#include <QFrame>
#include <QPushButton>
#include <QLabel>
#include <vector>

#include "fit.h"
#include "theme.h"

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QVBoxLayout;

QPushButton* makeButton(const QString& text, const char* variant = nullptr, theme::Icon ic = theme::Icon::More,
                        bool withIcon = false, const QColor& iconColor = theme::text);
QLabel* makeLabel(const QString& text, const char* role = nullptr);
QFrame* makeDivider();

// Segmented control (mutually exclusive buttons in a rounded track).
class Segmented : public QFrame {
    Q_OBJECT
public:
    Segmented(const QStringList& items, bool floating = false, QWidget* parent = nullptr);
    int index() const;
    void setIndex(int i);  // no signal
signals:
    void changed(int index);
private:
    QButtonGroup* group_;
};

// Draws a profile (fitted segments or a polyline) scaled into its rectangle.
class ProfilePreview : public QWidget {
public:
    explicit ProfilePreview(QWidget* parent = nullptr);
    void setProfile(const std::vector<cp::Pt>& pts, const std::vector<cp::Segment>& segs, bool closed);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    std::vector<cp::Pt> pts_;
    std::vector<cp::Segment> segs_;
    bool closed_ = false;
};

// Tier 0: nothing open yet.
class StartPage : public QWidget {
    Q_OBJECT
public:
    explicit StartPage(QWidget* parent = nullptr);
    void setSquare(double mm);
    void setDropHover(bool on);
signals:
    void openRequested();
    void sampleRequested();
    void squareChangeRequested();
    void helpRequested();
    void printSheetRequested();
private:
    QLabel* squareValue_;
    QFrame* drop_;
};

// Tier 1: the automatic result, floating over the photo.
class ResultCard : public QFrame {
    Q_OBJECT
public:
    explicit ResultCard(QWidget* parent = nullptr);
    void setResult(const QString& title, const QString& summary, const QStringList& checks, bool canExport);
signals:
    void adjustRequested();
    void exportRequested();
private:
    QLabel* title_;
    QLabel* summary_;
    QWidget* checks_;
    QPushButton* export_;
};

// Fallback when no grid is detected: click 4 corners of a known rectangle.
class CalibrateCard : public QFrame {
    Q_OBJECT
public:
    explicit CalibrateCard(QWidget* parent = nullptr);
    void setCount(int n);
    double rectW() const;
    double rectH() const;
    void setSquare(double mm);
signals:
    void undoRequested();
    void retryRequested();
private:
    QFrame* bars_[4];
    QLabel* status_;
    QDoubleSpinBox* w_;
    QDoubleSpinBox* h_;
};

// Export sheet: format + geometry, more options behind a disclosure.
class ExportDialog : public QDialog {
    Q_OBJECT
public:
    ExportDialog(QWidget* parent, const std::vector<cp::Pt>& pts, const std::vector<cp::Segment>& segs, bool closed,
                 const QString& sizeText, const QString& lengthText, const QString& geometryText, int format);
    int format() const;        // 0 DXF, 1 SVG, 2 CSV
    bool linesAndArcs() const;
    int origin() const;        // 0 profile corner, 1 grid origin
    bool gridLayer() const;
private:
    Segmented* format_;
    Segmented* geometry_;
    QComboBox* origin_;
    QCheckBox* grid_;
    QPushButton* save_;
};
