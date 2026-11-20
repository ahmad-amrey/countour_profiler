#include "mainwindow.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImageReader>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStatusBar>
#include <QTableWidget>
#include <QToolBar>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <cstring>

#include "profile_detect.h"
#include "synth.h"

namespace {

QImage toQImage(const cp::ImageU8& im) {
    QImage q(im.w, im.h, QImage::Format_RGB888);
    for (int y = 0; y < im.h; ++y) std::memcpy(q.scanLine(y), im.px(0, y), size_t(im.w) * 3);
    return q;
}

cp::ImageU8 fromQImage(const QImage& in) {
    const QImage q = in.convertToFormat(QImage::Format_RGB888);
    cp::ImageU8 im;
    im.resize(q.width(), q.height(), 3);
    for (int y = 0; y < im.h; ++y) std::memcpy(im.px(0, y), q.constScanLine(y), size_t(im.w) * 3);
    return im;
}

QDoubleSpinBox* spin(double lo, double hi, double val, int dec, double step, const QString& suffix = {}) {
    auto* s = new QDoubleSpinBox;
    s->setRange(lo, hi);
    s->setDecimals(dec);
    s->setSingleStep(step);
    s->setValue(val);
    if (!suffix.isEmpty()) s->setSuffix(suffix);
    s->setKeyboardTracking(false);
    return s;
}

cp::RectD intersect(const cp::RectD& a, const cp::RectD& b) {
    if (!b.valid()) return a;
    cp::RectD r{std::max(a.x0, b.x0), std::max(a.y0, b.y0), std::min(a.x1, b.x1), std::min(a.y1, b.y1)};
    return r.valid() ? r : a;
}

}  // namespace

MainWindow::MainWindow() {
    setWindowTitle(tr("Contour Profiler"));
    setAcceptDrops(true);
    buildUi();
    rectTimer_.setSingleShot(true);
    rectTimer_.setInterval(250);
    connect(&rectTimer_, &QTimer::timeout, this, &MainWindow::rebuildRectified);
    resize(1400, 900);
    updateInfo();
}

// ---------------------------------------------------------------------------
// UI construction
// ---------------------------------------------------------------------------
void MainWindow::buildUi() {
    canvas_ = new Canvas(&doc_);
    auto* scroll = new QScrollArea;
    scroll->setWidget(buildPanel());
    scroll->setWidgetResizable(true);
    scroll->setMinimumWidth(380);
    auto* split = new QSplitter;
    split->addWidget(canvas_);
    split->addWidget(scroll);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 0);
    setCentralWidget(split);

    cursorLabel_ = new QLabel;
    statusBar()->addPermanentWidget(cursorLabel_);

    // Actions.
    auto* openAct = new QAction(tr("&Open photo..."), this);
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &MainWindow::openImage);
    auto* autoAct = new QAction(tr("Auto &detect all"), this);
    autoAct->setShortcut(QKeySequence(Qt::Key_F5));
    autoAct->setToolTip(tr("Detect grid, calibrate and extract the profile (F5)"));
    connect(autoAct, &QAction::triggered, this, &MainWindow::runAutomatic);
    auto* dxfAct = new QAction(tr("Export &DXF..."), this);
    dxfAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    connect(dxfAct, &QAction::triggered, this, [this] { exportProfile(0); });
    auto* svgAct = new QAction(tr("Export &SVG..."), this);
    connect(svgAct, &QAction::triggered, this, [this] { exportProfile(1); });
    auto* csvAct = new QAction(tr("Export &CSV..."), this);
    connect(csvAct, &QAction::triggered, this, [this] { exportProfile(2); });
    auto* rectAct = new QAction(tr("Save &rectified image..."), this);
    connect(rectAct, &QAction::triggered, this, &MainWindow::saveRectified);
    auto* sampleAct = new QAction(tr("Create &sample photo..."), this);
    connect(sampleAct, &QAction::triggered, this, [this] {
        const QString fn = QFileDialog::getSaveFileName(this, tr("Save sample photo"), "sample_linegrid.png",
                                                        tr("PNG image (*.png)"));
        if (fn.isEmpty()) return;
        cp::SynthParams sp;
        const cp::SynthResult s = cp::renderSynthetic(sp);
        if (toQImage(s.img).save(fn)) loadImage(fn);
    });
    auto* quitAct = new QAction(tr("&Quit"), this);
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, this, &QWidget::close);

    undoAct_ = new QAction(tr("&Undo"), this);
    undoAct_->setShortcut(QKeySequence::Undo);
    connect(undoAct_, &QAction::triggered, this, &MainWindow::undo);
    redoAct_ = new QAction(tr("&Redo"), this);
    redoAct_->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::Key_Y)});
    connect(redoAct_, &QAction::triggered, this, &MainWindow::redo);

    auto* viewGroup = new QActionGroup(this);
    viewOrigAct_ = new QAction(tr("&Original photo"), viewGroup);
    viewOrigAct_->setShortcut(QKeySequence(Qt::Key_1));
    viewRectAct_ = new QAction(tr("&Rectified (top view)"), viewGroup);
    viewRectAct_->setShortcut(QKeySequence(Qt::Key_2));
    for (QAction* a : {viewOrigAct_, viewRectAct_}) a->setCheckable(true);
    viewOrigAct_->setChecked(true);
    connect(viewOrigAct_, &QAction::triggered, this, [this] { setView(Canvas::View::Original); });
    connect(viewRectAct_, &QAction::triggered, this, [this] { setView(Canvas::View::Rectified); });
    auto* fitAct = new QAction(tr("&Fit to window"), this);
    fitAct->setShortcut(QKeySequence(Qt::Key_F));
    connect(fitAct, &QAction::triggered, canvas_, &Canvas::fitToWindow);
    auto* gridAct = new QAction(tr("Show calibrated &grid"), this);
    gridAct->setCheckable(true);
    gridAct->setChecked(true);
    connect(gridAct, &QAction::toggled, canvas_, &Canvas::setShowGrid);
    auto* maskAct = new QAction(tr("Show detection &mask"), this);
    maskAct->setCheckable(true);
    maskAct->setChecked(true);
    connect(maskAct, &QAction::toggled, canvas_, &Canvas::setShowMask);
    auto* candAct = new QAction(tr("Show corner &candidates"), this);
    candAct->setCheckable(true);
    connect(candAct, &QAction::toggled, canvas_, &Canvas::setShowCandidates);

    auto* toolGroup = new QActionGroup(this);
    const char* toolNames[5] = {QT_TR_NOOP("Edit"), QT_TR_NOOP("Add reference"), QT_TR_NOOP("Draw profile"),
                                QT_TR_NOOP("Pick object"), QT_TR_NOOP("Pick colour")};
    const char* toolTips[5] = {
        QT_TR_NOOP("Drag reference points and profile vertices; Ctrl+click/double-click inserts; right-click deletes (E)"),
        QT_TR_NOOP("Click known marks on the pattern to add reference points (R)"),
        QT_TR_NOOP("Draw the profile by hand, click by click (D)"),
        QT_TR_NOOP("Click the wire to make it the detected object (O)"),
        QT_TR_NOOP("Click the wire to detect it by its colour (C)")};
    const Qt::Key keys[5] = {Qt::Key_E, Qt::Key_R, Qt::Key_D, Qt::Key_O, Qt::Key_C};
    for (int i = 0; i < 5; ++i) {
        toolActs_[i] = new QAction(tr(toolNames[i]), toolGroup);
        toolActs_[i]->setCheckable(true);
        toolActs_[i]->setToolTip(tr(toolTips[i]));
        toolActs_[i]->setShortcut(QKeySequence(keys[i]));
        connect(toolActs_[i], &QAction::triggered, this, [this, i] { setTool(Canvas::Tool(i)); });
    }
    toolActs_[0]->setChecked(true);

    auto* guideAct = new QAction(tr("Quick &guide"), this);
    guideAct->setShortcut(QKeySequence::HelpContents);
    connect(guideAct, &QAction::triggered, this, [this] {
        QMessageBox::information(
            this, tr("Quick guide"),
            tr("<b>Capture</b><br>Bend a wire or thin metal strip over the shape, lay it on a square pattern "
               "(cutting mat, graph paper, checkerboard) and take a photo from any angle. Keep the whole wire on "
               "the pattern and avoid strong glare.<br><br>"
               "<b>1. Open the photo</b> - the grid is detected, the perspective corrected and the profile "
               "extracted automatically.<br>"
               "<b>2. Check the calibration</b> - the cyan grid must lie on the pattern lines. Set the true "
               "square size. Wrong? Drag reference points, delete bad ones (right-click) or add your own "
               "(Add reference) and type their mm coordinates in the table.<br>"
               "<b>3. Check the profile</b> in the rectified view. Wrong object? Use Pick object. Low contrast? "
               "Use Pick colour or change the threshold. Edit vertices by dragging; Ctrl+click inserts, "
               "right-click deletes, or draw it by hand.<br>"
               "<b>4. Export</b> DXF (mm) for CAD, SVG or CSV."));
    });
    auto* aboutAct = new QAction(tr("&About"), this);
    connect(aboutAct, &QAction::triggered, this, [this] {
        QMessageBox::about(this, tr("About Contour Profiler"),
                           tr("<b>Contour Profiler %1</b><br>Extracts 2D profiles from perspective photos of a "
                              "bent wire on a calibration pattern and exports them as DXF/SVG/CSV.<br><br>"
                              "Built with Qt %2.")
                               .arg(QApplication::applicationVersion(), QT_VERSION_STR));
    });

    // Menus.
    QMenu* fm = menuBar()->addMenu(tr("&File"));
    fm->addAction(openAct);
    fm->addAction(sampleAct);
    fm->addSeparator();
    fm->addAction(dxfAct);
    fm->addAction(svgAct);
    fm->addAction(csvAct);
    fm->addAction(rectAct);
    fm->addSeparator();
    fm->addAction(quitAct);
    QMenu* em = menuBar()->addMenu(tr("&Edit"));
    em->addAction(undoAct_);
    em->addAction(redoAct_);
    em->addSeparator();
    for (QAction* a : toolActs_) em->addAction(a);
    em->addSeparator();
    em->addAction(autoAct);
    QMenu* vm = menuBar()->addMenu(tr("&View"));
    vm->addAction(viewOrigAct_);
    vm->addAction(viewRectAct_);
    vm->addAction(fitAct);
    vm->addSeparator();
    vm->addAction(gridAct);
    vm->addAction(maskAct);
    vm->addAction(candAct);
    QMenu* hm = menuBar()->addMenu(tr("&Help"));
    hm->addAction(guideAct);
    hm->addAction(aboutAct);

    // Toolbar.
    QToolBar* tb = addToolBar(tr("Main"));
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonTextOnly);
    tb->addAction(openAct);
    tb->addAction(autoAct);
    tb->addSeparator();
    tb->addAction(viewOrigAct_);
    tb->addAction(viewRectAct_);
    tb->addSeparator();
    for (QAction* a : toolActs_) tb->addAction(a);
    tb->addSeparator();
    tb->addAction(undoAct_);
    tb->addAction(redoAct_);
    tb->addSeparator();
    tb->addAction(dxfAct);

    // Canvas signals.
    connect(canvas_, &Canvas::aboutToModify, this, &MainWindow::pushUndo);
    connect(canvas_, &Canvas::refsChanged, this, &MainWindow::recalibrate);
    connect(canvas_, &Canvas::profileChanged, this, &MainWindow::updateInfo);
    connect(canvas_, &Canvas::addRefRequested, this, &MainWindow::addReference);
    connect(canvas_, &Canvas::pickObjectRequested, this,
            [this](QPointF w) { detectProfile(true, cp::Pt(w.x(), w.y())); });
    connect(canvas_, &Canvas::pickColorRequested, this, &MainWindow::pickColor);
    connect(canvas_, &Canvas::cursorInfo, cursorLabel_, &QLabel::setText);
    connect(canvas_, &Canvas::toolChanged, this, &MainWindow::setTool);
    connect(canvas_, &Canvas::refSelected, this, [this](int i) {
        if (i >= 0 && i < refTable_->rowCount()) refTable_->selectRow(i);
    });
}

QWidget* MainWindow::buildPanel() {
    auto* panel = new QWidget;
    auto* vl = new QVBoxLayout(panel);

    // 1. Calibration pattern.
    auto* g1 = new QGroupBox(tr("1  Calibration pattern"));
    auto* f1 = new QFormLayout(g1);
    patternCombo_ = new QComboBox;
    patternCombo_->addItems({tr("Line grid (cutting mat, graph paper)"), tr("Checkerboard")});
    f1->addRow(tr("Pattern"), patternCombo_);
    squareSpin_ = spin(0.1, 1000, 10, 3, 0.5, tr(" mm"));
    squareSpin_->setToolTip(tr("Real size of one grid square"));
    f1->addRow(tr("Square size"), squareSpin_);
    auto* detBtn = new QPushButton(tr("Detect grid"));
    f1->addRow(detBtn);
    gridInfo_ = new QLabel(tr("No photo loaded."));
    gridInfo_->setWordWrap(true);
    f1->addRow(gridInfo_);
    autoCheck_ = new QCheckBox(tr("Run everything automatically when a photo is opened"));
    autoCheck_->setChecked(true);
    f1->addRow(autoCheck_);
    vl->addWidget(g1);
    connect(detBtn, &QPushButton::clicked, this, &MainWindow::detectGrid);
    connect(squareSpin_, &QDoubleSpinBox::valueChanged, this, &MainWindow::applySquareSize);

    // 2. Reference points.
    auto* g2 = new QGroupBox(tr("2  Reference points (perspective && scale)"));
    auto* v2 = new QVBoxLayout(g2);
    refTable_ = new QTableWidget(0, 6);
    refTable_->setHorizontalHeaderLabels({"#", tr("x px"), tr("y px"), tr("X mm"), tr("Y mm"), tr("err mm")});
    refTable_->verticalHeader()->setVisible(false);
    refTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    refTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    refTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    refTable_->setMinimumHeight(170);
    v2->addWidget(refTable_);
    auto* h2 = new QHBoxLayout;
    auto* addBtn = new QPushButton(tr("Add point"));
    auto* delBtn = new QPushButton(tr("Delete"));
    auto* clrBtn = new QPushButton(tr("Clear all"));
    h2->addWidget(addBtn);
    h2->addWidget(delBtn);
    h2->addWidget(clrBtn);
    v2->addLayout(h2);
    snapCheck_ = new QCheckBox(tr("Snap to detected grid corners"));
    snapCheck_->setChecked(true);
    v2->addWidget(snapCheck_);
    auto* f2 = new QFormLayout;
    auto* hr = new QHBoxLayout;
    rectWSpin_ = spin(0.1, 10000, 100, 2, 1, tr(" mm"));
    rectHSpin_ = spin(0.1, 10000, 100, 2, 1, tr(" mm"));
    hr->addWidget(rectWSpin_);
    hr->addWidget(new QLabel("x"));
    hr->addWidget(rectHSpin_);
    f2->addRow(tr("Manual rectangle"), hr);
    v2->addLayout(f2);
    auto* hint = new QLabel(tr("Without a detected grid, click 4 corners of a known rectangle in the order "
                               "top-left, top-right, bottom-right, bottom-left. Any point's mm coordinates can be "
                               "edited in the table."));
    hint->setWordWrap(true);
    hint->setStyleSheet("color: gray;");
    v2->addWidget(hint);
    calInfo_ = new QLabel;
    calInfo_->setWordWrap(true);
    v2->addWidget(calInfo_);
    vl->addWidget(g2);
    connect(addBtn, &QPushButton::clicked, this, [this] {
        setView(Canvas::View::Original);
        setTool(Canvas::Tool::AddRef);
    });
    connect(delBtn, &QPushButton::clicked, this, [this] {
        std::vector<int> rows;
        for (const auto& r : refTable_->selectionModel()->selectedRows()) rows.push_back(r.row());
        if (rows.empty() && canvas_->selectedRef() >= 0) rows.push_back(canvas_->selectedRef());
        if (rows.empty()) return;
        pushUndo();
        std::sort(rows.rbegin(), rows.rend());
        for (int r : rows)
            if (r < int(doc_.refs.size())) doc_.refs.erase(doc_.refs.begin() + r);
        canvas_->setSelectedRef(-1);
        recalibrate(true);
    });
    connect(clrBtn, &QPushButton::clicked, this, [this] {
        pushUndo();
        doc_.refs.clear();
        recalibrate(true);
    });
    connect(snapCheck_, &QCheckBox::toggled, canvas_, &Canvas::setSnap);
    connect(refTable_, &QTableWidget::cellChanged, this, &MainWindow::onRefTableEdited);
    connect(refTable_, &QTableWidget::itemSelectionChanged, this, [this] {
        const auto rows = refTable_->selectionModel()->selectedRows();
        canvas_->setSelectedRef(rows.size() == 1 ? rows.front().row() : -1);
    });

    // 3. Profile.
    auto* g3 = new QGroupBox(tr("3  Profile detection"));
    auto* f3 = new QFormLayout(g3);
    modeCombo_ = new QComboBox;
    modeCombo_->addItems({tr("Auto (differs from pattern)"), tr("By colour")});
    f3->addRow(tr("Method"), modeCombo_);
    outputCombo_ = new QComboBox;
    outputCombo_->addItems({tr("Centre line (wire)"), tr("Outline (closed)")});
    f3->addRow(tr("Result"), outputCombo_);
    thrSpin_ = spin(0, 255, 0, 1, 2);
    thrSpin_->setSpecialValueText(tr("auto"));
    thrSpin_->setToolTip(tr("Detection threshold (0 = automatic). Lower finds fainter objects."));
    f3->addRow(tr("Threshold"), thrSpin_);
    auto* hc = new QHBoxLayout;
    colorBtn_ = new QPushButton;
    colorBtn_->setFixedWidth(48);
    colorBtn_->setStyleSheet(QString("background:%1").arg(pickedColor_.name()));
    colorTolSpin_ = spin(1, 255, 60, 0, 5);
    hc->addWidget(colorBtn_);
    hc->addWidget(new QLabel(tr("tolerance")));
    hc->addWidget(colorTolSpin_);
    f3->addRow(tr("Colour"), hc);
    marginSpin_ = spin(0, 1000, 2.5, 1, 0.5, tr(" mm"));
    marginSpin_->setToolTip(tr("How far beyond the reference points the profile is searched"));
    f3->addRow(tr("Search margin"), marginSpin_);
    smoothSpin_ = spin(0, 10, 0.3, 2, 0.05, tr(" mm"));
    f3->addRow(tr("Smoothing"), smoothSpin_);
    simplifySpin_ = spin(0, 10, 0.05, 3, 0.01, tr(" mm"));
    simplifySpin_->setToolTip(tr("Maximum deviation when reducing the number of vertices"));
    f3->addRow(tr("Simplify tolerance"), simplifySpin_);
    auto* detP = new QPushButton(tr("Detect profile"));
    f3->addRow(detP);
    auto* h3 = new QHBoxLayout;
    auto* simpBtn = new QPushButton(tr("Simplify"));
    auto* revBtn = new QPushButton(tr("Reverse"));
    auto* clrP = new QPushButton(tr("Clear"));
    h3->addWidget(simpBtn);
    h3->addWidget(revBtn);
    h3->addWidget(clrP);
    f3->addRow(h3);
    closedCheck_ = new QCheckBox(tr("Closed contour"));
    f3->addRow(closedCheck_);
    profileInfo_ = new QLabel;
    profileInfo_->setWordWrap(true);
    f3->addRow(profileInfo_);
    vl->addWidget(g3);
    connect(detP, &QPushButton::clicked, this, [this] { detectProfile(false, {}); });
    connect(simpBtn, &QPushButton::clicked, this, &MainWindow::simplifyProfile);
    connect(revBtn, &QPushButton::clicked, this, &MainWindow::reverseProfile);
    connect(clrP, &QPushButton::clicked, this, &MainWindow::clearProfile);
    connect(closedCheck_, &QCheckBox::toggled, this, [this](bool on) {
        if (on == doc_.closed) return;
        pushUndo();
        doc_.closed = on;
        canvas_->update();
        updateInfo();
    });
    connect(colorBtn_, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(pickedColor_, this, tr("Object colour"));
        if (!c.isValid()) return;
        pickedColor_ = c;
        colorBtn_->setStyleSheet(QString("background:%1").arg(c.name()));
        modeCombo_->setCurrentIndex(1);
    });
    connect(marginSpin_, &QDoubleSpinBox::valueChanged, this, [this] {
        updateDetectRoi();
        rectTimer_.start();
        canvas_->update();
    });

    // 4. Export.
    auto* g4 = new QGroupBox(tr("4  Export"));
    auto* f4 = new QFormLayout(g4);
    originCombo_ = new QComboBox;
    originCombo_->addItems({tr("Profile corner (bottom-left = 0,0)"), tr("Grid origin")});
    f4->addRow(tr("Origin"), originCombo_);
    expSmoothCheck_ = new QCheckBox(tr("Smooth curve (spline densification)"));
    f4->addRow(expSmoothCheck_);
    expGridCheck_ = new QCheckBox(tr("Include reference grid layer"));
    f4->addRow(expGridCheck_);
    auto* h4 = new QHBoxLayout;
    auto* bDxf = new QPushButton(tr("DXF"));
    auto* bSvg = new QPushButton(tr("SVG"));
    auto* bCsv = new QPushButton(tr("CSV"));
    auto* bImg = new QPushButton(tr("Rectified PNG"));
    for (auto* b : {bDxf, bSvg, bCsv, bImg}) h4->addWidget(b);
    f4->addRow(h4);
    vl->addWidget(g4);
    connect(bDxf, &QPushButton::clicked, this, [this] { exportProfile(0); });
    connect(bSvg, &QPushButton::clicked, this, [this] { exportProfile(1); });
    connect(bCsv, &QPushButton::clicked, this, [this] { exportProfile(2); });
    connect(bImg, &QPushButton::clicked, this, &MainWindow::saveRectified);

    vl->addStretch(1);
    return panel;
}

void MainWindow::setTool(Canvas::Tool t) {
    canvas_->setTool(t);
    toolActs_[int(t)]->setChecked(true);
}

void MainWindow::setView(Canvas::View v) {
    canvas_->setView(v);
    (v == Canvas::View::Original ? viewOrigAct_ : viewRectAct_)->setChecked(true);
}

void MainWindow::status(const QString& msg) { statusBar()->showMessage(msg, 8000); }

// ---------------------------------------------------------------------------
// Loading and the automatic pipeline
// ---------------------------------------------------------------------------
void MainWindow::openImage() {
    const QString fn = QFileDialog::getOpenFileName(
        this, tr("Open photo"), QFileInfo(doc_.path).absolutePath(),
        tr("Images (*.jpg *.jpeg *.png *.bmp *.tif *.tiff *.webp);;All files (*)"));
    if (!fn.isEmpty()) loadImage(fn);
}

bool MainWindow::loadImage(const QString& path) {
    QImageReader reader(path);
    reader.setAutoTransform(true);  // honour EXIF orientation
    QImage img = reader.read();
    if (img.isNull()) {
        QMessageBox::warning(this, tr("Open photo"), tr("Cannot read %1:\n%2").arg(path, reader.errorString()));
        return false;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const double square = doc_.squareMM;
    doc_ = Document();
    doc_.squareMM = square;
    doc_.path = path;
    doc_.image = img.convertToFormat(QImage::Format_RGB888);
    doc_.core = fromQImage(doc_.image);
    doc_.gray = cp::toGray(doc_.core);
    undo_.clear();
    redo_.clear();
    canvas_->imageChanged();
    setView(Canvas::View::Original);
    setWindowTitle(tr("Contour Profiler - %1").arg(QFileInfo(path).fileName()));
    gridInfo_->setText(tr("%1 x %2 px").arg(img.width()).arg(img.height()));
    QApplication::restoreOverrideCursor();
    updateRefTable();
    updateInfo();
    if (autoCheck_->isChecked()) runAutomatic();
    return true;
}

void MainWindow::runAutomatic() {
    if (!doc_.hasImage()) return;
    detectGrid();
    if (!doc_.cal.ok) return;
    rebuildRectified();
    detectProfile(false, {});
    setView(Canvas::View::Rectified);
}

void MainWindow::detectGrid() {
    if (!doc_.hasImage()) return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const cp::PatternType pt = cp::PatternType(patternCombo_->currentIndex());
    cp::GridResult g = cp::detectGrid(doc_.gray, pt);
    QApplication::restoreOverrideCursor();
    doc_.grid = g;
    if (!g.ok) {
        gridInfo_->setText(QString::fromStdString(g.message));
        status(QString::fromStdString(g.message));
        canvas_->update();
        return;
    }
    pushUndo();
    doc_.refs.clear();
    for (const auto& n : g.nodes) {
        RefPoint r;
        r.img = n.img;
        r.world = cp::gridWorld(n, doc_.squareMM);
        r.fromGrid = true;
        r.gi = n.i;
        r.gj = n.j;
        doc_.refs.push_back(r);
    }
    gridInfo_->setText(tr("Detected %1").arg(QString::fromStdString(g.message)));
    recalibrate(true);
}

void MainWindow::applySquareSize() {
    const double s = squareSpin_->value();
    const double old = doc_.squareMM;
    doc_.squareMM = s;
    if (doc_.refs.empty() || old <= 0) return;
    pushUndo();
    bool allGrid = true;
    for (auto& r : doc_.refs) {
        if (r.fromGrid) r.world = cp::Pt(r.gi * s, r.gj * s);
        else allGrid = false;
    }
    // A grid-only calibration scales uniformly: keep the profile on the photo.
    if (allGrid)
        for (auto& p : doc_.profile) p = p * (s / old);
    recalibrate(true);
}

cp::RectD MainWindow::imageFootprint() const {
    if (!doc_.cal.ok) return {};
    std::vector<cp::Pt> foot;
    const double w = doc_.image.width() - 1, h = doc_.image.height() - 1;
    for (cp::Pt c : {cp::Pt(0, 0), cp::Pt(w, 0), cp::Pt(w, h), cp::Pt(0, h)}) {
        bool ok = false;
        const cp::Pt p = doc_.cal.Hi2w.apply(c, &ok);
        if (!ok) return {};
        foot.push_back(p);
    }
    return cp::boundingBox(foot, 0);
}

void MainWindow::updateDetectRoi() {
    doc_.detectRoi = {};
    if (!doc_.cal.ok) return;
    std::vector<cp::Pt> w;
    for (const auto& r : doc_.refs) w.push_back(r.world);
    doc_.detectRoi = intersect(cp::boundingBox(w, marginSpin_->value()), imageFootprint());
}

void MainWindow::recalibrate(bool finished) {
    if (doc_.refs.size() >= 4) {
        std::vector<cp::Pt> img, world;
        for (const auto& r : doc_.refs) { img.push_back(r.img); world.push_back(r.world); }
        doc_.cal = cp::calibrate(img, world);
        if (doc_.cal.ok)
            for (size_t i = 0; i < doc_.refs.size(); ++i) doc_.refs[i].errMM = doc_.cal.residualsMM[i];
    } else {
        doc_.cal = cp::Calibration();
    }
    updateDetectRoi();
    if (finished) updateRefTable();
    updateInfo();
    canvas_->update();
    rectTimer_.start();
}

void MainWindow::addReference(QPointF imgPt) {
    pushUndo();
    RefPoint r;
    r.img = cp::Pt(imgPt.x(), imgPt.y());
    if (doc_.cal.ok) {
        // Suggest coordinates from the current calibration, snapped to the
        // nearest grid intersection when close.
        cp::Pt w = doc_.cal.Hi2w.apply(r.img);
        const double s = doc_.squareMM;
        const cp::Pt g(std::round(w.x / s) * s, std::round(w.y / s) * s);
        r.world = cp::dist(g, w) < 0.25 * s ? g : cp::Pt(std::round(w.x * 10) / 10, std::round(w.y * 10) / 10);
    } else {
        const double W = rectWSpin_->value(), H = rectHSpin_->value();
        const cp::Pt tmpl[4] = {{0, 0}, {W, 0}, {W, H}, {0, H}};
        r.world = tmpl[doc_.refs.size() % 4];
    }
    doc_.refs.push_back(r);
    recalibrate(true);
    refTable_->selectRow(int(doc_.refs.size()) - 1);
    status(tr("Reference point %1 added at %2, %3 mm - edit the table if the coordinates differ.")
               .arg(doc_.refs.size())
               .arg(r.world.x)
               .arg(r.world.y));
}

void MainWindow::rebuildRectified() {
    if (!doc_.cal.ok || !doc_.detectRoi.valid()) {
        doc_.rectified = QImage();
        doc_.rectRes = 0;
        doc_.rectArea = {};
        canvas_->rectifiedChanged();
        return;
    }
    cp::RectD a = doc_.detectRoi;
    const double m = doc_.squareMM;
    a = intersect({a.x0 - m, a.y0 - m, a.x1 + m, a.y1 + m}, imageFootprint());
    const cp::Pt c((a.x0 + a.x1) / 2, (a.y0 + a.y1) / 2);
    const double native = cp::pxPerMM(doc_.cal.Hw2i, c);
    double res = std::min({native, 3000.0 / std::max(a.w(), a.h()), 40.0});
    res = std::max(res, 0.2);
    cp::ImageU8 out;
    cp::warpPerspective(doc_.core, doc_.cal.Hw2i, a, res, out);
    doc_.rectified = toQImage(out);
    doc_.rectArea = a;
    doc_.rectRes = res;
    canvas_->rectifiedChanged();
}

// ---------------------------------------------------------------------------
// Profile
// ---------------------------------------------------------------------------
void MainWindow::detectProfile(bool hasSeed, cp::Pt seed) {
    if (!doc_.cal.ok || !doc_.detectRoi.valid()) {
        status(tr("Calibrate first: detect the grid or add at least 4 reference points."));
        return;
    }
    cp::ProfileParams prm;
    prm.mode = cp::DetectMode(modeCombo_->currentIndex());
    prm.output = cp::ProfileOutput(outputCombo_->currentIndex());
    prm.pattern = cp::PatternType(patternCombo_->currentIndex());
    prm.squareMM = doc_.squareMM;
    prm.threshold = thrSpin_->value();
    prm.color[0] = uint8_t(pickedColor_.red());
    prm.color[1] = uint8_t(pickedColor_.green());
    prm.color[2] = uint8_t(pickedColor_.blue());
    prm.colorTol = colorTolSpin_->value();
    prm.smoothMM = smoothSpin_->value();
    prm.simplifyMM = simplifySpin_->value();
    prm.hasSeed = hasSeed;
    prm.seed = seed;
    cp::RectD roi = doc_.detectRoi;
    if (hasSeed) {
        // A picked point outside the search region extends the region.
        const double m = doc_.squareMM;
        roi.x0 = std::min(roi.x0, seed.x - m); roi.y0 = std::min(roi.y0, seed.y - m);
        roi.x1 = std::max(roi.x1, seed.x + m); roi.y1 = std::max(roi.y1, seed.y + m);
        roi = intersect(roi, imageFootprint());
    }
    const cp::Pt c((roi.x0 + roi.x1) / 2, (roi.y0 + roi.y1) / 2);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const cp::ProfileResult r = cp::detectProfile(doc_.core, doc_.cal.Hw2i, roi, cp::pxPerMM(doc_.cal.Hw2i, c), prm);
    QApplication::restoreOverrideCursor();

    // Diagnostics overlay: red = above threshold, green = selected object.
    if (!r.mask.empty()) {
        QImage m(r.maskW, r.maskH, QImage::Format_ARGB32);
        for (int y = 0; y < r.maskH; ++y) {
            auto* line = reinterpret_cast<QRgb*>(m.scanLine(y));
            for (int x = 0; x < r.maskW; ++x) {
                const uint8_t v = r.mask[size_t(y) * r.maskW + x];
                line[x] = v == 2 ? qRgba(0, 255, 80, 80) : v == 1 ? qRgba(255, 40, 40, 90) : qRgba(0, 0, 0, 0);
            }
        }
        doc_.maskImage = m;
        doc_.maskArea = r.maskArea;
    }
    if (r.ok) {
        pushUndo();
        doc_.profile = r.pts;
        doc_.closed = r.closed;
        closedCheck_->blockSignals(true);
        closedCheck_->setChecked(r.closed);
        closedCheck_->blockSignals(false);
    }
    status(QString::fromStdString(r.message));
    updateInfo();
    canvas_->update();
}

void MainWindow::pickColor(QPointF imgPt) {
    const int cx = int(std::lround(imgPt.x())), cy = int(std::lround(imgPt.y()));
    double s[3] = {0, 0, 0};
    int n = 0;
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) {
            const int x = cx + dx, y = cy + dy;
            if (x < 0 || y < 0 || x >= doc_.core.w || y >= doc_.core.h) continue;
            const uint8_t* p = doc_.core.px(x, y);
            for (int c = 0; c < 3; ++c) s[c] += p[c];
            ++n;
        }
    if (!n) return;
    pickedColor_ = QColor(int(s[0] / n), int(s[1] / n), int(s[2] / n));
    colorBtn_->setStyleSheet(QString("background:%1").arg(pickedColor_.name()));
    modeCombo_->setCurrentIndex(1);
    if (doc_.cal.ok) {
        const cp::Pt w = doc_.cal.Hi2w.apply(cp::Pt(imgPt.x(), imgPt.y()));
        detectProfile(true, w);
    }
}

void MainWindow::simplifyProfile() {
    if (doc_.profile.size() < 3) return;
    pushUndo();
    doc_.profile = cp::simplifyDP(doc_.profile, std::max(1e-3, simplifySpin_->value()), doc_.closed);
    updateInfo();
    canvas_->update();
}

void MainWindow::reverseProfile() {
    if (doc_.profile.size() < 2) return;
    pushUndo();
    std::reverse(doc_.profile.begin(), doc_.profile.end());
    canvas_->update();
}

void MainWindow::clearProfile() {
    pushUndo();
    doc_.profile.clear();
    doc_.maskImage = QImage();
    updateInfo();
    canvas_->update();
}

// ---------------------------------------------------------------------------
// Export
// ---------------------------------------------------------------------------
std::vector<cp::ExportPolyline> MainWindow::exportPolylines() const {
    std::vector<cp::ExportPolyline> out;
    std::vector<cp::Pt> pts = doc_.profile;
    if (expSmoothCheck_->isChecked() && pts.size() >= 3) pts = cp::catmullRom(pts, 8, doc_.closed);
    // CAD convention: Y up.
    double ox = 0, oy = 0;
    if (originCombo_->currentIndex() == 0 && !pts.empty()) {
        double minx = 1e300, maxy = -1e300;
        for (const auto& p : pts) { minx = std::min(minx, p.x); maxy = std::max(maxy, p.y); }
        ox = minx;
        oy = maxy;
    }
    auto tf = [&](cp::Pt p) { return cp::Pt(p.x - ox, oy - p.y); };
    cp::ExportPolyline prof;
    for (const auto& p : pts) prof.pts.push_back(tf(p));
    prof.closed = doc_.closed;
    prof.layer = "PROFILE";
    prof.color = 1;
    out.push_back(prof);
    if (expGridCheck_->isChecked() && doc_.detectRoi.valid()) {
        const double s = doc_.squareMM;
        const cp::RectD& r = doc_.detectRoi;
        const int i0 = int(std::ceil(r.x0 / s)), i1 = int(std::floor(r.x1 / s));
        const int j0 = int(std::ceil(r.y0 / s)), j1 = int(std::floor(r.y1 / s));
        for (int i = i0; i <= i1 && i1 - i0 < 500; ++i) {
            cp::ExportPolyline l;
            l.pts = {tf(cp::Pt(i * s, j0 * s)), tf(cp::Pt(i * s, j1 * s))};
            l.layer = "GRID";
            l.color = 8;
            out.push_back(l);
        }
        for (int j = j0; j <= j1 && j1 - j0 < 500; ++j) {
            cp::ExportPolyline l;
            l.pts = {tf(cp::Pt(i0 * s, j * s)), tf(cp::Pt(i1 * s, j * s))};
            l.layer = "GRID";
            l.color = 8;
            out.push_back(l);
        }
    }
    return out;
}

void MainWindow::exportProfile(int kind) {
    if (doc_.profile.size() < 2) {
        QMessageBox::information(this, tr("Export"), tr("There is no profile to export yet."));
        return;
    }
    static const char* ext[3] = {"dxf", "svg", "csv"};
    static const char* filter[3] = {"DXF drawing (*.dxf)", "SVG drawing (*.svg)", "CSV points (*.csv)"};
    const QFileInfo fi(doc_.path);
    const QString def = fi.absolutePath() + "/" + fi.completeBaseName() + "_profile." + ext[kind];
    const QString fn = QFileDialog::getSaveFileName(this, tr("Export profile"), def, tr(filter[kind]));
    if (fn.isEmpty()) return;
    const auto polys = exportPolylines();
    const std::string content = kind == 0 ? cp::makeDXF(polys) : kind == 1 ? cp::makeSVG(polys) : cp::makeCSV(polys);
    QFile f(fn);
    if (!f.open(QIODevice::WriteOnly) || f.write(content.data(), qint64(content.size())) != qint64(content.size())) {
        QMessageBox::warning(this, tr("Export"), tr("Cannot write %1").arg(fn));
        return;
    }
    status(tr("Exported %1 (units: mm)").arg(fn));
}

void MainWindow::saveRectified() {
    if (doc_.rectified.isNull()) {
        QMessageBox::information(this, tr("Save rectified image"), tr("No rectified image yet (calibrate first)."));
        return;
    }
    const QFileInfo fi(doc_.path);
    const QString fn = QFileDialog::getSaveFileName(this, tr("Save rectified image"),
                                                    fi.absolutePath() + "/" + fi.completeBaseName() + "_rectified.png",
                                                    tr("PNG image (*.png)"));
    if (fn.isEmpty()) return;
    QImage img = doc_.rectified;
    // Embed the true scale so other programs import it at 1:1.
    img.setDotsPerMeterX(int(std::lround(doc_.rectRes * 1000)));
    img.setDotsPerMeterY(int(std::lround(doc_.rectRes * 1000)));
    if (img.save(fn)) status(tr("Saved %1 (%2 px/mm)").arg(fn).arg(doc_.rectRes, 0, 'f', 2));
    else QMessageBox::warning(this, tr("Save rectified image"), tr("Cannot write %1").arg(fn));
}

// ---------------------------------------------------------------------------
// Tables / info
// ---------------------------------------------------------------------------
void MainWindow::updateRefTable() {
    updatingTable_ = true;
    refTable_->setRowCount(int(doc_.refs.size()));
    for (int i = 0; i < int(doc_.refs.size()); ++i) {
        const RefPoint& r = doc_.refs[size_t(i)];
        auto put = [&](int col, const QString& txt, bool editable) {
            auto* it = new QTableWidgetItem(txt);
            Qt::ItemFlags fl = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
            if (editable) fl |= Qt::ItemIsEditable;
            it->setFlags(fl);
            refTable_->setItem(i, col, it);
        };
        put(0, QString::number(i + 1), false);
        put(1, QString::number(r.img.x, 'f', 1), false);
        put(2, QString::number(r.img.y, 'f', 1), false);
        put(3, QString::number(r.world.x, 'f', 2), true);
        put(4, QString::number(r.world.y, 'f', 2), true);
        put(5, doc_.cal.ok ? QString::number(r.errMM, 'f', 3) : QString("-"), false);
    }
    updatingTable_ = false;
}

void MainWindow::onRefTableEdited(int row, int col) {
    if (updatingTable_ || row < 0 || row >= int(doc_.refs.size()) || (col != 3 && col != 4)) return;
    bool ok = false;
    QString txt = refTable_->item(row, col)->text().trimmed();
    txt.replace(',', '.');
    const double v = txt.toDouble(&ok);
    if (!ok) { updateRefTable(); return; }
    pushUndo();
    RefPoint& r = doc_.refs[size_t(row)];
    (col == 3 ? r.world.x : r.world.y) = v;
    r.fromGrid = false;
    recalibrate(true);
}

void MainWindow::updateInfo() {
    if (!doc_.hasImage()) {
        calInfo_->setText(QString());
    } else if (doc_.cal.ok) {
        cp::Pt c(0, 0);
        for (const auto& r : doc_.refs) c += r.world;
        c = c / double(doc_.refs.size());
        calInfo_->setText(tr("<b>Calibrated</b> with %1 points - residual RMS %2 mm - %3 px/mm on the pattern")
                              .arg(doc_.refs.size())
                              .arg(doc_.cal.rmsMM, 0, 'f', 3)
                              .arg(cp::pxPerMM(doc_.cal.Hw2i, c), 0, 'f', 1));
    } else {
        calInfo_->setText(tr("<b>Not calibrated</b> - need at least 4 reference points (not all on one line); have %1.")
                              .arg(doc_.refs.size()));
    }
    if (doc_.profile.size() >= 2) {
        double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
        for (const auto& p : doc_.profile) {
            x0 = std::min(x0, p.x); y0 = std::min(y0, p.y);
            x1 = std::max(x1, p.x); y1 = std::max(y1, p.y);
        }
        profileInfo_->setText(tr("%1 vertices - length %2 mm - size %3 x %4 mm")
                                  .arg(doc_.profile.size())
                                  .arg(cp::polylineLength(doc_.profile, doc_.closed), 0, 'f', 2)
                                  .arg(x1 - x0, 0, 'f', 2)
                                  .arg(y1 - y0, 0, 'f', 2));
    } else {
        profileInfo_->setText(tr("No profile."));
    }
    undoAct_->setEnabled(!undo_.empty());
    redoAct_->setEnabled(!redo_.empty());
}

// ---------------------------------------------------------------------------
// Undo / redo
// ---------------------------------------------------------------------------
void MainWindow::pushUndo() {
    undo_.push_back(snapshot());
    if (undo_.size() > 200) undo_.erase(undo_.begin());
    redo_.clear();
    updateInfo();
}

void MainWindow::restore(const Snapshot& s) {
    doc_.refs = s.refs;
    doc_.profile = s.profile;
    doc_.closed = s.closed;
    closedCheck_->blockSignals(true);
    closedCheck_->setChecked(s.closed);
    closedCheck_->blockSignals(false);
    recalibrate(true);
}

void MainWindow::undo() {
    if (undo_.empty()) return;
    redo_.push_back(snapshot());
    const Snapshot s = undo_.back();
    undo_.pop_back();
    restore(s);
}

void MainWindow::redo() {
    if (redo_.empty()) return;
    undo_.push_back(snapshot());
    const Snapshot s = redo_.back();
    redo_.pop_back();
    restore(s);
}

// ---------------------------------------------------------------------------
// Drag & drop
// ---------------------------------------------------------------------------
void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e) {
    const auto urls = e->mimeData()->urls();
    if (!urls.isEmpty() && urls.front().isLocalFile()) loadImage(urls.front().toLocalFile());
}
