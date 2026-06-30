#include "PdfView.h"
#include <QPointer>
#include <QVBoxLayout>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QScrollBar>
#include <QFileDialog>
#include <QDir>
#include <QFile>
#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDebug>
#include <QCheckBox>
#include <algorithm>

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
    m_zoomTimer->setInterval(150);
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
    // After layout settles, scroll to first text so zoom starts on content, not margins
    QTimer::singleShot(80, this, &PdfView::scrollToFirstText);
}

void PdfView::scrollToFirstText() {
    if (!m_doc || m_pages.isEmpty()) return;
    QRectF tr = m_doc->firstTextRect(0);
    if (tr.isNull()) return;

    // tr is in fitz device space (zoom=1). Convert to screen pixels.
    int screenY = qRound(tr.top() * m_zoom);
    int screenX = qRound((tr.left() + tr.right()) * 0.5 * m_zoom);

    // Scroll vertically so the text starts near the top with a small margin
    int topMargin = qRound(20 * m_zoom);
    verticalScrollBar()->setValue(qMax(0, screenY - topMargin));

    // Center horizontally on the text block
    horizontalScrollBar()->setValue(
        qMax(0, screenX - viewport()->width() / 2));
}

void PdfView::applyZoom(float zoom) {
    // Save center before high-quality re-render
    QScrollBar *hb = horizontalScrollBar();
    QScrollBar *vb = verticalScrollBar();
    float docCx = m_zoom > 0.f ? (hb->value() + viewport()->width()  * 0.5f) / m_zoom : 0.f;
    float docCy = m_zoom > 0.f ? (vb->value() + viewport()->height() * 0.5f) / m_zoom : 0.f;

    m_zoom = zoom;
    for (auto *pw : m_pages) pw->reload();
    m_container->adjustSize();

    // Restore center after render (pixel-rounding may shift container size slightly)
    hb->setValue(qRound(docCx * zoom - viewport()->width()  * 0.5f));
    vb->setValue(qRound(docCy * zoom - viewport()->height() * 0.5f));
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
    repositionFormOverlays();
    update();
}

void PdfView::updateContainerGeometry() {
    m_resizeTimer->start();
}

void PdfView::setZoom(float zoom) {
    // Remember viewport center in document space before changing zoom
    QScrollBar *hb = horizontalScrollBar();
    QScrollBar *vb = verticalScrollBar();
    float prevZoom = m_zoom > 0.f ? m_zoom : 1.f;
    float docCx = (hb->value() + viewport()->width()  * 0.5f) / prevZoom;
    float docCy = (vb->value() + viewport()->height() * 0.5f) / prevZoom;

    float factor = zoom / prevZoom;
    m_zoom = zoom;
    emit zoomChanged(zoom);

    // Instant live preview by scaling existing pixmaps (no MuPDF call)
    for (auto *pw : m_pages) pw->scalePixmapPreview(factor);
    m_container->adjustSize();

    // Restore center
    hb->setValue(qRound(docCx * zoom - viewport()->width()  * 0.5f));
    vb->setValue(qRound(docCy * zoom - viewport()->height() * 0.5f));

    // High-quality re-render fires once the slider is released / idle
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
    // The signature is shown as a ghost preview in paintEvent at the actual
    // placement size — use a simple crosshair so it doesn't appear twice.
    for (auto *pw : m_pages) pw->setCursor(Qt::CrossCursor);
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
    m_eraserTimer = new QTimer(this);
    m_eraserTimer->setSingleShot(true);
    m_eraserTimer->setInterval(80);
    connect(m_eraserTimer, &QTimer::timeout, this, [this]() {
        if (m_needsEraserReload) { m_needsEraserReload = false; reload(); }
    });
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
    createFormOverlays();
}

void PdfPageWidget::createFormOverlays() {
    // Destroy old overlays
    for (auto *w : m_formOverlays) w->deleteLater();
    m_formOverlays.clear();
    if (!m_view->m_doc || !m_view->m_doc->isOpen()) return;

    const auto fields = m_view->m_doc->getFormFields();
    for (const auto &f : fields) {
        if (f.pageNum != m_pageNum || f.rect.isEmpty()) continue;
        QRectF sr = toScreen(f.rect.left(), f.rect.top(),
                             f.rect.right(), f.rect.bottom());
        if (sr.isEmpty()) continue;

        if (f.type == 1 /* PDF_WIDGET_TYPE_TEXT */ || f.type == 0) {
            auto *le = new QLineEdit(this);
            le->setText(f.value);
            le->setGeometry(sr.toRect());
            le->setStyleSheet("background: rgba(255,255,220,200); border: 1px solid #aaa;"
                              "border-radius: 2px; font-size: 11px; padding: 1px;");
            le->setPlaceholderText(f.name);
            le->show();
            QString fieldName = f.name;
            int pageNum = f.pageNum;
            connect(le, &QLineEdit::editingFinished, this, [=]() {
                m_view->m_doc->setFormField(pageNum, fieldName, le->text());
                emit m_view->modified();
            });
            m_formOverlays.append(le);
        } else if (f.type == 3 /* PDF_WIDGET_TYPE_CHECKBOX */) {
            auto *cb = new QCheckBox(this);
            cb->setChecked(f.checked || f.value.toLower() == "yes" || f.value == "1");
            cb->setGeometry(sr.toRect());
            cb->show();
            QString fieldName = f.name;
            int pageNum = f.pageNum;
            connect(cb, &QCheckBox::toggled, this, [=](bool checked) {
                m_view->m_doc->setFormField(pageNum, fieldName, checked ? "Yes" : "Off");
                emit m_view->modified();
            });
            m_formOverlays.append(cb);
        }
    }
}

void PdfPageWidget::repositionFormOverlays() {
    // After zoom change, reposition all overlays
    const auto fields = m_view->m_doc ? m_view->m_doc->getFormFields() : QVector<PdfDocument::FormField>{};
    int idx = 0;
    for (const auto &f : fields) {
        if (f.pageNum != m_pageNum || f.rect.isEmpty()) continue;
        if (idx >= m_formOverlays.size()) break;
        QRectF sr = toScreen(f.rect.left(), f.rect.top(),
                             f.rect.right(), f.rect.bottom());
        if (!sr.isEmpty()) m_formOverlays[idx]->setGeometry(sr.toRect());
        ++idx;
    }
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

    // Rubber-band text selection (m_selRects stored in PDF space; convert here)
    if (!m_selRects.isEmpty()) {
        float z = m_view->m_zoom;
        p.setBrush(QColor(0, 120, 215, 70));
        p.setPen(Qt::NoPen);
        for (const QRectF &r : m_selRects)
            p.drawRect(QRectF(r.x()*z, r.y()*z, r.width()*z, r.height()*z));
    }

    // Selected / dragged annotation highlight; show FreeText content during drag
    if (!m_selAnnotRect.isNull()) {
        float z = m_view->m_zoom;
        QRectF sr(m_selAnnotRect.x() * z, m_selAnnotRect.y() * z,
                  m_selAnnotRect.width() * z, m_selAnnotRect.height() * z);
        sr = sr.adjusted(-3, -3, 3, 3);
        p.setBrush(QColor(100, 160, 255, 30));
        p.setPen(QPen(QColor(80, 140, 240, 200), 1.5, Qt::DashLine));
        p.drawRect(sr);
        // Draw text content so the user can see what they are dragging
        if (m_movingAnnot && !m_selAnnotText.isEmpty()) {
            p.setPen(QColor(0, 0, 0, 200));
            QFont f = p.font();
            f.setPointSizeF(qMax(8.0, m_view->m_fontSize * z * 0.75));
            p.setFont(f);
            p.drawText(sr.adjusted(4, 2, -4, -2),
                       Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap,
                       m_selAnnotText);
        }
    }

    // Resize mode: draw bounding box + 8 handles
    if (m_resizeMode) {
        float z = m_view->m_zoom;
        QRectF sr(m_resizeRect.x() * z, m_resizeRect.y() * z,
                  m_resizeRect.width() * z, m_resizeRect.height() * z);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(0, 120, 215), 1.5, Qt::DashLine));
        p.drawRect(sr);
        p.setBrush(QColor(255, 255, 255));
        p.setPen(QPen(QColor(0, 120, 215), 1.5));
        static const int HS = 7;
        const auto handles = {
            sr.topLeft(), QPointF(sr.center().x(), sr.top()),    sr.topRight(),
            QPointF(sr.left(), sr.center().y()),                  QPointF(sr.right(), sr.center().y()),
            sr.bottomLeft(), QPointF(sr.center().x(), sr.bottom()), sr.bottomRight()
        };
        for (auto &hpt : handles)
            p.drawRect(QRectF(hpt.x()-HS/2, hpt.y()-HS/2, HS, HS));
    }

    // Highlight drag preview
    if (m_hlDragging) {
        QPointF sp = m_hlStart, ep = m_selEnd;
        p.setBrush(QColor(m_view->m_hlColor.red(), m_view->m_hlColor.green(),
                           m_view->m_hlColor.blue(), 80));
        p.setPen(Qt::NoPen);
        p.drawRect(QRectF(sp, ep).normalized());
    }

    // Signature drag-to-size preview
    if (m_view->m_tool == Tool::Signature && !m_view->m_sigPixmap.isNull()) {
        const QPixmap &sigPm = m_view->m_sigPixmap;
        if (!sigPm.isNull()) {
            if (m_sigSizing) {
                // Dragging to define the rectangle — preserve aspect ratio in preview
                QRect previewRect = QRect(m_sigOrigin, m_sigCurrent).normalized();
                if (previewRect.width() > 4 && previewRect.height() > 4) {
                    // Compute actual placed rect (aspect-clamped, centred)
                    float pw = previewRect.width(), ph = previewRect.height();
                    if (sigPm.width() > 0 && sigPm.height() > 0) {
                        float nat = float(sigPm.height()) / float(sigPm.width());
                        if (ph / pw > nat) ph = pw * nat;
                        else               pw = ph / nat;
                    }
                    QRect actual(previewRect.center().x() - int(pw / 2),
                                 previewRect.center().y() - int(ph / 2),
                                 int(pw), int(ph));
                    p.setOpacity(0.75);
                    p.drawPixmap(actual, sigPm);
                    p.setOpacity(1.0);
                    p.setPen(QPen(QColor(0, 120, 212), 2, Qt::DashLine));
                    p.setBrush(Qt::NoBrush);
                    p.drawRect(actual);
                } else {
                    p.setPen(QPen(QColor(0, 120, 212), 2, Qt::DashLine));
                    p.setBrush(Qt::NoBrush);
                    p.drawRect(previewRect);
                }
            } else {
                // Hovering — show a floating ghost of the signature at the cursor
                float baseW = 120.f;
                float aspect = float(sigPm.height()) / qMax(1, sigPm.width());
                QSize previewSize(int(baseW), int(baseW * aspect));
                QRect ghostRect(m_sigHoverPos.x() - previewSize.width() / 2,
                                m_sigHoverPos.y() - previewSize.height() / 2,
                                previewSize.width(), previewSize.height());
                p.setOpacity(0.55);
                p.drawPixmap(ghostRect, sigPm.scaled(previewSize, Qt::IgnoreAspectRatio,
                                                     Qt::SmoothTransformation));
                p.setOpacity(1.0);
                // Thin dashed outline around the ghost
                p.setPen(QPen(QColor(0, 120, 212, 160), 1, Qt::DashLine));
                p.setBrush(Qt::NoBrush);
                p.drawRect(ghostRect);
            }
        }
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

    // Resize mode: handle clicks on handles or outside to exit
    if (m_resizeMode) {
        int h = handleAt(ev->pos());
        if (h >= 0) {
            m_resizeHandle   = h;
            m_resizeDragging = true;
            m_resizeDragStart = toPdf(ev->pos());
        } else {
            // Click outside bounding box → commit and exit resize mode
            float z = m_view->m_zoom;
            QRectF sr(m_resizeRect.x()*z, m_resizeRect.y()*z,
                      m_resizeRect.width()*z, m_resizeRect.height()*z);
            exitResizeMode(!sr.contains(ev->pos()));
        }
        return;
    }

    QPointF pdf = toPdf(ev->pos());
    PdfDocument *doc = m_view->m_doc;

    switch (m_view->m_tool) {
    case Tool::Select: {
        QPointF pdfPt = toPdf(ev->pos());
        // Check any annotation first (ink, FreeText, highlight, image)
        QRectF ar;
        if (doc) ar = doc->annotRectAt(m_pageNum, (float)pdfPt.x(), (float)pdfPt.y());
        if (ar.isNull() && doc)
            ar = doc->freetextRectAt(m_pageNum, (float)pdfPt.x(), (float)pdfPt.y());
        if (!ar.isNull()) {
            m_selAnnotRect     = ar;
            m_selAnnotOrigRect = ar;
            m_annotDragStart   = pdfPt;
            m_movingAnnot      = false;
            m_selRects.clear();
            // Capture text content for FreeText drag preview
            m_selAnnotText = doc
                ? doc->freetextAt(m_pageNum,
                      float(ar.center().x()), float(ar.center().y()))
                : QString{};
            update();
        } else {
            // clicked empty space — deselect annotation and start text rubber-band
            m_selAnnotRect = {};
            m_selAnnotText.clear();
            m_selStart = ev->pos();
            m_selEnd   = ev->pos();
            m_selecting = true;
            m_selRects.clear();
            update();
        }
        break;
    }

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
        m_sigOrigin  = ev->pos();
        m_sigCurrent = ev->pos();
        m_sigSizing  = true;
        break;
    }
}

void PdfPageWidget::mouseMoveEvent(QMouseEvent *ev) {
    // Resize handle drag
    if (m_resizeDragging && m_resizeHandle >= 0) {
        QPointF cur = toPdf(ev->pos());
        double dx = cur.x() - m_resizeDragStart.x();
        double dy = cur.y() - m_resizeDragStart.y();
        QRectF r  = m_resizeOrigRect;
        // Handle indices: 0=TL 1=T 2=TR 3=L 4=R 5=BL 6=B 7=BR
        switch (m_resizeHandle) {
        case 0: r.setTopLeft(r.topLeft()     + QPointF(dx,dy)); break;
        case 1: r.setTop(r.top()             + dy);             break;
        case 2: r.setTopRight(r.topRight()   + QPointF(dx,dy)); break;
        case 3: r.setLeft(r.left()           + dx);             break;
        case 4: r.setRight(r.right()         + dx);             break;
        case 5: r.setBottomLeft(r.bottomLeft()  + QPointF(dx,dy)); break;
        case 6: r.setBottom(r.bottom()          + dy);             break;
        case 7: r.setBottomRight(r.bottomRight()+ QPointF(dx,dy)); break;
        }
        m_resizeRect = r.normalized();
        update();
        return;
    }
    // Update cursor when hovering over handles
    if (m_resizeMode && !(ev->buttons() & Qt::LeftButton)) {
        int h = handleAt(ev->pos());
        static const Qt::CursorShape cs[8] = {
            Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor,
            Qt::SizeHorCursor,   Qt::SizeHorCursor,
            Qt::SizeBDiagCursor, Qt::SizeVerCursor, Qt::SizeFDiagCursor
        };
        setCursor(h >= 0 ? cs[h] : Qt::ArrowCursor);
    }

    QPointF pdf = toPdf(ev->pos());
    PdfDocument *doc = m_view->m_doc;

    switch (m_view->m_tool) {
    case Tool::Select:
        if (!m_selAnnotRect.isNull() && (ev->buttons() & Qt::LeftButton)) {
            QPointF cur   = toPdf(ev->pos());
            QPointF delta = cur - m_annotDragStart; // total offset from drag start
            if (!m_movingAnnot && (qAbs(delta.x()) > 3 || qAbs(delta.y()) > 3))
                m_movingAnnot = true;
            if (m_movingAnnot) {
                // keep m_selAnnotRect as a live preview — offset from the ORIGINAL rect
                m_selAnnotRect = QRectF(
                    m_selAnnotOrigRect.x() + delta.x(),
                    m_selAnnotOrigRect.y() + delta.y(),
                    m_selAnnotOrigRect.width(),
                    m_selAnnotOrigRect.height());
                update();
            }
        } else if (m_selecting) {
            m_selEnd = ev->pos();
            m_selRects.clear();
            if (!doc) break;
            QPointF p0 = toPdf(m_selStart.toPoint());
            QPointF p1 = toPdf(m_selEnd.toPoint());
            // Per-letter word-processor selection
            const auto chars = doc->getChars(m_pageNum);
            if (!chars.isEmpty()) {
                float avgH = 0;
                for (const auto &c : chars) avgH += (c.y1 - c.y0);
                avgH /= chars.size();
                // Normalize so anchorY <= releaseY
                float anchorY = p0.y(), anchorX = p0.x();
                float releaseY = p1.y(), releaseX = p1.x();
                if (anchorY > releaseY) {
                    std::swap(anchorY, releaseY);
                    std::swap(anchorX, releaseX);
                }
                float lineThresh = qMax(avgH * 0.55f, 2.f);
                for (const CharBox &c : chars) {
                    if (c.ch == ' ') continue;
                    float cy = (c.y0 + c.y1) * 0.5f;
                    float cx = (c.x0 + c.x1) * 0.5f;
                    bool onFirst  = qAbs(cy - anchorY)  < lineThresh;
                    bool onLast   = qAbs(cy - releaseY) < lineThresh;
                    bool inMiddle = (cy > anchorY + lineThresh && cy < releaseY - lineThresh);
                    bool include  = false;
                    if (onFirst && onLast)      include = (cx >= anchorX && cx <= releaseX);
                    else if (onFirst)           include = (cx >= anchorX);
                    else if (onLast)            include = (cx <= releaseX);
                    else if (inMiddle)          include = true;
                    if (include) m_selRects.append(QRectF(c.x0, c.y0, c.x1-c.x0, c.y1-c.y0));
                }
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
            if (doc && doc->deleteAnnotAt(m_pageNum, (float)pdf.x(), (float)pdf.y())) {
                m_needsEraserReload = true;
                m_eraserTimer->start();
                emit m_view->modified();
            }
        }
        break;

    case Tool::Signature:
        m_sigHoverPos = ev->pos();
        if (m_sigSizing) {
            m_sigCurrent = ev->pos();
        }
        update();
        break;

    default: break;
    }
}

void PdfPageWidget::mouseReleaseEvent(QMouseEvent *ev) {
    if (ev->button() != Qt::LeftButton) return;

    // Resize handle release: commit resize
    if (m_resizeDragging) {
        m_resizeDragging = false;
        // Keep resize mode active (user can drag other handles)
        // Commit the intermediate resize so the annotation updates
        if (m_view->m_doc && m_resizeRect != m_resizeOrigRect) {
            float hx = (float)m_resizeAnnotHit.x(), hy = (float)m_resizeAnnotHit.y();
            if (m_view->m_doc->resizeAnnotAt(m_pageNum, hx, hy,
                    (float)m_resizeRect.left(), (float)m_resizeRect.top(),
                    (float)m_resizeRect.right(), (float)m_resizeRect.bottom())) {
                // After resize, re-detect the annotation at its new center
                m_resizeAnnotHit = m_resizeRect.center();
                m_resizeOrigRect = m_resizeRect;
                reload();
                emit m_view->modified();
            }
        }
        return;
    }

    QPointF pdf = toPdf(ev->pos());
    PdfDocument *doc = m_view->m_doc;

    switch (m_view->m_tool) {
    case Tool::Select: {
        if (m_movingAnnot && doc && !m_selAnnotOrigRect.isNull()) {
            float dx = (float)(m_selAnnotRect.x() - m_selAnnotOrigRect.x());
            float dy = (float)(m_selAnnotRect.y() - m_selAnnotOrigRect.y());
            // Try generic move first (handles ink, highlights, etc.), then FreeText fallback
            float cx = (float)(m_selAnnotOrigRect.x() + m_selAnnotOrigRect.width()  * 0.5f);
            float cy = (float)(m_selAnnotOrigRect.y() + m_selAnnotOrigRect.height() * 0.5f);
            bool moved = doc->moveAnnotAt(m_pageNum, cx, cy, dx, dy);
            if (!moved)
                moved = doc->moveFreetext(m_pageNum, m_selAnnotOrigRect, dx, dy);
            if (moved) {
                m_selAnnotOrigRect = m_selAnnotRect;
                reload();
                emit m_view->modified();
            } else {
                m_selAnnotRect = m_selAnnotOrigRect;
                update();
            }
        }
        m_movingAnnot = false;
        m_selecting   = false;

        if (!doc) break;

        // copy selected PDF text to clipboard when doing rubber-band
        if (m_selAnnotRect.isNull()) {
            QPointF p0 = toPdf(m_selStart.toPoint());
            QPointF p1 = toPdf(m_selEnd.toPoint());
            const auto chars = doc->getChars(m_pageNum);
            QString sel;
            if (!chars.isEmpty()) {
                float avgH = 0;
                for (const auto &c : chars) avgH += (c.y1 - c.y0);
                avgH /= chars.size();
                float anchorY = p0.y(), anchorX = p0.x();
                float releaseY = p1.y(), releaseX = p1.x();
                if (anchorY > releaseY) { std::swap(anchorY, releaseY); std::swap(anchorX, releaseX); }
                float lineThresh = qMax(avgH * 0.55f, 2.f);
                float prevY = -1e9f;
                for (const CharBox &c : chars) {
                    float cy = (c.y0 + c.y1) * 0.5f;
                    float cx = (c.x0 + c.x1) * 0.5f;
                    bool onFirst  = qAbs(cy - anchorY)  < lineThresh;
                    bool onLast   = qAbs(cy - releaseY) < lineThresh;
                    bool inMiddle = (cy > anchorY + lineThresh && cy < releaseY - lineThresh);
                    bool include  = false;
                    if (onFirst && onLast)  include = (cx >= anchorX && cx <= releaseX);
                    else if (onFirst)       include = (cx >= anchorX);
                    else if (onLast)        include = (cx <= releaseX);
                    else if (inMiddle)      include = true;
                    if (include) {
                        if (prevY > 0 && qAbs(cy - prevY) > lineThresh) sel += '\n';
                        sel += c.ch;
                        prevY = cy;
                    }
                }
            }
            sel = sel.trimmed();
            if (!sel.isEmpty()) {
                QApplication::clipboard()->setText(sel);
                emit m_view->selectionChanged(sel);
            }
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
        m_eraserTimer->stop();
        if (m_needsEraserReload) { m_needsEraserReload = false; reload(); }
        break;
    case Tool::Signature: {
        if (!m_sigSizing) break;
        m_sigSizing = false;
        update();
        if (!doc || m_view->m_sigPixmap.isNull()) break;
        QPoint origin  = m_sigOrigin;
        QPoint current = m_sigCurrent;
        int dx = current.x() - origin.x();
        int dy = current.y() - origin.y();
        bool dragged = (qAbs(dx) > 10 || qAbs(dy) > 10);
        if (dragged)
            placeSignatureRect(origin, current);
        else
            placeSignature(ev->pos());
        break;
    }
    default: break;
    }
}

void PdfPageWidget::mouseDoubleClickEvent(QMouseEvent *ev) {
    if (ev->button() != Qt::LeftButton) return;
    if (m_view->m_tool != Tool::Select) return;
    // Exit resize mode on double-click
    if (m_resizeMode) { exitResizeMode(true); return; }
    // double-click on a FreeText annotation — edit it in place
    placeText(ev->pos());
    m_selAnnotRect = {};
    m_selAnnotOrigRect = {};
}

void PdfPageWidget::contextMenuEvent(QContextMenuEvent *ev) {
    // If in resize mode already, exit on right-click
    if (m_resizeMode) {
        exitResizeMode(false);
        return;
    }

    PdfDocument *doc = m_view->m_doc;
    QPointF pdf = toPdf(ev->pos());
    QRectF annotRect;
    if (doc) annotRect = doc->annotRectAt(m_pageNum, (float)pdf.x(), (float)pdf.y());

    QMenu menu(this);
    QAction *actResize   = nullptr;
    QAction *actDelAnnot = nullptr;
    if (!annotRect.isNull()) {
        actResize   = menu.addAction(tr("Resize / Move Handles"));
        actDelAnnot = menu.addAction(tr("Delete"));
        menu.addSeparator();
    }
    QAction *actIns = menu.addAction(tr("Insert Blank Page"));
    QAction *chosen = menu.exec(ev->globalPos());
    if (actResize && chosen == actResize)       enterResizeMode(pdf, annotRect);
    else if (actDelAnnot && chosen == actDelAnnot) {
        if (doc && doc->deleteAnnotAt(m_pageNum, (float)pdf.x(), (float)pdf.y())) {
            m_view->refreshPage(m_pageNum);
            emit m_view->modified();
        }
    }
    else if (chosen == actIns)  emit m_view->pageInsertRequested(m_pageNum);
}

void PdfPageWidget::placeText(QPoint pos) {
    // Commit any existing editor first
    if (m_textEditor) commitTextEditor(false);

    PdfDocument *doc = m_view->m_doc;
    if (!doc) return;
    QPointF pdf = toPdf(pos);

    QString existing = doc->freetextAt(m_pageNum, (float)pdf.x(), (float)pdf.y());
    bool isEdit = !existing.isNull();

    // Create inline editor directly on the page widget
    auto *ed = new QLineEdit(this);
    m_textEditor = ed;
    ed->setText(isEdit ? existing : "");
    int pxSize = qMax(10, int(m_view->m_fontSize * m_view->m_zoom));
    QFont f("Arial", pxSize);
    ed->setFont(f);
    ed->setStyleSheet(
        "QLineEdit { background: rgba(255,255,200,240); color: #000;"
        " border: 1.5px solid #2288ff; padding: 2px; border-radius: 2px; }");
    int edH = pxSize + 10;
    int edW = qMax(180, width() / 2);
    // Keep within page bounds
    int ex = qBound(0, pos.x(), width()  - edW);
    int ey = qBound(0, pos.y() - edH, height() - edH);
    ed->setGeometry(ex, ey, edW, edH);
    ed->show();
    ed->setFocus(Qt::OtherFocusReason);
    if (isEdit) ed->selectAll();
    ed->installEventFilter(this);

    // Capture pdf+isEdit+existing by value into both lambdas
    QPointF capturedPdf = pdf;
    QString capturedExisting = existing;
    bool capturedIsEdit = isEdit;

    // Keep a weak-pointer-like guard: if a second editor is created before
    // focus-out fires on this one, the lambda compares m_textEditor != thisEd
    // and returns early to avoid committing the wrong editor's content.
    QPointer<QLineEdit> thisEd = ed;
    auto doCommit = [this, thisEd, capturedPdf, capturedIsEdit, capturedExisting]() {
        if (!m_textEditor || m_textEditor != thisEd) return;
        QLineEdit *editor = m_textEditor;
        m_textEditor = nullptr;
        editor->blockSignals(true);  // prevent re-entrant editingFinished from focus-out
        QString text = editor->text();
        editor->hide();
        editor->deleteLater();

        PdfDocument *doc = m_view->m_doc;
        if (!doc) return;

        if (capturedIsEdit) {
            if (!text.trimmed().isEmpty()) {
                if (doc->updateFreetext(m_pageNum,
                        (float)capturedPdf.x(), (float)capturedPdf.y(),
                        text, m_view->m_fontSize,
                        m_view->m_textColor.redF(),
                        m_view->m_textColor.greenF(),
                        m_view->m_textColor.blueF())) {
                    reload();
                    emit m_view->modified();
                }
            }
        } else if (!text.trimmed().isEmpty()) {
            if (doc->insertText(m_pageNum, text,
                    (float)capturedPdf.x(), (float)capturedPdf.y(),
                    m_view->m_fontSize,
                    m_view->m_textColor.redF(),
                    m_view->m_textColor.greenF(),
                    m_view->m_textColor.blueF())) {
                reload();
                emit m_view->modified();
            }
        }
    };

    // Commit on Enter; focus-out also commits via editingFinished
    connect(ed, &QLineEdit::returnPressed,    this, doCommit);
    connect(ed, &QLineEdit::editingFinished,  this, doCommit);
}

void PdfPageWidget::commitTextEditor(bool cancel) {
    if (!m_textEditor) return;
    if (cancel) {
        m_textEditor->hide();
        m_textEditor->deleteLater();
        m_textEditor = nullptr;
    } else {
        // Emit editingFinished to trigger the commit lambda
        Q_EMIT m_textEditor->editingFinished();
    }
}

bool PdfPageWidget::eventFilter(QObject *obj, QEvent *ev) {
    if (obj == m_textEditor && ev->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent*>(ev);
        if (ke->key() == Qt::Key_Escape) {
            commitTextEditor(true);  // cancel
            return true;
        }
    }
    return QWidget::eventFilter(obj, ev);
}

void PdfPageWidget::placeSignature(QPoint pos) {
    PdfDocument *doc = m_view->m_doc;
    if (!doc || m_view->m_sigPixmap.isNull()) return;

    QPointF pdf = toPdf(pos);
    const QPixmap &sigPm = m_view->m_sigPixmap;

    float baseW = 120.f / m_view->m_zoom;
    float aspect = sigPm.width() > 0 ? float(sigPm.height()) / sigPm.width() : 0.4f;
    float w = baseW, h = baseW * aspect;

    // Write to temp PNG, embed in PDF, delete temp immediately
    QString tmp = QDir::temp().filePath("pdfeditor_sig_tmp.png");
    sigPm.save(tmp, "PNG");
    if (doc->insertImage(m_pageNum, tmp, (float)pdf.x(), (float)pdf.y(), w, h)) {
        reload();
        emit m_view->modified();
    }
    QFile::remove(tmp);
}

void PdfPageWidget::placeSignatureRect(QPoint origin, QPoint end) {
    PdfDocument *doc = m_view->m_doc;
    if (!doc || m_view->m_sigPixmap.isNull()) return;

    QRect screenRect = QRect(origin, end).normalized();
    if (screenRect.width() < 5 || screenRect.height() < 5) {
        placeSignature(origin);
        return;
    }

    QPointF tl = toPdf(screenRect.topLeft());
    QPointF br = toPdf(screenRect.bottomRight());
    float w = (float)(br.x() - tl.x());
    float h = (float)(br.y() - tl.y());

    const QPixmap &sigPm = m_view->m_sigPixmap;
    if (sigPm.width() > 0 && sigPm.height() > 0) {
        float natAspect = float(sigPm.height()) / float(sigPm.width());
        if (h / w > natAspect) h = w * natAspect;
        else                   w = h / natAspect;
    }

    float midX = (float)((tl.x() + br.x()) * 0.5f);
    float midY = (float)((tl.y() + br.y()) * 0.5f);

    QString tmp = QDir::temp().filePath("pdfeditor_sig_tmp.png");
    sigPm.save(tmp, "PNG");
    if (doc->insertImage(m_pageNum, tmp, midX, midY, w, h)) {
        reload();
        emit m_view->modified();
    }
    QFile::remove(tmp);
}

// ──── Resize mode ────────────────────────────────────────────────────────────

static constexpr int HANDLE_SIZE = 8; // half-size in widget pixels

QRectF PdfPageWidget::handleRect(int handle) const {
    float z = m_view->m_zoom;
    QRectF sr(m_resizeRect.x()*z, m_resizeRect.y()*z,
              m_resizeRect.width()*z, m_resizeRect.height()*z);
    const QPointF pts[8] = {
        sr.topLeft(),
        QPointF(sr.center().x(), sr.top()),
        sr.topRight(),
        QPointF(sr.left(), sr.center().y()),
        QPointF(sr.right(), sr.center().y()),
        sr.bottomLeft(),
        QPointF(sr.center().x(), sr.bottom()),
        sr.bottomRight()
    };
    if (handle < 0 || handle >= 8) return {};
    return QRectF(pts[handle].x()-HANDLE_SIZE, pts[handle].y()-HANDLE_SIZE,
                  HANDLE_SIZE*2, HANDLE_SIZE*2);
}

int PdfPageWidget::handleAt(QPoint wp) const {
    if (!m_resizeMode) return -1;
    for (int i = 0; i < 8; ++i)
        if (handleRect(i).contains(wp)) return i;
    return -1;
}

void PdfPageWidget::enterResizeMode(QPointF pdfHit, QRectF pdfRect) {
    m_resizeMode      = true;
    m_resizeRect      = pdfRect;
    m_resizeOrigRect  = pdfRect;
    m_resizeAnnotHit  = pdfHit;
    m_resizeHandle    = -1;
    m_resizeDragging  = false;
    update();
}

void PdfPageWidget::exitResizeMode(bool commit) {
    if (commit && m_view->m_doc && m_resizeRect != m_resizeOrigRect) {
        float hx = (float)m_resizeAnnotHit.x(), hy = (float)m_resizeAnnotHit.y();
        if (m_view->m_doc->resizeAnnotAt(m_pageNum, hx, hy,
                (float)m_resizeRect.left(), (float)m_resizeRect.top(),
                (float)m_resizeRect.right(), (float)m_resizeRect.bottom())) {
            reload();
            emit m_view->modified();
        }
    }
    m_resizeMode     = false;
    m_resizeDragging = false;
    m_resizeHandle   = -1;
    update();
}
