#include <QApplication>
#include <QWidget>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QSettings>
#include <QIcon>
#include <QPixmap>
#include <QTranslator>
#include "MainWindow.h"
#include "Theme.h"

class AnimatedSplash : public QWidget {
    Q_OBJECT
public:
    explicit AnimatedSplash() : QWidget(nullptr,
        Qt::SplashScreen | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint) {
        setFixedSize(480, 280);
        m_timer.setInterval(16);
        connect(&m_timer, &QTimer::timeout, this, [this]() {
            m_shimmer = fmod(m_shimmer + 0.018f, 1.0f);
            update();
        });
        m_timer.start();
    }

    void setMsg(const QString &msg) { m_msg = msg; update(); }

    void finish(QWidget *w) {
        m_timer.stop();
        // Fade out quickly
        QTimer::singleShot(300, this, &AnimatedSplash::close);
        Q_UNUSED(w);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // Background
        p.fillRect(rect(), QColor("#1E1E1E"));

        // Subtle gradient at top
        QLinearGradient topGrad(0, 0, 0, 80);
        topGrad.setColorAt(0, QColor(0, 120, 212, 25));
        topGrad.setColorAt(1, Qt::transparent);
        p.fillRect(QRect(0, 0, width(), 80), topGrad);

        // PDF Icon (simplified)
        auto drawIcon = [&](QRect r) {
            int fold = r.width() / 5;
            QPainterPath page;
            page.moveTo(r.left(), r.top());
            page.lineTo(r.right() - fold, r.top());
            page.lineTo(r.right(), r.top() + fold);
            page.lineTo(r.right(), r.bottom());
            page.lineTo(r.left(), r.bottom());
            page.closeSubpath();
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, 230));
            p.drawPath(page);
            QPainterPath tri;
            tri.moveTo(r.right() - fold, r.top());
            tri.lineTo(r.right(), r.top() + fold);
            tri.lineTo(r.right() - fold, r.top() + fold);
            tri.closeSubpath();
            p.setBrush(QColor(200, 200, 200, 180));
            p.drawPath(tri);
            QRect band(r.left(), r.bottom() - r.height()/3, r.width(), r.height()/3);
            p.setBrush(QColor(0xDC, 0x35, 0x45));
            p.drawRect(band);
            p.setPen(Qt::white);
            QFont f; f.setPixelSize(band.height() / 2); f.setBold(true);
            p.setFont(f);
            p.drawText(band, Qt::AlignCenter, "PDF");
        };
        drawIcon(QRect(width()/2 - 28, 28, 56, 68));

        // Title
        p.setPen(Qt::white);
        p.setFont(QFont("Segoe UI", 22, QFont::Bold));
        p.drawText(QRect(0, 108, width(), 40), Qt::AlignCenter, "PDF Editor");

        // Subtitle
        p.setPen(QColor("#666666"));
        p.setFont(QFont("Segoe UI", 11));
        p.drawText(QRect(0, 150, width(), 26), Qt::AlignCenter,
                   "Professioneller PDF-Editor");

        // Loading bar track
        QRect barBg(60, 206, width()-120, 5);
        p.setBrush(QColor("#2A2A2A"));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(barBg, 2.5, 2.5);

        // Animated shimmer sweep (left -> right)
        float barW  = barBg.width();
        float shimW = barW * 0.42f;
        float shimX = m_shimmer * (barW + shimW) - shimW;
        QLinearGradient grad(barBg.left() + shimX, 0, barBg.left() + shimX + shimW, 0);
        grad.setColorAt(0.0f, QColor(0, 120, 212, 0));
        grad.setColorAt(0.35f, QColor(0, 120, 212, 230));
        grad.setColorAt(0.65f, QColor(0, 120, 212, 230));
        grad.setColorAt(1.0f, QColor(0, 120, 212, 0));
        p.setBrush(grad);
        p.drawRoundedRect(barBg, 2.5, 2.5);

        // Status message
        p.setPen(QColor("#555555"));
        p.setFont(QFont("Segoe UI", 9));
        p.drawText(QRect(0, 222, width(), 18), Qt::AlignCenter, m_msg);

        // Version
        p.drawText(QRect(0, 252, width(), 18), Qt::AlignCenter, "Version 1.0.0");
    }

private:
    QTimer  m_timer;
    float   m_shimmer = 0.f;
    QString m_msg = "Starte…";
};

#include "main.moc"

int main(int argc, char *argv[]) {
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    app.setApplicationName("PDFEditor");
    app.setOrganizationName("PDFEditor");
    app.setApplicationDisplayName("PDF Editor");
    app.setApplicationVersion("1.0.0");

    // Load language from saved settings
    {
        QSettings s("PDFEditor", "PDFEditor");
        QString lang = s.value("ui/language", "de").toString();
        if (lang != "de") {
            auto *translator = new QTranslator(&app);
            QString qmPath = QCoreApplication::applicationDirPath() + "/" + lang + ".qm";
            if (translator->load(qmPath)) {
                app.installTranslator(translator);
            }
        }
    }

    // App icon
    {
        QPixmap iconPm(256, 256);
        iconPm.fill(Qt::transparent);
        QPainter ip(&iconPm);
        ip.setRenderHint(QPainter::Antialiasing);
        ip.setBrush(QColor(220, 53, 69));
        ip.setPen(Qt::NoPen);
        ip.drawRoundedRect(iconPm.rect(), 40, 40);
        ip.setBrush(Qt::white);
        ip.drawRoundedRect(QRect(46, 26, 164, 204), 8, 8);
        ip.setBrush(QColor(220, 53, 69));
        int bandH = 72, bandY = 26 + 204 - bandH;
        ip.drawRoundedRect(QRect(46, bandY, 164, bandH), 8, 8);
        ip.drawRect(QRect(46, bandY, 164, 12));
        ip.setPen(Qt::white);
        ip.setFont(QFont("Segoe UI", 36, QFont::Bold));
        ip.drawText(QRect(46, bandY, 164, bandH), Qt::AlignCenter, "PDF");
        ip.end();
        app.setWindowIcon(QIcon(iconPm));
    }

    // Show animated splash FIRST – before any heavy work
    AnimatedSplash *splash = new AnimatedSplash;
    splash->show();
    app.processEvents();
    app.processEvents();

    // Create main window (stylesheet applied inside buildToolbar/etc. is skipped until show)
    splash->setMsg("Initialisiere…");
    app.processEvents();
    MainWindow win;

    // Apply theme after construction so widget-tree is already complete (one pass only)
    app.setStyleSheet(Theme::darkSheet());
    app.processEvents();

    QSettings s("PDFEditor", "PDFEditor");
    if (s.contains("geometry")) win.restoreGeometry(s.value("geometry").toByteArray());

    splash->setMsg("Bereit.");
    app.processEvents();

    win.show();
    splash->finish(&win);

    if (argc > 1) {
        QString path = QString::fromLocal8Bit(argv[1]);
        win.openFile(path);
    }

    return app.exec();
}
