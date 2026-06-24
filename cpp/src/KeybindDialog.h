#pragma once
#include <QDialog>
#include <QMap>
#include <QKeySequenceEdit>
#include <QAction>

struct ActionDef {
    QString id;
    QString category;
    QString name;
    QKeySequence defaultShortcut;
};

class KeybindDialog : public QDialog {
    Q_OBJECT
public:
    KeybindDialog(const QList<ActionDef> &defs,
                  QMap<QString, QAction*> &actMap,
                  QWidget *parent = nullptr);

private:
    QList<ActionDef> m_defs;
    QMap<QString, QAction*> &m_actMap;
    QMap<QString, QKeySequenceEdit*> m_editors;

    void resetAll();
    void apply();
};
