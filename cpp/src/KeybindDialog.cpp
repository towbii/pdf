#include "KeybindDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFrame>
#include <QSizePolicy>
#include <QSettings>

KeybindDialog::KeybindDialog(const QList<ActionDef> &defs,
                              QMap<QString, QAction*> &actMap,
                              QWidget *parent)
    : QDialog(parent), m_defs(defs), m_actMap(actMap) {
    setWindowTitle(tr("Tastenkürzel bearbeiten"));
    setModal(true);
    setMinimumSize(500, 580);

    auto *lay = new QVBoxLayout(this);
    lay->setSpacing(10);
    lay->setContentsMargins(14, 14, 14, 14);

    auto *hint = new QLabel(tr("Klick in ein Feld, dann Tastenkombination drücken."));
    hint->setStyleSheet("color: #8e8e93; font-size: 11px;");
    lay->addWidget(hint);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *content = new QWidget;
    auto *grid = new QGridLayout(content);
    grid->setSpacing(6);
    grid->setContentsMargins(2, 4, 2, 4);
    grid->setColumnStretch(1, 1);
    grid->setColumnMinimumWidth(0, 180);

    int row = 0;
    QString prevCat;
    QSettings settings("PDFEditor", "PDFEditor");

    for (const ActionDef &def : m_defs) {
        if (def.category != prevCat) {
            if (!prevCat.isEmpty()) {
                auto *sep = new QFrame;
                sep->setFrameShape(QFrame::HLine);
                sep->setStyleSheet("color: #3a3a3c;");
                grid->addWidget(sep, row++, 0, 1, 3);
            }
            auto *catLbl = new QLabel(def.category);
            catLbl->setStyleSheet(
                "font-weight: bold; font-size: 11px; color: #8e8e93; padding-top: 4px;");
            grid->addWidget(catLbl, row++, 0, 1, 3);
            prevCat = def.category;
        }

        auto *lbl = new QLabel(def.name);
        lbl->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

        QKeySequence cur = QKeySequence(settings.value(
            "shortcuts/" + def.id, def.defaultShortcut.toString()).toString());
        auto *ed = new QKeySequenceEdit(cur);
        ed->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_editors[def.id] = ed;

        auto *clr = new QPushButton("✕");
        clr->setFixedSize(24, 24);
        clr->setToolTip(tr("Kürzel entfernen"));
        clr->setStyleSheet("QPushButton{padding:0;font-size:10px;min-width:0;}");
        connect(clr, &QPushButton::clicked, this, [ed]() { ed->clear(); });

        grid->addWidget(lbl, row, 0);
        grid->addWidget(ed,  row, 1);
        grid->addWidget(clr, row, 2);
        row++;
    }

    scroll->setWidget(content);
    lay->addWidget(scroll);

    auto *botLay = new QHBoxLayout;
    auto *resetBtn = new QPushButton(tr("Alle zurücksetzen"));
    connect(resetBtn, &QPushButton::clicked, this, &KeybindDialog::resetAll);
    botLay->addWidget(resetBtn);
    botLay->addStretch();

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, this, &KeybindDialog::apply);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    botLay->addWidget(bb);
    lay->addLayout(botLay);
}

void KeybindDialog::resetAll() {
    for (const ActionDef &def : m_defs) {
        if (m_editors.contains(def.id))
            m_editors[def.id]->setKeySequence(def.defaultShortcut);
    }
}

void KeybindDialog::apply() {
    QSettings settings("PDFEditor", "PDFEditor");
    for (const ActionDef &def : m_defs) {
        if (!m_editors.contains(def.id)) continue;
        QKeySequence ks = m_editors[def.id]->keySequence();
        settings.setValue("shortcuts/" + def.id, ks.toString());
        if (m_actMap.contains(def.id)) {
            m_actMap[def.id]->setShortcut(ks);
            // Update tooltip to show new shortcut
            QString base = m_actMap[def.id]->property("baseTip").toString();
            if (!ks.isEmpty())
                m_actMap[def.id]->setToolTip(base + "  " + ks.toString());
            else
                m_actMap[def.id]->setToolTip(base);
        }
    }
    accept();
}
