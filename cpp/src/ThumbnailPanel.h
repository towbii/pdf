#pragma once
#include <QWidget>
#include <QScrollArea>
#include <QLabel>
#include <QVBoxLayout>
#include <QVector>
#include <QPoint>
#include "PdfDocument.h"

class ThumbnailItem : public QWidget {
    Q_OBJECT
public:
    ThumbnailItem(int pageNum, QWidget *parent = nullptr);
    void setPixmap(const QPixmap &px);
    void setSelected(bool s);
    int pageNum() const { return m_pageNum; }
signals:
    void clicked(int pageNum);
    void deleteRequested(int pageNum);
    void duplicateRequested(int pageNum);
    void insertRequested(int pageNum);
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;
private:
    int     m_pageNum;
    QPixmap m_pixmap;
    QLabel *m_img;
    QLabel *m_lbl;
    bool    m_selected  = false;
    QPoint  m_dragStart;
    bool    m_dragStarted = false;
};

class ThumbnailPanel : public QScrollArea {
    Q_OBJECT
public:
    explicit ThumbnailPanel(QWidget *parent = nullptr);
    void setDocument(PdfDocument *doc);
    void setCurrentPage(int pageNum);
    void refreshAll();
    void refreshPage(int pageNum);

signals:
    void pageClicked(int pageNum);
    void pageDeleteRequested(int pageNum);
    void pageDuplicateRequested(int pageNum);
    void pageInsertRequested(int pageNum);
    void pageMoveRequested(int from, int to);

protected:
    void mousePressEvent(QMouseEvent *ev) override;

private:
    PdfDocument  *m_doc       = nullptr;
    QWidget      *m_container = nullptr;
    QVBoxLayout  *m_vlay      = nullptr;
    QVector<ThumbnailItem*> m_items;
    int           m_pumpIdx   = 0;

    // drag-and-drop state
    int     m_dragFrom         = -1;
    int     m_dropIndicatorPos = -1;
    QPoint  m_dragMousePos;          // last viewport mouse pos during drag
    int     m_scrollDir        = 0;  // -1 up, 0 none, +1 down
    QTimer *m_scrollTimer      = nullptr;
    QWidget *m_dropLine        = nullptr;

    void buildItems();
    void pumpNext();
    void beginDrag(int fromPage, QPoint globalPos);
    void updateDragIndicator(int dropPos);
    void endDrag(int dropPos);
    int  dropPosAt(QPoint viewportPt) const;

    bool eventFilter(QObject *obj, QEvent *ev) override;

    friend class ThumbnailItem;
};
