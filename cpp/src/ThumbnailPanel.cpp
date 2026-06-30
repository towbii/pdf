#include "ThumbnailPanel.h"
#include <QPainter>
#include <QMouseEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QDrag>
#include <QContextMenuEvent>
#include <QMenu>
#include <QTimer>
#include <QScrollBar>
#include <QApplication>
#include <QFrame>

static constexpr int THUMB_W      = 110;
static constexpr int THUMB_PAD    = 8;
static constexpr int THUMB_MARGIN = 4;

// ═══════════════════════════════════════════════════════════
// ThumbnailItem
// ═══════════════════════════════════════════════════════════

ThumbnailItem::ThumbnailItem(int pageNum, QWidget *parent)
    : QWidget(parent), m_pageNum(pageNum) {
    auto *lay = new QVBoxLayout(this);
    lay->setSpacing(3);
    lay->setContentsMargins(THUMB_PAD, THUMB_PAD, THUMB_PAD, THUMB_PAD);

    m_img = new QLabel;
    m_img->setAlignment(Qt::AlignCenter);
    m_img->setFixedWidth(THUMB_W);
    m_img->setMinimumHeight(80);
    lay->addWidget(m_img, 0, Qt::AlignHCenter);

    m_lbl = new QLabel(QString::number(pageNum + 1));
    m_lbl->setAlignment(Qt::AlignCenter);
    m_lbl->setStyleSheet("font-size: 10px; color: #8e8e93;");
    lay->addWidget(m_lbl);

    setFixedWidth(THUMB_W + THUMB_PAD * 2);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover);
}

void ThumbnailItem::setPixmap(const QPixmap &px) {
    m_pixmap = px;
    m_img->setPixmap(px.scaledToWidth(THUMB_W, Qt::SmoothTransformation));
    m_img->setFixedHeight(m_img->pixmap().height());
    adjustSize();
    update();
}

void ThumbnailItem::setSelected(bool s) {
    m_selected = s;
    setStyleSheet(s ? "background: #0a84ff; border-radius: 8px;"
                    : "background: transparent; border-radius: 8px;");
    update();
}

void ThumbnailItem::paintEvent(QPaintEvent *ev) {
    QWidget::paintEvent(ev);
    if (!m_pixmap.isNull()) {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QRect imgRect = m_img->geometry();
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 30));
        p.drawRoundedRect(imgRect.adjusted(2, 2, 3, 3), 3, 3);
    }
}

void ThumbnailItem::mousePressEvent(QMouseEvent *ev) {
    if (ev->button() == Qt::LeftButton) {
        m_dragStart   = ev->pos();
        m_dragStarted = false;
        emit clicked(m_pageNum);
    }
}

void ThumbnailItem::mouseMoveEvent(QMouseEvent *ev) {
    if (!(ev->buttons() & Qt::LeftButton)) return;
    if (m_dragStarted) return;
    if ((ev->pos() - m_dragStart).manhattanLength() < QApplication::startDragDistance())
        return;

    m_dragStarted = true;

    // parent chain: ThumbnailItem → m_container → viewport() → ThumbnailPanel
    auto *panel = qobject_cast<ThumbnailPanel*>(
        parent() && parent()->parent() ? parent()->parent()->parent() : nullptr);
    if (!panel) return;

    panel->beginDrag(m_pageNum, mapToGlobal(ev->pos()));
}

void ThumbnailItem::mouseReleaseEvent(QMouseEvent *) {
    m_dragStarted = false;
}

void ThumbnailItem::contextMenuEvent(QContextMenuEvent *ev) {
    emit clicked(m_pageNum);
    QMenu menu(this);
    QAction *actDup = menu.addAction(tr("Duplicate Page"));
    QAction *actIns = menu.addAction(tr("Insert Blank Page"));
    menu.addSeparator();
    QAction *actDel = menu.addAction(tr("Delete Page"));
    QAction *chosen = menu.exec(ev->globalPos());
    if (chosen == actDel)  emit deleteRequested(m_pageNum);
    if (chosen == actDup)  emit duplicateRequested(m_pageNum);
    if (chosen == actIns)  emit insertRequested(m_pageNum);
}

// ═══════════════════════════════════════════════════════════
// ThumbnailPanel
// ═══════════════════════════════════════════════════════════

ThumbnailPanel::ThumbnailPanel(QWidget *parent) : QScrollArea(parent) {
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setFixedWidth(THUMB_W + THUMB_PAD * 2 + THUMB_MARGIN * 2
                  + verticalScrollBar()->sizeHint().width() + 4);

    m_container = new QWidget;
    m_vlay = new QVBoxLayout(m_container);
    m_vlay->setSpacing(6);
    m_vlay->setContentsMargins(THUMB_MARGIN, THUMB_MARGIN, THUMB_MARGIN, THUMB_MARGIN);
    m_vlay->addStretch();
    setWidget(m_container);

    // Drop-indicator line (hidden by default)
    m_dropLine = new QFrame(m_container);
    m_dropLine->setFixedHeight(3);
    m_dropLine->setStyleSheet("background: #0a84ff; border-radius: 1px;");
    m_dropLine->hide();

    // Handle mouse events on the viewport for drag tracking
    viewport()->installEventFilter(this);

    // Timer-based auto-scroll during drag (fires every 20ms while near edge)
    m_scrollTimer = new QTimer(this);
    m_scrollTimer->setInterval(20);
    connect(m_scrollTimer, &QTimer::timeout, this, [this]() {
        verticalScrollBar()->setValue(verticalScrollBar()->value() + m_scrollDir * 10);
        updateDragIndicator(dropPosAt(m_dragMousePos));
    });
}

void ThumbnailPanel::setDocument(PdfDocument *doc) {
    m_doc = doc;
    buildItems();
}

void ThumbnailPanel::buildItems() {
    for (auto *it : m_items) it->deleteLater();
    m_items.clear();

    if (!m_doc || !m_doc->isOpen()) return;

    // Remove the stretch before adding items
    QLayoutItem *stretch = m_vlay->takeAt(m_vlay->count() - 1);
    delete stretch;

    int n = m_doc->pageCount();
    m_items.reserve(n);
    for (int i = 0; i < n; ++i) {
        auto *item = new ThumbnailItem(i, m_container);
        m_vlay->addWidget(item, 0, Qt::AlignHCenter);
        m_items.append(item);
        connect(item, &ThumbnailItem::clicked,            this, &ThumbnailPanel::pageClicked);
        connect(item, &ThumbnailItem::deleteRequested,    this, &ThumbnailPanel::pageDeleteRequested);
        connect(item, &ThumbnailItem::duplicateRequested, this, &ThumbnailPanel::pageDuplicateRequested);
        connect(item, &ThumbnailItem::insertRequested,    this, &ThumbnailPanel::pageInsertRequested);
    }
    m_vlay->addStretch();

    m_pumpIdx = 0;
    pumpNext();
}

void ThumbnailPanel::pumpNext() {
    if (!m_doc || m_pumpIdx >= m_items.size()) return;
    QPixmap px = m_doc->renderThumbnail(m_pumpIdx, THUMB_W);
    if (!px.isNull()) m_items[m_pumpIdx]->setPixmap(px);
    m_pumpIdx++;
    if (m_pumpIdx < m_items.size())
        QTimer::singleShot(15, this, &ThumbnailPanel::pumpNext);
}

void ThumbnailPanel::setCurrentPage(int pageNum) {
    for (int i = 0; i < m_items.size(); ++i)
        m_items[i]->setSelected(i == pageNum);
    if (pageNum >= 0 && pageNum < m_items.size())
        ensureWidgetVisible(m_items[pageNum]);
}

void ThumbnailPanel::refreshAll() {
    m_pumpIdx = 0;
    pumpNext();
}

void ThumbnailPanel::refreshPage(int pageNum) {
    if (!m_doc || pageNum < 0 || pageNum >= m_items.size()) return;
    QPixmap px = m_doc->renderThumbnail(pageNum, THUMB_W);
    if (!px.isNull()) m_items[pageNum]->setPixmap(px);
}

// Mouse-click on the empty area below thumbnails adds a new page
void ThumbnailPanel::mousePressEvent(QMouseEvent *ev) {
    if (ev->button() != Qt::LeftButton) { QScrollArea::mousePressEvent(ev); return; }
    // Check if click is in the stretch area below all items
    if (m_items.isEmpty()) {
        emit pageInsertRequested(-1);
        return;
    }
    ThumbnailItem *last = m_items.last();
    int lastBottom = last->mapTo(m_container, QPoint(0, last->height())).y();
    QPoint inContainer = m_container->mapFrom(this, ev->pos());
    if (inContainer.y() > lastBottom + 10) {
        emit pageInsertRequested(m_items.size() - 1);
    }
    QScrollArea::mousePressEvent(ev);
}

// ── Drag handling ────────────────────────────────────────

int ThumbnailPanel::dropPosAt(QPoint viewportPt) const {
    QPoint c = m_container->mapFrom(viewport(), viewportPt);
    for (int i = 0; i < m_items.size(); ++i)
        if (c.y() < m_items[i]->geometry().center().y()) return i;
    return m_items.size();
}

void ThumbnailPanel::beginDrag(int fromPage, QPoint /*globalPos*/) {
    if (m_items.isEmpty() || fromPage < 0 || fromPage >= m_items.size()) return;
    m_dragFrom         = fromPage;
    m_dropIndicatorPos = -1;
    m_dragMousePos     = {};
    m_scrollDir        = 0;
    m_items[fromPage]->setStyleSheet(
        "background: #1a1a1a; border-radius: 8px;");
    viewport()->grabMouse();
    setCursor(Qt::ClosedHandCursor);
}

void ThumbnailPanel::updateDragIndicator(int dropPos) {
    m_dropIndicatorPos = dropPos;
    if (m_items.isEmpty()) { m_dropLine->hide(); return; }

    int lineY;
    if (dropPos < m_items.size())
        lineY = m_items[dropPos]->pos().y() - 3;
    else
        lineY = m_items.last()->pos().y() + m_items.last()->height() + 1;

    m_dropLine->setGeometry(THUMB_MARGIN, lineY,
                            m_container->width() - THUMB_MARGIN * 2, 3);
    m_dropLine->show();
    m_dropLine->raise();
}

void ThumbnailPanel::endDrag(int dropPos) {
    m_scrollTimer->stop();
    m_scrollDir = 0;
    m_dropLine->hide();
    viewport()->releaseMouse();
    unsetCursor();

    if (m_dragFrom >= 0 && m_dragFrom < m_items.size())
        m_items[m_dragFrom]->setStyleSheet("background: transparent; border-radius: 8px;");

    int from = m_dragFrom;
    m_dragFrom = -1;
    m_dropIndicatorPos = -1;

    if (from >= 0 && dropPos >= 0 && dropPos != from && dropPos != from + 1) {
        emit pageMoveRequested(from, dropPos);
    }
}

bool ThumbnailPanel::eventFilter(QObject *obj, QEvent *ev) {
    if (obj == viewport()) {
        // Empty-area left-click below last thumbnail → add page
        if (ev->type() == QEvent::MouseButtonPress && m_dragFrom < 0) {
            auto *me = static_cast<QMouseEvent*>(ev);
            if (me->button() == Qt::LeftButton) {
                if (m_items.isEmpty()) {
                    emit pageInsertRequested(-1);
                    return true;
                }
                ThumbnailItem *last = m_items.last();
                int lastBottom = last->mapTo(m_container, QPoint(0, last->height())).y();
                QPoint inContainer = m_container->mapFrom(viewport(), me->pos());
                if (inContainer.y() > lastBottom + 10) {
                    emit pageInsertRequested(m_items.size() - 1);
                    return true;
                }
            }
        }
        // Empty-area right-click → context menu to insert a blank page
        if (ev->type() == QEvent::ContextMenu) {
            auto *ce = static_cast<QContextMenuEvent*>(ev);
            bool inEmpty = m_items.isEmpty();
            if (!inEmpty) {
                ThumbnailItem *last = m_items.last();
                int lastBottom = last->mapTo(m_container, QPoint(0, last->height())).y();
                QPoint inContainer = m_container->mapFrom(viewport(), ce->pos());
                inEmpty = (inContainer.y() > lastBottom + 10);
            }
            if (inEmpty) {
                QMenu menu(this);
                QAction *actIns = menu.addAction(tr("Insert Blank Page"));
                if (menu.exec(ce->globalPos()) == actIns)
                    emit pageInsertRequested(m_items.isEmpty() ? -1 : m_items.size() - 1);
                return true;
            }
        }
    }
    if (obj == viewport() && m_dragFrom >= 0) {
        if (ev->type() == QEvent::MouseMove) {
            auto *me = static_cast<QMouseEvent*>(ev);
            m_dragMousePos = me->pos();

            // Timer-based auto-scroll near edges
            const int ZONE = 40;
            int y = me->pos().y();
            if (y < ZONE) {
                m_scrollDir = -1;
                if (!m_scrollTimer->isActive()) m_scrollTimer->start();
            } else if (y > viewport()->height() - ZONE) {
                m_scrollDir = 1;
                if (!m_scrollTimer->isActive()) m_scrollTimer->start();
            } else {
                m_scrollDir = 0;
                m_scrollTimer->stop();
            }

            updateDragIndicator(dropPosAt(me->pos()));
            return true;
        }
        if (ev->type() == QEvent::MouseButtonRelease) {
            auto *me = static_cast<QMouseEvent*>(ev);
            endDrag(dropPosAt(me->pos()));
            return true;
        }
    }
    return QScrollArea::eventFilter(obj, ev);
}
