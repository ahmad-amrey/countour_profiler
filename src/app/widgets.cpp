#include "widgets.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

using theme::Icon;

QPushButton* makeButton(const QString& text, const char* variant, Icon ic, bool withIcon, const QColor& iconColor) {
    auto* b = new QPushButton(text);
    if (variant) b->setProperty("variant", variant);
    if (withIcon) {
        b->setIcon(theme::icon(ic, iconColor, 16));
        b->setIconSize(QSize(16, 16));
    }
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

QLabel* makeLabel(const QString& text, const char* role) {
    auto* l = new QLabel(text);
    if (role) l->setProperty("role", role);
    return l;
}

QFrame* makeDivider() {
    auto* f = new QFrame;
    f->setObjectName("Divider");
    return f;
}

// --- Segmented -----------------------------------------------------------------
Segmented::Segmented(const QStringList& items, bool floating, QWidget* parent) : QFrame(parent) {
    setObjectName("Segmented");
    setProperty("floating", floating);
    auto* l = new QHBoxLayout(this);
    l->setContentsMargins(4, 4, 4, 4);
    l->setSpacing(2);
    group_ = new QButtonGroup(this);
    group_->setExclusive(true);
    for (int i = 0; i < items.size(); ++i) {
        auto* b = new QPushButton(items[i]);
        b->setProperty("variant", "seg");
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        group_->addButton(b, i);
        l->addWidget(b, floating ? 0 : 1);
    }
    if (!items.isEmpty()) group_->button(0)->setChecked(true);
    connect(group_, &QButtonGroup::idClicked, this, &Segmented::changed);
}

int Segmented::index() const { return group_->checkedId(); }
void Segmented::setIndex(int i) {
    if (auto* b = group_->button(i)) b->setChecked(true);
}

// --- ProfilePreview ------------------------------------------------------------
ProfilePreview::ProfilePreview(QWidget* parent) : QWidget(parent) { setMinimumSize(120, 140); }

void ProfilePreview::setProfile(const std::vector<cp::Pt>& pts, const std::vector<cp::Segment>& segs, bool closed) {
    pts_ = pts;
    segs_ = segs;
    closed_ = closed;
    update();
}

void ProfilePreview::paintEvent(QPaintEvent*) {
    std::vector<cp::Pt> draw = segs_.empty() ? pts_ : cp::sampleSegments(segs_, 0.05);
    if (draw.size() < 2) return;
    double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
    for (const auto& p : draw) {
        x0 = std::min(x0, p.x); y0 = std::min(y0, p.y);
        x1 = std::max(x1, p.x); y1 = std::max(y1, p.y);
    }
    const QRectF r = QRectF(rect()).adjusted(10, 10, -10, -10);
    const double s = std::min(r.width() / std::max(1e-6, x1 - x0), r.height() / std::max(1e-6, y1 - y0));
    const double ox = r.center().x() - s * (x0 + x1) / 2, oy = r.center().y() - s * (y0 + y1) / 2;
    auto map = [&](const cp::Pt& p) { return QPointF(ox + s * p.x, oy + s * p.y); };
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPolygonF poly;
    for (const auto& q : draw) poly << map(q);
    p.setPen(QPen(theme::accent, 3.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (closed_) p.drawPolygon(poly); else p.drawPolyline(poly);
    p.setPen(QPen(theme::text, 1.6));
    p.setBrush(QColor("#121418"));
    for (size_t i = 0; i + 1 < segs_.size(); ++i) p.drawEllipse(map(segs_[i].b), 3.5, 3.5);
}

// --- StartPage -----------------------------------------------------------------
StartPage::StartPage(QWidget* parent) : QWidget(parent) {
    setObjectName("Start");
    setAttribute(Qt::WA_StyledBackground);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(40, 40, 40, 40);
    outer->setSpacing(32);
    outer->addStretch(1);

    drop_ = new QFrame;
    drop_->setObjectName("DropZone");
    drop_->setFixedSize(640, 340);
    auto* dl = new QVBoxLayout(drop_);
    dl->setContentsMargins(40, 24, 40, 24);
    dl->setSpacing(16);
    dl->addStretch(1);
    auto* tile = new QFrame;
    tile->setObjectName("IconTile");
    tile->setFixedSize(72, 72);
    auto* tl = new QVBoxLayout(tile);
    auto* camera = new QLabel;
    camera->setPixmap(theme::pixmap(Icon::Camera, theme::accent, 34, devicePixelRatioF()));
    camera->setAlignment(Qt::AlignCenter);
    tl->addWidget(camera);
    dl->addWidget(tile, 0, Qt::AlignHCenter);
    auto* h1 = makeLabel(tr("Drop a photo of your wire"), "h1");
    h1->setAlignment(Qt::AlignCenter);
    dl->addWidget(h1);
    auto* sub = makeLabel(tr("Lay the bent wire on a grid — cutting mat, graph paper or checkerboard — and shoot from "
                             "any angle. Everything else is automatic."),
                          "muted");
    sub->setWordWrap(true);
    sub->setAlignment(Qt::AlignCenter);
    sub->setMinimumWidth(430);
    sub->setMaximumWidth(460);
    sub->setMinimumHeight(76);
    sub->setStyleSheet("font-size: 15px;");
    dl->addWidget(sub, 0, Qt::AlignHCenter);
    auto* row = new QHBoxLayout;
    row->setSpacing(14);
    row->addStretch(1);
    auto* choose = makeButton(tr("Choose photo…"), "primary");
    choose->setProperty("variant", "primary");
    choose->setMinimumHeight(44);
    choose->setStyleSheet("font-size: 15px; padding: 0 22px; border-radius: 11px;");
    auto* sample = makeButton(tr("Try a sample"), "link");
    row->addWidget(choose);
    row->addWidget(sample);
    row->addStretch(1);
    dl->addSpacing(4);
    dl->addLayout(row);
    dl->addStretch(1);
    outer->addWidget(drop_, 0, Qt::AlignHCenter);
    connect(choose, &QPushButton::clicked, this, &StartPage::openRequested);
    connect(sample, &QPushButton::clicked, this, &StartPage::sampleRequested);

    auto* chip = new QFrame;
    chip->setObjectName("Chip");
    chip->setFixedHeight(40);
    auto* cl = new QHBoxLayout(chip);
    cl->setContentsMargins(16, 0, 6, 0);
    cl->setSpacing(10);
    auto* gridIcon = new QLabel;
    gridIcon->setPixmap(theme::pixmap(Icon::Grid, theme::muted, 16, devicePixelRatioF()));
    cl->addWidget(gridIcon);
    cl->addWidget(makeLabel(tr("Grid squares"), "muted"));
    squareValue_ = makeLabel("10 mm");
    squareValue_->setStyleSheet("font-weight: 600;");
    cl->addWidget(squareValue_);
    auto* change = makeButton(tr("Change"));
    change->setStyleSheet("min-height: 30px; border-radius: 15px; border: none; background: #252931; font-size: 12px;");
    cl->addWidget(change);
    auto* chipRow = new QHBoxLayout;
    chipRow->setSpacing(16);
    chipRow->addStretch(1);
    chipRow->addWidget(chip);
    auto* printLink = makeButton(tr("Print a calibration sheet"), "link");
    printLink->setToolTip(tr("No cutting mat? Print a grid this program reads automatically."));
    chipRow->addWidget(printLink);
    chipRow->addStretch(1);
    outer->addLayout(chipRow);
    connect(printLink, &QPushButton::clicked, this, &StartPage::printSheetRequested);
    connect(change, &QPushButton::clicked, this, &StartPage::squareChangeRequested);

    auto* steps = new QWidget;
    steps->setFixedWidth(780);
    auto* sl = new QHBoxLayout(steps);
    sl->setContentsMargins(0, 0, 0, 0);
    sl->setSpacing(20);
    const QString texts[3] = {tr("<b>Bend</b> a wire over the shape you want to copy"),
                              tr("<b>Photograph</b> it on the grid — any angle"),
                              tr("<b>Export</b> a DXF with clean lines and arcs")};
    for (int i = 0; i < 3; ++i) {
        auto* w = new QWidget;
        auto* l = new QHBoxLayout(w);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(12);
        auto* n = makeLabel(QString::number(i + 1));
        n->setFixedSize(26, 26);
        n->setAlignment(Qt::AlignCenter);
        n->setStyleSheet("background: #252931; border-radius: 13px; font-weight: 600;");
        l->addWidget(n, 0, Qt::AlignTop);
        auto* t = makeLabel(texts[i], "muted");
        t->setWordWrap(true);
        t->setTextFormat(Qt::RichText);
        t->setMinimumHeight(40);
        t->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        l->addWidget(t, 1);
        sl->addWidget(w, 1);
    }
    outer->addWidget(steps, 0, Qt::AlignHCenter);
    outer->addStretch(1);
}

void StartPage::setSquare(double mm) { squareValue_->setText(tr("%1 mm").arg(mm, 0, 'g', 4)); }

void StartPage::setDropHover(bool on) {
    drop_->setProperty("hover", on);
    drop_->style()->unpolish(drop_);
    drop_->style()->polish(drop_);
}

// --- ResultCard ----------------------------------------------------------------
ResultCard::ResultCard(QWidget* parent) : QFrame(parent) {
    setObjectName("Card");
    setAttribute(Qt::WA_StyledBackground);
    auto* l = new QHBoxLayout(this);
    l->setContentsMargins(22, 16, 16, 16);
    l->setSpacing(20);
    auto* left = new QVBoxLayout;
    left->setSpacing(8);
    auto* top = new QHBoxLayout;
    top->setSpacing(12);
    title_ = makeLabel(tr("Profile ready"), "big");
    summary_ = makeLabel(QString(), "muted");
    summary_->setStyleSheet("font-size: 14px;");
    summary_->setMaximumWidth(380);
    top->addWidget(title_);
    top->addWidget(summary_, 0, Qt::AlignBottom);
    top->addStretch(1);
    left->addLayout(top);
    checks_ = new QWidget;
    auto* cl = new QHBoxLayout(checks_);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(18);
    left->addWidget(checks_);
    l->addLayout(left, 1);
    auto* adjust = makeButton(tr("Adjust"), "large", Icon::Sliders, true);
    export_ = makeButton(tr("Export DXF"), "primary", Icon::Download, true, theme::onAccent);
    export_->setMinimumHeight(44);
    export_->setStyleSheet("font-size: 14px; border-radius: 11px; padding: 0 20px;");
    l->addWidget(adjust);
    l->addWidget(export_);
    connect(adjust, &QPushButton::clicked, this, &ResultCard::adjustRequested);
    connect(export_, &QPushButton::clicked, this, &ResultCard::exportRequested);
}

void ResultCard::setResult(const QString& title, const QString& summary, const QStringList& checks, bool canExport) {
    title_->setText(title);
    summary_->setText(summary);
    auto* cl = static_cast<QHBoxLayout*>(checks_->layout());
    while (QLayoutItem* it = cl->takeAt(0)) {
        delete it->widget();
        delete it;
    }
    for (const QString& c : checks) {
        const bool good = !c.startsWith('!');
        auto* w = new QWidget;
        auto* l = new QHBoxLayout(w);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(6);
        auto* ic = new QLabel;
        ic->setPixmap(theme::pixmap(good ? Icon::Check : Icon::Warning, good ? theme::ok : theme::warn, 16,
                                    devicePixelRatioF()));
        l->addWidget(ic);
        l->addWidget(makeLabel(good ? c : c.mid(1), "muted"));
        cl->addWidget(w);
    }
    cl->addStretch(1);
    export_->setEnabled(canExport);
}

// --- CalibrateCard ---------------------------------------------------------------
CalibrateCard::CalibrateCard(QWidget* parent) : QFrame(parent) {
    setObjectName("Card");
    setAttribute(Qt::WA_StyledBackground);
    setFixedWidth(370);
    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(20, 20, 20, 20);
    l->setSpacing(14);
    auto* head = new QHBoxLayout;
    head->setSpacing(10);
    auto* ic = new QLabel;
    ic->setFixedSize(32, 32);
    ic->setAlignment(Qt::AlignCenter);
    ic->setStyleSheet("background: rgba(242,181,68,38); border-radius: 10px;");
    ic->setPixmap(theme::pixmap(Icon::Warning, theme::warn, 18, devicePixelRatioF()));
    head->addWidget(ic);
    head->addWidget(makeLabel(tr("Grid not found automatically"), "title"), 1);
    l->addLayout(head);
    auto* info = makeLabel(tr("Click the 4 corners of one grid square — or of any rectangle you know the size of — "
                              "in order around it."),
                           "muted");
    info->setWordWrap(true);
    l->addWidget(info);
    auto* bars = new QHBoxLayout;
    bars->setSpacing(6);
    for (auto*& b : bars_) {
        b = new QFrame;
        b->setFixedHeight(5);
        bars->addWidget(b, 1);
    }
    l->addLayout(bars);
    status_ = makeLabel(QString(), "small");
    l->addWidget(status_);
    auto* size = new QHBoxLayout;
    size->setSpacing(8);
    size->addWidget(makeLabel(tr("Rectangle size"), "muted"), 1);
    w_ = new QDoubleSpinBox;
    h_ = new QDoubleSpinBox;
    for (auto* s : {w_, h_}) {
        s->setRange(0.1, 10000);
        s->setDecimals(2);
        s->setValue(10);
        s->setFixedWidth(84);
        s->setSuffix(" mm");
        s->setButtonSymbols(QAbstractSpinBox::NoButtons);
    }
    size->addWidget(w_);
    size->addWidget(makeLabel("×", "muted"));
    size->addWidget(h_);
    l->addLayout(size);
    auto* btns = new QHBoxLayout;
    btns->setSpacing(8);
    auto* undo = makeButton(tr("Undo point"));
    auto* retry = makeButton(tr("Try detection again"));
    btns->addWidget(undo, 1);
    btns->addWidget(retry, 1);
    l->addLayout(btns);
    auto* tip = makeLabel(tr("Tip: more points make it more accurate — keep clicking grid corners after the first four, "
                             "then fix their mm values under Adjust."),
                          "faint");
    tip->setWordWrap(true);
    l->addWidget(tip);
    connect(undo, &QPushButton::clicked, this, &CalibrateCard::undoRequested);
    connect(retry, &QPushButton::clicked, this, &CalibrateCard::retryRequested);
    setCount(0);
}

void CalibrateCard::setCount(int n) {
    for (int i = 0; i < 4; ++i)
        bars_[i]->setStyleSheet(QString("background: %1; border-radius: 2px;").arg(i < n ? "#FF8A3D" : "#2E333C"));
    status_->setText(n < 4 ? tr("%1 of 4 corners placed").arg(n) : tr("%1 points placed — calibrated").arg(n));
}

double CalibrateCard::rectW() const { return w_->value(); }
double CalibrateCard::rectH() const { return h_->value(); }
void CalibrateCard::setSquare(double mm) {
    w_->setValue(mm);
    h_->setValue(mm);
}

// --- ExportDialog ----------------------------------------------------------------
ExportDialog::ExportDialog(QWidget* parent, const std::vector<cp::Pt>& pts, const std::vector<cp::Segment>& segs,
                           bool closed, const QString& sizeText, const QString& lengthText,
                           const QString& geometryText, int format)
    : QDialog(parent) {
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(true);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* card = new QFrame;
    card->setObjectName("Card");
    card->setStyleSheet("QFrame#Card { background: #1A1D22; border-radius: 20px; }");
    card->setFixedWidth(480);
    outer->addWidget(card);
    auto* l = new QVBoxLayout(card);
    l->setContentsMargins(24, 18, 24, 24);
    l->setSpacing(16);

    auto* head = new QHBoxLayout;
    head->addWidget(makeLabel(tr("Export profile"), "h2"), 1);
    auto* close = makeButton(QString(), "icon", Icon::Close, true, theme::muted);
    close->setToolTip(tr("Close"));
    head->addWidget(close);
    l->addLayout(head);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);

    auto* prev = new QFrame;
    prev->setObjectName("Preview");
    prev->setFixedHeight(188);
    auto* pl = new QHBoxLayout(prev);
    pl->setContentsMargins(20, 12, 24, 12);
    pl->setSpacing(24);
    auto* pp = new ProfilePreview;
    pp->setProfile(pts, segs, closed);
    pl->addWidget(pp, 1);
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(6);
    const QString keys[4] = {tr("Size"), tr("Length"), tr("Geometry"), tr("Units")};
    const QString vals[4] = {sizeText, lengthText, geometryText, tr("millimetres")};
    for (int i = 0; i < 4; ++i) {
        grid->addWidget(makeLabel(keys[i], "muted"), i, 0);
        auto* v = makeLabel(vals[i]);
        v->setStyleSheet("font-weight: 600;");
        grid->addWidget(v, i, 1);
    }
    pl->addLayout(grid);
    l->addWidget(prev);

    auto row = [&](const QString& name, QWidget* w) {
        auto* h = new QHBoxLayout;
        h->setSpacing(16);
        auto* lab = makeLabel(name, "muted");
        lab->setFixedWidth(88);
        h->addWidget(lab);
        h->addWidget(w, 1);
        l->addLayout(h);
    };
    format_ = new Segmented({"DXF", "SVG", "CSV"});
    format_->setIndex(format);
    row(tr("Format"), format_);
    geometry_ = new Segmented({tr("Lines && arcs"), tr("Raw points")});
    geometry_->setIndex(segs.empty() ? 1 : 0);
    row(tr("Geometry"), geometry_);

    auto* more = makeButton(tr("More options — origin, reference grid layer"), "flat", Icon::ChevronRight, true,
                            theme::muted);
    more->setCheckable(true);
    l->addWidget(more, 0, Qt::AlignLeft);
    auto* moreBox = new QWidget;
    auto* ml = new QVBoxLayout(moreBox);
    ml->setContentsMargins(0, 0, 0, 0);
    ml->setSpacing(10);
    origin_ = new QComboBox;
    origin_->addItems({tr("Profile corner (bottom-left = 0, 0)"), tr("Grid origin")});
    auto* oh = new QHBoxLayout;
    auto* olab = makeLabel(tr("Origin"), "muted");
    olab->setFixedWidth(88);
    oh->addWidget(olab);
    oh->addWidget(origin_, 1);
    ml->addLayout(oh);
    grid_ = new QCheckBox(tr("Include the reference grid as its own layer"));
    ml->addWidget(grid_);
    moreBox->setVisible(false);
    l->addWidget(moreBox);
    connect(more, &QPushButton::toggled, this, [this, more, moreBox](bool on) {
        moreBox->setVisible(on);
        more->setIcon(theme::icon(on ? Icon::ChevronLeft : Icon::ChevronRight, theme::muted, 16));
        adjustSize();
    });

    auto* foot = new QHBoxLayout;
    foot->addStretch(1);
    auto* cancel = makeButton(tr("Cancel"), "large");
    cancel->setProperty("variant", "ghost");
    cancel->setMinimumHeight(44);
    save_ = makeButton(QString(), "primary");
    save_->setMinimumHeight(44);
    save_->setDefault(true);
    foot->addWidget(cancel);
    foot->addWidget(save_);
    l->addLayout(foot);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(save_, &QPushButton::clicked, this, &QDialog::accept);
    auto updateSave = [this] {
        static const char* names[3] = {"DXF", "SVG", "CSV"};
        save_->setText(tr("Save %1…").arg(names[std::clamp(format_->index(), 0, 2)]));
        geometry_->setEnabled(format_->index() != 2);
    };
    connect(format_, &Segmented::changed, this, updateSave);
    updateSave();
}

int ExportDialog::format() const { return format_->index(); }
bool ExportDialog::linesAndArcs() const { return geometry_->index() == 0 && format_->index() != 2; }
int ExportDialog::origin() const { return origin_->currentIndex(); }
bool ExportDialog::gridLayer() const { return grid_->isChecked(); }

// --- PrintSheetDialog --------------------------------------------------------------
PrintSheetDialog::PrintSheetDialog(QWidget* parent, double squareMM) : QDialog(parent) {
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(true);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* card = new QFrame;
    card->setObjectName("Card");
    card->setStyleSheet("QFrame#Card { background: #1A1D22; border-radius: 20px; }");
    outer->addWidget(card);
    auto* h = new QHBoxLayout(card);
    h->setContentsMargins(24, 20, 24, 24);
    h->setSpacing(24);

    preview_ = new QLabel;
    preview_->setFixedSize(300, 424);
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setStyleSheet("background: #121418; border: 1px solid #262A31; border-radius: 14px;");
    h->addWidget(preview_);

    auto* l = new QVBoxLayout;
    l->setSpacing(14);
    auto* head = new QHBoxLayout;
    head->addWidget(makeLabel(tr("Print a calibration sheet"), "h2"), 1);
    auto* close = makeButton(QString(), "icon", Icon::Close, true, theme::muted);
    head->addWidget(close);
    l->addLayout(head);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    auto* intro = makeLabel(tr("A grid this program reads automatically. Print it at 100 % (actual size) and lay "
                               "the wire on it."),
                            "muted");
    intro->setWordWrap(true);
    intro->setFixedWidth(330);
    l->addWidget(intro);

    auto row = [&](const QString& name, QWidget* w) {
        auto* r = new QHBoxLayout;
        r->setSpacing(14);
        auto* lab = makeLabel(name, "muted");
        lab->setFixedWidth(92);
        r->addWidget(lab);
        r->addWidget(w, 1);
        l->addLayout(r);
    };
    pattern_ = new Segmented({tr("Line grid"), tr("Checkerboard")});
    row(tr("Pattern"), pattern_);
    background_ = new Segmented({tr("White"), tr("Dark")});
    background_->setToolTip(tr("Dark: light lines on black, like a cutting mat. Best for silver wire (uses more ink)."));
    row(tr("Paper"), background_);
    square_ = new QDoubleSpinBox;
    square_->setRange(2, 50);
    square_->setDecimals(1);
    square_->setValue(squareMM);
    square_->setSuffix(" mm");
    square_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    row(tr("Square size"), square_);
    paper_ = new QComboBox;
    paper_->addItem("A4", int(QPageSize::A4));
    paper_->addItem("US Letter", int(QPageSize::Letter));
    paper_->addItem("A3", int(QPageSize::A3));
    row(tr("Sheet"), paper_);
    tip_ = makeLabel(QString(), "faint");
    tip_->setWordWrap(true);
    tip_->setFixedWidth(330);
    l->addWidget(tip_);
    l->addStretch(1);
    auto* foot = new QHBoxLayout;
    foot->addStretch(1);
    auto* cancel = makeButton(tr("Cancel"), "ghost");
    cancel->setMinimumHeight(44);
    auto* save = makeButton(tr("Save PDF…"), "primary", Icon::Download, true, theme::onAccent);
    save->setMinimumHeight(44);
    save->setDefault(true);
    foot->addWidget(cancel);
    foot->addWidget(save);
    l->addLayout(foot);
    h->addLayout(l);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(save, &QPushButton::clicked, this, &QDialog::accept);
    connect(pattern_, &Segmented::changed, this, [this] { refresh(); });
    connect(background_, &Segmented::changed, this, [this] { refresh(); });
    connect(square_, &QDoubleSpinBox::valueChanged, this, [this] { refresh(); });
    connect(paper_, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
    refresh();
}

SheetOptions PrintSheetDialog::options() const {
    SheetOptions o;
    o.checkerboard = pattern_->index() == 1;
    o.dark = !o.checkerboard && background_->index() == 1;
    o.squareMM = square_->value();
    o.page = QPageSize::PageSizeId(paper_->currentData().toInt());
    return o;
}

void PrintSheetDialog::refresh() {
    const SheetOptions o = options();
    background_->setEnabled(!o.checkerboard);
    const QSizeF mm = sheetSizeMM(o);
    const double k = std::min((preview_->width() - 24) / mm.width(), (preview_->height() - 24) / mm.height());
    const qreal dpr = devicePixelRatioF();
    QImage img = renderSheet(o, k * dpr);
    img.setDevicePixelRatio(dpr);
    preview_->setPixmap(QPixmap::fromImage(img));
    tip_->setText(o.checkerboard ? tr("Checkerboard: use a copper or coloured wire — silver disappears on the white "
                                      "squares.")
                  : o.dark       ? tr("Dark paper gives silver and shiny wire the best contrast.")
                                 : tr("White paper: use a dark or coloured wire. For silver wire choose Dark."));
}
