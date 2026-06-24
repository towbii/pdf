#pragma once
#include <QWidget>
#include <QScrollArea>
#include <QLabel>
#include <QVBoxLayout>
#include <QVector>
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
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
private:
    int m_pageNum;
    QPixmap m_pixmap;
    QLabel *m_img;
    QLabel *m_lbl;
    bool m_selected = false;
};

class ThumbnailPanel : public QScrollArea {
    Q_OBJECT
public:
    explicit ThumbnailPanel(QWidget *parent = nullptr);
    void setDocument(PdfDocument *doc);
    void setCurrentPage(int pageNum);
    void refreshAll();
signals:
    void pageClicked(int pageNum);
private:
    PdfDocument *m_doc = nullptr;
    QWidget *m_container;
    QVBoxLayout *m_vlay;
    QVector<ThumbnailItem*> m_items;
    int m_pumpIdx = 0;

    void buildItems();
    void pumpNext();
};
