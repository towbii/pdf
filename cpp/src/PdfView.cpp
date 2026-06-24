#include "PdfView.h"
#include <QVBoxLayout>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QScrollBar>
#include <QInputDialog>
#include <QFileDialog>
#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDebug>

static constexpr int PAGE_GAP   = 16;
static constexpr int PAGE_MARGIN = 8;

// ═══════════════════════════════════════════════════════════
// PdfView
// ═══════════════════════════════════════════════════════════

PdfView::PdfView(PdfDocument *doc, QWidget *parent)
    : QScrollArea(parent), m_doc(doc) {
    setWidgetResizable(false);
    setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    m_container = new QWidget;
    m_vlay = new QVBoxLayout(m_container);
    m_vlay->setSpacing(PAGE_GAP);
    m_vlay->setContentsMargins(PAGE_MARGIN, PAGE_MARGIN, PAGE_MARGIN, PAGE_MARGIN);
    m_vlay->setAlignment(Qt::AlignHCenter);
    setWidget(m_container);

    m_zoomTimer = new QTimer(this);
    m_zoomTimer->setSingleShot(true);
    m_zoomTimer->setInterval(120);
    connect(m_zoomTimer, &QTimer::timeout, this, [this]() {
        if (m_pendingZoom > 0) applyZoom(m_pendingZoom);
        m_pendingZoom = -1.f;
    });

    m_resizeTimer = new QTimer(this);
    m_resizeTimer->setSingleShot(true);
    m_resizeTimer->setInterval(60);
    connect(m_resizeTimer, &QTimer::timeout, m_container, &QWidget::adjustSize);
}

void PdfView::setDocument(PdfDocument *doc) {
    m_doc = doc;
    buildPages();
}

void PdfView::buildPages() {
    // Clear old
    for (auto *pw : m_pages) pw->deleteLater();
    m_pages.clear();

    if (!m_doc || !m_doc->isOpen()) return;

    int n = m_doc->pageCount();
    for (int i = 0; i < n; ++i) {
        auto *pw = new PdfPageWidget(i, this);
        m_vlay->addWidget(pw, 0, Qt::AlignHCenter);
        m_pages.append(pw);
    }
    // Render first page immediately; rest lazily
    if (!m_pages.isEmpty()) m_pages[0]->reload();
    for (int i = 1; i < m_pages.size(); ++i) {
        int idx = i;
        QTimer::singleShot(i * 5, m_pages[i], [this, idx]() {
            if (idx < m_pages.size()) m_pages[idx]->reload();
        });
    }
    m_resizeTimer->start();
}

void PdfView::applyZoom(float zoom) {
    m_zoom = zoom;
    emit zoomChanged(zoom);
    for (auto *pw : m_pages) pw->reload();
    m_resizeTimer->start();
}

void PdfPageWidget::scalePixmapPreview(float factor) {
    if (m_pixmap.isNull()) return;
    // Always scale from the last high-quality render to avoid cumulative degradation
    if (!m_renderedPixmap.isNull() && m_renderedZoom > 0.f) {
        float scale = (m_view->m_zoom) / m_renderedZoom;
        QSize newSize = (QSizeF(m_renderedPixmap.size()) * scale).toSize();
        m_pixmap = m_renderedPixmap.scaled(newSize, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    } else {
        QSize newSize = (QSizeF(m_pixmap.size()) * factor).toSize();
        m_pixmap = m_pixmap.scaled(newSize, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }
    setFixedSize(m_pixmap.size());
    update();
}

void PdfView::updateContainerGeometry() {
    m_resizeTimer->start();
}

void PdfView::setZoom(float zoom) {
    float factor = zoom / m_zoom;
    m_zoom = zoom;
    emit zoomChanged(zoom);
    for (auto *pw : m_pages) pw->scalePixmapPreview(factor);
    m_resizeTimer->start();
    m_pendingZoom = zoom;
    m_zoomTimer->start();
}

void PdfView::setTool(Tool t) {
    m_tool = t;

    if (t == Tool::Eraser) {
        QPixmap pm(32, 32);
        pm.fill(Qt::transparent);
        QPainter cp(&pm);
        cp.setRenderHint(QPainter::Antialiasing);
        // Dark outer ring for visibility on white backgrounds
        cp.setPen(QPen(QColor(60, 60, 60, 220), 2.5));
        cp.setBrush(Qt::NoBrush);
        cp.drawEllipse(QRectF(2.5, 2.5, 27, 27));
        // White inner fill
        cp.setPen(QPen(QColor(255, 255, 255, 180), 1.5));
        cp.setBrush(QColor(255, 255, 255, 60));
        cp.drawEllipse(QRectF(4, 4, 24, 24));
        cp.end();
        QCursor cur(pm, 16, 16);
        for (auto *pw : m_pages) pw->setCursor(cur);
        return;
    }

    if (t == Tool::Signature) {
        updateSignatureCursor();
        return;
    }

    Qt::CursorShape cur = Qt::ArrowCursor;
    switch (t) {
        case Tool::Select:    cur = Qt::IBeamCursor;    break;
        case Tool::Highlight: cur = Qt::CrossCursor;    break;
        case Tool::Pen:       cur = Qt::CrossCursor;    break;
        case Tool::Text:      cur = Qt::IBeamCursor;    break;
        default: break;
    }
    for (auto *pw : m_pages) pw->setCursor(cur);
}

void PdfView::updateSignatureCursor() {
    if (m_sigPath.isEmpty()) {
        for (auto *pw : m_pages) pw->setCursor(Qt::CrossCursor);
        return;
    }
    QPixmap sigPm(m_sigPath);
    if (!sigPm.isNull()) {
        // Cursor shows signature at actual insertion size on screen
        // Insertion is fixed 150px wide; height preserves aspect ratio
        int curW = 150;
        int curH = qMax(8, int(sigPm.height() * 150.f / qMax(sigPm.width(), 1)));
        curH = qMin(curH, 100); // cap height

        QPixmap curPm(curW, curH);
        curPm.fill(Qt::transparent);
        QPainter p(&curPm);
        QPixmap scaled = sigPm.scaled(curW, curH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p.setOpacity(0.80);
        // Center scaled image in curPm
        int ox = (curW - scaled.width()) / 2;
        int oy = (curH - scaled.height()) / 2;
        p.drawPixmap(ox, oy, scaled);
        p.setOpacity(1.0);
        p.setPen(QPen(QColor(0, 120, 212, 160), 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRect(0, 0, curW - 1, curH - 1));
        p.end();

        // Hotspot at center so placed image center matches cursor center
        QCursor cur(curPm, curW / 2, curH / 2);
        for (auto *pw : m_pages) pw->setCursor(cur);
    } else {
        for (auto *pw : m_pages) pw->setCursor(Qt::CrossCursor);
    }
}

void PdfView::refreshAll() {
    for (auto *pw : m_pages) pw->reload();
}

void PdfView::refreshPage(int pageNum) {
    if (pageNum >= 0 && pageNum < m_pages.size()) {
        m_doc->invalidatePage(pageNum);
        m_pages[pageNum]->reload();
    }
}

void PdfView::scrollToPage(int pageNum) {
    if (pageNum >= 0 && pageNum < m_pages.size())
        ensureWidgetVisible(m_pages[pageNum]);
}

int PdfView::currentPage() const {
    int center = viewport()->rect().center().y() + verticalScrollBar()->value();
    for (int i = m_pages.size()-1; i >= 0; --i) {
        if (m_pages[i]->pos().y() <= center) return i;
    }
    return 0;
}

void PdfView::wheelEvent(QWheelEvent *ev) {
    if (ev->modifiers() & Qt::ControlModifier) {
        float delta = 0.f;
        // pixelDelta: smooth trackpad (continuous); angleDelta: mouse wheel (stepped)
        if (!ev->pixelDelta().isNull()) {
            delta = ev->pixelDelta().y() * 0.003f;
        } else {
            delta = ev->angleDelta().y() * (0.1f / 120.f);
        }
        float newZoom = qBound(0.25f, m_zoom + delta, 5.f);
        if (qAbs(newZoom - m_zoom) > 0.001f) {
            setZoom(newZoom);           // immediate pixmap preview
            emit zoomChanged(m_zoom);  // update slider/label
        }
        ev->accept();
    } else {
        QScrollArea::wheelEvent(ev);
    }
}

// ═══════════════════════════════════════════════════════════
// PdfPageWidget
// ═══════════════════════════════════════════════════════════

PdfPageWidget::PdfPageWidget(int pageNum, PdfView *view)
    : QWidget(nullptr), m_pageNum(pageNum), m_view(view) {
    setMouseTracking(true);
}

void PdfPageWidget::reload() {
    if (!m_view->m_doc || !m_view->m_doc->isOpen()) {
        m_pixmap = QPixmap();
        m_renderedPixmap = QPixmap();
        m_renderedZoom = 0.f;
    } else {
        m_view->m_doc->invalidatePage(m_pageNum);
        m_pixmap = m_view->m_doc->renderPage(m_pageNum, m_view->m_zoom);
        m_renderedPixmap = m_pixmap;
        m_renderedZoom = m_view->m_zoom;
    }
    if (!m_pixmap.isNull()) {
        setFixedSize(m_pixmap.size());
    }
    update();
    m_view->updateContainerGeometry();
}

void PdfPageWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Shadow
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0,0,0,40));
    p.drawRoundedRect(rect().adjusted(3,3,3,3), 3, 3);

    // Page
    if (!m_pixmap.isNull()) p.drawPixmap(0, 0, m_pixmap);
    else {
        p.fillRect(rect(), Qt::white);
        p.setPen(QColor(180,180,180));
        p.drawRect(rect().adjusted(0,0,-1,-1));
    }

    // Selection overlay
    if (!m_selRects.isEmpty()) {
        p.setBrush(QColor(0, 120, 215, 70));
        p.setPen(Qt::NoPen);
        for (const QRectF &r : m_selRects) p.drawRect(r);
    }

    // Highlight drag preview
    if (m_hlDragging) {
        QPointF sp = m_hlStart, ep = m_selEnd;
        p.setBrush(QColor(m_view->m_hlColor.red(), m_view->m_hlColor.green(),
                           m_view->m_hlColor.blue(), 80));
        p.setPen(Qt::NoPen);
        p.drawRect(QRectF(sp, ep).normalized());
    }

    // Pen stroke preview
    if (m_penDown && m_penStroke.size() >= 2) {
        p.setPen(QPen(m_view->m_penColor,
                      m_view->m_penWidth * m_view->m_zoom,
                      Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        QVector<QPointF> scr;
        scr.reserve(m_penStroke.size());
        for (const QPointF &pt : m_penStroke) {
            scr.append(pt * m_view->m_zoom);
        }
        for (int i = 1; i < scr.size(); ++i)
            p.drawLine(scr[i-1], scr[i]);
    }
}

QPointF PdfPageWidget::toPdf(QPoint wp) const {
    float z = m_view->m_zoom;
    return QPointF(wp.x() / z, wp.y() / z);
}

QRectF PdfPageWidget::toScreen(float x0, float y0, float x1, float y1) const {
    float z = m_view->m_zoom;
    return QRectF(x0*z, y0*z, (x1-x0)*z, (y1-y0)*z);
}

void PdfPageWidget::mousePressEvent(QMouseEvent *ev) {
    if (ev->button() != Qt::LeftButton) return;
    QPointF pdf = toPdf(ev->pos());
    PdfDocument *doc = m_view->m_doc;

    switch (m_view->m_tool) {
    case Tool::Select:
        m_selStart = ev->pos();
        m_selEnd   = ev->pos();
        m_selecting = true;
        m_selRects.clear();
        update();
        break;

    case Tool::Highlight:
        m_hlStart    = ev->pos();
        m_selEnd     = ev->pos();
        m_hlDragging = true;
        break;

    case Tool::Pen:
        m_penStroke.clear();
        m_penStroke.append(pdf);
        m_penDown = true;
        break;

    case Tool::Eraser:
        m_erasing = true;
        if (doc) {
            if (doc->deleteAnnotAt(m_pageNum, (float)pdf.x(), (float)pdf.y())) {
                reload();
                emit m_view->modified();
            }
        }
        break;

    case Tool::Text:
        placeText(ev->pos());
        break;

    case Tool::Signature:
        placeSignature(ev->pos());
        break;
    }
}

void PdfPageWidget::mouseMoveEvent(QMouseEvent *ev) {
    QPointF pdf = toPdf(ev->pos());
    PdfDocument *doc = m_view->m_doc;

    switch (m_view->m_tool) {
    case Tool::Select:
        if (m_selecting) {
            m_selEnd = ev->pos();
            // compute word rects
            m_selRects.clear();
            if (!doc) break;
            QPointF p0 = toPdf(m_selStart.toPoint());
            QPointF p1 = toPdf(m_selEnd.toPoint());
            float sx0 = qMin(p0.x(), p1.x()), sy0 = qMin(p0.y(), p1.y());
            float sx1 = qMax(p0.x(), p1.x()), sy1 = qMax(p0.y(), p1.y());
            for (const WordBox &w : doc->getWords(m_pageNum)) {
                bool ov = !(w.x1 < sx0 || w.x0 > sx1 || w.y1 < sy0 || w.y0 > sy1);
                if (ov) m_selRects.append(toScreen(w.x0, w.y0, w.x1, w.y1));
            }
            update();
        }
        break;

    case Tool::Highlight:
        if (m_hlDragging) { m_selEnd = ev->pos(); update(); }
        break;

    case Tool::Pen:
        if (m_penDown) {
            m_penStroke.append(pdf);
            update();
        }
        break;

    case Tool::Eraser:
        if (m_erasing && (ev->buttons() & Qt::LeftButton)) {
            if (doc) {
                if (doc->deleteAnnotAt(m_pageNum, (float)pdf.x(), (float)pdf.y())) {
                    reload();
                    emit m_view->modified();
                }
            }
        }
        break;

    default: break;
    }
}

void PdfPageWidget::mouseReleaseEvent(QMouseEvent *ev) {
    if (ev->button() != Qt::LeftButton) return;
    QPointF pdf = toPdf(ev->pos());
    PdfDocument *doc = m_view->m_doc;

    switch (m_view->m_tool) {
    case Tool::Select: {
        m_selecting = false;
        // Build selected text from words
        if (!doc) break;
        QPointF p0 = toPdf(m_selStart.toPoint());
        QPointF p1 = toPdf(m_selEnd.toPoint());
        float sx0 = qMin(p0.x(), p1.x()), sy0 = qMin(p0.y(), p1.y());
        float sx1 = qMax(p0.x(), p1.x()), sy1 = qMax(p0.y(), p1.y());
        QString sel;
        for (const WordBox &w : doc->getWords(m_pageNum)) {
            bool ov = !(w.x1 < sx0 || w.x0 > sx1 || w.y1 < sy0 || w.y0 > sy1);
            if (ov && !w.text.isEmpty() && w.text != " ") {
                sel += w.text;
            }
        }
        sel = sel.trimmed();
        if (!sel.isEmpty()) {
            QApplication::clipboard()->setText(sel);
            emit m_view->selectionChanged(sel);
        }
        break;
    }
    case Tool::Highlight: {
        m_hlDragging = false;
        if (!doc) break;
        QPointF p0 = toPdf(m_hlStart.toPoint());
        QPointF p1 = toPdf(ev->pos());
        if (doc->addHighlight(m_pageNum,
                (float)p0.x(), (float)p0.y(), (float)p1.x(), (float)p1.y(),
                m_view->m_hlColor.redF(), m_view->m_hlColor.greenF(),
                m_view->m_hlColor.blueF())) {
            reload();
            emit m_view->modified();
        }
        update();
        break;
    }
    case Tool::Pen: {
        m_penDown = false;
        if (!doc || m_penStroke.size() < 2) { m_penStroke.clear(); break; }
        if (doc->addInkStroke(m_pageNum, m_penStroke,
                m_view->m_penColor.redF(), m_view->m_penColor.greenF(),
                m_view->m_penColor.blueF(), m_view->m_penWidth)) {
            reload();
            emit m_view->modified();
        }
        m_penStroke.clear();
        break;
    }
    case Tool::Eraser:
        m_erasing = false;
        break;
    default: break;
    }
}

void PdfPageWidget::placeText(QPoint pos) {
    PdfDocument *doc = m_view->m_doc;
    if (!doc) return;
    QPointF pdf = toPdf(pos);

    // Check if clicking on existing FreeText annotation
    QString existing = doc->freetextAt(m_pageNum, (float)pdf.x(), (float)pdf.y());
    bool isEdit = !existing.isNull();

    bool ok;
    QString text = QInputDialog::getText(
        this,
        isEdit ? tr("Text bearbeiten") : tr("Text einfügen"),
        tr("Text:"),
        QLineEdit::Normal,
        existing,
        &ok);
    if (!ok) return;

    if (isEdit) {
        if (doc->updateFreetext(m_pageNum, (float)pdf.x(), (float)pdf.y(),
                text, m_view->m_fontSize,
                m_view->m_textColor.redF(), m_view->m_textColor.greenF(),
                m_view->m_textColor.blueF())) {
            reload();
            emit m_view->modified();
        }
    } else if (!text.trimmed().isEmpty()) {
        if (doc->insertText(m_pageNum, text, (float)pdf.x(), (float)pdf.y(),
                m_view->m_fontSize,
                m_view->m_textColor.redF(), m_view->m_textColor.greenF(),
                m_view->m_textColor.blueF())) {
            reload();
            emit m_view->modified();
        }
    }
}

void PdfPageWidget::placeSignature(QPoint pos) {
    PdfDocument *doc = m_view->m_doc;
    if (!doc) return;
    if (m_view->m_sigPath.isEmpty()) return;

    QPointF pdf = toPdf(pos);

    // Preserve aspect ratio; 150 screen px wide → 150/zoom PDF units wide
    QPixmap sigPm(m_view->m_sigPath);
    float baseW = 150.f / m_view->m_zoom;
    float aspect = sigPm.isNull() || sigPm.width() == 0 ? 0.4f
                   : float(sigPm.height()) / sigPm.width();
    float w = baseW;
    float h = baseW * aspect;

    // insertImage centers at (cx,cy) — cursor hotspot is also centered
    if (doc->insertImage(m_pageNum, m_view->m_sigPath,
            (float)pdf.x(), (float)pdf.y(), w, h)) {
        reload();
        emit m_view->modified();
    }
}
