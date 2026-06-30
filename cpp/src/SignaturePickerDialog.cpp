#include "SignaturePickerDialog.h"
#include "SignatureDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QScrollArea>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QMessageBox>
#include <QListWidgetItem>
#include <QPainter>
#include <QMimeData>
#include <QUrl>
#include <QBuffer>
#ifdef Q_OS_WIN
#  include <windows.h>
#  include <wincrypt.h>
#endif

// ── DPAPI: encrypt/decrypt with the current Windows user's key ─────────────
// Encrypted files can only be read by the same Windows account on the same
// machine — protects against disk theft and other-user access.

QByteArray SignaturePickerDialog::dpEncrypt(const QByteArray &plain) {
#ifdef Q_OS_WIN
    DATA_BLOB in, out;
    in.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plain.constData()));
    in.cbData = static_cast<DWORD>(plain.size());
    if (CryptProtectData(&in, nullptr, nullptr, nullptr, nullptr,
                         CRYPTPROTECT_LOCAL_MACHINE & 0, &out)) {
        QByteArray result(reinterpret_cast<char*>(out.pbData), out.cbData);
        LocalFree(out.pbData);
        return result;
    }
#endif
    return plain; // fallback: store unencrypted if DPAPI unavailable
}

QByteArray SignaturePickerDialog::dpDecrypt(const QByteArray &cipher) {
#ifdef Q_OS_WIN
    DATA_BLOB in, out;
    in.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(cipher.constData()));
    in.cbData = static_cast<DWORD>(cipher.size());
    if (CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out)) {
        QByteArray result(reinterpret_cast<char*>(out.pbData), out.cbData);
        LocalFree(out.pbData);
        return result;
    }
#endif
    return cipher; // fallback
}

QString SignaturePickerDialog::saveEncrypted(const QPixmap &px, const QString &destPath) {
    QByteArray pngBytes;
    QBuffer buf(&pngBytes);
    buf.open(QIODevice::WriteOnly);
    if (!px.save(&buf, "PNG")) return {};
    buf.close();

    QByteArray encrypted = dpEncrypt(pngBytes);
    QFile f(destPath);
    if (!f.open(QIODevice::WriteOnly)) return {};
    f.write(encrypted);
    f.close();
    return destPath;
}

QPixmap SignaturePickerDialog::loadEncrypted(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QByteArray cipher = f.readAll();
    f.close();
    QByteArray plain = dpDecrypt(cipher);
    QPixmap px;
    px.loadFromData(plain, "PNG");
    return px;
}

SignaturePickerDialog::SignaturePickerDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Select Signature"));
    setAcceptDrops(true);
    setMinimumSize(640, 700);

    m_sigsDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                + "/signatures";
    QDir().mkpath(m_sigsDir);

    // Migrate legacy single-signature file
    QString legacySig = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                        + "/saved_signature.png";
    if (QFile::exists(legacySig)) {
        QString dest = m_sigsDir + "/Signature_1.png";
        if (!QFile::exists(dest)) QFile::rename(legacySig, dest);
        else QFile::remove(legacySig);
    }

    auto *lay = new QVBoxLayout(this);
    lay->setSpacing(12);
    lay->setContentsMargins(16, 16, 16, 16);

    lay->addWidget(new QLabel(tr("Saved Signatures:")));

    // List with large thumbnails so the full signature is visible
    m_list = new QListWidget;
    m_list->setViewMode(QListWidget::IconMode);
    m_list->setIconSize(QSize(220, 90));
    m_list->setSpacing(10);
    m_list->setResizeMode(QListWidget::Adjust);
    m_list->setMovement(QListWidget::Static);
    m_list->setFixedHeight(260);
    m_list->setStyleSheet(
        "QListWidget { background: #1E1E1E; border: 1px solid #333; border-radius: 8px; outline: none; }"
        "QListWidget::item { background: #2D2D2D; border-radius: 6px; margin: 4px; padding: 4px; }"
        "QListWidget::item:selected { background: #0C3F6C; border: 1px solid #0078D4; }"
        "QListWidget::item:hover { background: #3A3A3A; }");
    lay->addWidget(m_list);

    auto *dropHint = new QLabel(tr("↓ Drag a PNG or JPG here to import as signature"));
    dropHint->setAlignment(Qt::AlignCenter);
    dropHint->setStyleSheet("color:#666; font-size:11px; padding:4px;");
    lay->addWidget(dropHint);

    // Preview — tall enough to show a full signature comfortably
    m_preview = new QLabel(tr("No selection"));
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumHeight(220);
    m_preview->setStyleSheet(
        "border: 1px dashed #444; border-radius: 8px; background: #252526;");
    lay->addWidget(m_preview, 1);

    // Buttons row
    auto *btnRow = new QHBoxLayout;
    auto *newBtn = new QPushButton(tr("+ New Signature"));
    newBtn->setStyleSheet(
        "QPushButton { background: #0078D4; color: white; border: none;"
        "  border-radius: 8px; padding: 7px 16px; font-weight: 600; }"
        "QPushButton:hover { background: #1088E4; }");
    auto *delBtn = new QPushButton(tr("Delete"));
    delBtn->setStyleSheet(
        "QPushButton { background: #CC3333; color: white; border: none;"
        "  border-radius: 8px; padding: 7px 16px; }"
        "QPushButton:hover { background: #DD4444; }");
    btnRow->addWidget(newBtn);
    btnRow->addWidget(delBtn);
    btnRow->addStretch();
    lay->addLayout(btnRow);

    // Dialog buttons
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText(tr("Use"));
    bb->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
    lay->addWidget(bb);

    connect(m_list, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *cur, QListWidgetItem *) {
        if (!cur) { m_preview->setText(tr("No selection")); m_selected.clear(); return; }
        m_selected = cur->data(Qt::UserRole).toString();
        QPixmap px = loadEncrypted(m_selected);
        if (!px.isNull()) {
            // Fit inside preview area keeping aspect ratio
            QSize maxSz(m_preview->width() - 16, m_preview->minimumHeight() - 16);
            if (maxSz.width() < 200) maxSz.setWidth(600);
            m_preview->setPixmap(px.scaled(maxSz, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        } else {
            m_preview->setText(tr("Preview not available"));
        }
    });

    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        m_selected = item->data(Qt::UserRole).toString();
        accept();
    });

    connect(newBtn, &QPushButton::clicked, this, &SignaturePickerDialog::addNewSignature);
    connect(delBtn, &QPushButton::clicked, this, &SignaturePickerDialog::deleteSelected);

    connect(bb, &QDialogButtonBox::accepted, this, [this]() {
        if (m_selected.isEmpty()) {
            QMessageBox::information(this, tr("No Selection"),
                tr("Please select a signature."));
            return;
        }
        accept();
    });
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    loadSignatures();
}

void SignaturePickerDialog::loadSignatures() {
    m_list->clear();
    QDir dir(m_sigsDir);

    // Migrate any legacy plain PNG/JPG files to encrypted .sig format
    const QStringList legacy = dir.entryList({"*.png", "*.jpg", "*.jpeg"}, QDir::Files);
    for (const QString &f : legacy) {
        QString oldPath = m_sigsDir + "/" + f;
        QString newPath = m_sigsDir + "/" + QFileInfo(f).baseName() + ".sig";
        if (!QFile::exists(newPath)) {
            QPixmap px(oldPath);
            if (!px.isNull()) saveEncrypted(px, newPath);
        }
        QFile::remove(oldPath);
    }

    // Load encrypted .sig files
    const QStringList files = dir.entryList({"*.sig"}, QDir::Files, QDir::Time);
    for (const QString &f : files) {
        QString path = m_sigsDir + "/" + f;
        QPixmap px = loadEncrypted(path);
        QIcon icon;
        if (!px.isNull()) {
            QPixmap thumb(220, 90);
            thumb.fill(Qt::white);
            QPainter p(&thumb);
            QPixmap scaled = px.scaled(QSize(210, 82), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            p.drawPixmap((thumb.width()-scaled.width())/2,
                         (thumb.height()-scaled.height())/2, scaled);
            icon = QIcon(thumb);
        }
        QString label = QFileInfo(f).baseName();
        auto *item = new QListWidgetItem(icon, label);
        item->setData(Qt::UserRole, path);
        item->setSizeHint(QSize(240, 130));
        m_list->addItem(item);
    }
}

void SignaturePickerDialog::addNewSignature() {
    SignatureDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted || dlg.savedPath().isEmpty()) return;

    QString ts   = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString dest = m_sigsDir + "/Signature_" + ts + ".sig";
    QPixmap px(dlg.savedPath());
    if (!px.isNull()) saveEncrypted(px, dest);
    QFile::remove(dlg.savedPath()); // delete unencrypted temp file immediately

    loadSignatures();
    // Select the newly added item
    for (int i = 0; i < m_list->count(); i++) {
        if (m_list->item(i)->data(Qt::UserRole).toString() == dest) {
            m_list->setCurrentRow(i);
            break;
        }
    }
}

void SignaturePickerDialog::deleteSelected() {
    auto *item = m_list->currentItem();
    if (!item) return;
    QString path = item->data(Qt::UserRole).toString();
    auto res = QMessageBox::question(this, tr("Delete"),
        tr("Really delete this signature?"),
        QMessageBox::Yes | QMessageBox::No);
    if (res != QMessageBox::Yes) return;
    QFile::remove(path);
    m_selected.clear();
    m_preview->setText(tr("No selection"));
    loadSignatures();
}

void SignaturePickerDialog::importImage(const QString &path) {
    QPixmap px(path);
    if (px.isNull()) return;
    QString ts   = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString dest = m_sigsDir + "/Signature_" + ts + ".sig";
    if (saveEncrypted(px, dest).isEmpty()) return;
    loadSignatures();
    for (int i = 0; i < m_list->count(); i++) {
        if (m_list->item(i)->data(Qt::UserRole).toString() == dest) {
            m_list->setCurrentRow(i);
            break;
        }
    }
}

void SignaturePickerDialog::dragEnterEvent(QDragEnterEvent *ev) {
    if (!ev->mimeData()->hasUrls()) return;
    for (const QUrl &u : ev->mimeData()->urls()) {
        QString p = u.toLocalFile().toLower();
        if (p.endsWith(".png") || p.endsWith(".jpg") || p.endsWith(".jpeg")) {
            ev->acceptProposedAction();
            return;
        }
    }
}

void SignaturePickerDialog::dropEvent(QDropEvent *ev) {
    for (const QUrl &u : ev->mimeData()->urls()) {
        QString p = u.toLocalFile();
        QString pl = p.toLower();
        if (pl.endsWith(".png") || pl.endsWith(".jpg") || pl.endsWith(".jpeg"))
            importImage(p);
    }
    ev->acceptProposedAction();
}
