#include "SignatureDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QLabel>
#include <QFrame>
#include <QPixmap>
#include <QTabWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QMessageBox>
#include <QStandardPaths>
#include <QDateTime>
#include <QDir>

// ── Drawing canvas (QWidget-based for reliable mouse events) ─────────────────
class DrawCanvas : public QWidget {
public:
    explicit DrawCanvas(QWidget *parent = nullptr) : QWidget(parent) {
        setFixedSize(400, 180);
        setAttribute(Qt::WA_OpaquePaintEvent);
        setCursor(Qt::CrossCursor);
        clearDrawing();
    }

    void clearDrawing() {
        m_pix = QPixmap(size());
        m_pix.fill(Qt::white);
        m_hasContent = false;
        update();
    }

    bool hasContent() const { return m_hasContent; }
    QPixmap canvas() const { return m_pix; }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.drawPixmap(0, 0, m_pix);
        p.setPen(QPen(QColor("#555555"), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(rect().adjusted(0, 0, -1, -1));
    }

    void mousePressEvent(QMouseEvent *ev) override {
        if (ev->button() != Qt::LeftButton) return;
        m_drawing = true;
        m_lastPt = ev->position().toPoint();
    }

    void mouseMoveEvent(QMouseEvent *ev) override {
        if (!m_drawing || !(ev->buttons() & Qt::LeftButton)) return;
        QPoint cur = ev->position().toPoint();
        QPainter p(&m_pix);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(Qt::black, 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawLine(m_lastPt, cur);
        m_lastPt = cur;
        m_hasContent = true;
        update();
    }

    void mouseReleaseEvent(QMouseEvent *ev) override {
        if (ev->button() == Qt::LeftButton) m_drawing = false;
    }

private:
    QPixmap m_pix;
    QPoint  m_lastPt;
    bool    m_drawing    = false;
    bool    m_hasContent = false;
};

// ── SignatureDialog ──────────────────────────────────────────────────────────

SignatureDialog::SignatureDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Unterschrift"));
    setModal(true);
    setMinimumWidth(440);

    auto *lay = new QVBoxLayout(this);
    lay->setSpacing(10);
    lay->setContentsMargins(16, 16, 16, 16);

    auto *tabs = new QTabWidget;

    // ── Tab 1: Draw ──────────────────────────────────────────
    auto *drawPage = new QWidget;
    auto *drawLay  = new QVBoxLayout(drawPage);
    drawLay->setContentsMargins(8, 8, 8, 8);

    auto *hint = new QLabel(tr("Halten Sie die Maustaste gedrückt und zeichnen Sie Ihre Unterschrift:"));
    hint->setWordWrap(true);
    hint->setStyleSheet("color: #888888; font-size: 11px;");
    drawLay->addWidget(hint);
    drawLay->addSpacing(6);

    auto *canvas = new DrawCanvas;
    drawLay->addWidget(canvas, 0, Qt::AlignCenter);

    auto *clearBtn = new QPushButton(tr("Löschen"));
    connect(clearBtn, &QPushButton::clicked, canvas, &DrawCanvas::clearDrawing);
    drawLay->addWidget(clearBtn, 0, Qt::AlignRight);
    tabs->addTab(drawPage, tr("Zeichnen"));

    // ── Tab 2: Upload PNG ────────────────────────────────────
    auto *uploadPage = new QWidget;
    auto *uploadLay  = new QVBoxLayout(uploadPage);
    uploadLay->setContentsMargins(8, 8, 8, 8);
    m_preview = new QLabel(tr("Kein Bild gewählt"));
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setFixedHeight(160);
    m_preview->setStyleSheet("border: 1px dashed #636366; border-radius: 6px;");
    auto *chooseBtn = new QPushButton(tr("PNG / JPG auswählen …"));
    connect(chooseBtn, &QPushButton::clicked, this, &SignatureDialog::choosePng);
    uploadLay->addWidget(m_preview);
    uploadLay->addWidget(chooseBtn, 0, Qt::AlignRight);
    tabs->addTab(uploadPage, tr("Datei"));

    lay->addWidget(tabs);

    auto *bb = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText(tr("Einfügen"));
    bb->button(QDialogButtonBox::Cancel)->setText(tr("Abbrechen"));

    connect(bb, &QDialogButtonBox::accepted, this, [=]() {
        if (tabs->currentIndex() == 0) {
            if (!canvas->hasContent()) {
                QMessageBox::information(this, tr("Leere Unterschrift"),
                    tr("Bitte zeichnen Sie zuerst eine Unterschrift."));
                return;
            }
            QString tmpDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
            QDir().mkpath(tmpDir);
            QString fname = tmpDir + "/sig_" +
                QDateTime::currentDateTime().toString("yyyyMMddHHmmss") + ".png";

            QPixmap px = canvas->canvas();
            QImage img = px.toImage();
            int left = img.width(), right = 0, top = img.height(), bottom = 0;
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x)
                    if (img.pixel(x, y) != qRgb(255, 255, 255)) {
                        left   = qMin(left,   x); right  = qMax(right,  x);
                        top    = qMin(top,    y); bottom = qMax(bottom, y);
                    }
            if (right > left && bottom > top) {
                int margin = 4;
                QImage cropped = img.copy(
                    qMax(0, left - margin), qMax(0, top - margin),
                    right - left + 2*margin, bottom - top + 2*margin);
                cropped.save(fname, "PNG");
                m_path = fname;
                accept();
            }
        } else {
            if (m_path.isEmpty()) {
                QMessageBox::information(this, tr("Keine Datei"),
                    tr("Bitte wählen Sie zuerst eine Bilddatei aus."));
                return;
            }
            accept();
        }
    });
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);
}

void SignatureDialog::choosePng() {
    QString f = QFileDialog::getOpenFileName(
        this, tr("Unterschrift wählen"), {},
        tr("Bilder (*.png *.jpg *.jpeg *.bmp)"));
    if (f.isEmpty()) return;
    m_path = f;
    QPixmap px(f);
    if (!px.isNull())
        m_preview->setPixmap(px.scaledToWidth(360, Qt::SmoothTransformation));
}
