#pragma once
#include <QDialog>
#include <QLabel>
#include <QString>

class SignatureDialog : public QDialog {
    Q_OBJECT
public:
    explicit SignatureDialog(QWidget *parent = nullptr);
    QString savedPath() const { return m_path; }

private:
    QString m_path;
    QLabel *m_preview;

    void choosePng();
    void drawSignature();
};
