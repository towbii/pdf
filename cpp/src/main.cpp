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

static constexpr const char *APP_VERSION = "1.5.1";
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
                   "Professional PDF Editor");

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
        p.drawText(QRect(0, 252, width(), 18), Qt::AlignCenter, QString("Version %1").arg(APP_VERSION));
    }

private:
    QTimer  m_timer;
    float   m_shimmer = 0.f;
    QString m_msg = "Starting…";
};

#include "main.moc"

int main(int argc, char *argv[]) {
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    app.setApplicationName("PDFEditor");
    app.setOrganizationName("PDFEditor");
    app.setApplicationDisplayName("PDF Editor");
    app.setApplicationVersion(APP_VERSION);

    // Load language translator — default is English ("en").
    // Try the embedded Qt resource first (always available), fall back to
    // a .qm file next to the executable for custom/third-party translations.
    {
        QSettings s("PDFEditor", "PDFEditor");
        QString lang = s.value("ui/language", "en").toString();
        // Only install a translator for languages other than the default English source
        if (lang != "en") {
            auto *translator = new QTranslator(&app);
            bool loaded = translator->load(":/i18n/" + lang);
            if (!loaded)
                loaded = translator->load(QCoreApplication::applicationDirPath()
                                          + "/" + lang + ".qm");
            if (loaded)
                app.installTranslator(translator);
        }
    }

    // App icon — dark navy bg / blue-gradient header / orange edit accent
    {
        // Build at 256×256; also produce a 64×64 variant for crisp taskbar rendering
        auto buildIcon = [](int sz) -> QPixmap {
            QPixmap pm(sz, sz);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing);
            p.setRenderHint(QPainter::TextAntialiasing);
            p.setPen(Qt::NoPen);

            const qreal s = sz / 256.0;  // scale factor from 256-unit design

            auto sc = [&](qreal v) { return int(v * s + 0.5); };
            auto sr = [&](qreal x, qreal y, qreal w, qreal h) {
                return QRectF(x * s, y * s, w * s, h * s);
            };

            // 1) Background: dark navy gradient
            {
                QLinearGradient g(0, 0, 0, sz);
                g.setColorAt(0, QColor(0x14, 0x1f, 0x38));
                g.setColorAt(1, QColor(0x07, 0x0c, 0x1a));
                p.setBrush(g);
                p.drawRoundedRect(0, 0, sz, sz, 46 * s, 46 * s);
            }

            // 2) Document drop shadow
            p.setBrush(QColor(0, 0, 0, 64));
            p.drawRoundedRect(sr(50, 33, 158, 196), 13 * s, 13 * s);

            // 3) Document: clean white body
            p.setBrush(QColor(0xf2, 0xf6, 0xfb));
            p.drawRoundedRect(sr(46, 29, 158, 196), 13 * s, 13 * s);

            // 4) Blue-gradient header (top 38% of doc height)
            {
                QLinearGradient g(46 * s, 29 * s, (46 + 158) * s, (29 + 80) * s);
                g.setColorAt(0, QColor(0x1a, 0x8f, 0xff));  // electric blue
                g.setColorAt(1, QColor(0x50, 0x3e, 0xf5));  // deep indigo
                p.setBrush(g);
                p.setClipRect(QRectF(46 * s, 29 * s, 158 * s, 80 * s));
                p.drawRoundedRect(sr(46, 29, 158, 196), 13 * s, 13 * s);
                p.setClipping(false);
            }

            // 5) Folded corner — top-right triangle
            {
                const qreal fz = 26 * s;
                QPointF a((46 + 158) * s - fz, 29 * s);
                QPointF b((46 + 158) * s,       29 * s);
                QPointF c((46 + 158) * s,       29 * s + fz);
                p.setBrush(QColor(0xa8, 0xc4, 0xe0));
                p.drawPolygon(QPolygonF({a, b, c}));
            }

            // 6) "PDF" text centered in header
            {
                QFont f("Segoe UI", sc(27), QFont::Bold);
                p.setFont(f);
                p.setPen(QColor(255, 255, 255, 245));
                p.drawText(QRectF(46 * s, 29 * s, 158 * s, 80 * s),
                           Qt::AlignCenter, "PDF");
                p.setPen(Qt::NoPen);
            }

            // 7) Simulated text lines in document body
            {
                const int lx = 63, baseY = 29 + 80 + 13;
                const int lws[] = {116, 82, 110, 74};
                for (int i = 0; i < 4; i++) {
                    p.setBrush(QColor(0xb0, 0xc8, 0xdf, 210));
                    p.drawRoundedRect(sr(lx, baseY + i * 22, lws[i], 8), 4 * s, 4 * s);
                }
            }

            // 8) Orange edit-accent band at bottom of document
            {
                QLinearGradient g(46 * s, (29 + 196 - 30) * s,
                                  (46 + 158) * s, (29 + 196) * s);
                g.setColorAt(0, QColor(0xff, 0xa0, 0x2e));  // amber
                g.setColorAt(1, QColor(0xf9, 0x6d, 0x10));  // deep orange
                p.setBrush(g);
                p.setClipRect(QRectF(46 * s, (29 + 196 - 30) * s, 158 * s, 30 * s));
                p.drawRoundedRect(sr(46, 29, 158, 196), 13 * s, 13 * s);
                p.setClipping(false);
            }

            // 9) Small white edit-cursor spark inside the orange band
            {
                p.setBrush(QColor(255, 255, 255, 200));
                const int mx = 46 + 79, my = 29 + 196 - 15;  // center of band
                // Vertical cursor bar
                p.drawRoundedRect(sr(mx - 2, my - 7, 4, 14), 2 * s, 2 * s);
                // Horizontal serif top
                p.drawRoundedRect(sr(mx - 6, my - 7, 12, 3), 1 * s, 1 * s);
                // Horizontal serif bottom
                p.drawRoundedRect(sr(mx - 6, my + 4, 12, 3), 1 * s, 1 * s);
            }

            p.end();
            return pm;
        };

        QIcon icon;
        icon.addPixmap(buildIcon(256));
        icon.addPixmap(buildIcon(64));
        icon.addPixmap(buildIcon(32));
        icon.addPixmap(buildIcon(16));
        app.setWindowIcon(icon);
    }

    // Show animated splash FIRST – before any heavy work
    AnimatedSplash *splash = new AnimatedSplash;
    splash->show();
    app.processEvents();
    app.processEvents();

    // Create main window (stylesheet applied inside buildToolbar/etc. is skipped until show)
    splash->setMsg("Initializing…");
    app.processEvents();
    MainWindow win;

    // Apply theme after construction so widget-tree is already complete (one pass only)
    app.setStyleSheet(Theme::darkSheet());
    app.processEvents();

    QSettings s("PDFEditor", "PDFEditor");
    if (s.contains("geometry")) win.restoreGeometry(s.value("geometry").toByteArray());

    splash->setMsg("Ready.");
    app.processEvents();

    win.show();
    splash->finish(&win);

    if (argc > 1) {
        QString path = QString::fromLocal8Bit(argv[1]);
        win.openFile(path);
    }

    return app.exec();
}
