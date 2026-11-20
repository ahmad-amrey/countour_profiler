#pragma once
#include <QColor>
#include <QMainWindow>
#include <QTimer>
#include <vector>

#include "canvas.h"
#include "document.h"
#include "export.h"

class QAction;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QTableWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    bool loadImage(const QString& path);
    void showView(bool original) { setView(original ? Canvas::View::Original : Canvas::View::Rectified); }

protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    // UI
    void buildUi();
    QWidget* buildPanel();
    void setTool(Canvas::Tool t);
    void setView(Canvas::View v);

    // Pipeline steps
    void openImage();
    void runAutomatic();
    void detectGrid();
    void recalibrate(bool finished);
    void addReference(QPointF img);
    void applySquareSize();
    void rebuildRectified();
    void detectProfile(bool hasSeed, cp::Pt seed);
    void pickColor(QPointF img);
    void simplifyProfile();
    void reverseProfile();
    void clearProfile();

    // Export
    std::vector<cp::ExportPolyline> exportPolylines() const;
    void exportProfile(int kind);  // 0 DXF, 1 SVG, 2 CSV
    void saveRectified();

    // Helpers
    cp::RectD imageFootprint() const;
    void updateDetectRoi();
    void updateRefTable();
    void updateInfo();
    void onRefTableEdited(int row, int col);
    void status(const QString& msg);

    // Undo
    struct Snapshot {
        std::vector<RefPoint> refs;
        std::vector<cp::Pt> profile;
        bool closed;
    };
    void pushUndo();
    void undo();
    void redo();
    void restore(const Snapshot& s);
    Snapshot snapshot() const { return {doc_.refs, doc_.profile, doc_.closed}; }

    Document doc_;
    Canvas* canvas_ = nullptr;
    std::vector<Snapshot> undo_, redo_;
    QTimer rectTimer_;
    bool updatingTable_ = false;
    QColor pickedColor_ = QColor(200, 200, 200);

    // Widgets
    QComboBox* patternCombo_ = nullptr;
    QDoubleSpinBox* squareSpin_ = nullptr;
    QLabel* gridInfo_ = nullptr;
    QTableWidget* refTable_ = nullptr;
    QCheckBox* snapCheck_ = nullptr;
    QDoubleSpinBox* rectWSpin_ = nullptr;
    QDoubleSpinBox* rectHSpin_ = nullptr;
    QLabel* calInfo_ = nullptr;
    QDoubleSpinBox* marginSpin_ = nullptr;
    QComboBox* modeCombo_ = nullptr;
    QComboBox* outputCombo_ = nullptr;
    QDoubleSpinBox* thrSpin_ = nullptr;
    QDoubleSpinBox* colorTolSpin_ = nullptr;
    QDoubleSpinBox* smoothSpin_ = nullptr;
    QDoubleSpinBox* simplifySpin_ = nullptr;
    QPushButton* colorBtn_ = nullptr;
    QCheckBox* closedCheck_ = nullptr;
    QLabel* profileInfo_ = nullptr;
    QCheckBox* autoCheck_ = nullptr;
    QCheckBox* expSmoothCheck_ = nullptr;
    QCheckBox* expGridCheck_ = nullptr;
    QComboBox* originCombo_ = nullptr;
    QLabel* cursorLabel_ = nullptr;
    QAction* viewOrigAct_ = nullptr;
    QAction* viewRectAct_ = nullptr;
    QAction* toolActs_[5] = {};
    QAction* undoAct_ = nullptr;
    QAction* redoAct_ = nullptr;
};
