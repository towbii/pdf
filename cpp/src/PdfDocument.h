#pragma once
#include <QImage>
#include <QPixmap>
#include <QString>
#include <QVector>
#include <QRectF>
#include <QPointF>
#include <QByteArray>
#include <QFile>
#include <QMap>

extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}

struct WordBox {
    QString text;
    float x0, y0, x1, y1; // canonical PDF coords (top-left origin, MuPDF space)
};

struct PageSize {
    float width = 0, height = 0;
    int rotation = 0;
};

class PdfDocument {
public:
    PdfDocument();
    ~PdfDocument();

    // Open / save / close
    bool open(const QString &path);
    bool save(const QString &path = {});
    void close();

    bool isOpen() const { return m_doc != nullptr; }
    int  pageCount() const;
    QString filePath() const { return m_filePath; }

    // Rendering
    QPixmap renderPage(int pageNum, float zoom) const;
    QPixmap renderThumbnail(int pageNum, int targetWidth) const;
    PageSize pageSize(int pageNum) const;

    // Text
    QVector<WordBox> getWords(int pageNum);
    void invalidatePage(int pageNum);

    // Annotations – all coords in MuPDF page space (top-left origin, y down)
    bool addHighlight(int pageNum, float x0, float y0, float x1, float y1,
                      float r=1.f, float g=1.f, float b=0.f);
    bool addInkStroke(int pageNum, const QVector<QPointF> &pts,
                      float r=0.f, float g=0.f, float b=0.f, float width=2.f);
    bool insertText(int pageNum, const QString &text, float x, float y,
                    float fontsize=12.f, float r=0.f, float g=0.f, float b=0.f);
    QString freetextAt(int pageNum, float x, float y) const;
    bool updateFreetext(int pageNum, float x, float y, const QString &newText,
                        float fontsize=12.f, float r=0.f, float g=0.f, float b=0.f);
    bool deleteAnnotAt(int pageNum, float x, float y);

    // Image
    bool insertImage(int pageNum, const QString &imgPath,
                     float cx, float cy, float w, float h);

    // Page ops
    bool rotatePage(int pageNum, int degrees);
    bool deletePage(int pageNum);
    bool duplicatePage(int pageNum);
    bool movePage(int from, int to);

    // Form fields
    struct FormField {
        QString name;
        int type; // PDF_WIDGET_TYPE_*
        QString value;
        QVector<QString> choices;
        bool checked = false;
        int pageNum = 0;
    };
    QVector<FormField> getFormFields() const;
    bool setFormField(int pageNum, const QString &name, const QString &value);
    bool hasFormFields() const;

    // Undo / redo
    bool canUndo() const { return !m_history.isEmpty(); }
    bool canRedo() const { return !m_redoStack.isEmpty(); }
    bool undo();
    bool redo();
    void snapshot(); // call before any modification

private:
    fz_context   *m_ctx  = nullptr;
    fz_document  *m_doc  = nullptr;
    pdf_document *m_pdoc = nullptr; // same object, PDF-specific handle

    QString m_filePath;
    int m_imgSeq = 0;
    fz_buffer *m_docBuf = nullptr; // keeps stream data alive for lazy loading

    mutable QMap<int, QVector<WordBox>> m_wordCache;

    static constexpr int MAX_HISTORY = 20;
    QVector<QByteArray> m_history;
    QVector<QByteArray> m_redoStack;

    QByteArray toBytes() const;
    bool fromBytes(const QByteArray &data);

    QPixmap pixmapFromMupdf(fz_page *page, float zoom) const;
    pdf_page* loadPdfPage(int pageNum) const;
};
