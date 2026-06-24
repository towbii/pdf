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

SignaturePickerDialog::SignaturePickerDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Unterschrift auswählen"));
    setMinimumSize(520, 380);

    m_sigsDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                + "/signatures";
    QDir().mkpath(m_sigsDir);

    // Migrate legacy single-signature file
    QString legacySig = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                        + "/saved_signature.png";
    if (QFile::exists(legacySig)) {
        QString dest = m_sigsDir + "/Unterschrift_1.png";
        if (!QFile::exists(dest)) QFile::rename(legacySig, dest);
        else QFile::remove(legacySig);
    }

    auto *lay = new QVBoxLayout(this);
    lay->setSpacing(12);
    lay->setContentsMargins(16, 16, 16, 16);

    lay->addWidget(new QLabel(tr("Gespeicherte Unterschriften:")));

    // List with thumbnails
    m_list = new QListWidget;
    m_list->setViewMode(QListWidget::IconMode);
    m_list->setIconSize(QSize(140, 60));
    m_list->setSpacing(8);
    m_list->setResizeMode(QListWidget::Adjust);
    m_list->setMovement(QListWidget::Static);
    m_list->setFixedHeight(200);
    m_list->setStyleSheet(
        "QListWidget { background: #1E1E1E; border: 1px solid #333; border-radius: 8px; outline: none; }"
        "QListWidget::item { background: #2D2D2D; border-radius: 6px; margin: 4px; padding: 4px; }"
        "QListWidget::item:selected { background: #0C3F6C; border: 1px solid #0078D4; }"
        "QListWidget::item:hover { background: #3A3A3A; }");
    lay->addWidget(m_list);

    // Preview
    m_preview = new QLabel(tr("Keine Auswahl"));
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumHeight(80);
    m_preview->setStyleSheet(
        "border: 1px dashed #444; border-radius: 8px; background: #252526;");
    lay->addWidget(m_preview);

    // Buttons row
    auto *btnRow = new QHBoxLayout;
    auto *newBtn = new QPushButton(tr("+ Neue Unterschrift"));
    newBtn->setStyleSheet(
        "QPushButton { background: #0078D4; color: white; border: none;"
        "  border-radius: 8px; padding: 7px 16px; font-weight: 600; }"
        "QPushButton:hover { background: #1088E4; }");
    auto *delBtn = new QPushButton(tr("Löschen"));
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
    bb->button(QDialogButtonBox::Ok)->setText(tr("Verwenden"));
    bb->button(QDialogButtonBox::Cancel)->setText(tr("Abbrechen"));
    lay->addWidget(bb);

    connect(m_list, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *cur, QListWidgetItem *) {
        if (!cur) { m_preview->setText(tr("Keine Auswahl")); m_selected.clear(); return; }
        m_selected = cur->data(Qt::UserRole).toString();
        QPixmap px(m_selected);
        if (!px.isNull())
            m_preview->setPixmap(px.scaledToWidth(360, Qt::SmoothTransformation));
        else
            m_preview->setText(tr("Vorschau nicht verfügbar"));
    });

    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        m_selected = item->data(Qt::UserRole).toString();
        accept();
    });

    connect(newBtn, &QPushButton::clicked, this, &SignaturePickerDialog::addNewSignature);
    connect(delBtn, &QPushButton::clicked, this, &SignaturePickerDialog::deleteSelected);

    connect(bb, &QDialogButtonBox::accepted, this, [this]() {
        if (m_selected.isEmpty()) {
            QMessageBox::information(this, tr("Keine Auswahl"),
                tr("Bitte wählen Sie eine Unterschrift aus."));
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
    const QStringList files = dir.entryList({"*.png", "*.jpg"}, QDir::Files, QDir::Time);
    for (const QString &f : files) {
        QString path = m_sigsDir + "/" + f;
        QPixmap px(path);
        QIcon icon;
        if (!px.isNull()) {
            QPixmap thumb(140, 60);
            thumb.fill(Qt::white);
            QPainter p(&thumb);
            QPixmap scaled = px.scaled(QSize(134, 54), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            p.drawPixmap((thumb.width()-scaled.width())/2, (thumb.height()-scaled.height())/2, scaled);
            icon = QIcon(thumb);
        }
        QString label = QFileInfo(f).baseName();
        auto *item = new QListWidgetItem(icon, label);
        item->setData(Qt::UserRole, path);
        item->setSizeHint(QSize(156, 90));
        m_list->addItem(item);
    }
}

void SignaturePickerDialog::addNewSignature() {
    SignatureDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted || dlg.savedPath().isEmpty()) return;

    // Generate a unique name
    QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString dest = m_sigsDir + "/Unterschrift_" + ts + ".png";
    QFile::copy(dlg.savedPath(), dest);

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
    auto res = QMessageBox::question(this, tr("Löschen"),
        tr("Diese Unterschrift wirklich löschen?"),
        QMessageBox::Yes | QMessageBox::No);
    if (res != QMessageBox::Yes) return;
    QFile::remove(path);
    m_selected.clear();
    m_preview->setText(tr("Keine Auswahl"));
    loadSignatures();
}
