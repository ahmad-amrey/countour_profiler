#pragma once
#include <QColor>
#include <QMainWindow>
#include <QTimer>
#include <vector>

#include "canvas.h"
#include "document.h"
#include "export.h"
#include "i18n.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFrame;
class QLabel;
class QListWidget;
class QPushButton;
class QScrollArea;
class QSlider;
class QStackedWidget;
class QTableWidget;
class QToolButton;
class CalibrateCard;
class ResultCard;
class Segmented;
class StartPage;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    bool loadImage(const QString& path);
    void showView(bool original) { setView(original ? Canvas::View::Original : Canvas::View::Rectified); }
    void showDetails(bool on) { setDetails(on); }
    void showArcs() { setDetails(true); showArcsPage(true); }

    struct Snapshot {
        std::vector<RefPoint> refs;
        std::vector<cp::Pt> profile;
        bool closed;
    };
    // Everything needed to rebuild the window (used when switching language).
    struct UiState {
        Document doc;
        std::vector<Snapshot> undo, redo;
        bool details = false, calibrating = false, gridDetected = false, arcsPage = false;
        double lastAutoThr = 0, margin = 2.5, colorTol = 60;
        QColor color;
        int pattern = 0, method = 0, sens = 50, smooth = 30, tol = 50;
        bool outline = false, fitOn = true;
        Canvas::View view = Canvas::View::Original;
    };
    UiState state() const;
    void adopt(const UiState& st);
    // Rebuilds the window in another language, keeping the photo and edits.
    MainWindow* switchLanguage(i18n::Lang lang, bool offscreen = false);

protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragLeaveEvent(QDragLeaveEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    // UI construction
    void buildUi();
    QWidget* buildHeader();
    QWidget* buildRail();
    QWidget* buildStage();
    QWidget* buildPanel();
    QWidget* buildArcsPage();
    void layoutOverlays();
    void buildActions();

    // Tiers and modes
    void setDetails(bool on);
    void showArcsPage(bool on);
    void setCalibrating(bool on);
    void setTool(Canvas::Tool t);
    void setView(Canvas::View v);

    // Pipeline
    void openImage();
    void openSample();
    void closeDocument();
    void printSheet();
    void changeSquareSize();
    void runAutomatic();
    bool detectGrid();
    void recalibrate(bool finished);
    void addReference(QPointF img);
    void applySquareSize(double s);
    void rebuildRectified();
    void detectProfile(bool hasSeed, cp::Pt seed);
    void pickColor(QPointF img);
    void refit();

    // Export
    void exportDialog(int format);
    void exportFile(int format, bool arcs, int origin, bool grid);
    void saveRectified();

    // Helpers
    cp::RectD imageFootprint() const;
    void updateDetectRoi();
    void updateRefTable();
    void updateAll();
    void updateResultCard();
    void updatePanel();
    void updateSegList();
    void onRefTableEdited(int row, int col);
    double threshold() const;
    void status(const QString& msg);

    // Undo
    void pushUndo();
    void undo();
    void redo();
    void restore(const Snapshot& s);
    Snapshot snapshot() const { return {doc_.refs, doc_.profile, doc_.closed}; }

    Document doc_;
    std::vector<Snapshot> undo_, redo_;
    QTimer rectTimer_;
    bool details_ = false, calibrating_ = false, updatingTable_ = false, gridDetected_ = false;
    double lastAutoThr_ = 0;
    QColor pickedColor_ = QColor(200, 200, 200);
    int lastFormat_ = 0;
    QString lastMessage_;

    // Shell
    QStackedWidget* pages_ = nullptr;
    StartPage* start_ = nullptr;
    QPushButton* fileChip_ = nullptr;
    QPushButton* helpBtn_ = nullptr;
    QPushButton* langBtn_ = nullptr;
    QPushButton* openBtn_ = nullptr;
    QPushButton* exportBtn_ = nullptr;
    QPushButton* moreBtn_ = nullptr;
    QWidget* rail_ = nullptr;
    QToolButton* toolBtns_[5] = {};
    QToolButton* undoBtn_ = nullptr;
    QToolButton* redoBtn_ = nullptr;

    // Stage
    QWidget* stage_ = nullptr;
    Canvas* canvas_ = nullptr;
    Segmented* viewSeg_ = nullptr;
    QFrame* zoomBox_ = nullptr;
    ResultCard* resultCard_ = nullptr;
    CalibrateCard* calibCard_ = nullptr;
    QLabel* hint_ = nullptr;
    QFrame* dim_ = nullptr;

    // Details panel
    QScrollArea* panelScroll_ = nullptr;
    QStackedWidget* panelStack_ = nullptr;
    QLabel* calStatus_ = nullptr;
    QComboBox* patternCombo_ = nullptr;
    QDoubleSpinBox* squareSpin_ = nullptr;
    QPushButton* editRefsBtn_ = nullptr;
    QWidget* refsBox_ = nullptr;
    QTableWidget* refTable_ = nullptr;
    QCheckBox* snapCheck_ = nullptr;
    Segmented* methodSeg_ = nullptr;
    QSlider* sensSlider_ = nullptr;
    QLabel* sensValue_ = nullptr;
    QSlider* smoothSlider_ = nullptr;
    QLabel* smoothValue_ = nullptr;
    QWidget* colorRow_ = nullptr;
    QPushButton* colorBtn_ = nullptr;
    QDoubleSpinBox* colorTolSpin_ = nullptr;
    QDoubleSpinBox* marginSpin_ = nullptr;
    QCheckBox* outlineCheck_ = nullptr;
    QCheckBox* fitCheck_ = nullptr;
    QSlider* tolSlider_ = nullptr;
    QLabel* tolValue_ = nullptr;
    QPushButton* fitSummaryBtn_ = nullptr;
    QSlider* tolSlider2_ = nullptr;
    QLabel* tolValue2_ = nullptr;
    QListWidget* segList_ = nullptr;
    QCheckBox* showPointsCheck_ = nullptr;
};
