#include "MainWindow.h"
#include "SignatureDialog.h"
#include "SignaturePickerDialog.h"
#include "PdfToolDialogs.h"
#include "Theme.h"
#include <QApplication>
#include <QMenuBar>
#include <QStatusBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QColorDialog>
#include <QInputDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QSettings>
#include <QScrollBar>
#include <QActionGroup>
#include <QStyle>
#include <QToolButton>
#include <QWidgetAction>
#include <QCloseEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QPainter>
#include <QPainterPath>
#include <QFrame>
#include <QStackedWidget>
#include <QListWidget>
#include <QFileInfo>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QTextBrowser>
#include <QComboBox>
#include <QSpinBox>
#include <QLabel>
#include <QStandardPaths>
#include <QDir>
#include <QCoreApplication>
#include <QDebug>
#include <QKeyEvent>
#include <QLineEdit>
#include <QTextEdit>
#include <QProcess>
#include <QIntValidator>
#include <QPrinter>
#include <QPrintDialog>

extern "C" {
#include <mupdf/fitz.h>
}

static constexpr float ZOOM_STEP = 0.15f;
static constexpr float ZOOM_MIN  = 0.25f;
static constexpr float ZOOM_MAX  = 5.0f;

// ─────────────────────────────────────────────────────────────
// Small helper: draw a PDF-icon using QPainter (no image file needed)
// ─────────────────────────────────────────────────────────────
static void drawPdfIcon(QPainter &p, QRect r) {
    // White page with folded corner
    int fold = r.width() / 5;
    QPainterPath page;
    page.moveTo(r.left(), r.top());
    page.lineTo(r.right() - fold, r.top());
    page.lineTo(r.right(), r.top() + fold);
    page.lineTo(r.right(), r.bottom());
    page.lineTo(r.left(), r.bottom());
    page.closeSubpath();

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 240));
    p.drawPath(page);

    // Fold triangle
    QPainterPath tri;
    tri.moveTo(r.right() - fold, r.top());
    tri.lineTo(r.right(), r.top() + fold);
    tri.lineTo(r.right() - fold, r.top() + fold);
    tri.closeSubpath();
    p.setBrush(QColor(200, 200, 200, 180));
    p.drawPath(tri);

    // Red/orange band at bottom with "PDF" text
    QRect band(r.left(), r.bottom() - r.height()/3, r.width(), r.height()/3);
    p.setBrush(QColor(0xE3, 0x35, 0x35));
    p.drawRect(band);

    p.setPen(Qt::white);
    QFont f = p.font();
    f.setPixelSize(band.height() / 2);
    f.setBold(true);
    p.setFont(f);
    p.drawText(band, Qt::AlignCenter, "PDF");
}

// ─────────────────────────────────────────────────────────────
// Recent files helpers
// ─────────────────────────────────────────────────────────────

QStringList MainWindow::recentFiles() const {
    QSettings s("PDFEditor", "PDFEditor");
    return s.value("recentFiles").toStringList();
}

void MainWindow::addRecentFile(const QString &path) {
    QSettings s("PDFEditor", "PDFEditor");
    QStringList list = s.value("recentFiles").toStringList();
    list.removeAll(path);
    list.prepend(path);
    while (list.size() > MAX_RECENT) list.removeLast();
    s.setValue("recentFiles", list);
    rebuildRecentList();
}

void MainWindow::rebuildRecentList() {
    if (!m_recentList) return;
    m_recentList->clear();
    const QStringList files = recentFiles();
    for (const QString &f : files) {
        if (!QFileInfo::exists(f)) continue;
        QFileInfo fi(f);
        auto *item = new QListWidgetItem(
            QString("  %1   \n  %2").arg(fi.fileName(), fi.absolutePath()));
        item->setData(Qt::UserRole, f);
        item->setSizeHint(QSize(0, 52));
        m_recentList->addItem(item);
    }
    if (m_recentList->count() == 0) {
        auto *item = new QListWidgetItem("  No recently opened files");
        item->setFlags(Qt::NoItemFlags);
        item->setForeground(QColor("#666666"));
        m_recentList->addItem(item);
    }
}

QWidget *MainWindow::buildWelcomeWidget()
{
    auto *w = new QWidget;
    w->setObjectName("welcomeWidget");

    auto *root = new QVBoxLayout(w);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Thin top bar with branding ──
    auto *header = new QWidget;
    header->setObjectName("welcomeHeader");
    header->setFixedHeight(52);
    {
        auto *hl = new QHBoxLayout(header);
        hl->setContentsMargins(28, 0, 28, 0);

        auto *logo = new QLabel;
        logo->setText(
            "<span style='font-size:18px; font-weight:700; letter-spacing:-0.5px;'>PDF</span>"
            "<span style='font-size:18px; font-weight:300; color:#CC3232;'> Editor</span>");
        hl->addWidget(logo);
        hl->addStretch();

        auto *ver = new QLabel("v1.0");
        ver->setObjectName("verLabel");
        hl->addWidget(ver);
    }
    root->addWidget(header);

    // thin rule below header
    auto *rule = new QFrame;
    rule->setObjectName("welcomeRule");
    rule->setFixedHeight(1);
    root->addWidget(rule);

    // ── Body: centered card ──
    auto *body = new QWidget;
    body->setObjectName("welcomeBody");
    auto *bodyVl = new QVBoxLayout(body);
    bodyVl->setContentsMargins(24, 0, 24, 32);
    bodyVl->setSpacing(0);

    bodyVl->addStretch(2);

    // Card
    auto *card = new QWidget;
    card->setObjectName("welcomeCard");
    card->setFixedWidth(520);
    auto *cardVl = new QVBoxLayout(card);
    cardVl->setContentsMargins(0, 0, 0, 0);
    cardVl->setSpacing(0);

    // ── Drop zone ──
    auto *drop = new QFrame;
    drop->setObjectName("dropZone");
    drop->setFixedHeight(190);
    drop->setCursor(Qt::PointingHandCursor);
    {
        auto *dz = new QVBoxLayout(drop);
        dz->setAlignment(Qt::AlignCenter);
        dz->setSpacing(14);
        dz->setContentsMargins(32, 32, 32, 32);

        auto *iconLbl = new QLabel("⬆", drop);
        iconLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
        iconLbl->setAlignment(Qt::AlignCenter);
        iconLbl->setObjectName("dropIcon");
        dz->addWidget(iconLbl);

        auto *hint = new QLabel(tr("Drop a PDF here"), drop);
        hint->setAttribute(Qt::WA_TransparentForMouseEvents);
        hint->setAlignment(Qt::AlignCenter);
        hint->setObjectName("dropHint");
        dz->addWidget(hint);

        auto *orLbl = new QLabel(tr("or"), drop);
        orLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
        orLbl->setAlignment(Qt::AlignCenter);
        orLbl->setObjectName("dropOr");
        dz->addWidget(orLbl);

        auto *openBtn = new QPushButton(tr("Choose File…"), drop);
        openBtn->setFixedSize(140, 34);
        openBtn->setObjectName("dropOpenBtn");
        connect(openBtn, &QPushButton::clicked, this, &MainWindow::openDialog);
        dz->addWidget(openBtn, 0, Qt::AlignCenter);

        auto *overlay = new QPushButton(drop);
        overlay->setFlat(true);
        overlay->setStyleSheet("background:transparent; border:none;");
        overlay->setGeometry(0, 0, 4000, 4000);
        overlay->lower();
        connect(overlay, &QPushButton::clicked, this, &MainWindow::openDialog);
    }
    cardVl->addWidget(drop);
    cardVl->addSpacing(24);

    // ── Recent files ──
    auto *recentLbl = new QLabel(tr("Recent Files"));
    recentLbl->setObjectName("recentLabel");
    cardVl->addWidget(recentLbl);
    cardVl->addSpacing(6);

    m_recentList = new QListWidget;
    m_recentList->setFrameShape(QFrame::NoFrame);
    m_recentList->setFixedHeight(168);
    m_recentList->setObjectName("recentList");
    rebuildRecentList();
    connect(m_recentList, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *item) {
        openFile(item->data(Qt::UserRole).toString());
    });
    cardVl->addWidget(m_recentList);

    bodyVl->addWidget(card, 0, Qt::AlignHCenter);
    bodyVl->addStretch(3);
    root->addWidget(body, 1);
    return w;
}

// ─────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    m_pdf = new PdfDocument;

    setWindowTitle("PDF Editor");
    setMinimumSize(900, 640);
    resize(1280, 820);
    setAcceptDrops(true);

    // ── Stacked central widget: 0 = welcome, 1 = editor ──
    m_stack = new QStackedWidget(this);

    // Welcome screen
    m_welcomeWidget = buildWelcomeWidget();
    m_stack->addWidget(m_welcomeWidget);  // index 0

    // Editor (splitter + thumbs + view-container)
    m_splitter = new QSplitter(Qt::Horizontal);
    m_splitter->setHandleWidth(1);

    m_thumbs = new ThumbnailPanel;
    m_thumbs->setObjectName("thumbPanel");
    m_splitter->addWidget(m_thumbs);

    // ── Right panel: view + collapsible search bar ──
    m_view = new PdfView(m_pdf);
    auto *viewCont = new QWidget;
    auto *vcLay = new QVBoxLayout(viewCont);
    vcLay->setContentsMargins(0, 0, 0, 0);
    vcLay->setSpacing(0);
    vcLay->addWidget(m_view);

    m_searchBar = new QWidget;
    m_searchBar->setObjectName("searchBar");
    m_searchBar->hide();
    auto *sbLay = new QHBoxLayout(m_searchBar);
    sbLay->setContentsMargins(8, 4, 8, 4);
    sbLay->setSpacing(6);
    sbLay->addWidget(new QLabel(tr("Find:")));
    m_searchEdit = new QLineEdit;
    m_searchEdit->setPlaceholderText(tr("Search in document…"));
    m_searchEdit->setFixedWidth(220);
    sbLay->addWidget(m_searchEdit);
    auto *prevBtn = new QPushButton("◀");
    prevBtn->setFixedWidth(28); prevBtn->setToolTip(tr("Previous (Shift+Enter)"));
    auto *nextBtn = new QPushButton("▶");
    nextBtn->setFixedWidth(28); nextBtn->setToolTip(tr("Next (Enter)"));
    sbLay->addWidget(prevBtn);
    sbLay->addWidget(nextBtn);
    m_searchStatus = new QLabel;
    m_searchStatus->setStyleSheet("color:#888; min-width:70px;");
    sbLay->addWidget(m_searchStatus);
    sbLay->addStretch();
    auto *closeSearch = new QPushButton("✕");
    closeSearch->setFixedWidth(24);
    sbLay->addWidget(closeSearch);
    vcLay->addWidget(m_searchBar);
    m_splitter->addWidget(viewCont);

    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({150, 1});
    m_stack->addWidget(m_splitter);     // index 1

    setCentralWidget(m_stack);
    m_stack->setCurrentIndex(0);

    buildMenus();
    buildToolbar();
    buildStatusBar();

    connect(m_thumbs, &ThumbnailPanel::pageClicked,            m_view,   &PdfView::scrollToPage);
    connect(m_thumbs, &ThumbnailPanel::pageDeleteRequested,    this, &MainWindow::deletePage);
    connect(m_thumbs, &ThumbnailPanel::pageDuplicateRequested, this, &MainWindow::duplicatePage);
    connect(m_thumbs, &ThumbnailPanel::pageInsertRequested,    this, &MainWindow::insertBlankPage);
    connect(m_thumbs, &ThumbnailPanel::pageMoveRequested, this, [this](int from, int to) {
        if (!m_pdf->isOpen()) return;
        if (m_pdf->movePage(from, to)) {
            m_view->setDocument(m_pdf);
            m_thumbs->setDocument(m_pdf);
            onModified();
        }
    });
    connect(m_view,   &PdfView::pageDeleteRequested,    this, &MainWindow::deletePage);
    connect(m_view,   &PdfView::pageDuplicateRequested, this, &MainWindow::duplicatePage);
    connect(m_view,   &PdfView::pageInsertRequested,    this, &MainWindow::insertBlankPage);
    connect(m_view,   &PdfView::modified, this, &MainWindow::onModified);
    connect(m_view,   &PdfView::pageChanged, this, [this](int pg) {
        m_statusPage->setText(tr("Page %1 / %2").arg(pg+1).arg(m_pdf->pageCount()));
        m_thumbs->setCurrentPage(pg);
        updatePageNav(pg);
    });

    // Search bar signals
    connect(m_searchEdit, &QLineEdit::textChanged,  this, [this](const QString &q){ searchRun(q); });
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &MainWindow::searchNext);
    connect(nextBtn,      &QPushButton::clicked,     this, &MainWindow::searchNext);
    connect(prevBtn,      &QPushButton::clicked,     this, &MainWindow::searchPrev);
    connect(closeSearch,  &QPushButton::clicked,     this, &MainWindow::searchHide);

    connect(m_view,   &PdfView::zoomChanged, this, [this](float z) {
        int pct = qRound(z * 100);
        // Update slider without re-triggering valueChanged
        m_zoomSlider->blockSignals(true);
        m_zoomSlider->setValue(pct);
        m_zoomSlider->blockSignals(false);
        // Update inline label
        if (m_zoomPctLabel) {
            m_zoomPctLabel->setText(QString("%1%").arg(pct));
            m_zoomPctLabel->setStyleSheet(pct == 100
                ? "color:#CC3232; font-weight:700;"
                : "color: inherit; font-weight:400;");
        }
        m_statusZoom->setText(QString("%1%").arg(pct));
    });
    connect(m_view,   &PdfView::selectionChanged, this, [this](const QString &t) {
        if (!t.isEmpty())
            statusBar()->showMessage(tr("Copied: \"%1\"").arg(t.left(40)), 2000);
    });

    applyTheme();
    updateUndoState();

    // Restore geometry
    QSettings s("PDFEditor", "PDFEditor");
    if (s.contains("geometry"))  restoreGeometry(s.value("geometry").toByteArray());
    if (s.contains("splitter"))  m_splitter->restoreState(s.value("splitter").toByteArray());
}

// ─────────────────────────────────────────────────────────────
// Actions & menus
// ─────────────────────────────────────────────────────────────

QString MainWindow::shortcutFor(const QString &id, const QString &defaultKey) const {
    QSettings s("PDFEditor", "PDFEditor");
    return s.value("shortcuts/" + id, defaultKey).toString();
}

QAction *MainWindow::makeAct(const QString &id, const QString &text,
                               const QString &tip, const QString &defaultKey,
                               const QString &category) {
    auto *act = new QAction(text, this);
    QString sc = shortcutFor(id, defaultKey);
    act->setShortcut(QKeySequence(sc));
    act->setToolTip(sc.isEmpty() ? tip : tip + "  " + sc);
    act->setProperty("baseTip", tip);
    m_actMap[id] = act;
    ActionDef def;
    def.id = id; def.name = text;
    def.category = category;
    def.defaultShortcut = QKeySequence(defaultKey);
    m_actionDefs.append(def);
    return act;
}

QAction *MainWindow::makeTool(const QString &icon, const QString &label,
                                const QString &tip, Tool toolMode) {
    auto *act = new QAction(icon + "  " + label, this);
    act->setCheckable(true);
    act->setToolTip(tip);
    connect(act, &QAction::triggered, this, [=]() { setActiveTool(toolMode); });
    return act;
}

void MainWindow::buildMenus() {
    auto *mb = menuBar();

    // ── File ──
    auto *mFile = mb->addMenu(tr("&File"));
    {
        auto *a = makeAct("file.open", tr("Open …"), tr("Open PDF"), "Ctrl+O", tr("File"));
        connect(a, &QAction::triggered, this, &MainWindow::openDialog);
        mFile->addAction(a);
    }
    {
        auto *a = makeAct("file.save", tr("Save"), tr("Save PDF"), "Ctrl+S", tr("File"));
        connect(a, &QAction::triggered, this, &MainWindow::saveFile);
        mFile->addAction(a);
    }
    {
        auto *a = makeAct("file.saveas", tr("Save As …"), tr("Save As"), "Ctrl+Shift+S", tr("File"));
        connect(a, &QAction::triggered, this, &MainWindow::saveAs);
        mFile->addAction(a);
    }
    {
        auto *a = makeAct("file.print", tr("Print …"), tr("Print PDF"), "Ctrl+P", tr("File"));
        connect(a, &QAction::triggered, this, &MainWindow::printPdf);
        mFile->addAction(a);
    }
    mFile->addSeparator();
    auto *mRecent = mFile->addMenu(tr("Recently Opened"));
    connect(mRecent, &QMenu::aboutToShow, this, [this, mRecent]() {
        mRecent->clear();
        const QStringList files = recentFiles();
        if (files.isEmpty()) {
            mRecent->addAction(tr("(empty)"))->setEnabled(false);
        } else {
            for (const QString &f : files) {
                QFileInfo fi(f);
                auto *a = mRecent->addAction(fi.fileName());
                a->setToolTip(f);
                connect(a, &QAction::triggered, this, [this, f]() { openFile(f); });
            }
            mRecent->addSeparator();
            auto *clearAct = mRecent->addAction(tr("Clear List"));
            connect(clearAct, &QAction::triggered, this, [this]() {
                QSettings s("PDFEditor", "PDFEditor");
                s.remove("recentFiles");
                rebuildRecentList();
            });
        }
    });
    mFile->addSeparator();
    {
        auto *a = makeAct("file.close", tr("Close"), tr("Close file"), "Ctrl+W", tr("File"));
        connect(a, &QAction::triggered, this, &MainWindow::closeFile);
        mFile->addAction(a);
    }
    mFile->addSeparator();
    {
        auto *a = new QAction(tr("Exit"), this);
        a->setShortcut(QKeySequence("Alt+F4"));
        connect(a, &QAction::triggered, this, &QMainWindow::close);
        mFile->addAction(a);
    }

    // ── Edit ──
    auto *mEdit = mb->addMenu(tr("&Edit"));
    {
        m_actUndo = makeAct("edit.undo", tr("Undo"), tr("Undo last action"), "Ctrl+Z", tr("Edit"));
        connect(m_actUndo, &QAction::triggered, this, &MainWindow::undo);
        mEdit->addAction(m_actUndo);
    }
    {
        m_actRedo = makeAct("edit.redo", tr("Redo"), tr("Redo action"), "Ctrl+Y", tr("Edit"));
        connect(m_actRedo, &QAction::triggered, this, &MainWindow::redo);
        mEdit->addAction(m_actRedo);
    }
    mEdit->addSeparator();
    {
        auto *a = makeAct("edit.find", tr("Find …"), tr("Search text in document"), "Ctrl+F", tr("Edit"));
        connect(a, &QAction::triggered, this, &MainWindow::searchShow);
        mEdit->addAction(a);
    }

    // ── View ──
    auto *mView = mb->addMenu(tr("&View"));
    {
        auto *a = makeAct("view.zoomin", tr("Zoom In"), tr("Zoom In"), "Ctrl++", tr("View"));
        connect(a, &QAction::triggered, this, &MainWindow::zoomIn);
        mView->addAction(a);
    }
    {
        auto *a = makeAct("view.zoomout", tr("Zoom Out"), tr("Zoom Out"), "Ctrl+-", tr("View"));
        connect(a, &QAction::triggered, this, &MainWindow::zoomOut);
        mView->addAction(a);
    }
    {
        auto *a = makeAct("view.zoomfit", tr("Fit to Window"), tr("Fit to Window"), "Ctrl+0", tr("View"));
        connect(a, &QAction::triggered, this, &MainWindow::zoomFit);
        mView->addAction(a);
    }
    mView->addSeparator();
    {
        auto *a = makeAct("view.thumbs", tr("Page Panel"), tr("Toggle page panel"), "Ctrl+B");
        a->setCheckable(true); a->setChecked(true);
        connect(a, &QAction::toggled, this, &MainWindow::toggleThumbs);
        mView->addAction(a);
    }

    // ── Page ──
    auto *mPage = mb->addMenu(tr("&Page"));
    {
        auto *a = makeAct("page.rotleft", tr("Rotate Left"), tr("Rotate 90° left"), "Ctrl+[", tr("Page"));
        connect(a, &QAction::triggered, this, &MainWindow::rotateLeft);
        mPage->addAction(a);
    }
    {
        auto *a = makeAct("page.rotright", tr("Rotate Right"), tr("Rotate 90° right"), "Ctrl+]", tr("Page"));
        connect(a, &QAction::triggered, this, &MainWindow::rotateRight);
        mPage->addAction(a);
    }
    mPage->addSeparator();
    {
        auto *a = makeAct("page.delete", tr("Delete Page"), tr("Delete current page"), "", tr("Page"));
        connect(a, &QAction::triggered, this, &MainWindow::deletePage);
        mPage->addAction(a);
    }
    {
        auto *a = makeAct("page.dup", tr("Duplicate Page"), tr("Duplicate current page"), "", tr("Page"));
        connect(a, &QAction::triggered, this, &MainWindow::duplicatePage);
        mPage->addAction(a);
    }

    // ── Tools ──
    auto *mTools = mb->addMenu(tr("&Tools"));
    {
        auto *a = makeAct("tools.merge", tr("Merge PDFs …"),
                          tr("Insert pages from other PDFs"), "", tr("Tools"));
        connect(a, &QAction::triggered, this, &MainWindow::mergePdfs);
        mTools->addAction(a);
    }
    {
        auto *a = makeAct("tools.split", tr("Extract Pages …"),
                          tr("Save page range as a new file"), "", tr("Tools"));
        connect(a, &QAction::triggered, this, &MainWindow::splitPdf);
        mTools->addAction(a);
    }
    mTools->addSeparator();
    {
        auto *a = makeAct("tools.insertimage", tr("Insert Image …"),
                          tr("Place an image on the current page"), "", tr("Tools"));
        connect(a, &QAction::triggered, this, &MainWindow::insertImageFromFile);
        mTools->addAction(a);
    }

    // ── Settings (direct action, not a submenu) ──
    {
        auto *a = new QAction(tr("&Settings"), this);
        a->setShortcut(QKeySequence("Ctrl+,"));
        connect(a, &QAction::triggered, this, &MainWindow::openSettings);
        mb->addAction(a);
    }

    // ── Help ──
    auto *mHelp = mb->addMenu(tr("&Help"));
    {
        auto *a = new QAction(tr("About PDF Editor …"), this);
        connect(a, &QAction::triggered, this, &MainWindow::showAbout);
        mHelp->addAction(a);
    }
}

void MainWindow::buildToolbar() {
    // ── File / Edit toolbar ──
    auto *tbFile = addToolBar(tr("File"));
    tbFile->setObjectName("tbFile");
    tbFile->setMovable(false);
    tbFile->setToolButtonStyle(Qt::ToolButtonTextOnly);

    auto *openBtn = new QToolButton;
    openBtn->setText(tr("Open"));
    openBtn->setToolTip(tr("Open PDF  Ctrl+O"));
    openBtn->setMinimumHeight(28);
    connect(openBtn, &QToolButton::clicked, this, &MainWindow::openDialog);
    tbFile->addWidget(openBtn);

    auto *saveBtn = new QToolButton;
    saveBtn->setText(tr("Save"));
    saveBtn->setToolTip(tr("Save  Ctrl+S"));
    saveBtn->setMinimumHeight(28);
    connect(saveBtn, &QToolButton::clicked, this, &MainWindow::saveFile);
    tbFile->addWidget(saveBtn);

    tbFile->addSeparator();

    auto *undoBtn = new QToolButton;
    undoBtn->setDefaultAction(m_actUndo);
    tbFile->addWidget(undoBtn);

    auto *redoBtn = new QToolButton;
    redoBtn->setDefaultAction(m_actRedo);
    tbFile->addWidget(redoBtn);

    // ── Tools toolbar ──
    auto *tbTools = addToolBar(tr("Tools"));
    tbTools->setObjectName("tbTools");
    tbTools->setMovable(false);
    tbTools->setToolButtonStyle(Qt::ToolButtonTextOnly);

    auto *toolGroup = new QActionGroup(this);
    toolGroup->setExclusive(true);

    auto addTool = [&](const QString &label, const QString &tip, Tool t) -> QAction* {
        auto *a = new QAction(label, this);
        a->setCheckable(true);
        a->setToolTip(tip);
        toolGroup->addAction(a);
        tbTools->addAction(a);
        connect(a, &QAction::triggered, this, [=]() { setActiveTool(t); });
        return a;
    };

    m_actSelect    = addTool(tr("Select"),    tr("Select & copy text  [S]"),        Tool::Select);
    m_actHighlight = addTool(tr("Highlight"), tr("Highlight text  [H]"),            Tool::Highlight);
    m_actPen       = addTool(tr("Pen"),       tr("Freehand drawing  [P]"),          Tool::Pen);
    m_actEraser    = addTool(tr("Eraser"),    tr("Delete annotation  [E]"),         Tool::Eraser);
    m_actText      = addTool(tr("Text"),      tr("Insert text  [T]"),               Tool::Text);

    m_actSignature = new QAction(tr("Signature"), this);
    m_actSignature->setCheckable(true);
    m_actSignature->setToolTip(tr("Insert signature  [U]"));
    toolGroup->addAction(m_actSignature);
    tbTools->addAction(m_actSignature);
    connect(m_actSignature, &QAction::triggered, this, &MainWindow::openSignatureDialog);

    m_actSelect->setChecked(true);

    tbTools->addSeparator();

    // ── Zoom ──
    auto *zoomLabel = new QLabel(tr("Zoom"));
    zoomLabel->setObjectName("zoomLabel");
    tbTools->addWidget(zoomLabel);

    m_zoomSlider = new QSlider(Qt::Horizontal);
    m_zoomSlider->setRange(25, 400);
    m_zoomSlider->setFixedWidth(120);
    m_zoomSlider->setToolTip(tr("Zoom (Ctrl+Scroll)"));
    tbTools->addWidget(m_zoomSlider);

    // Percentage label right next to the slider
    m_zoomPctLabel = new QLabel("100%");
    m_zoomPctLabel->setFixedWidth(46);
    m_zoomPctLabel->setAlignment(Qt::AlignCenter);
    m_zoomPctLabel->setObjectName("zoomPctLabel");
    tbTools->addWidget(m_zoomPctLabel);

    // Helper: update label and highlight when snapped to 100%
    auto updateZoomLabel = [this](int v) {
        bool snapped = (v == 100);
        m_zoomPctLabel->setText(QString("%1%").arg(v));
        m_zoomPctLabel->setStyleSheet(snapped
            ? "color:#CC3232; font-weight:700;"   // accent when at 100%
            : "color: inherit; font-weight:400;");
    };

    // During drag: snap the zoom VALUE (not the slider) so the page
    // reflects 100% while the thumb is anywhere in the ±5 zone
    connect(m_zoomSlider, &QSlider::valueChanged, this, [this, updateZoomLabel](int v) {
        int effective = (v != 100 && qAbs(v - 100) <= 12) ? 100 : v;
        updateZoomLabel(effective);
        m_view->setZoom(effective / 100.f);
    });

    // On release: also snap the slider thumb itself so it sits at 100
    connect(m_zoomSlider, &QSlider::sliderReleased, this, [this, updateZoomLabel]() {
        int v = m_zoomSlider->value();
        if (v != 100 && qAbs(v - 100) <= 12) {
            m_zoomSlider->setValue(100);   // thumb snaps visually after release
            updateZoomLabel(100);
            m_view->setZoom(1.0f);
        }
    });

    {
        QSettings qs("PDFEditor", "PDFEditor");
        m_zoomSlider->setValue(qs.value("view/defaultZoom", 100).toInt());
    }

    tbTools->addSeparator();

    // ── Page navigation ──
    {
        auto *wrap = new QWidget;
        auto *lay  = new QHBoxLayout(wrap);
        lay->setContentsMargins(4, 0, 4, 0);
        lay->setSpacing(2);

        auto *prevPage = new QPushButton("◀");
        prevPage->setFixedSize(24, 24);
        prevPage->setToolTip(tr("Previous page"));
        prevPage->setFlat(true);

        m_pageEdit = new QLineEdit("1");
        m_pageEdit->setFixedWidth(38);
        m_pageEdit->setAlignment(Qt::AlignCenter);
        m_pageEdit->setValidator(new QIntValidator(1, 99999, this));
        m_pageEdit->setToolTip(tr("Page number — press Enter to jump"));

        m_pageTotalLabel = new QLabel("/ 1");
        m_pageTotalLabel->setStyleSheet("color:#777;");

        auto *nextPage = new QPushButton("▶");
        nextPage->setFixedSize(24, 24);
        nextPage->setToolTip(tr("Next page"));
        nextPage->setFlat(true);

        lay->addWidget(prevPage);
        lay->addWidget(m_pageEdit);
        lay->addWidget(m_pageTotalLabel);
        lay->addWidget(nextPage);
        tbTools->addWidget(wrap);

        connect(m_pageEdit, &QLineEdit::returnPressed, this, [this]() {
            bool ok;
            int pg = m_pageEdit->text().toInt(&ok);
            if (ok && m_pdf->isOpen())
                m_view->scrollToPage(qBound(1, pg, m_pdf->pageCount()) - 1);
        });
        connect(prevPage, &QPushButton::clicked, this, [this]() {
            if (m_pdf->isOpen())
                m_view->scrollToPage(qMax(0, m_view->currentPage() - 1));
        });
        connect(nextPage, &QPushButton::clicked, this, [this]() {
            if (m_pdf->isOpen())
                m_view->scrollToPage(qMin(m_pdf->pageCount() - 1, m_view->currentPage() + 1));
        });
    }

    tbTools->addSeparator();

    // ── Color swatches ──
    // Each color button is paired with a thin colored strip below it.
    auto makeColorWidget = [&](const QString &label, const QString &tip,
                               QFrame **swatchOut,
                               std::function<QColor()> getColor,
                               std::function<void(QColor)> setColor) {
        auto *wrap = new QWidget;
        wrap->setToolTip(tip);
        auto *vl = new QVBoxLayout(wrap);
        vl->setContentsMargins(2, 3, 2, 3);
        vl->setSpacing(3);

        auto *btn = new QToolButton;
        btn->setText(label);
        btn->setMinimumHeight(22);
        btn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        vl->addWidget(btn);

        auto *swatch = new QFrame;
        swatch->setFixedHeight(3);
        swatch->setFrameShape(QFrame::NoFrame);
        vl->addWidget(swatch);
        *swatchOut = swatch;

        swatch->setStyleSheet(QString("background:%1; border-radius:1px;")
                              .arg(getColor().name()));

        connect(btn, &QToolButton::clicked, this, [=]() {
            QColor c = QColorDialog::getColor(getColor(), this, tip);
            if (c.isValid()) {
                setColor(c);
                swatch->setStyleSheet(QString("background:%1; border-radius:1px;")
                                      .arg(c.name()));
            }
        });
        return wrap;
    };

    tbTools->addWidget(makeColorWidget(
        tr("Pen Color"), tr("Choose pen color"),
        &m_penColorSwatch,
        [this]() { return m_view->penColor(); },
        [this](QColor c) { m_view->setPenColor(c); }));

    tbTools->addWidget(makeColorWidget(
        tr("Highlight Color"), tr("Choose highlight color"),
        &m_hlColorSwatch,
        [this]() { return m_view->highlightColor(); },
        [this](QColor c) { m_view->setHighlightColor(c); }));
}

void MainWindow::buildStatusBar() {
    m_statusFile = new QLabel;
    m_statusPage = new QLabel;
    m_statusZoom = new QLabel("100%");
    statusBar()->addWidget(m_statusFile, 1);
    statusBar()->addPermanentWidget(m_statusPage);
    statusBar()->addPermanentWidget(m_statusZoom);
    statusBar()->setSizeGripEnabled(false);
}

// ─────────────────────────────────────────────────────────────
// Theme
// ─────────────────────────────────────────────────────────────

void MainWindow::applyTheme() {
    qApp->setStyleSheet(m_darkMode ? Theme::darkSheet() : Theme::lightSheet());

    if (m_darkMode) {
        m_welcomeWidget->setStyleSheet(R"(
QWidget#welcomeWidget   { background: #1C1C1C; }
QWidget#welcomeHeader   { background: #242424; }
QFrame#welcomeRule      { background: #2A2A2A; }
QWidget#welcomeBody     { background: #1C1C1C; }
QWidget#welcomeCard     { background: transparent; }
QFrame#dropZone {
    border: 1.5px dashed #444444;
    border-radius: 10px;
    background: #222222;
}
QFrame#dropZone:hover {
    border-color: #CC3232;
    background: #252020;
}
QLabel#dropIcon    { font-size: 28px; color: #555555; background: transparent; }
QLabel#dropHint    { font-size: 16px; font-weight: 600; color: #CCCCCC; background: transparent; }
QLabel#dropOr      { font-size: 12px; color: #555555; background: transparent; }
QPushButton#dropOpenBtn {
    background: #CC3232; color: #FFFFFF; border: none;
    border-radius: 6px; font-size: 13px; font-weight: 600;
}
QPushButton#dropOpenBtn:hover   { background: #DD4444; }
QPushButton#dropOpenBtn:pressed { background: #AA2424; }
QLabel#recentLabel { font-size: 11px; font-weight: 600; color: #888888; background: transparent; }
QLabel#verLabel    { font-size: 11px; color: #444444; background: transparent; }
QListWidget#recentList {
    background: #222222; border: 1px solid #333333;
    border-radius: 8px; padding: 4px; outline: none;
}
QListWidget#recentList::item          { color: #BBBBBB; padding: 7px 12px; border-radius: 5px; }
QListWidget#recentList::item:hover    { background: #2C2C2C; color: #FFFFFF; }
QListWidget#recentList::item:selected { background: #CC3232; color: #FFFFFF; }
)");
    } else {
        m_welcomeWidget->setStyleSheet(R"(
QWidget#welcomeWidget   { background: #F2F2F2; }
QWidget#welcomeHeader   { background: #FFFFFF; }
QFrame#welcomeRule      { background: #E0E0E0; }
QWidget#welcomeBody     { background: #F2F2F2; }
QWidget#welcomeCard     { background: transparent; }
QFrame#dropZone {
    border: 1.5px dashed #CCCCCC;
    border-radius: 10px;
    background: #FFFFFF;
}
QFrame#dropZone:hover {
    border-color: #CC3232;
    background: #FFF8F8;
}
QLabel#dropIcon    { font-size: 28px; color: #BBBBBB; background: transparent; }
QLabel#dropHint    { font-size: 16px; font-weight: 600; color: #333333; background: transparent; }
QLabel#dropOr      { font-size: 12px; color: #AAAAAA; background: transparent; }
QPushButton#dropOpenBtn {
    background: #CC3232; color: #FFFFFF; border: none;
    border-radius: 6px; font-size: 13px; font-weight: 600;
}
QPushButton#dropOpenBtn:hover   { background: #DD4444; }
QPushButton#dropOpenBtn:pressed { background: #AA2424; }
QLabel#recentLabel { font-size: 11px; font-weight: 600; color: #888888; background: transparent; }
QLabel#verLabel    { font-size: 11px; color: #AAAAAA; background: transparent; }
QListWidget#recentList {
    background: #FFFFFF; border: 1px solid #E0E0E0;
    border-radius: 8px; padding: 4px; outline: none;
}
QListWidget#recentList::item          { color: #333333; padding: 7px 12px; border-radius: 5px; }
QListWidget#recentList::item:hover    { background: #F5F5F5; color: #1C1C1C; }
QListWidget#recentList::item:selected { background: #CC3232; color: #FFFFFF; }
)");
    }
}

// ─────────────────────────────────────────────────────────────
// Show / hide editor vs welcome
// ─────────────────────────────────────────────────────────────

void MainWindow::showEditor() {
    m_stack->setCurrentIndex(1);
}

void MainWindow::showWelcome() {
    m_stack->setCurrentIndex(0);
    rebuildRecentList();
}

// ─────────────────────────────────────────────────────────────
// File operations
// ─────────────────────────────────────────────────────────────

void MainWindow::openFile(const QString &path) {
    if (!confirmDiscard()) return;
    if (!m_pdf->open(path)) {
        QMessageBox::warning(this, tr("Error"),
            tr("Could not open file:\n%1").arg(path));
        return;
    }
    m_view->setDocument(m_pdf);
    m_thumbs->setDocument(m_pdf);
    m_modified = false;
    m_statusFile->setText(QFileInfo(path).fileName());
    m_statusPage->setText(tr("Page 1 / %1").arg(m_pdf->pageCount()));
    m_statusZoom->setText(QString("%1%").arg(qRound(m_view->zoom() * 100)));
    updateTitle();
    updateUndoState();
    addRecentFile(path);
    updatePageNav(0);
    searchHide();
    showEditor();
}

void MainWindow::openDialog() {
    if (!confirmDiscard()) return;
    QString path = QFileDialog::getOpenFileName(
        this, tr("Open PDF"), {},
        tr("PDF Files (*.pdf);;All Files (*)"));
    if (!path.isEmpty()) openFile(path);
}

void MainWindow::saveFile() {
    if (!m_pdf->isOpen()) return;
    if (m_pdf->filePath().isEmpty()) { saveAs(); return; }
    if (m_pdf->save()) {
        setModified(false);
        statusBar()->showMessage(tr("Saved"), 2000);
    } else {
        QMessageBox::warning(this, tr("Error"), tr("Save failed."));
    }
}

void MainWindow::saveAs() {
    if (!m_pdf->isOpen()) return;
    QString path = QFileDialog::getSaveFileName(
        this, tr("Save As"), m_pdf->filePath(),
        tr("PDF Files (*.pdf)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(".pdf", Qt::CaseInsensitive)) path += ".pdf";
    if (m_pdf->save(path)) {
        setModified(false);
        m_statusFile->setText(QFileInfo(path).fileName());
        updateTitle();
        statusBar()->showMessage(tr("Saved"), 2000);
    } else {
        QMessageBox::warning(this, tr("Error"), tr("Save failed."));
    }
}

void MainWindow::closeFile() {
    if (!confirmDiscard()) return;
    m_pdf->close();
    m_view->setDocument(nullptr);
    m_thumbs->setDocument(nullptr);
    m_modified = false;
    m_statusFile->clear();
    m_statusPage->clear();
    m_statusZoom->clear();
    updateTitle();
    updateUndoState();
    showWelcome();
}

bool MainWindow::confirmDiscard() {
    if (!m_modified || !m_pdf->isOpen()) return true;
    QMessageBox mb(this);
    mb.setWindowTitle(tr("Unsaved Changes"));
    mb.setText(tr("This file has unsaved changes."));
    mb.setInformativeText(tr("Do you want to save the changes?"));
    mb.setIcon(QMessageBox::Question);
    auto *btnSave    = mb.addButton(tr("Save"),         QMessageBox::AcceptRole);
    auto *btnDiscard = mb.addButton(tr("Don't Save"),   QMessageBox::DestructiveRole);
    auto *btnCancel  = mb.addButton(tr("Cancel"),       QMessageBox::RejectRole);
    mb.setDefaultButton(btnSave);
    mb.exec();
    if (mb.clickedButton() == btnCancel)  return false;
    if (mb.clickedButton() == btnSave)    saveFile();
    Q_UNUSED(btnDiscard);
    return true;
}

// ─────────────────────────────────────────────────────────────
// Undo / redo
// ─────────────────────────────────────────────────────────────

void MainWindow::undo() {
    if (!m_pdf->canUndo()) return;
    int oldN = m_pdf->pageCount();
    m_pdf->undo();
    if (m_pdf->pageCount() != oldN) {
        m_view->setDocument(m_pdf);
        m_thumbs->setDocument(m_pdf);
    } else {
        m_view->refreshAll();
        m_thumbs->refreshAll();
    }
    updateUndoState();
    m_statusPage->setText(
        tr("Seite %1 / %2").arg(m_view->currentPage()+1).arg(m_pdf->pageCount()));
}

void MainWindow::redo() {
    if (!m_pdf->canRedo()) return;
    int oldN = m_pdf->pageCount();
    m_pdf->redo();
    if (m_pdf->pageCount() != oldN) {
        m_view->setDocument(m_pdf);
        m_thumbs->setDocument(m_pdf);
    } else {
        m_view->refreshAll();
        m_thumbs->refreshAll();
    }
    updateUndoState();
    m_statusPage->setText(
        tr("Seite %1 / %2").arg(m_view->currentPage()+1).arg(m_pdf->pageCount()));
}

void MainWindow::updateUndoState() {
    m_actUndo->setEnabled(m_pdf->canUndo());
    m_actRedo->setEnabled(m_pdf->canRedo());
}

// ─────────────────────────────────────────────────────────────
// Zoom
// ─────────────────────────────────────────────────────────────

void MainWindow::zoomIn() {
    float z = qBound(ZOOM_MIN, m_view->zoom() + ZOOM_STEP, ZOOM_MAX);
    m_view->setZoom(z);
    m_zoomSlider->setValue(qRound(z * 100));
}
void MainWindow::zoomOut() {
    float z = qBound(ZOOM_MIN, m_view->zoom() - ZOOM_STEP, ZOOM_MAX);
    m_view->setZoom(z);
    m_zoomSlider->setValue(qRound(z * 100));
}
void MainWindow::zoomFit() {
    if (!m_pdf->isOpen()) return;
    PageSize ps = m_pdf->pageSize(m_view->currentPage());
    float available = m_view->viewport()->width() - 32.f;
    float z = (ps.width > 0) ? qBound(ZOOM_MIN, available / ps.width, ZOOM_MAX) : 1.f;
    m_view->setZoom(z);
    m_zoomSlider->setValue(qRound(z * 100));
}
void MainWindow::zoomActual() {
    m_view->setZoom(1.f);
    m_zoomSlider->setValue(100);
}

// ─────────────────────────────────────────────────────────────
// Page ops
// ─────────────────────────────────────────────────────────────

void MainWindow::rotateLeft() {
    if (!m_pdf->isOpen()) return;
    int pg = m_view->currentPage();
    if (m_pdf->rotatePage(pg, -90)) { m_view->refreshPage(pg); m_thumbs->refreshAll(); onModified(); }
}
void MainWindow::rotateRight() {
    if (!m_pdf->isOpen()) return;
    int pg = m_view->currentPage();
    if (m_pdf->rotatePage(pg, 90)) { m_view->refreshPage(pg); m_thumbs->refreshAll(); onModified(); }
}
void MainWindow::deletePage() {
    if (!m_pdf->isOpen() || m_pdf->pageCount() <= 1) return;
    int pg = m_view->currentPage();
    auto ret = QMessageBox::question(this, tr("Delete Page"),
        tr("Really delete page %1?").arg(pg+1));
    if (ret != QMessageBox::Yes) return;
    if (m_pdf->deletePage(pg)) {
        m_view->setDocument(m_pdf);
        m_thumbs->setDocument(m_pdf);
        onModified();
    }
}
void MainWindow::duplicatePage() {
    if (!m_pdf->isOpen()) return;
    int pg = m_view->currentPage();
    if (m_pdf->duplicatePage(pg)) {
        m_view->setDocument(m_pdf);
        m_thumbs->setDocument(m_pdf);
        onModified();
    }
}
void MainWindow::insertBlankPage() {
    if (!m_pdf->isOpen()) return;
    int pg = m_view->currentPage();
    if (m_pdf->insertBlankPage(pg)) {
        m_view->setDocument(m_pdf);
        m_thumbs->setDocument(m_pdf);
        onModified();
    }
}
void MainWindow::mergePdfs() {
    if (!m_pdf->isOpen()) {
        QMessageBox::information(this, tr("Note"),
            tr("Please open a PDF first to merge pages into."));
        return;
    }
    MergeDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;
    QStringList paths = dlg.selectedPaths();
    if (paths.isEmpty()) return;
    if (m_pdf->mergeFrom(paths)) {
        m_view->setDocument(m_pdf);
        m_thumbs->setDocument(m_pdf);
        onModified();
        statusBar()->showMessage(
            tr("%1 file(s) merged.").arg(paths.size()), 3000);
    } else {
        QMessageBox::warning(this, tr("Error"), tr("Merge failed."));
    }
}

void MainWindow::splitPdf() {
    if (!m_pdf->isOpen()) {
        QMessageBox::information(this, tr("Note"), tr("Please open a PDF first."));
        return;
    }
    SplitDialog dlg(m_pdf->pageCount(), this);
    if (dlg.exec() != QDialog::Accepted) return;
    QList<int> pages = dlg.selectedPages();
    if (pages.isEmpty()) {
        QMessageBox::warning(this, tr("Error"), tr("Invalid page range."));
        return;
    }
    if (m_pdf->extractPages(dlg.outputPath(), pages)) {
        statusBar()->showMessage(
            tr("%1 pages saved to %2").arg(pages.size()).arg(dlg.outputPath()), 4000);
    } else {
        QMessageBox::warning(this, tr("Error"), tr("Extraction failed."));
    }
}

void MainWindow::addWatermark() {
    // Watermark feature has been removed.
}

void MainWindow::insertImageToPage() {
    if (!m_pdf->isOpen()) return;
    QString path = QFileDialog::getOpenFileName(
        this, tr("Choose Image"), {},
        tr("Images (*.png *.jpg *.jpeg *.bmp);;All Files (*)"));
    if (path.isEmpty()) return;
    int pg = m_view->currentPage();
    PageSize ps = m_pdf->pageSize(pg);
    if (m_pdf->insertImage(pg, path, ps.width/2.f, ps.height/2.f, 150.f, 60.f)) {
        m_view->refreshPage(pg);
        onModified();
    }
}

// ─────────────────────────────────────────────────────────────
// Dialogs
// ─────────────────────────────────────────────────────────────

void MainWindow::openFormFiller() {
    // Form fields are shown as inline overlays automatically when a PDF is opened.
}

void MainWindow::openSignatureDialog() {
    SignaturePickerDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted && !dlg.selectedPath().isEmpty()) {
        m_view->setSignaturePixmap(dlg.selectedPixmap());
        setActiveTool(Tool::Signature);
        m_actSignature->setChecked(true);
        statusBar()->showMessage(tr("Signature: click on page to place, drag to resize"), 3000);
    }
}

void MainWindow::openKeybinds() {
    KeybindDialog dlg(m_actionDefs, m_actMap, this);
    dlg.exec();
}

void MainWindow::openSettings() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Settings"));
    dlg.setMinimumSize(520, 460);

    auto *mainLay = new QVBoxLayout(&dlg);
    mainLay->setContentsMargins(0, 0, 0, 0);
    mainLay->setSpacing(0);

    auto *tabs = new QTabWidget(&dlg);
    mainLay->addWidget(tabs, 1);

    QSettings s("PDFEditor", "PDFEditor");

    // ─────────────────────────────────────────
    // Tab: General
    // ─────────────────────────────────────────
    auto *genTab  = new QWidget;
    auto *genLay  = new QVBoxLayout(genTab);
    genLay->setContentsMargins(20, 20, 20, 20);
    genLay->setSpacing(12);

    auto mkLabel = [](const QString &t) {
        auto *l = new QLabel(t);
        l->setMinimumWidth(160);
        return l;
    };
    auto mkRow = [](QWidget *lbl, QWidget *ctrl, QHBoxLayout *&hl) {
        hl = new QHBoxLayout;
        hl->addWidget(lbl);
        hl->addWidget(ctrl, 1);
        return hl;
    };

    auto *startupCombo = new QComboBox;
    startupCombo->addItem(tr("Welcome screen"), "welcome");
    startupCombo->addItem(tr("Open last file"),  "lastfile");
    startupCombo->addItem(tr("Empty editor"),    "empty");
    startupCombo->setCurrentIndex(
        startupCombo->findData(s.value("startup/mode", "welcome")));
    QHBoxLayout *hl1; genLay->addLayout(mkRow(mkLabel(tr("On startup:")), startupCombo, hl1));

    auto *zoomCombo = new QComboBox;
    for (int v : {50, 75, 100, 125, 150, 175, 200})
        zoomCombo->addItem(QString("%1%").arg(v), v);
    {
        int zi = zoomCombo->findData(s.value("view/defaultZoom", 100));
        zoomCombo->setCurrentIndex(zi >= 0 ? zi : 2);
    }
    QHBoxLayout *hl2; genLay->addLayout(mkRow(mkLabel(tr("Default zoom:")), zoomCombo, hl2));

    auto *recentSpin = new QSpinBox;
    recentSpin->setRange(3, 20);
    recentSpin->setValue(s.value("recentFiles/max", 10).toInt());
    QHBoxLayout *hl3; genLay->addLayout(mkRow(mkLabel(tr("Recent files (max):")), recentSpin, hl3));

    genLay->addStretch();
    tabs->addTab(genTab, tr("General"));

    // ─────────────────────────────────────────
    // Tab: Appearance
    // ─────────────────────────────────────────
    auto *appTab = new QWidget;
    auto *appLay = new QVBoxLayout(appTab);
    appLay->setContentsMargins(20, 20, 20, 20);
    appLay->setSpacing(12);

    auto *themeRow = new QHBoxLayout;
    auto *themeLabel = new QLabel(tr("Theme:"));
    auto *themeCombo = new QComboBox;
    themeCombo->addItem(tr("Light"));
    themeCombo->addItem(tr("Dark"));
    themeCombo->setCurrentIndex(m_darkMode ? 1 : 0);
    connect(themeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int idx) {
        m_darkMode = (idx == 1);
        applyTheme();
    });
    themeRow->addWidget(themeLabel);
    themeRow->addWidget(themeCombo);
    themeRow->addStretch();
    appLay->addLayout(themeRow);

    auto *spacingSpin = new QSpinBox;
    spacingSpin->setRange(4, 48);
    spacingSpin->setValue(s.value("view/pageSpacing", 16).toInt());
    QHBoxLayout *hlSp; appLay->addLayout(mkRow(mkLabel(tr("Page spacing (px):")), spacingSpin, hlSp));

    appLay->addStretch();
    tabs->addTab(appTab, tr("Appearance"));

    // ─────────────────────────────────────────
    // Tab: Shortcuts
    // ─────────────────────────────────────────
    auto *scTab = new QWidget;
    auto *scLay = new QVBoxLayout(scTab);
    scLay->setContentsMargins(20, 20, 20, 20);
    scLay->setSpacing(8);

    auto *scTable = new QListWidget;
    scTable->setAlternatingRowColors(true);
    scTable->setSelectionMode(QAbstractItemView::NoSelection);

    // Populate with current shortcuts (read-only display for now)
    for (const auto &def : m_actionDefs) {
        QSettings qs("PDFEditor", "PDFEditor");
        QString key = qs.value("shortcuts/" + def.id, def.defaultShortcut.toString()).toString();
        auto *item = new QListWidgetItem(
            QString("%1   %2").arg(def.name, -30).arg(key));
        scTable->addItem(item);
    }

    scLay->addWidget(new QLabel(tr("To customize shortcuts, double-click an action:")));
    scLay->addWidget(scTable, 1);

    auto *editScBtn = new QPushButton(tr("Edit Shortcuts …"));
    connect(editScBtn, &QPushButton::clicked, this, [this]() {
        KeybindDialog dlg2(m_actionDefs, m_actMap, this);
        dlg2.exec();
    });
    scLay->addWidget(editScBtn);

    tabs->addTab(scTab, tr("Shortcuts"));

    // ─────────────────────────────────────────
    // OK / Cancel
    // ─────────────────────────────────────────
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                    &dlg);
    bb->setContentsMargins(12, 8, 12, 12);
    connect(bb, &QDialogButtonBox::accepted, &dlg, [&]() {
        s.setValue("startup/mode",     startupCombo->currentData().toString());
        s.setValue("view/defaultZoom", zoomCombo->currentData().toInt());
        s.setValue("recentFiles/max",  recentSpin->value());
        s.setValue("view/pageSpacing", spacingSpin->value());
        dlg.accept();
    });
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    mainLay->addWidget(bb);

    dlg.exec();
}

void MainWindow::registerAsDefaultPdf() {
    // This option has been removed.
}

void MainWindow::toggleThumbs() {
    m_thumbs->setVisible(!m_thumbs->isVisible());
}

void MainWindow::showAbout() {
    auto *dlg = new QDialog(this);
    dlg->setWindowTitle(tr("About PDF Editor"));
    dlg->setFixedWidth(420);
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    auto *vl = new QVBoxLayout(dlg);
    vl->setSpacing(0);
    vl->setContentsMargins(0, 0, 0, 0);

    // Header
    auto *header = new QWidget;
    header->setFixedHeight(100);
    header->setStyleSheet("background: #0078D4; border-radius: 0;");
    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(24, 0, 24, 0);
    hl->setSpacing(16);

    QPixmap iconPm(48, 58);
    iconPm.fill(Qt::transparent);
    QPainter iconP(&iconPm);
    iconP.setRenderHint(QPainter::Antialiasing);
    drawPdfIcon(iconP, QRect(0, 0, 48, 56));
    iconP.end();
    auto *iconLbl = new QLabel;
    iconLbl->setPixmap(iconPm);
    iconLbl->setStyleSheet("background: transparent;");

    auto *hTitleCol = new QVBoxLayout;
    hTitleCol->setSpacing(2);
    auto *hTitle = new QLabel("PDF Editor");
    hTitle->setStyleSheet("font-size: 20px; font-weight: 700; color: white; background: transparent;");
    auto *hVer = new QLabel(QString("Version %1").arg(QApplication::applicationVersion()));
    hVer->setStyleSheet("font-size: 13px; color: rgba(255,255,255,0.75); background: transparent;");
    hTitleCol->addWidget(hTitle);
    hTitleCol->addWidget(hVer);

    hl->addWidget(iconLbl);
    hl->addLayout(hTitleCol);
    hl->addStretch();
    vl->addWidget(header);

    // Content
    auto *content = new QWidget;
    content->setStyleSheet("background: #252526;");
    auto *cl = new QVBoxLayout(content);
    cl->setContentsMargins(24, 20, 24, 20);
    cl->setSpacing(10);

    auto addRow = [&](const QString &label, const QString &value) {
        auto *row = new QHBoxLayout;
        auto *lbl = new QLabel(label);
        lbl->setStyleSheet("color: #858585; font-size: 12px; min-width: 130px; background: transparent;");
        auto *val = new QLabel(value);
        val->setStyleSheet("color: #D4D4D4; font-size: 12px; background: transparent;");
        val->setTextInteractionFlags(Qt::TextSelectableByMouse);
        row->addWidget(lbl);
        row->addWidget(val, 1);
        cl->addLayout(row);
    };

    addRow(tr("Created by"),    "towbii");
    addRow(tr("Build Date"),    QString(__DATE__));
    addRow(tr("Qt Version"),    QT_VERSION_STR);
    addRow(tr("MuPDF Version"), FZ_VERSION);

    vl->addWidget(content);

    // Button
    auto *btnBox = new QWidget;
    btnBox->setStyleSheet("background: #252526; border-top: 1px solid #3C3C3C;");
    auto *bl = new QHBoxLayout(btnBox);
    bl->setContentsMargins(16, 12, 16, 12);
    bl->addStretch();
    auto *closeBtn = new QPushButton(tr("Close"));
    closeBtn->setDefault(true);
    connect(closeBtn, &QPushButton::clicked, dlg, &QDialog::accept);
    bl->addWidget(closeBtn);
    vl->addWidget(btnBox);

    dlg->exec();
}

// ─────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────

void MainWindow::keyPressEvent(QKeyEvent *ev) {
    // Tool shortcuts: only when no text-input widget has focus
    QWidget *fw = focusWidget();
    bool inTextInput = fw && (qobject_cast<QLineEdit*>(fw) || qobject_cast<QTextEdit*>(fw));
    if (!inTextInput && m_pdf && m_pdf->isOpen()) {
        switch (ev->key()) {
        case Qt::Key_S: setActiveTool(Tool::Select);    m_actSelect->setChecked(true);    return;
        case Qt::Key_H: setActiveTool(Tool::Highlight); m_actHighlight->setChecked(true); return;
        case Qt::Key_P: setActiveTool(Tool::Pen);       m_actPen->setChecked(true);       return;
        case Qt::Key_E: setActiveTool(Tool::Eraser);    m_actEraser->setChecked(true);    return;
        case Qt::Key_T: setActiveTool(Tool::Text);      m_actText->setChecked(true);      return;
        case Qt::Key_U: openSignatureDialog();                                             return;
        case Qt::Key_Escape: setActiveTool(Tool::Select); m_actSelect->setChecked(true);  return;
        default: break;
        }
    }
    QMainWindow::keyPressEvent(ev);
}

void MainWindow::setActiveTool(Tool t) {
    m_tool = t;
    m_view->setTool(t);
    m_actSelect->setChecked(t == Tool::Select);
    m_actHighlight->setChecked(t == Tool::Highlight);
    m_actPen->setChecked(t == Tool::Pen);
    m_actEraser->setChecked(t == Tool::Eraser);
    m_actText->setChecked(t == Tool::Text);
    m_actSignature->setChecked(t == Tool::Signature);
}

void MainWindow::setModified(bool m) {
    m_modified = m;
    updateTitle();
    updateUndoState();
}

void MainWindow::onModified() {
    setModified(true);
    if (m_pdf && m_pdf->isOpen())
        m_thumbs->refreshPage(m_view->currentPage());
}

void MainWindow::updateTitle() {
    QString title;
    if (m_pdf->isOpen()) {
        QFileInfo fi(m_pdf->filePath());
        title = fi.fileName();
        if (m_modified) title.prepend("● ");
        title += " — PDF Editor";
    } else {
        title = "PDF Editor";
    }
    setWindowTitle(title);
}

// ─────────────────────────────────────────────────────────────
// Events
// ─────────────────────────────────────────────────────────────

void MainWindow::closeEvent(QCloseEvent *ev) {
    if (!confirmDiscard()) { ev->ignore(); return; }
    QSettings s("PDFEditor", "PDFEditor");
    s.setValue("geometry", saveGeometry());
    s.setValue("splitter", m_splitter->saveState());
    ev->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *ev) {
    if (ev->mimeData()->hasUrls()) ev->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *ev) {
    const auto urls = ev->mimeData()->urls();
    if (urls.isEmpty()) return;

    // Separate PDFs from images
    QStringList pdfPaths, imgPaths;
    static const QStringList imgExts = {"png","jpg","jpeg","bmp","gif","webp","tiff","tif"};
    for (const QUrl &u : urls) {
        QString path = u.toLocalFile();
        QString ext  = QFileInfo(path).suffix().toLower();
        if (ext == "pdf")           pdfPaths << path;
        else if (imgExts.contains(ext)) imgPaths << path;
    }

    // ── Images dropped ──
    if (!imgPaths.isEmpty()) {
        if (!m_pdf->isOpen()) {
            QMessageBox::information(this, tr("No document open"),
                tr("Open a PDF first, then drop an image to insert it."));
            return;
        }
        for (const QString &imgPath : imgPaths) {
            QPixmap px(imgPath);
            if (px.isNull()) continue;
            auto btn = QMessageBox::question(this, tr("Insert image"),
                tr("How do you want to insert \"%1\"?").arg(QFileInfo(imgPath).fileName()),
                tr("New page"), tr("Place on current page"), tr("Cancel"), 0, 2);
            if (btn == 2) continue;
            if (btn == 0) {
                // Insert as new page: create blank page then embed image full-page
                int after = m_view->currentPage();
                m_pdf->snapshot();
                if (!m_pdf->insertBlankPage(after)) continue;
                int newPg = after + 1;
                auto sz = m_pdf->pageSize(newPg);
                m_pdf->insertImage(newPg, imgPath, sz.width/2.f, sz.height/2.f,
                                   sz.width, sz.height);
                m_view->setDocument(m_pdf);
                m_thumbs->setDocument(m_pdf);
                m_view->scrollToPage(newPg);
                onModified();
            } else {
                // Place as movable element using signature tool
                m_view->setSignaturePixmap(px);
                setActiveTool(Tool::Signature);
                m_actSignature->setChecked(true);
                statusBar()->showMessage(
                    tr("Image loaded — click to place, drag to resize"), 3000);
            }
        }
        return;
    }

    // ── PDFs dropped ──
    if (m_pdf->isOpen() && !pdfPaths.isEmpty()) {
        auto btn = QMessageBox::question(this, tr("PDF dropped"),
            tr("A PDF is already open.\n\nAppend the dropped file(s) to the current document, or open as a new document?"),
            tr("Append"), tr("Open new"), QString(), 0, 1);
        if (btn == 0) {
            if (!m_pdf->mergeFrom(pdfPaths))
                QMessageBox::warning(this, tr("Merge failed"),
                    tr("Could not merge one or more files."));
            m_view->setDocument(m_pdf);
            m_thumbs->setDocument(m_pdf);
            onModified();
            return;
        }
        for (const QString &path : pdfPaths)
            QProcess::startDetached(QApplication::applicationFilePath(), {path});
        return;
    }

    // No document open yet — open first dropped file
    if (!pdfPaths.isEmpty())
        openFile(pdfPaths.first());
    else if (!imgPaths.isEmpty())
        QMessageBox::information(this, tr("No document open"),
            tr("Open a PDF first, then drop an image to insert it."));
}

// ─────────────────────────────────────────────────────────────
// Page navigation helper
// ─────────────────────────────────────────────────────────────

void MainWindow::updatePageNav(int pg) {
    if (m_pageEdit) {
        m_pageEdit->blockSignals(true);
        m_pageEdit->setText(QString::number(pg + 1));
        m_pageEdit->blockSignals(false);
    }
    if (m_pageTotalLabel && m_pdf->isOpen())
        m_pageTotalLabel->setText(QString("/ %1").arg(m_pdf->pageCount()));
}

// ─────────────────────────────────────────────────────────────
// Print
// ─────────────────────────────────────────────────────────────

void MainWindow::printPdf() {
    if (!m_pdf->isOpen()) return;
    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dlg(&printer, this);
    if (dlg.exec() != QDialog::Accepted) return;

    QPainter painter;
    if (!painter.begin(&printer)) return;

    int from = printer.fromPage() > 0 ? printer.fromPage() - 1 : 0;
    int to   = printer.toPage()   > 0 ? printer.toPage()   - 1 : m_pdf->pageCount() - 1;
    to = qMin(to, m_pdf->pageCount() - 1);

    for (int pg = from; pg <= to; ++pg) {
        if (pg > from) printer.newPage();
        QRectF pageRt = printer.pageRect(QPrinter::DevicePixel);
        float dpi  = static_cast<float>(printer.resolution());
        QPixmap px = m_pdf->renderPage(pg, dpi / 72.f);
        painter.drawPixmap(pageRt.toRect(), px);
    }
    painter.end();
}

// ─────────────────────────────────────────────────────────────
// Search
// ─────────────────────────────────────────────────────────────

void MainWindow::searchShow() {
    m_searchBar->show();
    m_searchEdit->setFocus();
    m_searchEdit->selectAll();
}

void MainWindow::searchHide() {
    m_searchBar->hide();
    m_searchEdit->clear();
    m_view->clearSearch();
    m_searchHits.clear();
    m_searchCache.clear();
    m_searchCurHit = -1;
    m_searchStatus->clear();
}

void MainWindow::searchRun(const QString &query) {
    m_searchHits.clear();
    m_searchCache.clear();
    m_searchCurHit = -1;
    m_view->clearSearch();

    if (query.isEmpty() || !m_pdf->isOpen()) {
        m_searchStatus->clear();
        return;
    }

    for (int pg = 0; pg < m_pdf->pageCount(); ++pg) {
        auto rects = m_pdf->searchText(pg, query);
        if (!rects.isEmpty()) {
            m_searchCache[pg] = rects;
            for (int i = 0; i < rects.size(); ++i)
                m_searchHits << SearchHit{pg, i};
        }
    }

    if (!m_searchHits.isEmpty()) {
        m_searchCurHit = 0;
        const auto &h = m_searchHits[0];
        m_view->setSearchResults(m_searchCache, h.page, h.idx);
        m_view->scrollToPage(h.page);
    }

    m_searchStatus->setText(m_searchHits.isEmpty()
        ? tr("No matches")
        : tr("1 / %1").arg(m_searchHits.size()));
}

void MainWindow::searchNext() {
    if (m_searchHits.isEmpty()) { searchShow(); return; }
    m_searchCurHit = (m_searchCurHit + 1) % m_searchHits.size();
    searchGoTo(m_searchCurHit);
}

void MainWindow::searchPrev() {
    if (m_searchHits.isEmpty()) return;
    m_searchCurHit = (m_searchCurHit - 1 + m_searchHits.size()) % m_searchHits.size();
    searchGoTo(m_searchCurHit);
}

void MainWindow::searchGoTo(int hitIdx) {
    if (hitIdx < 0 || hitIdx >= m_searchHits.size()) return;
    const auto &h = m_searchHits[hitIdx];
    m_view->setSearchResults(m_searchCache, h.page, h.idx);
    m_view->scrollToPage(h.page);
    m_searchStatus->setText(tr("%1 / %2").arg(hitIdx + 1).arg(m_searchHits.size()));
}


// ─────────────────────────────────────────────────────────────
// Insert image from file
// ─────────────────────────────────────────────────────────────

void MainWindow::insertImageFromFile() {
    if (!m_pdf->isOpen()) {
        QMessageBox::information(this, tr("No document"), tr("Open a PDF first."));
        return;
    }
    QString path = QFileDialog::getOpenFileName(
        this, tr("Insert Image"), {},
        tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp);;All Files (*)"));
    if (path.isEmpty()) return;
    QPixmap px(path);
    if (px.isNull()) {
        QMessageBox::warning(this, tr("Error"), tr("Could not load the selected image."));
        return;
    }
    m_view->setSignaturePixmap(px);
    setActiveTool(Tool::Signature);
    m_actSignature->setChecked(true);
    statusBar()->showMessage(tr("Image loaded — click to place, drag to resize"), 3000);
}
