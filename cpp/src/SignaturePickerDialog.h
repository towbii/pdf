#pragma once
#include <QDialog>
#include <QListWidget>
#include <QLabel>
#include <QString>
#include <QStringList>

class SignaturePickerDialog : public QDialog {
    Q_OBJECT
public:
    explicit SignaturePickerDialog(QWidget *parent = nullptr);
    QString selectedPath() const { return m_selected; }

private:
    QListWidget *m_list = nullptr;
    QLabel      *m_preview = nullptr;
    QString      m_selected;
    QString      m_sigsDir;

    void loadSignatures();
    void addNewSignature();
    void deleteSelected();
};
