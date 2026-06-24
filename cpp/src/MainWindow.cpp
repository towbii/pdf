#include "MainWindow.h"
#include "SignatureDialog.h"
#include "SignaturePickerDialog.h"
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
        auto *item = new QListWidgetItem("  Keine zuletzt geöffneten Dateien");
        item->setFlags(Qt::NoItemFlags);
        item->setForeground(QColor("#666666"));
        m_recentList->addItem(item);
    }
}

// ─────────────────────────────────────────────────────────────
// Welcome widget
// ─────────────────────────────────────────────────────────────

QWidget *MainWindow::buildWelcomeWidget() {
    auto *w = new QWidget;
    w->setObjectName("welcomeWidget");
    auto *outer = new QVBoxLayout(w);
    outer->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    outer->setContentsMargins(0, 0, 0, 0);

    // ── Center card ──────────────────────────────────────
    auto *card = new QFrame;
    card->setObjectName("welcomeCard");
    card->setFixedWidth(480);
    card->setStyleSheet(
        "QFrame#welcomeCard {"
        "  background: #252526;"
        "  border-radius: 16px;"
        "  border: 1px solid #333333;"
        "}");

    auto *vl = new QVBoxLayout(card);
    vl->setSpacing(0);
    vl->setContentsMargins(40, 40, 40, 40);

    // ── Title ────────────────────────────────────────────
    auto *appTitle = new QLabel("PDF Editor");
    appTitle->setAlignment(Qt::AlignCenter);
    appTitle->setStyleSheet(
        "font-size: 28px; font-weight: 700; color: #FFFFFF; background: transparent;"
        "letter-spacing: -0.5px;");
    vl->addWidget(appTitle);

    auto *appSub = new QLabel("Professioneller PDF-Editor");
    appSub->setAlignment(Qt::AlignCenter);
    appSub->setStyleSheet("font-size: 13px; color: #666666; background: transparent;");
    vl->addSpacing(6);
    vl->addWidget(appSub);
    vl->addSpacing(32);

    // ── Open button ───────────────────────────────────────
    auto *openBtn = new QPushButton("Datei öffnen …");
    openBtn->setFixedHeight(44);
    openBtn->setStyleSheet(
        "QPushButton {"
        "  background: #0078D4; color: white; border: none;"
        "  border-radius: 10px; font-size: 14px; font-weight: 600;"
        "}"
        "QPushButton:hover { background: #1088E4; }"
        "QPushButton:pressed { background: #006CBE; }");
    connect(openBtn, &QPushButton::clicked, this, &MainWindow::openDialog);
    vl->addWidget(openBtn);

    auto *dropHint = new QLabel("oder PDF per Drag & Drop hier ablegen");
    dropHint->setAlignment(Qt::AlignCenter);
    dropHint->setStyleSheet("font-size: 12px; color: #555555; background: transparent;");
    vl->addSpacing(8);
    vl->addWidget(dropHint);
    vl->addSpacing(32);

    // ── Divider ───────────────────────────────────────────
    auto *divider = new QFrame;
    divider->setFixedHeight(1);
    divider->setStyleSheet("background: #333333; border: none;");
    vl->addWidget(divider);
    vl->addSpacing(20);

    // ── Recent files ──────────────────────────────────────
    auto *recentHeader = new QLabel("ZULETZT GEÖFFNET");
    recentHeader->setStyleSheet(
        "font-size: 10px; font-weight: 700; color: #555555; background: transparent;"
        "letter-spacing: 1.5px;");
    vl->addWidget(recentHeader);
    vl->addSpacing(10);

    m_recentList = new QListWidget;
    m_recentList->setFixedHeight(168);
    m_recentList->setStyleSheet(
        "QListWidget { background: #1E1E1E; border: 1px solid #2E2E2E;"
        "  border-radius: 10px; outline: none; padding: 4px; }"
        "QListWidget::item { color: #D4D4D4; border-radius: 7px; padding: 4px 8px; }"
        "QListWidget::item:hover    { background: #2A2D2E; }"
        "QListWidget::item:selected { background: #0C3F6C; color: #FFFFFF; }");
    rebuildRecentList();
    connect(m_recentList, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem *item) {
        QString path = item->data(Qt::UserRole).toString();
        if (!path.isEmpty()) openFile(path);
    });
    vl->addWidget(m_recentList);

    outer->addWidget(card);
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

    // Editor (splitter + thumbs + view)
    m_splitter = new QSplitter(Qt::Horizontal);
    m_splitter->setHandleWidth(1);
    m_thumbs = new ThumbnailPanel;
    m_thumbs->setObjectName("thumbPanel");
    m_splitter->addWidget(m_thumbs);
    m_view = new PdfView(m_pdf);
    m_splitter->addWidget(m_view);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({150, 1});
    m_stack->addWidget(m_splitter);     // index 1

    setCentralWidget(m_stack);
    m_stack->setCurrentIndex(0);

    buildMenus();
    buildToolbar();
    buildStatusBar();

    connect(m_thumbs, &ThumbnailPanel::pageClicked, m_view, &PdfView::scrollToPage);
    connect(m_view,   &PdfView::modified, this, &MainWindow::onModified);
    connect(m_view,   &PdfView::pageChanged, this, [this](int pg) {
        m_statusPage->setText(tr("Seite %1 / %2").arg(pg+1).arg(m_pdf->pageCount()));
        m_thumbs->setCurrentPage(pg);
    });
    connect(m_view,   &PdfView::zoomChanged, this, [this](float z) {
        m_statusZoom->setText(QString("%1%").arg(qRound(z * 100)));
        m_zoomSlider->blockSignals(true);
        m_zoomSlider->setValue(qRound(z * 100));
        m_zoomSlider->blockSignals(false);
    });
    connect(m_view,   &PdfView::selectionChanged, this, [this](const QString &t) {
        if (!t.isEmpty())
            statusBar()->showMessage(tr("Kopiert: \"%1\"").arg(t.left(40)), 2000);
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

    // ── Datei ──
    auto *mFile = mb->addMenu(tr("&Datei"));
    {
        auto *a = makeAct("file.open", tr("Öffnen …"), tr("PDF öffnen"), "Ctrl+O", tr("Datei"));
        connect(a, &QAction::triggered, this, &MainWindow::openDialog);
        mFile->addAction(a);
    }
    {
        auto *a = makeAct("file.save", tr("Speichern"), tr("PDF speichern"), "Ctrl+S", tr("Datei"));
        connect(a, &QAction::triggered, this, &MainWindow::saveFile);
        mFile->addAction(a);
    }
    {
        auto *a = makeAct("file.saveas", tr("Speichern unter …"), tr("Speichern unter"), "Ctrl+Shift+S", tr("Datei"));
        connect(a, &QAction::triggered, this, &MainWindow::saveAs);
        mFile->addAction(a);
    }
    mFile->addSeparator();
    // Recent files submenu
    auto *mRecent = mFile->addMenu(tr("Zuletzt geöffnet"));
    connect(mRecent, &QMenu::aboutToShow, this, [this, mRecent]() {
        mRecent->clear();
        const QStringList files = recentFiles();
        if (files.isEmpty()) {
            mRecent->addAction(tr("(leer)"))->setEnabled(false);
        } else {
            for (const QString &f : files) {
                QFileInfo fi(f);
                auto *a = mRecent->addAction(fi.fileName());
                a->setToolTip(f);
                connect(a, &QAction::triggered, this, [this, f]() { openFile(f); });
            }
            mRecent->addSeparator();
            auto *clearAct = mRecent->addAction(tr("Liste leeren"));
            connect(clearAct, &QAction::triggered, this, [this]() {
                QSettings s("PDFEditor", "PDFEditor");
                s.remove("recentFiles");
                rebuildRecentList();
            });
        }
    });
    mFile->addSeparator();
    {
        auto *a = makeAct("file.close", tr("Schließen"), tr("Datei schließen"), "Ctrl+W", tr("Datei"));
        connect(a, &QAction::triggered, this, &MainWindow::closeFile);
        mFile->addAction(a);
    }
    mFile->addSeparator();
    {
        auto *a = new QAction(tr("Beenden"), this);
        a->setShortcut(QKeySequence("Alt+F4"));
        connect(a, &QAction::triggered, this, &QMainWindow::close);
        mFile->addAction(a);
    }

    // ── Bearbeiten ──
    auto *mEdit = mb->addMenu(tr("&Bearbeiten"));
    {
        m_actUndo = makeAct("edit.undo", tr("Rückgängig"), tr("Schritt zurück"), "Ctrl+Z", tr("Bearbeiten"));
        connect(m_actUndo, &QAction::triggered, this, &MainWindow::undo);
        mEdit->addAction(m_actUndo);
    }
    {
        m_actRedo = makeAct("edit.redo", tr("Wiederholen"), tr("Schritt voraus"), "Ctrl+Y", tr("Bearbeiten"));
        connect(m_actRedo, &QAction::triggered, this, &MainWindow::redo);
        mEdit->addAction(m_actRedo);
    }

    // ── Ansicht ──
    auto *mView = mb->addMenu(tr("&Ansicht"));
    {
        auto *a = makeAct("view.zoomin", tr("Vergrößern"), tr("Vergrößern"), "Ctrl++", tr("Ansicht"));
        connect(a, &QAction::triggered, this, &MainWindow::zoomIn);
        mView->addAction(a);
    }
    {
        auto *a = makeAct("view.zoomout", tr("Verkleinern"), tr("Verkleinern"), "Ctrl+-", tr("Ansicht"));
        connect(a, &QAction::triggered, this, &MainWindow::zoomOut);
        mView->addAction(a);
    }
    {
        auto *a = makeAct("view.zoomfit", tr("Auf Fenster anpassen"), tr("Anpassen"), "Ctrl+0", tr("Ansicht"));
        connect(a, &QAction::triggered, this, &MainWindow::zoomFit);
        mView->addAction(a);
    }
    mView->addSeparator();
    {
        auto *a = makeAct("view.thumbs", tr("Seitenleiste"), tr("Seitenleiste ein/aus"), "Ctrl+B");
        a->setCheckable(true); a->setChecked(true);
        connect(a, &QAction::toggled, this, &MainWindow::toggleThumbs);
        mView->addAction(a);
    }
    mView->addSeparator();
    {
        auto *a = new QAction(tr("Dunkles Design"), this);
        a->setCheckable(true); a->setChecked(m_darkMode);
        connect(a, &QAction::toggled, this, [this](bool on) {
            m_darkMode = on; applyTheme();
        });
        mView->addAction(a);
    }

    // ── Seite ──
    auto *mPage = mb->addMenu(tr("&Seite"));
    {
        auto *a = makeAct("page.rotleft", tr("Links drehen"), tr("90° links"), "Ctrl+[", tr("Seite"));
        connect(a, &QAction::triggered, this, &MainWindow::rotateLeft);
        mPage->addAction(a);
    }
    {
        auto *a = makeAct("page.rotright", tr("Rechts drehen"), tr("90° rechts"), "Ctrl+]", tr("Seite"));
        connect(a, &QAction::triggered, this, &MainWindow::rotateRight);
        mPage->addAction(a);
    }
    mPage->addSeparator();
    {
        auto *a = makeAct("page.delete", tr("Seite löschen"), tr("Seite löschen"), "", tr("Seite"));
        connect(a, &QAction::triggered, this, &MainWindow::deletePage);
        mPage->addAction(a);
    }
    {
        auto *a = makeAct("page.dup", tr("Seite duplizieren"), tr("Seite duplizieren"), "", tr("Seite"));
        connect(a, &QAction::triggered, this, &MainWindow::duplicatePage);
        mPage->addAction(a);
    }
    mPage->addSeparator();
    {
        auto *a = makeAct("page.form", tr("Formular ausfüllen …"), tr("Formular"), "Ctrl+F", tr("Seite"));
        connect(a, &QAction::triggered, this, &MainWindow::openFormFiller);
        mPage->addAction(a);
    }

    // ── Einstellungen ──
    auto *mSettings = mb->addMenu(tr("&Einstellungen"));
    {
        auto *a = new QAction(tr("Einstellungen …"), this);
        connect(a, &QAction::triggered, this, &MainWindow::openSettings);
        mSettings->addAction(a);
    }
    {
        auto *a = new QAction(tr("Tastenkürzel …"), this);
        connect(a, &QAction::triggered, this, &MainWindow::openKeybinds);
        mSettings->addAction(a);
    }
    mSettings->addSeparator();
    {
        auto *a = new QAction(tr("Als Standard-PDF-Viewer registrieren …"), this);
        connect(a, &QAction::triggered, this, &MainWindow::registerAsDefaultPdf);
        mSettings->addAction(a);
    }

    // ── Hilfe ──
    auto *mHelp = mb->addMenu(tr("&Hilfe"));
    {
        auto *a = new QAction(tr("Über PDF Editor …"), this);
        connect(a, &QAction::triggered, this, &MainWindow::showAbout);
        mHelp->addAction(a);
    }
}

void MainWindow::buildToolbar() {
    // ── File toolbar ──
    auto *tbFile = addToolBar(tr("Datei"));
    tbFile->setObjectName("tbFile");
    tbFile->setMovable(false);
    tbFile->setIconSize(QSize(18, 18));
    tbFile->setToolButtonStyle(Qt::ToolButtonTextOnly);

    auto *openBtn = new QToolButton;
    openBtn->setText("  " + tr("Öffnen"));
    openBtn->setToolTip(tr("PDF öffnen  Ctrl+O"));
    openBtn->setMinimumHeight(32);
    openBtn->setStyleSheet(
        "QToolButton { font-weight: 600; padding: 5px 16px; }"
        "QToolButton:hover { background: #3A3A3A; border-radius: 6px; }");
    connect(openBtn, &QToolButton::clicked, this, &MainWindow::openDialog);
    tbFile->addWidget(openBtn);

    auto *saveBtn = new QToolButton;
    saveBtn->setText("  " + tr("Speichern"));
    saveBtn->setToolTip(tr("Speichern  Ctrl+S"));
    saveBtn->setMinimumHeight(32);
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
    auto *tbTools = addToolBar(tr("Werkzeuge"));
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

    m_actSelect    = addTool(tr("Auswahl"),      tr("Text auswählen & kopieren  [S]"),  Tool::Select);
    m_actHighlight = addTool(tr("Markieren"),    tr("Text markieren mit Farbe  [H]"),    Tool::Highlight);
    m_actPen       = addTool(tr("Stift"),        tr("Freihand zeichnen  [P]"),           Tool::Pen);
    m_actEraser    = addTool(tr("Radierer"),     tr("Anmerkung löschen  [E]"),           Tool::Eraser);
    m_actText      = addTool(tr("Text"),         tr("Text einfügen / bearbeiten  [T]"),  Tool::Text);

    m_actSignature = new QAction(tr("Unterschrift"), this);
    m_actSignature->setCheckable(true);
    m_actSignature->setToolTip(tr("Unterschrift einfügen  [U]"));
    toolGroup->addAction(m_actSignature);
    tbTools->addAction(m_actSignature);
    connect(m_actSignature, &QAction::triggered, this, &MainWindow::openSignatureDialog);

    m_actSelect->setChecked(true);

    tbTools->addSeparator();

    auto *zoomLabel = new QLabel("  Zoom:");
    zoomLabel->setStyleSheet("color: #858585; font-size: 12px; background: transparent;");
    tbTools->addWidget(zoomLabel);

    m_zoomSlider = new QSlider(Qt::Horizontal);
    m_zoomSlider->setRange(25, 400);
    m_zoomSlider->setValue(150);
    m_zoomSlider->setFixedWidth(130);
    m_zoomSlider->setToolTip(tr("Zoom ändern (Ctrl+Mausrad)"));
    connect(m_zoomSlider, &QSlider::valueChanged, this, [this](int v) {
        m_view->setZoom(v / 100.f);
    });
    tbTools->addWidget(m_zoomSlider);

    tbTools->addSeparator();

    auto *penColorBtn = new QToolButton;
    penColorBtn->setText(tr("Stiftfarbe"));
    penColorBtn->setToolTip(tr("Stiftfarbe wählen"));
    connect(penColorBtn, &QToolButton::clicked, this, [=]() {
        QColor c = QColorDialog::getColor(Qt::black, this, tr("Stiftfarbe"));
        if (c.isValid()) m_view->setPenColor(c);
    });
    tbTools->addWidget(penColorBtn);

    auto *hlColorBtn = new QToolButton;
    hlColorBtn->setText(tr("Markierfarbe"));
    hlColorBtn->setToolTip(tr("Markierungsfarbe wählen"));
    connect(hlColorBtn, &QToolButton::clicked, this, [=]() {
        QColor c = QColorDialog::getColor(Qt::yellow, this, tr("Markierfarbe"));
        if (c.isValid()) m_view->setHighlightColor(c);
    });
    tbTools->addWidget(hlColorBtn);
}

void MainWindow::buildStatusBar() {
    m_statusFile = new QLabel;
    m_statusPage = new QLabel;
    m_statusZoom = new QLabel("140%");
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
    // Welcome widget needs explicit background since it's a child of QStackedWidget
    m_welcomeWidget->setStyleSheet(
        "QWidget#welcomeWidget { background: #1E1E1E; }");
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
        QMessageBox::warning(this, tr("Fehler"),
            tr("Konnte Datei nicht öffnen:\n%1").arg(path));
        return;
    }
    m_view->setDocument(m_pdf);
    m_thumbs->setDocument(m_pdf);
    m_modified = false;
    m_statusFile->setText(QFileInfo(path).fileName());
    m_statusPage->setText(tr("Seite 1 / %1").arg(m_pdf->pageCount()));
    m_statusZoom->setText("140%");
    updateTitle();
    updateUndoState();
    addRecentFile(path);
    showEditor();
}

void MainWindow::openDialog() {
    if (!confirmDiscard()) return;
    QString path = QFileDialog::getOpenFileName(
        this, tr("PDF öffnen"), {},
        tr("PDF-Dateien (*.pdf);;Alle Dateien (*)"));
    if (!path.isEmpty()) openFile(path);
}

void MainWindow::saveFile() {
    if (!m_pdf->isOpen()) return;
    if (m_pdf->filePath().isEmpty()) { saveAs(); return; }
    if (m_pdf->save()) {
        setModified(false);
        statusBar()->showMessage(tr("Gespeichert"), 2000);
    } else {
        QMessageBox::warning(this, tr("Fehler"), tr("Speichern fehlgeschlagen."));
    }
}

void MainWindow::saveAs() {
    if (!m_pdf->isOpen()) return;
    QString path = QFileDialog::getSaveFileName(
        this, tr("Speichern unter"), m_pdf->filePath(),
        tr("PDF-Dateien (*.pdf)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(".pdf", Qt::CaseInsensitive)) path += ".pdf";
    if (m_pdf->save(path)) {
        setModified(false);
        m_statusFile->setText(QFileInfo(path).fileName());
        updateTitle();
        statusBar()->showMessage(tr("Gespeichert"), 2000);
    } else {
        QMessageBox::warning(this, tr("Fehler"), tr("Speichern fehlgeschlagen."));
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
    mb.setWindowTitle(tr("Ungespeicherte Änderungen"));
    mb.setText(tr("Die Datei hat ungespeicherte Änderungen."));
    mb.setInformativeText(tr("Möchten Sie die Änderungen speichern?"));
    mb.setIcon(QMessageBox::Question);
    auto *btnSave    = mb.addButton(tr("Speichern"),      QMessageBox::AcceptRole);
    auto *btnDiscard = mb.addButton(tr("Nicht speichern"),QMessageBox::DestructiveRole);
    auto *btnCancel  = mb.addButton(tr("Abbrechen"),      QMessageBox::RejectRole);
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
    auto ret = QMessageBox::question(this, tr("Seite löschen"),
        tr("Seite %1 wirklich löschen?").arg(pg+1));
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
void MainWindow::insertImageToPage() {
    if (!m_pdf->isOpen()) return;
    QString path = QFileDialog::getOpenFileName(
        this, tr("Bild wählen"), {},
        tr("Bilder (*.png *.jpg *.jpeg *.bmp);;Alle Dateien (*)"));
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
    if (!m_pdf->isOpen()) return;
    auto fields = m_pdf->getFormFields();
    QMessageBox::information(this, tr("Formular"),
        fields.isEmpty()
            ? tr("Dieses Dokument enthält keine ausfüllbaren Felder.")
            : tr("Formular enthält %1 Feld(er).").arg(fields.size()));
}

void MainWindow::openSignatureDialog() {
    SignaturePickerDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted && !dlg.selectedPath().isEmpty()) {
        m_view->setSignaturePath(dlg.selectedPath());
        setActiveTool(Tool::Signature);
        m_actSignature->setChecked(true);
        statusBar()->showMessage(tr("Unterschrift: auf Seite klicken zum Einfügen"), 3000);
    }
}

void MainWindow::openKeybinds() {
    KeybindDialog dlg(m_actionDefs, m_actMap, this);
    dlg.exec();
}

void MainWindow::openSettings() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Einstellungen"));
    dlg.setMinimumWidth(480);

    auto *lay = new QVBoxLayout(&dlg);
    lay->setSpacing(16);
    lay->setContentsMargins(20, 20, 20, 20);

    QSettings s("PDFEditor", "PDFEditor");

    auto makeHeader = [](const QString &t) {
        auto *l = new QLabel(t);
        l->setStyleSheet("font-weight: 700; font-size: 13px; color: #CCCCCC;");
        return l;
    };
    auto makeSep = []() {
        auto *f = new QFrame;
        f->setFrameShape(QFrame::HLine);
        f->setStyleSheet("color: #3C3C3C;");
        return f;
    };

    // ── Allgemein ──
    lay->addWidget(makeHeader(tr("Allgemein")));

    auto *startupRow = new QHBoxLayout;
    startupRow->addWidget(new QLabel(tr("Beim Start:")));
    auto *startupCombo = new QComboBox;
    startupCombo->addItem(tr("Willkommensbildschirm"), "welcome");
    startupCombo->addItem(tr("Letzte Datei öffnen"),   "lastfile");
    startupCombo->addItem(tr("Leerer Editor"),          "empty");
    QString startupVal = s.value("startup/mode", "welcome").toString();
    startupCombo->setCurrentIndex(startupCombo->findData(startupVal));
    startupRow->addWidget(startupCombo, 1);
    lay->addLayout(startupRow);

    auto *zoomRow = new QHBoxLayout;
    zoomRow->addWidget(new QLabel(tr("Standard-Zoom:")));
    auto *zoomCombo = new QComboBox;
    for (int v : {50, 75, 100, 125, 150, 175, 200})
        zoomCombo->addItem(QString("%1%").arg(v), v);
    int zoomVal = s.value("view/defaultZoom", 150).toInt();
    int zi = zoomCombo->findData(zoomVal);
    zoomCombo->setCurrentIndex(zi >= 0 ? zi : 4);
    zoomRow->addWidget(zoomCombo, 1);
    lay->addLayout(zoomRow);

    lay->addWidget(makeSep());

    // ── Ansicht ──
    lay->addWidget(makeHeader(tr("Ansicht")));

    auto *spacingRow = new QHBoxLayout;
    spacingRow->addWidget(new QLabel(tr("Seitenabstand (px):")));
    auto *spacingSpin = new QSpinBox;
    spacingSpin->setRange(4, 48);
    spacingSpin->setValue(s.value("view/pageSpacing", 16).toInt());
    spacingRow->addWidget(spacingSpin, 1);
    lay->addLayout(spacingRow);

    auto *recentRow = new QHBoxLayout;
    recentRow->addWidget(new QLabel(tr("Zuletzt geöffnet (max.):")));
    auto *recentSpin = new QSpinBox;
    recentSpin->setRange(3, 20);
    recentSpin->setValue(s.value("recentFiles/max", 10).toInt());
    recentRow->addWidget(recentSpin, 1);
    lay->addLayout(recentRow);

    lay->addWidget(makeSep());

    // ── Sprache ──
    lay->addWidget(makeHeader(tr("Sprache")));
    auto *langRow = new QHBoxLayout;
    langRow->addWidget(new QLabel(tr("Sprache:")));
    auto *langCombo = new QComboBox;
    langCombo->addItem("Deutsch",  "de");
    langCombo->addItem("English",  "en");
    QString lang = s.value("ui/language", "de").toString();
    langCombo->setCurrentIndex(langCombo->findData(lang));
    langRow->addWidget(langCombo, 1);
    lay->addLayout(langRow);

    auto *langHint = new QLabel(tr("* Sprachänderung wird nach Neustart übernommen."));
    langHint->setStyleSheet("color: #666666; font-size: 11px;");
    lay->addWidget(langHint);

    lay->addWidget(makeSep());

    // ── Unterschrift ──
    lay->addWidget(makeHeader(tr("Unterschrift")));
    auto *sigRow = new QHBoxLayout;
    sigRow->addWidget(new QLabel(tr("Gespeicherte Unterschrift:")));
    QString appDir2 = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString savedSig = appDir2 + "/saved_signature.png";
    auto *sigStatus = new QLabel(QFile::exists(savedSig) ? tr("Vorhanden") : tr("Keine"));
    sigStatus->setStyleSheet(QFile::exists(savedSig) ?
        "color: #4CAF50;" : "color: #666666;");
    sigRow->addWidget(sigStatus, 1);
    auto *clearSigBtn = new QPushButton(tr("Löschen"));
    clearSigBtn->setEnabled(QFile::exists(savedSig));
    connect(clearSigBtn, &QPushButton::clicked, &dlg, [=]() mutable {
        QFile::remove(savedSig);
        m_view->setSignaturePath({});
        sigStatus->setText(tr("Keine"));
        sigStatus->setStyleSheet("color: #666666;");
        clearSigBtn->setEnabled(false);
    });
    sigRow->addWidget(clearSigBtn);
    lay->addLayout(sigRow);

    lay->addStretch();

    // ── Buttons ──
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText(tr("Übernehmen"));
    bb->button(QDialogButtonBox::Cancel)->setText(tr("Abbrechen"));
    connect(bb, &QDialogButtonBox::accepted, &dlg, [&]() {
        QString oldLang = s.value("ui/language", "de").toString();
        QString newLang = langCombo->currentData().toString();
        s.setValue("startup/mode",      startupCombo->currentData().toString());
        s.setValue("view/defaultZoom",  zoomCombo->currentData().toInt());
        s.setValue("view/pageSpacing",  spacingSpin->value());
        s.setValue("recentFiles/max",   recentSpin->value());
        s.setValue("ui/language",       newLang);
        dlg.accept();

        if (newLang != oldLang) {
            auto res = QMessageBox::question(this,
                tr("Sprache geändert"),
                tr("Die Sprache wurde geändert. Jetzt neu starten?"),
                QMessageBox::Yes | QMessageBox::No);
            if (res == QMessageBox::Yes) {
                // Pass currently open file so it reopens after restart
                QStringList args;
                if (m_pdf && m_pdf->isOpen() && !m_pdf->filePath().isEmpty())
                    args << m_pdf->filePath();
                QProcess::startDetached(QCoreApplication::applicationFilePath(), args);
                QApplication::quit();
            }
        }
    });
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(bb);

    dlg.exec();
}

void MainWindow::registerAsDefaultPdf() {
#ifdef Q_OS_WIN
    QString exePath = QCoreApplication::applicationFilePath().replace('/', '\\');
    QSettings reg("HKEY_CURRENT_USER\\Software\\Classes", QSettings::NativeFormat);

    // Register ProgID
    reg.setValue("PDFEditor.Document/Default", "PDF Editor Dokument");
    reg.setValue("PDFEditor.Document/shell/open/command/Default",
                 QString("\"%1\" \"%2\"").arg(exePath, "%1"));

    // Register as capable application
    QSettings cap("HKEY_CURRENT_USER\\Software\\PDFEditor", QSettings::NativeFormat);
    cap.setValue("ApplicationName", "PDF Editor");
    cap.setValue("ApplicationDescription", "Professioneller PDF-Editor");

    QSettings regApps("HKEY_CURRENT_USER\\Software\\RegisteredApplications", QSettings::NativeFormat);
    regApps.setValue("PDFEditor", "Software\\PDFEditor\\Capabilities");

    QSettings capExt("HKEY_CURRENT_USER\\Software\\PDFEditor\\Capabilities\\FileAssociations", QSettings::NativeFormat);
    capExt.setValue(".pdf", "PDFEditor.Document");

    QMessageBox::information(this, tr("Standard-Viewer"),
        tr("PDF Editor wurde als fähige Anwendung registriert.\n\n"
           "Um PDF Editor als Standard festzulegen:\n"
           "Windows-Einstellungen → Apps → Standard-Apps → PDF Editor auswählen\n\n"
           "oder: Windows-Einstellungen → Standard-Apps → .pdf → PDF Editor"));
#else
    QMessageBox::information(this, tr("Standard-Viewer"),
        tr("Diese Funktion ist nur unter Windows verfügbar."));
#endif
}

void MainWindow::toggleThumbs() {
    m_thumbs->setVisible(!m_thumbs->isVisible());
}

void MainWindow::showAbout() {
    auto *dlg = new QDialog(this);
    dlg->setWindowTitle(tr("Über PDF Editor"));
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
    auto *hVer = new QLabel("Version 1.0.0");
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

    addRow(tr("Build-Datum"), QString(__DATE__));
    addRow(tr("Qt-Version"), QT_VERSION_STR);
    addRow(tr("MuPDF-Version"), FZ_VERSION);

    vl->addWidget(content);

    // Button
    auto *btnBox = new QWidget;
    btnBox->setStyleSheet("background: #252526; border-top: 1px solid #3C3C3C;");
    auto *bl = new QHBoxLayout(btnBox);
    bl->setContentsMargins(16, 12, 16, 12);
    bl->addStretch();
    auto *closeBtn = new QPushButton(tr("Schließen"));
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

void MainWindow::onModified() { setModified(true); }

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
    if (!urls.isEmpty()) openFile(urls.first().toLocalFile());
}
