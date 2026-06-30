#pragma once
#include <QWidget>
#include <QScrollArea>
#include <QLabel>
#include <QVBoxLayout>
#include <QVector>
#include <QPointF>
#include <QRectF>
#include <QTimer>
#include <QPixmap>
#include <QLineEdit>
#include <QMap>
#include "PdfDocument.h"

enum class Tool { Select, Highlight, Pen, Eraser, Text, Signature };

class PdfPageWidget;

class PdfView : public QScrollArea {
    Q_OBJECT
public:
    explicit PdfView(PdfDocument *doc, QWidget *parent = nullptr);

    void setDocument(PdfDocument *doc);
    void setZoom(float zoom);
    float zoom() const { return m_zoom; }
    void setTool(Tool t);
    Tool tool() const { return m_tool; }
    void setSignaturePixmap(const QPixmap &px) {
        m_sigPixmap = px;
        if (m_tool == Tool::Signature) updateSignatureCursor();
    }

    // Search highlight support
    void setSearchResults(const QMap<int, QVector<QRectF>> &results,
                          int currentPage, int currentIdxInPage);
    void clearSearch();

    void setHighlightColor(QColor c) { m_hlColor = c; }
    void setPenColor(QColor c)       { m_penColor = c; }
    void setPenWidth(float w)        { m_penWidth = w; }
    void setFontSize(float s)        { m_fontSize = s; }
    void setTextColor(QColor c)      { m_textColor = c; }

    QColor penColor()       const { return m_penColor; }
    QColor highlightColor() const { return m_hlColor; }

    void refreshAll();
    void refreshPage(int pageNum);
    void scrollToPage(int pageNum);
    int currentPage() const;
    void updateContainerGeometry();

signals:
    void modified();
    void pageChanged(int pageNum);
    void selectionChanged(const QString &text);
    void zoomChanged(float zoom);
    void pageDeleteRequested(int pageNum);
    void pageDuplicateRequested(int pageNum);
    void pageInsertRequested(int pageNum);

private:
    PdfDocument *m_doc;
    float m_zoom = 1.0f;
    Tool  m_tool = Tool::Select;
    QPixmap m_sigPixmap;
    QMap<int, QVector<QRectF>> m_searchResults;
    int     m_searchCurPage = -1;
    int     m_searchCurIdx  = -1;
    QColor m_hlColor  {255, 255, 0};
    QColor m_penColor {0, 0, 0};
    float  m_penWidth = 2.f;
    float  m_fontSize = 12.f;
    QColor m_textColor{0, 0, 0};

    QWidget *m_container   = nullptr;
    QVBoxLayout *m_vlay    = nullptr;
    QVector<PdfPageWidget*> m_pages;
    QTimer *m_zoomTimer    = nullptr;
    QTimer *m_resizeTimer  = nullptr;
    float   m_pendingZoom  = -1.f;

    void buildPages();
    void applyZoom(float zoom);
    void scrollToFirstText();
    void updateSignatureCursor();
    void wheelEvent(QWheelEvent *ev) override;
    friend class PdfPageWidget;
};

// ──── One widget per page ────────────────────────────────────────────
class PdfPageWidget : public QWidget {
    Q_OBJECT
public:
    PdfPageWidget(int pageNum, PdfView *view);
    void reload();
    void scalePixmapPreview(float factor);
    int pageNum() const { return m_pageNum; }

protected:
    void paintEvent(QPaintEvent *ev) override;
    void mousePressEvent(QMouseEvent *ev) override;
    void mouseMoveEvent(QMouseEvent *ev) override;
    void mouseReleaseEvent(QMouseEvent *ev) override;
    void mouseDoubleClickEvent(QMouseEvent *ev) override;
    void contextMenuEvent(QContextMenuEvent *ev) override;
    bool eventFilter(QObject *obj, QEvent *ev) override;

private:
    int       m_pageNum;
    PdfView  *m_view;
    QPixmap   m_pixmap;
    QPixmap   m_renderedPixmap;
    float     m_renderedZoom = 0.f;
    float     m_previewZoom  = 1.0f;

    // rubber-band text selection
    QPointF   m_selStart, m_selEnd;
    bool      m_selecting = false;
    QVector<QRectF> m_selRects;

    // FreeText annotation selection / drag
    QRectF    m_selAnnotRect;     // current (preview) rect while dragging, PDF coords
    QRectF    m_selAnnotOrigRect; // rect at the start of the drag, for the actual move call
    QPointF   m_annotDragStart;   // PDF coords where drag began
    bool      m_movingAnnot = false;
    QString   m_selAnnotText;     // FreeText content for drag preview

    // highlight
    QPointF   m_hlStart;
    bool      m_hlDragging = false;

    // signature drag-to-size
    QPoint    m_sigOrigin;
    QPoint    m_sigCurrent;
    bool      m_sigSizing = false;

    // pen
    QVector<QPointF> m_penStroke;
    bool m_penDown = false;

    // eraser
    bool    m_erasing           = false;
    bool    m_needsEraserReload = false;
    QTimer *m_eraserTimer       = nullptr;

    // signature hover position (for floating preview before drag)
    QPoint m_sigHoverPos;

    // inline text editor overlay
    QLineEdit *m_textEditor = nullptr;

    // form field overlays (created when PDF has form fields)
    QVector<QWidget*> m_formOverlays;

    // resize mode (right-click on annotation)
    bool    m_resizeMode     = false;
    QRectF  m_resizeRect;       // current resize rect in PDF coords
    QRectF  m_resizeOrigRect;   // original rect at resize start, PDF coords
    QPointF m_resizeAnnotHit;   // PDF point used to identify which annotation
    int     m_resizeHandle    = -1; // 0=TL 1=T 2=TR 3=L 4=R 5=BL 6=B 7=BR
    bool    m_resizeDragging  = false;
    QPointF m_resizeDragStart; // PDF coords at handle drag start

    // coordinate conversion
    QPointF toPdf(QPoint widgetPt) const;
    QRectF  toScreen(float x0, float y0, float x1, float y1) const;

    void placeText(QPoint pos);
    void commitTextEditor(bool cancel = false);
    void placeSignature(QPoint pos);
    void placeSignatureRect(QPoint origin, QPoint end);
    void createFormOverlays();
    void repositionFormOverlays();
    void enterResizeMode(QPointF pdfHit, QRectF pdfRect);
    void exitResizeMode(bool commit);
    int  handleAt(QPoint widgetPt) const;
    QRectF handleRect(int handle) const; // in widget coords
};
