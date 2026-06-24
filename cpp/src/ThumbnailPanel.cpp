#include "ThumbnailPanel.h"
#include <QPainter>
#include <QMouseEvent>
#include <QTimer>
#include <QScrollBar>

static constexpr int THUMB_W    = 110;
static constexpr int THUMB_PAD  = 8;
static constexpr int THUMB_MARGIN = 4;

// ═══════════════════════════════════════════════════════════
// ThumbnailItem
// ═══════════════════════════════════════════════════════════

ThumbnailItem::ThumbnailItem(int pageNum, QWidget *parent)
    : QWidget(parent), m_pageNum(pageNum) {
    QVBoxLayout *lay = new QVBoxLayout(this);
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
    if (s)
        setStyleSheet("background: #0a84ff; border-radius: 8px;");
    else
        setStyleSheet("background: transparent; border-radius: 8px;");
    update();
}

void ThumbnailItem::paintEvent(QPaintEvent *ev) {
    QWidget::paintEvent(ev);
    if (!m_pixmap.isNull()) {
        // draw a subtle shadow around the thumbnail
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QRect imgRect = m_img->geometry();
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0,0,0,30));
        p.drawRoundedRect(imgRect.adjusted(2,2,3,3), 3, 3);
    }
}

void ThumbnailItem::mousePressEvent(QMouseEvent *) {
    emit clicked(m_pageNum);
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
}

void ThumbnailPanel::setDocument(PdfDocument *doc) {
    m_doc = doc;
    buildItems();
}

void ThumbnailPanel::buildItems() {
    for (auto *it : m_items) it->deleteLater();
    m_items.clear();

    if (!m_doc || !m_doc->isOpen()) return;

    // Remove stretch
    QLayoutItem *stretch = m_vlay->takeAt(m_vlay->count() - 1);
    delete stretch;

    int n = m_doc->pageCount();
    m_items.reserve(n);
    for (int i = 0; i < n; ++i) {
        auto *item = new ThumbnailItem(i, m_container);
        m_vlay->addWidget(item, 0, Qt::AlignHCenter);
        m_items.append(item);
        connect(item, &ThumbnailItem::clicked, this, &ThumbnailPanel::pageClicked);
    }
    m_vlay->addStretch();

    // Pump thumbnails lazily
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
