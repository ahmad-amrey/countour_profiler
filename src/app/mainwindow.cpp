#include "mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImageReader>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

#include "profile_detect.h"
#include "synth.h"
#include "theme.h"
#include "widgets.h"

using theme::Icon;

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

cp::RectD intersect(const cp::RectD& a, const cp::RectD& b) {
    if (!b.valid()) return a;
    cp::RectD r{std::max(a.x0, b.x0), std::max(a.y0, b.y0), std::min(a.x1, b.x1), std::min(a.y1, b.y1)};
    return r.valid() ? r : a;
}

// "3 arcs · 2 lines" with correct singulars.
QString geometrySummary(int arcs, int lines, const QString& sep) {
    const QString a = arcs == 1 ? QObject::tr("1 arc") : QObject::tr("%1 arcs").arg(arcs);
    const QString l = lines == 1 ? QObject::tr("1 line") : QObject::tr("%1 lines").arg(lines);
    if (arcs == 0) return l;
    if (lines == 0) return a;
    return a + sep + l;
}

// Slider positions <-> values.
double tolFromSlider(int v) { return 0.01 * std::pow(100.0, v / 100.0); }      // 0.01 .. 1 mm
int sliderFromTol(double t) { return int(std::lround(100.0 * std::log(t / 0.01) / std::log(100.0))); }

QWidget* sliderRow(const QString& name, QSlider*& slider, QLabel*& value, int lo, int hi, int val) {
    auto* w = new QWidget;
    auto* l = new QVBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(4);
    auto* top = new QHBoxLayout;
    top->addWidget(makeLabel(name, "small"), 1);
    value = makeLabel(QString(), "value");
    top->addWidget(value);
    l->addLayout(top);
    slider = new QSlider(Qt::Horizontal);
    slider->setRange(lo, hi);
    slider->setValue(val);
    slider->setCursor(Qt::PointingHandCursor);
    l->addWidget(slider);
    return w;
}

QWidget* section(QVBoxLayout*& body) {
    auto* w = new QWidget;
    w->setObjectName("PanelPage");
    body = new QVBoxLayout(w);
    body->setContentsMargins(20, 14, 20, 18);
    body->setSpacing(12);
    return w;
}

QToolButton* toolButton(Icon ic, const QString& tip, bool checkable = true) {
    auto* b = new QToolButton;
    b->setProperty("variant", "tool");
    b->setCheckable(checkable);
    b->setToolTip(tip);
    QIcon icon;
    for (qreal dpr : {1.0, 2.0}) {
        icon.addPixmap(theme::pixmap(ic, theme::muted, 19, dpr), QIcon::Normal, QIcon::Off);
        icon.addPixmap(theme::pixmap(ic, theme::accent, 19, dpr), QIcon::Normal, QIcon::On);
        icon.addPixmap(theme::pixmap(ic, QColor("#4A505A"), 19, dpr), QIcon::Disabled, QIcon::Off);
    }
    b->setIcon(icon);
    b->setIconSize(QSize(19, 19));
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

}  // namespace

MainWindow::MainWindow() {
    setWindowTitle(tr("Contour Profiler"));
    setAcceptDrops(true);
    setMinimumSize(1100, 720);
    buildUi();
    buildActions();
    rectTimer_.setSingleShot(true);
    rectTimer_.setInterval(250);
    connect(&rectTimer_, &QTimer::timeout, this, &MainWindow::rebuildRectified);
    resize(1440, 900);
    updateAll();
}

// ---------------------------------------------------------------------------
// UI construction
// ---------------------------------------------------------------------------
void MainWindow::buildUi() {
    auto* root = new QWidget;
    root->setObjectName("Root");
    auto* v = new QVBoxLayout(root);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    v->addWidget(buildHeader());

    pages_ = new QStackedWidget;
    start_ = new StartPage;
    pages_->addWidget(start_);
    auto* work = new QWidget;
    auto* h = new QHBoxLayout(work);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);
    h->addWidget(buildRail());
    h->addWidget(buildStage(), 1);
    h->addWidget(buildPanel());
    pages_->addWidget(work);
    v->addWidget(pages_, 1);
    setCentralWidget(root);
    statusBar()->hide();

    connect(start_, &StartPage::openRequested, this, &MainWindow::openImage);
    connect(start_, &StartPage::sampleRequested, this, &MainWindow::openSample);
    connect(start_, &StartPage::squareChangeRequested, this, &MainWindow::changeSquareSize);
    connect(start_, &StartPage::printSheetRequested, this, &MainWindow::printSheet);

    dim_ = new QFrame(root);
    dim_->setStyleSheet("background: rgba(8,9,11,185);");
    dim_->hide();
}

QWidget* MainWindow::buildHeader() {
    auto* hdr = new QWidget;
    hdr->setObjectName("Header");
    hdr->setAttribute(Qt::WA_StyledBackground);
    hdr->setFixedHeight(56);
    auto* l = new QHBoxLayout(hdr);
    l->setContentsMargins(20, 0, 12, 0);
    l->setSpacing(10);
    auto* logo = new QLabel;
    logo->setPixmap(theme::pixmap(Icon::Logo, theme::accent, 26, devicePixelRatioF()));
    l->addWidget(logo);
    l->addWidget(makeLabel(tr("Contour Profiler"), "title"));
    l->addSpacing(8);
    fileChip_ = makeButton(QString(), "chip", Icon::Close, true, theme::muted);
    fileChip_->setLayoutDirection(Qt::RightToLeft);  // close icon after the name
    fileChip_->setToolTip(tr("Close photo and go back to the start (Ctrl+W)"));
    l->addWidget(fileChip_);
    connect(fileChip_, &QPushButton::clicked, this, &MainWindow::closeDocument);
    l->addStretch(1);
    helpBtn_ = makeButton(tr("How it works"), "ghost", Icon::Help, true, theme::muted);
    openBtn_ = makeButton(tr("Open…"), "ghost");
    exportBtn_ = makeButton(tr("Export DXF"), "primary", Icon::Download, true, theme::onAccent);
    moreBtn_ = makeButton(QString(), "icon", Icon::More, true, theme::muted);
    moreBtn_->setToolTip(tr("More"));
    for (auto* b : {helpBtn_, openBtn_, exportBtn_}) l->addWidget(b);
    l->addWidget(moreBtn_);
    connect(openBtn_, &QPushButton::clicked, this, &MainWindow::openImage);
    connect(exportBtn_, &QPushButton::clicked, this, [this] { exportDialog(0); });
    connect(helpBtn_, &QPushButton::clicked, this, [this] {
        QMessageBox::information(
            this, tr("How it works"),
            tr("<p><b>1. Bend</b> a wire or thin metal strip over the shape you want to copy.</p>"
               "<p><b>2. Photograph</b> it lying on a grid (cutting mat, graph paper or checkerboard) from any "
               "angle. Keep the whole wire on the grid; avoid glare.</p>"
               "<p><b>3. Open</b> the photo: the grid is found, perspective and scale corrected and the wire traced "
               "automatically, then rebuilt as clean lines and arcs.</p>"
               "<p><b>4. Export</b> a DXF in millimetres. Need to fix something? <b>Adjust</b> opens every detail."
               "</p>"));
    });

    auto* menu = new QMenu(this);
    menu->addAction(tr("Open photo…"), QKeySequence::Open, this, &MainWindow::openImage);
    menu->addAction(tr("Open a sample photo"), this, &MainWindow::openSample);
    menu->addAction(tr("Close photo"), QKeySequence::Close, this, &MainWindow::closeDocument);
    menu->addSeparator();
    menu->addAction(tr("Print a calibration sheet…"), this, &MainWindow::printSheet);
    menu->addSeparator();
    menu->addAction(tr("Export DXF…"), this, [this] { exportDialog(0); });
    menu->addAction(tr("Export SVG…"), this, [this] { exportDialog(1); });
    menu->addAction(tr("Export CSV…"), this, [this] { exportDialog(2); });
    menu->addAction(tr("Save corrected image…"), this, &MainWindow::saveRectified);
    menu->addSeparator();
    auto* mask = menu->addAction(tr("Show detection mask"));
    mask->setCheckable(true);
    mask->setChecked(false);
    connect(mask, &QAction::toggled, this, [this](bool on) { canvas_->setShowMask(on); });
    auto* cand = menu->addAction(tr("Show grid corner candidates"));
    cand->setCheckable(true);
    connect(cand, &QAction::toggled, this, [this](bool on) { canvas_->setShowCandidates(on); });
    menu->addSeparator();
    menu->addAction(tr("How it works"), this, [this] { helpBtn_->click(); });
    menu->addAction(tr("About Contour Profiler"), this, [this] {
        QMessageBox::about(this, tr("About Contour Profiler"),
                           tr("<b>Contour Profiler %1</b><br>2D profiles from photos of a bent wire on a grid, "
                              "exported as clean lines and arcs.<br><br>Built with Qt %2.")
                               .arg(QApplication::applicationVersion(), QT_VERSION_STR));
    });
    connect(moreBtn_, &QPushButton::clicked, this,
            [this, menu] { menu->popup(moreBtn_->mapToGlobal(QPoint(moreBtn_->width() - menu->sizeHint().width(), moreBtn_->height() + 4))); });
    return hdr;
}

QWidget* MainWindow::buildRail() {
    rail_ = new QWidget;
    rail_->setObjectName("Rail");
    rail_->setAttribute(Qt::WA_StyledBackground);
    rail_->setFixedWidth(60);
    auto* l = new QVBoxLayout(rail_);
    l->setContentsMargins(9, 14, 9, 14);
    l->setSpacing(6);
    const Icon icons[5] = {Icon::Pointer, Icon::AddPoint, Icon::Draw, Icon::Target, Icon::Pipette};
    const QString tips[5] = {tr("Edit points (E)\nDrag points · double-click the line to add · right-click to delete"),
                             tr("Add reference point (R)\nClick a known mark on the grid"),
                             tr("Draw profile (D)\nClick to place points; they become lines and arcs"),
                             tr("Pick the wire (O)\nClick the wire if something else was traced"),
                             tr("Pick wire colour (C)\nDetect the wire by its colour")};
    const int order[5] = {0, 2, 3, 4, 1};  // edit, draw, pick wire, pick colour, add reference
    for (int k = 0; k < 5; ++k) {
        const int i = order[k];
        toolBtns_[i] = toolButton(icons[i], tips[i]);
        l->addWidget(toolBtns_[i], 0, Qt::AlignHCenter);
        connect(toolBtns_[i], &QToolButton::clicked, this, [this, i] { setTool(Canvas::Tool(i)); });
    }
    toolBtns_[0]->setChecked(true);
    rail_->setVisible(false);  // shown with the details tier
    l->addStretch(1);
    undoBtn_ = toolButton(Icon::Undo, tr("Undo (Ctrl+Z)"), false);
    redoBtn_ = toolButton(Icon::Redo, tr("Redo (Ctrl+Y)"), false);
    l->addWidget(undoBtn_, 0, Qt::AlignHCenter);
    l->addWidget(redoBtn_, 0, Qt::AlignHCenter);
    connect(undoBtn_, &QToolButton::clicked, this, &MainWindow::undo);
    connect(redoBtn_, &QToolButton::clicked, this, &MainWindow::redo);
    return rail_;
}

QWidget* MainWindow::buildStage() {
    stage_ = new QWidget;
    auto* l = new QVBoxLayout(stage_);
    l->setContentsMargins(0, 0, 0, 0);
    canvas_ = new Canvas(&doc_);
    l->addWidget(canvas_);
    stage_->installEventFilter(this);

    viewSeg_ = new Segmented({tr("Photo"), tr("Corrected")}, true, stage_);
    {
        const auto btns = viewSeg_->findChildren<QPushButton*>();
        if (btns.size() == 2) {
            btns[0]->setToolTip(tr("The photo as taken (1)"));
            btns[1]->setToolTip(tr("Perspective removed, true scale in mm (2)"));
        }
    }
    connect(viewSeg_, &Segmented::changed, this,
            [this](int i) { setView(i == 0 ? Canvas::View::Original : Canvas::View::Rectified); });

    zoomBox_ = new QFrame(stage_);
    zoomBox_->setObjectName("FloatBox");
    auto* zl = new QVBoxLayout(zoomBox_);
    zl->setContentsMargins(4, 4, 4, 4);
    zl->setSpacing(2);
    struct Z { Icon ic; QString tip; std::function<void()> fn; };
    const Z zs[3] = {{Icon::Plus, tr("Zoom in"), [this] { canvas_->zoomBy(1.25); }},
                     {Icon::Minus, tr("Zoom out"), [this] { canvas_->zoomBy(0.8); }},
                     {Icon::Fit, tr("Fit to window (F)"), [this] { canvas_->fitToWindow(); }}};
    for (const Z& z : zs) {
        auto* b = makeButton(QString(), "icon", z.ic, true, theme::text);
        b->setToolTip(z.tip);
        b->setFixedSize(36, 36);
        b->setStyleSheet("max-width: 36px; min-width: 36px; min-height: 36px;");
        zl->addWidget(b);
        connect(b, &QPushButton::clicked, this, z.fn);
    }

    resultCard_ = new ResultCard(stage_);
    connect(resultCard_, &ResultCard::adjustRequested, this, [this] { setDetails(true); });
    connect(resultCard_, &ResultCard::exportRequested, this, [this] { exportDialog(0); });

    calibCard_ = new CalibrateCard(stage_);
    connect(calibCard_, &CalibrateCard::undoRequested, this, [this] {
        if (doc_.refs.empty()) return;
        pushUndo();
        doc_.refs.pop_back();
        recalibrate(true);
    });
    connect(calibCard_, &CalibrateCard::retryRequested, this, &MainWindow::runAutomatic);

    hint_ = makeLabel(QString(), "hint");
    hint_->setParent(stage_);

    // Canvas signals.
    connect(canvas_, &Canvas::aboutToModify, this, &MainWindow::pushUndo);
    connect(canvas_, &Canvas::refsChanged, this, &MainWindow::recalibrate);
    connect(canvas_, &Canvas::profileChanged, this, &MainWindow::refit);
    connect(canvas_, &Canvas::addRefRequested, this, &MainWindow::addReference);
    connect(canvas_, &Canvas::pickObjectRequested, this,
            [this](QPointF w) { detectProfile(true, cp::Pt(w.x(), w.y())); });
    connect(canvas_, &Canvas::pickColorRequested, this, &MainWindow::pickColor);
    connect(canvas_, &Canvas::toolChanged, this, &MainWindow::setTool);
    connect(canvas_, &Canvas::refSelected, this, [this](int i) {
        if (i >= 0 && i < refTable_->rowCount()) refTable_->selectRow(i);
    });
    return stage_;
}

QWidget* MainWindow::buildPanel() {
    panelScroll_ = new QScrollArea;
    panelScroll_->setObjectName("PanelScroll");
    panelScroll_->setFixedWidth(372);
    panelScroll_->setWidgetResizable(true);
    panelScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    panelStack_ = new QStackedWidget;
    panelStack_->setObjectName("Panel");
    panelScroll_->setWidget(panelStack_);

    auto* page = new QWidget;
    page->setObjectName("PanelPage");
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    auto* head = new QHBoxLayout;
    head->setContentsMargins(20, 16, 12, 12);
    head->addWidget(makeLabel(tr("Adjust"), "title"), 1);
    auto* close = makeButton(QString(), "icon", Icon::Close, true, theme::muted);
    close->setToolTip(tr("Close details (Esc)"));
    head->addWidget(close);
    v->addLayout(head);
    connect(close, &QPushButton::clicked, this, [this] { setDetails(false); });

    // Calibration.
    v->addWidget(makeDivider());
    QVBoxLayout* b = nullptr;
    v->addWidget(section(b));
    {
        auto* t = new QHBoxLayout;
        t->addWidget(makeLabel(tr("Calibration"), "h3"), 1);
        calStatus_ = makeLabel(QString(), "small");
        calStatus_->setTextFormat(Qt::RichText);
        t->addWidget(calStatus_);
        b->addLayout(t);
        auto* g = new QHBoxLayout;
        g->setSpacing(10);
        auto* c1 = new QVBoxLayout;
        c1->setSpacing(6);
        c1->addWidget(makeLabel(tr("Pattern"), "small"));
        patternCombo_ = new QComboBox;
        patternCombo_->addItems({tr("Line grid"), tr("Checkerboard")});
        patternCombo_->setToolTip(tr("Line grid: cutting mats, graph paper (light or dark lines)."));
        c1->addWidget(patternCombo_);
        auto* c2 = new QVBoxLayout;
        c2->setSpacing(6);
        c2->addWidget(makeLabel(tr("Square size"), "small"));
        squareSpin_ = new QDoubleSpinBox;
        squareSpin_->setRange(0.1, 1000);
        squareSpin_->setDecimals(2);
        squareSpin_->setValue(10);
        squareSpin_->setSuffix(" mm");
        squareSpin_->setButtonSymbols(QAbstractSpinBox::NoButtons);
        squareSpin_->setKeyboardTracking(false);
        c2->addWidget(squareSpin_);
        g->addLayout(c1, 1);
        g->addLayout(c2, 1);
        b->addLayout(g);
        auto* bb = new QHBoxLayout;
        bb->setSpacing(8);
        auto* again = makeButton(tr("Detect again"));
        editRefsBtn_ = makeButton(tr("Reference points"));
        editRefsBtn_->setCheckable(true);
        bb->addWidget(again, 1);
        bb->addWidget(editRefsBtn_, 1);
        b->addLayout(bb);
        refsBox_ = new QWidget;
        auto* rl = new QVBoxLayout(refsBox_);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(8);
        refTable_ = new QTableWidget(0, 5);
        refTable_->setHorizontalHeaderLabels({"#", tr("X mm"), tr("Y mm"), tr("err mm"), ""});
        refTable_->setColumnHidden(4, true);
        refTable_->verticalHeader()->setVisible(false);
        refTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        refTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        refTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
        refTable_->setMinimumHeight(180);
        rl->addWidget(refTable_);
        auto* rb = new QHBoxLayout;
        auto* add = makeButton(tr("Add"));
        auto* del = makeButton(tr("Delete"));
        auto* clr = makeButton(tr("Clear"));
        rb->addWidget(add);
        rb->addWidget(del);
        rb->addWidget(clr);
        rl->addLayout(rb);
        snapCheck_ = new QCheckBox(tr("Snap to grid corners"));
        snapCheck_->setChecked(true);
        rl->addWidget(snapCheck_);
        auto* hint = makeLabel(tr("Points are coloured by fit: green < 0.1 mm, orange < 0.3 mm, red worse. Edit the "
                                  "mm values to use any known marks."),
                               "faint");
        hint->setWordWrap(true);
        rl->addWidget(hint);
        refsBox_->setVisible(false);
        b->addWidget(refsBox_);

        connect(again, &QPushButton::clicked, this, &MainWindow::runAutomatic);
        connect(squareSpin_, &QDoubleSpinBox::valueChanged, this, &MainWindow::applySquareSize);
        connect(editRefsBtn_, &QPushButton::toggled, this, [this](bool on) {
            refsBox_->setVisible(on);
            if (on) setView(Canvas::View::Original);
        });
        connect(add, &QPushButton::clicked, this, [this] {
            setView(Canvas::View::Original);
            setTool(Canvas::Tool::AddRef);
        });
        connect(del, &QPushButton::clicked, this, [this] {
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
        connect(clr, &QPushButton::clicked, this, [this] {
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
    }

    // Wire detection.
    v->addWidget(makeDivider());
    v->addWidget(section(b));
    {
        b->addWidget(makeLabel(tr("Wire detection"), "h3"));
        methodSeg_ = new Segmented({tr("Automatic"), tr("By colour")});
        b->addWidget(methodSeg_);
        b->addWidget(sliderRow(tr("Sensitivity"), sensSlider_, sensValue_, 0, 100, 50));
        sensSlider_->setToolTip(tr("Middle = automatic. Right finds fainter wires, left ignores more clutter."));
        colorRow_ = new QWidget;
        auto* cr = new QHBoxLayout(colorRow_);
        cr->setContentsMargins(0, 0, 0, 0);
        cr->setSpacing(8);
        cr->addWidget(makeLabel(tr("Colour"), "small"));
        colorBtn_ = new QPushButton;
        colorBtn_->setFixedSize(44, 30);
        colorBtn_->setCursor(Qt::PointingHandCursor);
        cr->addWidget(colorBtn_);
        cr->addStretch(1);
        cr->addWidget(makeLabel(tr("Tolerance"), "small"));
        colorTolSpin_ = new QDoubleSpinBox;
        colorTolSpin_->setRange(1, 255);
        colorTolSpin_->setDecimals(0);
        colorTolSpin_->setValue(60);
        colorTolSpin_->setButtonSymbols(QAbstractSpinBox::NoButtons);
        colorTolSpin_->setFixedWidth(64);
        cr->addWidget(colorTolSpin_);
        colorRow_->setVisible(false);
        b->addWidget(colorRow_);
        b->addWidget(sliderRow(tr("Smoothing"), smoothSlider_, smoothValue_, 0, 100, 30));
        auto* mr = new QHBoxLayout;
        mr->addWidget(makeLabel(tr("Search beyond the grid"), "small"), 1);
        marginSpin_ = new QDoubleSpinBox;
        marginSpin_->setRange(0, 1000);
        marginSpin_->setDecimals(1);
        marginSpin_->setValue(2.5);
        marginSpin_->setSuffix(" mm");
        marginSpin_->setButtonSymbols(QAbstractSpinBox::NoButtons);
        marginSpin_->setKeyboardTracking(false);
        marginSpin_->setFixedWidth(90);
        mr->addWidget(marginSpin_);
        b->addLayout(mr);
        outlineCheck_ = new QCheckBox(tr("Trace the outline instead of the centre line"));
        outlineCheck_->setProperty("role", "small");
        b->addWidget(outlineCheck_);
        auto* trace = makeButton(tr("Trace again"));
        b->addWidget(trace);

        connect(methodSeg_, &Segmented::changed, this, [this](int i) {
            colorRow_->setVisible(i == 1);
            sensSlider_->parentWidget()->setVisible(i == 0);
        });
        connect(sensSlider_, &QSlider::valueChanged, this, [this] { updatePanel(); });
        connect(smoothSlider_, &QSlider::valueChanged, this, [this] { updatePanel(); });
        for (QSlider* s : {sensSlider_, smoothSlider_})
            connect(s, &QSlider::sliderReleased, this, [this] { detectProfile(false, {}); });
        connect(colorBtn_, &QPushButton::clicked, this, [this] {
            const QColor c = QColorDialog::getColor(pickedColor_, this, tr("Wire colour"));
            if (!c.isValid()) return;
            pickedColor_ = c;
            methodSeg_->setIndex(1);
            colorRow_->setVisible(true);
            updatePanel();
            detectProfile(false, {});
        });
        connect(marginSpin_, &QDoubleSpinBox::valueChanged, this, [this] {
            updateDetectRoi();
            rectTimer_.start();
            canvas_->update();
        });
        connect(trace, &QPushButton::clicked, this, [this] { detectProfile(false, {}); });
    }

    // Lines & arcs.
    v->addWidget(makeDivider());
    v->addWidget(section(b));
    {
        auto* t = new QHBoxLayout;
        t->addWidget(makeLabel(tr("Lines & arcs"), "h3"), 1);
        fitCheck_ = new QCheckBox(tr("Fit automatically"));
        fitCheck_->setProperty("role", "small");
        fitCheck_->setChecked(true);
        fitCheck_->setLayoutDirection(Qt::RightToLeft);
        t->addWidget(fitCheck_);
        b->addLayout(t);
        b->addWidget(sliderRow(tr("Tolerance"), tolSlider_, tolValue_, 0, 100, sliderFromTol(0.10)));
        tolSlider_->setToolTip(tr("Largest allowed deviation from the traced points. Higher = fewer, cleaner "
                                  "segments."));
        auto* r = new QHBoxLayout;
        r->addWidget(makeLabel(tr("Result"), "muted"), 1);
        fitSummaryBtn_ = makeButton(QString(), "link");
        r->addWidget(fitSummaryBtn_);
        b->addLayout(r);
        connect(fitCheck_, &QCheckBox::toggled, this, [this](bool on) {
            doc_.fitEnabled = on;
            refit();
        });
        connect(tolSlider_, &QSlider::valueChanged, this, [this](int val) {
            doc_.fitTol = tolFromSlider(val);
            if (tolSlider2_->value() != val) tolSlider2_->setValue(val);
            refit();
        });
        connect(fitSummaryBtn_, &QPushButton::clicked, this, [this] { showArcsPage(true); });
    }
    v->addWidget(makeDivider());
    v->addStretch(1);
    panelStack_->addWidget(page);
    panelStack_->addWidget(buildArcsPage());
    panelScroll_->setVisible(false);
    return panelScroll_;
}

QWidget* MainWindow::buildArcsPage() {
    auto* page = new QWidget;
    page->setObjectName("PanelPage");
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    auto* head = new QHBoxLayout;
    head->setContentsMargins(12, 16, 12, 4);
    head->setSpacing(6);
    auto* back = makeButton(QString(), "icon", Icon::ChevronLeft, true, theme::muted);
    back->setToolTip(tr("Back to Adjust"));
    head->addWidget(back);
    head->addWidget(makeLabel(tr("Lines & arcs"), "title"), 1);
    v->addLayout(head);
    connect(back, &QPushButton::clicked, this, [this] { showArcsPage(false); });
    auto* d = makeLabel(tr("Traced or clicked points are rebuilt as the fewest lines and arcs that stay within "
                           "tolerance — ready for CAD."),
                        "muted");
    d->setWordWrap(true);
    d->setContentsMargins(20, 0, 20, 12);
    v->addWidget(d);
    QVBoxLayout* b = nullptr;
    auto* top = section(b);
    b->setContentsMargins(20, 0, 20, 16);
    b->addWidget(sliderRow(tr("Tolerance"), tolSlider2_, tolValue2_, 0, 100, sliderFromTol(0.10)));
    showPointsCheck_ = new QCheckBox(tr("Show the source points"));
    b->addWidget(showPointsCheck_);
    v->addWidget(top);
    v->addWidget(makeDivider());
    segList_ = new QListWidget;
    segList_->setStyleSheet("QListWidget { border: none; background: #16181C; padding: 8px; }"
                            "QListWidget::item { min-height: 44px; }");
    segList_->setSpacing(2);
    v->addWidget(segList_, 1);
    connect(tolSlider2_, &QSlider::valueChanged, this, [this](int val) {
        if (tolSlider_->value() != val) tolSlider_->setValue(val);
    });
    connect(showPointsCheck_, &QCheckBox::toggled, canvas_, &Canvas::setShowPoints);
    connect(segList_, &QListWidget::currentRowChanged, this, [this](int row) {
        doc_.selectedSeg = row;
        canvas_->update();
    });
    return page;
}

void MainWindow::buildActions() {
    auto add = [this](const QKeySequence& k, std::function<void()> fn) {
        auto* a = new QAction(this);
        a->setShortcut(k);
        a->setShortcutContext(Qt::WindowShortcut);
        connect(a, &QAction::triggered, this, fn);
        addAction(a);
    };
    add(QKeySequence::Open, [this] { openImage(); });
    add(QKeySequence(Qt::CTRL | Qt::Key_W), [this] { closeDocument(); });
    add(QKeySequence(Qt::CTRL | Qt::Key_E), [this] { exportDialog(0); });
    add(QKeySequence::Undo, [this] { undo(); });
    add(QKeySequence::Redo, [this] { redo(); });
    add(QKeySequence(Qt::CTRL | Qt::Key_Y), [this] { redo(); });
    add(QKeySequence(Qt::Key_F5), [this] { runAutomatic(); });
    add(QKeySequence(Qt::Key_1), [this] { setView(Canvas::View::Original); });
    add(QKeySequence(Qt::Key_2), [this] { setView(Canvas::View::Rectified); });
    add(QKeySequence(Qt::Key_F), [this] { canvas_->fitToWindow(); });
    const Qt::Key keys[5] = {Qt::Key_E, Qt::Key_R, Qt::Key_D, Qt::Key_O, Qt::Key_C};
    for (int i = 0; i < 5; ++i)
        add(QKeySequence(keys[i]), [this, i] {
            if (!doc_.hasImage()) return;
            setDetails(true);
            setTool(Canvas::Tool(i));
        });
}

bool MainWindow::eventFilter(QObject* o, QEvent* e) {
    if (o == stage_ && (e->type() == QEvent::Resize || e->type() == QEvent::Show)) layoutOverlays();
    return QMainWindow::eventFilter(o, e);
}

void MainWindow::layoutOverlays() {
    if (!stage_) return;
    const int W = stage_->width(), H = stage_->height();
    viewSeg_->adjustSize();
    viewSeg_->move(20, 20);
    zoomBox_->adjustSize();
    zoomBox_->move(W - zoomBox_->width() - 20, 20);
    const int cw = std::min(780, W - 40);
    resultCard_->setFixedWidth(cw);
    resultCard_->adjustSize();
    resultCard_->move((W - cw) / 2, H - resultCard_->height() - 24);
    calibCard_->adjustSize();
    calibCard_->move(24, 24);
    hint_->adjustSize();
    hint_->move(20, H - hint_->height() - 20);
    if (dim_ && centralWidget()) dim_->setGeometry(centralWidget()->rect());
}

// ---------------------------------------------------------------------------
// Tiers and modes
// ---------------------------------------------------------------------------
void MainWindow::setDetails(bool on) {
    if (!doc_.hasImage()) on = false;
    details_ = on;
    rail_->setVisible(on);
    panelScroll_->setVisible(on);
    canvas_->setDetails(on);
    if (!on) {
        showArcsPage(false);
        setTool(Canvas::Tool::Select);
    }
    updateAll();
}

void MainWindow::showArcsPage(bool on) {
    panelStack_->setCurrentIndex(on ? 1 : 0);
    doc_.showSegColors = on;
    if (!on) {
        doc_.selectedSeg = -1;
        segList_->clearSelection();
    }
    canvas_->update();
}

void MainWindow::setCalibrating(bool on) {
    calibrating_ = on;
    if (on) {
        calibCard_->setSquare(doc_.squareMM);
        setView(Canvas::View::Original);
        canvas_->setTool(Canvas::Tool::AddRef);
    } else if (canvas_->tool() == Canvas::Tool::AddRef) {
        setTool(Canvas::Tool::Select);
    }
    updateAll();
}

void MainWindow::setTool(Canvas::Tool t) {
    if (t == Canvas::Tool::AddRef) setView(Canvas::View::Original);
    canvas_->setTool(t);
    for (int i = 0; i < 5; ++i)
        if (toolBtns_[i]) toolBtns_[i]->setChecked(i == int(t));
    updateAll();
}

void MainWindow::setView(Canvas::View v) {
    canvas_->setView(v);
    viewSeg_->setIndex(v == Canvas::View::Original ? 0 : 1);
}

void MainWindow::status(const QString& msg) {
    lastMessage_ = msg;
    updateAll();
}

// ---------------------------------------------------------------------------
// Loading and the automatic pipeline
// ---------------------------------------------------------------------------
void MainWindow::openImage() {
    const QString fn = QFileDialog::getOpenFileName(
        this, tr("Open photo"), QFileInfo(doc_.path).absolutePath(),
        tr("Images (*.jpg *.jpeg *.png *.bmp *.tif *.tiff *.webp);;All files (*)"));
    if (!fn.isEmpty()) loadImage(fn);
}

void MainWindow::openSample() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString fn = QDir(dir).filePath("contour_profiler_sample.png");
    cp::SynthParams sp;
    sp.squareMM = doc_.squareMM;
    if (toQImage(cp::renderSynthetic(sp).img).save(fn)) loadImage(fn);
}

void MainWindow::closeDocument() {
    if (!doc_.hasImage()) return;
    setDetails(false);
    calibrating_ = false;
    const double square = doc_.squareMM;
    const bool fitOn = doc_.fitEnabled;
    const double tol = doc_.fitTol;
    doc_ = Document();
    doc_.squareMM = square;
    doc_.fitEnabled = fitOn;
    doc_.fitTol = tol;
    undo_.clear();
    redo_.clear();
    lastMessage_.clear();
    canvas_->imageChanged();
    canvas_->rectifiedChanged();
    updateRefTable();
    setWindowTitle(tr("Contour Profiler"));
    pages_->setCurrentIndex(0);
    updateAll();
}

void MainWindow::printSheet() {
    PrintSheetDialog dlg(this, doc_.squareMM);
    dim_->setGeometry(centralWidget()->rect());
    dim_->raise();
    dim_->show();
    dlg.adjustSize();
    dlg.move(mapToGlobal(QPoint((width() - dlg.width()) / 2, std::max(40, (height() - dlg.height()) / 2 - 20))));
    const int res = dlg.exec();
    dim_->hide();
    if (res != QDialog::Accepted) return;
    const SheetOptions o = dlg.options();
    const QString def = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
                            .filePath(QString("calibration_%1mm_%2.pdf")
                                          .arg(o.squareMM, 0, 'g', 4)
                                          .arg(o.checkerboard ? "checkerboard" : (o.dark ? "grid_dark" : "grid")));
    const QString fn = QFileDialog::getSaveFileName(this, tr("Save calibration sheet"), def, tr("PDF (*.pdf)"));
    if (fn.isEmpty()) return;
    QString err;
    if (!writeSheetPdf(fn, o, &err)) {
        QMessageBox::warning(this, tr("Calibration sheet"), err);
        return;
    }
    if (std::fabs(o.squareMM - doc_.squareMM) > 1e-9) {
        doc_.squareMM = o.squareMM;
        squareSpin_->setValue(o.squareMM);
        start_->setSquare(o.squareMM);
    }
    QMessageBox::information(this, tr("Calibration sheet"),
                             tr("Saved %1.\n\nPrint it at 100 % (\"Actual size\", no scaling) and check that the bar "
                                "at the bottom measures 100 mm.")
                                 .arg(QFileInfo(fn).fileName()));
}

void MainWindow::changeSquareSize() {
    bool ok = false;
    const double v = QInputDialog::getDouble(this, tr("Grid squares"), tr("Size of one grid square (mm):"),
                                             doc_.squareMM, 0.1, 1000, 2, &ok);
    if (!ok) return;
    squareSpin_->setValue(v);
    doc_.squareMM = v;
    start_->setSquare(v);
}

bool MainWindow::loadImage(const QString& path) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QImage img = reader.read();
    if (img.isNull()) {
        QMessageBox::warning(this, tr("Open photo"), tr("Cannot read %1:\n%2").arg(path, reader.errorString()));
        return false;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const double square = doc_.squareMM;
    const bool fitOn = doc_.fitEnabled;
    const double tol = doc_.fitTol;
    doc_ = Document();
    doc_.squareMM = square;
    doc_.fitEnabled = fitOn;
    doc_.fitTol = tol;
    doc_.path = path;
    doc_.image = img.convertToFormat(QImage::Format_RGB888);
    doc_.core = fromQImage(doc_.image);
    doc_.gray = cp::toGray(doc_.core);
    undo_.clear();
    redo_.clear();
    lastAutoThr_ = 0;
    gridDetected_ = false;
    lastMessage_.clear();
    pages_->setCurrentIndex(1);
    canvas_->imageChanged();
    setView(Canvas::View::Original);
    setWindowTitle(tr("Contour Profiler - %1").arg(QFileInfo(path).fileName()));
    QApplication::restoreOverrideCursor();
    updateRefTable();
    runAutomatic();
    return true;
}

void MainWindow::runAutomatic() {
    if (!doc_.hasImage()) return;
    if (!detectGrid()) {
        setCalibrating(true);
        return;
    }
    setCalibrating(false);
    rebuildRectified();
    detectProfile(false, {});
    setView(Canvas::View::Rectified);
}

bool MainWindow::detectGrid() {
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const cp::GridResult g = cp::detectGrid(doc_.gray, cp::PatternType(patternCombo_->currentIndex()));
    QApplication::restoreOverrideCursor();
    doc_.grid = g;
    if (!g.ok) {
        // Try the other pattern type before giving up.
        const int other = 1 - patternCombo_->currentIndex();
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const cp::GridResult g2 = cp::detectGrid(doc_.gray, cp::PatternType(other));
        QApplication::restoreOverrideCursor();
        if (!g2.ok) {
            gridDetected_ = false;
            status(QString::fromStdString(g.message));
            return false;
        }
        patternCombo_->setCurrentIndex(other);
        doc_.grid = g2;
    }
    pushUndo();
    doc_.refs.clear();
    for (const auto& n : doc_.grid.nodes) {
        RefPoint r;
        r.img = n.img;
        r.world = cp::gridWorld(n, doc_.squareMM);
        r.fromGrid = true;
        r.gi = n.i;
        r.gj = n.j;
        doc_.refs.push_back(r);
    }
    gridDetected_ = true;
    recalibrate(true);
    return doc_.cal.ok;
}

void MainWindow::applySquareSize(double s) {
    const double old = doc_.squareMM;
    doc_.squareMM = s;
    start_->setSquare(s);
    if (doc_.refs.empty() || old <= 0 || std::fabs(s - old) < 1e-12) return;
    pushUndo();
    bool allGrid = true;
    for (auto& r : doc_.refs) {
        if (r.fromGrid) r.world = cp::Pt(r.gi * s, r.gj * s);
        else allGrid = false;
    }
    if (allGrid)
        for (auto& p : doc_.profile) p = p * (s / old);
    recalibrate(true);
    refit();
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
        for (const auto& r : doc_.refs) {
            img.push_back(r.img);
            world.push_back(r.world);
        }
        doc_.cal = cp::calibrate(img, world);
        if (doc_.cal.ok)
            for (size_t i = 0; i < doc_.refs.size(); ++i) doc_.refs[i].errMM = doc_.cal.residualsMM[i];
    } else {
        doc_.cal = cp::Calibration();
    }
    updateDetectRoi();
    if (finished) updateRefTable();
    if (calibrating_) calibCard_->setCount(int(doc_.refs.size()));
    canvas_->update();
    rectTimer_.start();
    updateAll();
}

void MainWindow::addReference(QPointF imgPt) {
    pushUndo();
    RefPoint r;
    r.img = cp::Pt(imgPt.x(), imgPt.y());
    const bool useTemplate = calibrating_ || !doc_.cal.ok;
    if (!useTemplate) {
        cp::Pt w = doc_.cal.Hi2w.apply(r.img);
        const double s = doc_.squareMM;
        const cp::Pt g(std::round(w.x / s) * s, std::round(w.y / s) * s);
        r.world = cp::dist(g, w) < 0.25 * s ? g : cp::Pt(std::round(w.x * 10) / 10, std::round(w.y * 10) / 10);
    } else if (doc_.refs.size() < 4) {
        const double W = calibCard_->rectW(), H = calibCard_->rectH();
        const cp::Pt tmpl[4] = {{0, 0}, {W, 0}, {W, H}, {0, H}};
        r.world = tmpl[doc_.refs.size()];
    } else {
        // Beyond four: predict from the current calibration (snapped to the grid).
        cp::Pt w = doc_.cal.ok ? doc_.cal.Hi2w.apply(r.img) : cp::Pt();
        const double s = doc_.squareMM;
        r.world = cp::Pt(std::round(w.x / s) * s, std::round(w.y / s) * s);
    }
    doc_.refs.push_back(r);
    recalibrate(true);
    if (calibrating_ && doc_.refs.size() == 4 && doc_.cal.ok) {
        // Calibrated by hand: continue the automatic flow.
        rebuildRectified();
        detectProfile(false, {});
        setCalibrating(false);
        setView(Canvas::View::Rectified);
    }
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

double MainWindow::threshold() const {
    const int v = sensSlider_->value();
    if (v == 50) return 0;  // automatic
    const double base = lastAutoThr_ > 0 ? lastAutoThr_ : 45;
    return base * std::pow(2.0, (50 - v) / 25.0);
}

void MainWindow::detectProfile(bool hasSeed, cp::Pt seed) {
    if (!doc_.cal.ok || !doc_.detectRoi.valid()) {
        status(tr("Calibrate first: detect the grid or click 4 corners of a known rectangle."));
        return;
    }
    cp::ProfileParams prm;
    prm.mode = cp::DetectMode(methodSeg_->index());
    prm.output = outlineCheck_->isChecked() ? cp::ProfileOutput::Outline : cp::ProfileOutput::Centerline;
    prm.pattern = cp::PatternType(patternCombo_->currentIndex());
    prm.squareMM = doc_.squareMM;
    prm.threshold = prm.mode == cp::DetectMode::Color ? 0 : threshold();
    prm.color[0] = uint8_t(pickedColor_.red());
    prm.color[1] = uint8_t(pickedColor_.green());
    prm.color[2] = uint8_t(pickedColor_.blue());
    prm.colorTol = colorTolSpin_->value();
    prm.smoothMM = smoothSlider_->value() / 100.0;
    prm.simplifyMM = 0.02;  // dense enough for the line/arc fit
    prm.hasSeed = hasSeed;
    prm.seed = seed;
    cp::RectD roi = doc_.detectRoi;
    if (hasSeed) {
        const double m = doc_.squareMM;
        roi.x0 = std::min(roi.x0, seed.x - m);
        roi.y0 = std::min(roi.y0, seed.y - m);
        roi.x1 = std::max(roi.x1, seed.x + m);
        roi.y1 = std::max(roi.y1, seed.y + m);
        roi = intersect(roi, imageFootprint());
    }
    const cp::Pt c((roi.x0 + roi.x1) / 2, (roi.y0 + roi.y1) / 2);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const cp::ProfileResult r = cp::detectProfile(doc_.core, doc_.cal.Hw2i, roi, cp::pxPerMM(doc_.cal.Hw2i, c), prm);
    QApplication::restoreOverrideCursor();
    if (prm.threshold <= 0 && prm.mode == cp::DetectMode::PatternSubtraction) lastAutoThr_ = r.thresholdUsed;

    if (!r.mask.empty()) {
        QImage m(r.maskW, r.maskH, QImage::Format_ARGB32);
        for (int y = 0; y < r.maskH; ++y) {
            auto* line = reinterpret_cast<QRgb*>(m.scanLine(y));
            for (int x = 0; x < r.maskW; ++x) {
                const uint8_t v = r.mask[size_t(y) * r.maskW + x];
                line[x] = v == 2 ? qRgba(70, 195, 138, 80) : v == 1 ? qRgba(255, 80, 60, 90) : qRgba(0, 0, 0, 0);
            }
        }
        doc_.maskImage = m;
        doc_.maskArea = r.maskArea;
    }
    if (r.ok) {
        pushUndo();
        doc_.profile = r.pts;
        doc_.closed = r.closed;
        lastMessage_.clear();
    } else {
        lastMessage_ = QString::fromStdString(r.message);
    }
    refit();
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
    methodSeg_->setIndex(1);
    colorRow_->setVisible(true);
    sensSlider_->parentWidget()->setVisible(false);
    if (doc_.cal.ok) detectProfile(true, doc_.cal.Hi2w.apply(cp::Pt(imgPt.x(), imgPt.y())));
}

void MainWindow::refit() {
    doc_.segs.clear();
    if (doc_.fitEnabled && doc_.profile.size() >= 2) {
        cp::FitOptions o;
        o.tol = doc_.fitTol;
        doc_.segs = cp::fitLinesArcs(doc_.profile, doc_.closed, o);
    }
    if (doc_.selectedSeg >= int(doc_.segs.size())) doc_.selectedSeg = -1;
    updateSegList();
    canvas_->update();
    updateAll();
}

// ---------------------------------------------------------------------------
// Export
// ---------------------------------------------------------------------------
void MainWindow::exportDialog(int format) {
    if (doc_.profile.size() < 2) {
        QMessageBox::information(this, tr("Export"), tr("There is no profile to export yet."));
        return;
    }
    double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
    const auto pts = doc_.segs.empty() ? doc_.profile : cp::sampleSegments(doc_.segs, 0.05);
    for (const auto& p : pts) {
        x0 = std::min(x0, p.x); y0 = std::min(y0, p.y);
        x1 = std::max(x1, p.x); y1 = std::max(y1, p.y);
    }
    int na = 0, nl = 0;
    double len = 0;
    for (const auto& s : doc_.segs) {
        (s.arc ? na : nl)++;
        len += s.length();
    }
    if (doc_.segs.empty()) len = cp::polylineLength(doc_.profile, doc_.closed);
    const QString geo = doc_.segs.empty() ? tr("%1 points").arg(doc_.profile.size())
                                          : geometrySummary(na, nl, " · ");
    ExportDialog dlg(this, doc_.profile, doc_.segs, doc_.closed,
                     QString("%1 × %2 mm").arg(x1 - x0, 0, 'f', 1).arg(y1 - y0, 0, 'f', 1),
                     QString("%1 mm").arg(len, 0, 'f', 1), geo, format);
    dim_->setGeometry(centralWidget()->rect());
    dim_->raise();
    dim_->show();
    dlg.adjustSize();
    dlg.move(mapToGlobal(QPoint((width() - dlg.width()) / 2, std::max(60, (height() - dlg.height()) / 2 - 40))));
    const int res = dlg.exec();
    dim_->hide();
    if (res == QDialog::Accepted) exportFile(dlg.format(), dlg.linesAndArcs(), dlg.origin(), dlg.gridLayer());
}

void MainWindow::exportFile(int kind, bool arcs, int origin, bool grid) {
    static const char* ext[3] = {"dxf", "svg", "csv"};
    static const char* filter[3] = {"DXF drawing (*.dxf)", "SVG drawing (*.svg)", "CSV points (*.csv)"};
    lastFormat_ = kind;
    const QFileInfo fi(doc_.path);
    const QString def = fi.absolutePath() + "/" + fi.completeBaseName() + "_profile." + ext[kind];
    const QString fn = QFileDialog::getSaveFileName(this, tr("Export profile"), def, tr(filter[kind]));
    if (fn.isEmpty()) return;

    // Geometry in world mm (Y down), then CAD convention (Y up) with the origin.
    std::vector<cp::Pt> pts;
    std::vector<double> bulge;
    const bool useArcs = arcs && !doc_.segs.empty() && kind != 2;
    if (useArcs) {
        for (const auto& s : doc_.segs) {
            pts.push_back(s.a);
            bulge.push_back(-s.bulge());  // the Y flip reverses the turning direction
        }
        if (!doc_.closed) pts.push_back(doc_.segs.back().b);
    } else if (!doc_.segs.empty() && kind == 2 && arcs) {
        pts = cp::sampleSegments(doc_.segs, 0.05);
    } else {
        pts = doc_.profile;
    }
    double ox = 0, oy = 0;
    if (origin == 0) {
        const auto ext2 = doc_.segs.empty() ? pts : cp::sampleSegments(doc_.segs, 0.02);
        double minx = 1e300, maxy = -1e300;
        for (const auto& p : ext2) {
            minx = std::min(minx, p.x);
            maxy = std::max(maxy, p.y);
        }
        ox = minx;
        oy = maxy;
    }
    auto tf = [&](cp::Pt p) { return cp::Pt(p.x - ox, oy - p.y); };
    std::vector<cp::ExportPolyline> polys;
    cp::ExportPolyline prof;
    for (const auto& p : pts) prof.pts.push_back(tf(p));
    prof.bulge = bulge;
    prof.closed = doc_.closed;
    prof.layer = "PROFILE";
    prof.color = 1;
    polys.push_back(prof);
    if (grid && doc_.detectRoi.valid()) {
        const double s = doc_.squareMM;
        const cp::RectD& r = doc_.detectRoi;
        const int i0 = int(std::ceil(r.x0 / s)), i1 = int(std::floor(r.x1 / s));
        const int j0 = int(std::ceil(r.y0 / s)), j1 = int(std::floor(r.y1 / s));
        for (int i = i0; i <= i1 && i1 - i0 < 500; ++i) {
            cp::ExportPolyline l;
            l.pts = {tf(cp::Pt(i * s, j0 * s)), tf(cp::Pt(i * s, j1 * s))};
            l.layer = "GRID";
            l.color = 8;
            polys.push_back(l);
        }
        for (int j = j0; j <= j1 && j1 - j0 < 500; ++j) {
            cp::ExportPolyline l;
            l.pts = {tf(cp::Pt(i0 * s, j * s)), tf(cp::Pt(i1 * s, j * s))};
            l.layer = "GRID";
            l.color = 8;
            polys.push_back(l);
        }
    }
    const std::string content = kind == 0 ? cp::makeDXF(polys) : kind == 1 ? cp::makeSVG(polys) : cp::makeCSV(polys);
    QFile f(fn);
    if (!f.open(QIODevice::WriteOnly) || f.write(content.data(), qint64(content.size())) != qint64(content.size())) {
        QMessageBox::warning(this, tr("Export"), tr("Cannot write %1").arg(fn));
        return;
    }
    status(tr("Saved %1").arg(QFileInfo(fn).fileName()));
}

void MainWindow::saveRectified() {
    if (doc_.rectified.isNull()) {
        QMessageBox::information(this, tr("Save corrected image"), tr("No corrected view yet — calibrate first."));
        return;
    }
    const QFileInfo fi(doc_.path);
    const QString fn = QFileDialog::getSaveFileName(this, tr("Save corrected image"),
                                                    fi.absolutePath() + "/" + fi.completeBaseName() + "_corrected.png",
                                                    tr("PNG image (*.png)"));
    if (fn.isEmpty()) return;
    QImage img = doc_.rectified;
    img.setDotsPerMeterX(int(std::lround(doc_.rectRes * 1000)));
    img.setDotsPerMeterY(int(std::lround(doc_.rectRes * 1000)));
    if (!img.save(fn)) QMessageBox::warning(this, tr("Save corrected image"), tr("Cannot write %1").arg(fn));
}

// ---------------------------------------------------------------------------
// State -> widgets
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
        put(1, QString::number(r.world.x, 'f', 2), true);
        put(2, QString::number(r.world.y, 'f', 2), true);
        put(3, doc_.cal.ok ? QString::number(r.errMM, 'f', 3) : QString("-"), false);
    }
    updatingTable_ = false;
}

void MainWindow::onRefTableEdited(int row, int col) {
    if (updatingTable_ || row < 0 || row >= int(doc_.refs.size()) || (col != 1 && col != 2)) return;
    bool ok = false;
    QString txt = refTable_->item(row, col)->text().trimmed();
    txt.replace(',', '.');
    const double v = txt.toDouble(&ok);
    if (!ok) {
        updateRefTable();
        return;
    }
    pushUndo();
    RefPoint& r = doc_.refs[size_t(row)];
    (col == 1 ? r.world.x : r.world.y) = v;
    r.fromGrid = false;
    recalibrate(true);
}

void MainWindow::updateSegList() {
    if (!segList_) return;
    const QSignalBlocker block(segList_);
    segList_->clear();
    for (size_t i = 0; i < doc_.segs.size(); ++i) {
        const auto& s = doc_.segs[i];
        const QString dims = s.arc ? QString("R %1 · %2°").arg(s.r, 0, 'f', 2).arg(std::fabs(s.sweep) * 57.29577951308232, 0, 'f', 0)
                                   : QString("%1 mm").arg(s.length(), 0, 'f', 2);
        auto* it = new QListWidgetItem(QString("%1\t%2").arg(s.arc ? tr("Arc") : tr("Line"), dims));
        QPixmap sw(22, 10);
        sw.fill(Qt::transparent);
        {
            QPainter p(&sw);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(s.arc ? theme::accent : theme::line);
            p.drawRoundedRect(QRectF(0, 3, 22, 4), 2, 2);
        }
        it->setIcon(QIcon(sw));
        it->setToolTip(tr("Largest deviation %1 mm").arg(s.maxErr, 0, 'f', 3));
        segList_->addItem(it);
    }
    segList_->setIconSize(QSize(22, 10));
    if (doc_.selectedSeg >= 0 && doc_.selectedSeg < segList_->count()) segList_->setCurrentRow(doc_.selectedSeg);
}

void MainWindow::updatePanel() {
    if (!calStatus_) return;
    if (doc_.cal.ok)
        calStatus_->setText(QString("<span style='color:#46C38A'>&#10003;</span> %1 points · ±%2 mm")
                                .arg(doc_.refs.size())
                                .arg(doc_.cal.rmsMM, 0, 'f', 2));
    else
        calStatus_->setText(QString("<span style='color:#F2B544'>&#9888;</span> %1").arg(tr("not calibrated")));
    sensValue_->setText(sensSlider_->value() == 50 ? tr("Auto") : QString::number(threshold(), 'f', 0));
    smoothValue_->setText(QString("%1 mm").arg(smoothSlider_->value() / 100.0, 0, 'f', 2));
    colorBtn_->setStyleSheet(QString("background: %1; border: 1px solid #3A404A; border-radius: 8px; min-height: 28px;")
                                 .arg(pickedColor_.name()));
    const QString tol = QString("%1 mm").arg(doc_.fitTol, 0, 'f', doc_.fitTol < 0.1 ? 3 : 2);
    tolValue_->setText(tol);
    tolValue2_->setText(tol);
    int na = 0, nl = 0;
    for (const auto& s : doc_.segs) (s.arc ? na : nl)++;
    fitSummaryBtn_->setText(doc_.segs.empty() ? tr("—") : geometrySummary(na, nl, " · ") + tr(" — edit"));
    fitSummaryBtn_->setEnabled(!doc_.segs.empty());
}

void MainWindow::updateResultCard() {
    if (!resultCard_) return;
    const bool has = doc_.profile.size() >= 2;
    QString title, summary;
    QStringList checks;
    if (has) {
        int na = 0, nl = 0;
        double len = 0;
        for (const auto& s : doc_.segs) {
            (s.arc ? na : nl)++;
            len += s.length();
        }
        if (doc_.segs.empty()) len = cp::polylineLength(doc_.profile, doc_.closed);
        title = tr("Profile ready");
        summary = doc_.segs.empty() ? tr("%1 mm long").arg(len, 0, 'f', 1)
                                    : tr("%1 mm long · %2").arg(len, 0, 'f', 1).arg(geometrySummary(na, nl, ", "));
    } else {
        title = doc_.cal.ok ? tr("No wire found") : tr("Not calibrated");
        summary = doc_.cal.ok ? tr("Adjust › click the wire, or pick its colour") : tr("Click 4 corners of a known rectangle");
    }
    if (doc_.cal.ok) {
        checks << (gridDetected_ ? tr("Grid found · %1 mm").arg(doc_.squareMM, 0, 'g', 4)
                                 : tr("Calibrated from %1 points").arg(doc_.refs.size()));
        checks << tr("Perspective corrected");
        checks << (doc_.cal.rmsMM < 0.2 ? tr("Fit ±%1 mm").arg(std::max(0.01, doc_.cal.rmsMM), 0, 'f', 2)
                                        : "!" + tr("Calibration fit ±%1 mm").arg(doc_.cal.rmsMM, 0, 'f', 2));
    } else {
        checks << "!" + tr("Grid not found");
    }
    resultCard_->setResult(title, summary, checks, has);
    layoutOverlays();
}

void MainWindow::updateAll() {
    const bool hasImg = doc_.hasImage();
    const bool hasProfile = doc_.profile.size() >= 2;
    fileChip_->setVisible(hasImg);
    fileChip_->setText(QFileInfo(doc_.path).fileName());
    helpBtn_->setVisible(!hasImg);
    openBtn_->setVisible(hasImg);
    exportBtn_->setVisible(hasImg);
    exportBtn_->setEnabled(hasProfile);
    moreBtn_->setVisible(hasImg);
    if (!hasImg) pages_->setCurrentIndex(0);
    if (undoBtn_) {
        undoBtn_->setEnabled(!undo_.empty());
        redoBtn_->setEnabled(!redo_.empty());
    }
    resultCard_->setVisible(hasImg && !details_ && !calibrating_);
    calibCard_->setVisible(hasImg && calibrating_);
    viewSeg_->setVisible(hasImg && !calibrating_ && doc_.cal.ok);
    zoomBox_->setVisible(hasImg);
    QString hint;
    if (details_) {
        switch (canvas_->tool()) {
            case Canvas::Tool::Select: hint = tr("Drag a point · double-click the line to add one · right-click to delete"); break;
            case Canvas::Tool::AddRef: hint = tr("Click a known grid corner to add a reference point"); break;
            case Canvas::Tool::DrawProfile: hint = tr("Click to place points — they become lines and arcs · Enter to finish"); break;
            case Canvas::Tool::PickObject: hint = tr("Click on the wire"); break;
            case Canvas::Tool::PickColor: hint = tr("Click on the wire to sample its colour"); break;
        }
        if (!lastMessage_.isEmpty()) hint = lastMessage_;
    }
    hint_->setText(hint);
    hint_->setVisible(!hint.isEmpty() && hasImg);
    updatePanel();
    updateResultCard();
}

// ---------------------------------------------------------------------------
// Undo / redo
// ---------------------------------------------------------------------------
void MainWindow::pushUndo() {
    undo_.push_back(snapshot());
    if (undo_.size() > 200) undo_.erase(undo_.begin());
    redo_.clear();
    if (undoBtn_) {
        undoBtn_->setEnabled(true);
        redoBtn_->setEnabled(false);
    }
}

void MainWindow::restore(const Snapshot& s) {
    doc_.refs = s.refs;
    doc_.profile = s.profile;
    doc_.closed = s.closed;
    recalibrate(true);
    refit();
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
    if (e->mimeData()->hasUrls()) {
        e->acceptProposedAction();
        start_->setDropHover(true);
    }
}

void MainWindow::dragLeaveEvent(QDragLeaveEvent*) { start_->setDropHover(false); }

void MainWindow::dropEvent(QDropEvent* e) {
    start_->setDropHover(false);
    const auto urls = e->mimeData()->urls();
    if (!urls.isEmpty() && urls.front().isLocalFile()) loadImage(urls.front().toLocalFile());
}
