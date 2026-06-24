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
    void setSignaturePath(const QString &path) {
        m_sigPath = path;
        if (m_tool == Tool::Signature) updateSignatureCursor();
    }

    void setHighlightColor(QColor c) { m_hlColor = c; }
    void setPenColor(QColor c)       { m_penColor = c; }
    void setPenWidth(float w)        { m_penWidth = w; }
    void setFontSize(float s)        { m_fontSize = s; }
    void setTextColor(QColor c)      { m_textColor = c; }

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

private:
    PdfDocument *m_doc;
    float m_zoom = 1.5f;
    Tool  m_tool = Tool::Select;
    QString m_sigPath;
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

private:
    int       m_pageNum;
    PdfView  *m_view;
    QPixmap   m_pixmap;
    QPixmap   m_renderedPixmap;    // last high-quality rendered pixmap
    float     m_renderedZoom = 0.f; // zoom at which m_renderedPixmap was rendered
    float     m_previewZoom = 1.0f;

    // selection
    QPointF   m_selStart, m_selEnd;
    bool      m_selecting = false;
    QVector<QRectF> m_selRects; // screen rects

    // highlight
    QPointF   m_hlStart;
    bool      m_hlDragging = false;

    // pen
    QVector<QPointF> m_penStroke; // in PDF coords
    bool m_penDown = false;

    // eraser state
    bool m_erasing = false;

    // coordinate conversion
    QPointF toPdf(QPoint widgetPt) const;
    QRectF  toScreen(float x0, float y0, float x1, float y1) const;

    void placeText(QPoint pos);
    void placeSignature(QPoint pos);
};
