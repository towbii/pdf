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

struct CharBox {
    QChar ch;
    float x0, y0, x1, y1;
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
    QPixmap renderPage(int pageNum, float zoom);
    QPixmap renderThumbnail(int pageNum, int targetWidth) const;
    PageSize pageSize(int pageNum) const;

    // Text
    QVector<WordBox> getWords(int pageNum);
    QVector<CharBox> getChars(int pageNum);
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
    // returns the rect of the FreeText hit at (x,y), or a null rect if none
    QRectF freetextRectAt(int pageNum, float x, float y) const;
    bool moveFreetext(int pageNum, const QRectF &oldRect, float dx, float dy);
    // Generic: moves any annotation (FreeText or Ink) that is at (x,y) by (dx,dy)
    bool moveAnnotAt(int pageNum, float x, float y, float dx, float dy);
    // Resize annotation hit at (x,y) to new bounding rect
    bool resizeAnnotAt(int pageNum, float x, float y,
                       float newX0, float newY0, float newX1, float newY1);
    // Returns the bounding rect (PDF device coords) of any annotation at (x,y)
    QRectF annotRectAt(int pageNum, float x, float y) const;
    bool deleteAnnotAt(int pageNum, float x, float y);

    // Image
    bool insertImage(int pageNum, const QString &imgPath,
                     float cx, float cy, float w, float h);

    // Page ops
    bool rotatePage(int pageNum, int degrees);
    bool deletePage(int pageNum);
    bool duplicatePage(int pageNum);
    bool insertBlankPage(int afterPageNum);
    bool movePage(int from, int to);

    // Returns bounding rect (fitz device space) of the first text block on the page.
    // Returns null rect if the page has no text.
    QRectF firstTextRect(int pageNum);

    // Full-text search — returns bounding rects (PDF coords) of all hits on page
    QVector<QRectF> searchText(int pageNum, const QString &query) const;

    // Document outline (bookmarks / table of contents)
    struct OutlineItem {
        QString title;
        int page = -1;  // 0-based page index, -1 if unknown
        QVector<OutlineItem> children;
    };
    QVector<OutlineItem> getOutline() const;

    // PDF tools
    // Merge: appends all pages from each path in 'paths' to current doc
    bool mergeFrom(const QStringList &paths);
    // Extract pages (1-based page numbers) to a new file
    bool extractPages(const QString &outPath, const QList<int> &pages1based);
    // Save compressed copy (imageQuality 0–100, 0=lossless)
    bool saveCopy(const QString &outPath, int imageQuality = 60);
    // Add semi-transparent text watermark on every page
    bool addWatermarkText(const QString &text, int fontSize = 48,
                          float opacity = 0.15f, int angleDeg = 45);

    // Form fields
    struct FormField {
        QString name;
        int type; // PDF_WIDGET_TYPE_*
        QString value;
        QVector<QString> choices;
        bool checked = false;
        int pageNum = 0;
        QRectF rect; // bounding rect in PDF page space
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
    fz_buffer *m_docBuf = nullptr;

    mutable QMap<int, QVector<WordBox>> m_wordCache;
    mutable QMap<int, QVector<CharBox>> m_charCache;

    static constexpr int MAX_HISTORY = 20;
    QVector<QByteArray> m_history;
    QVector<QByteArray> m_redoStack;

    QByteArray toBytes() const;
    bool fromBytes(const QByteArray &data);

    QPixmap pixmapFromMupdf(fz_page *page, float zoom) const;
    pdf_page* loadPdfPage(int pageNum) const;
};
