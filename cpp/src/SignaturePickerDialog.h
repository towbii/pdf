#pragma once
#include <QDialog>
#include <QListWidget>
#include <QLabel>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QPixmap>
#include <QDragEnterEvent>
#include <QDropEvent>

class SignaturePickerDialog : public QDialog {
    Q_OBJECT
public:
    explicit SignaturePickerDialog(QWidget *parent = nullptr);
    QString selectedPath() const { return m_selected; }

protected:
    void dragEnterEvent(QDragEnterEvent *ev) override;
    void dropEvent(QDropEvent *ev) override;

private:
    QListWidget *m_list = nullptr;
    QLabel      *m_preview = nullptr;
    QString      m_selected;
    QString      m_sigsDir;

    void loadSignatures();
    void addNewSignature();
    void deleteSelected();
    void importImage(const QString &path);

    // DPAPI helpers — encrypt/decrypt using the current Windows user's key
    static QByteArray dpEncrypt(const QByteArray &plain);
    static QByteArray dpDecrypt(const QByteArray &cipher);

    // Save pixmap as encrypted .sig file; returns saved path or empty on error
    QString saveEncrypted(const QPixmap &px, const QString &destPath);
    // Load pixmap from encrypted .sig file
    static QPixmap loadEncrypted(const QString &path);
};
