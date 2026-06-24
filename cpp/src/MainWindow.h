#pragma once
#include <QMainWindow>
#include <QAction>
#include <QToolBar>
#include <QLabel>
#include <QSlider>
#include <QSplitter>
#include <QStackedWidget>
#include <QListWidget>
#include <QMap>
#include "PdfDocument.h"
#include "PdfView.h"
#include "ThumbnailPanel.h"
#include "KeybindDialog.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    void openFile(const QString &path);

protected:
    void closeEvent(QCloseEvent *ev) override;
    void dragEnterEvent(QDragEnterEvent *ev) override;
    void dropEvent(QDropEvent *ev) override;
    void keyPressEvent(QKeyEvent *ev) override;

private:
    // Core
    PdfDocument    *m_pdf;
    PdfView        *m_view;
    ThumbnailPanel *m_thumbs;
    QSplitter      *m_splitter;

    // Welcome screen
    QStackedWidget *m_stack;
    QWidget        *m_welcomeWidget;
    QListWidget    *m_recentList;

    // UI state
    bool   m_modified = false;
    bool   m_darkMode = true;
    Tool   m_tool     = Tool::Select;

    // Status bar
    QLabel *m_statusPage;
    QLabel *m_statusZoom;
    QLabel *m_statusFile;

    // Toolbar
    QSlider *m_zoomSlider;

    // Tool actions (checkable)
    QAction *m_actSelect, *m_actHighlight, *m_actPen,
            *m_actEraser, *m_actText, *m_actSignature;
    QAction *m_actUndo, *m_actRedo;

    // Keybind action map
    QList<ActionDef>       m_actionDefs;
    QMap<QString,QAction*> m_actMap;

    // Recent files
    static constexpr int MAX_RECENT = 10;
    void addRecentFile(const QString &path);
    QStringList recentFiles() const;
    void rebuildRecentList();

    // Builders
    QWidget *buildWelcomeWidget();
    void buildMenus();
    void buildToolbar();
    void buildStatusBar();
    QAction *makeTool(const QString &icon, const QString &label,
                      const QString &tip, Tool toolMode);
    QAction *makeAct(const QString &id, const QString &text,
                     const QString &tip, const QString &defaultKey,
                     const QString &category = {});

    void applyTheme();
    void updateTitle();
    void updateUndoState();
    void onModified();
    void showEditor();
    void showWelcome();

    // File ops
    void openDialog();
    void saveFile();
    void saveAs();
    void closeFile();

    // Actions
    void undo();
    void redo();
    void zoomIn();
    void zoomOut();
    void zoomFit();
    void zoomActual();
    void rotateLeft();
    void rotateRight();
    void deletePage();
    void duplicatePage();
    void insertImageToPage();
    void openFormFiller();
    void openSignatureDialog();
    void openKeybinds();
    void openSettings();
    void registerAsDefaultPdf();
    void toggleThumbs();
    void showAbout();

    void setActiveTool(Tool t);
    void setModified(bool m);
    bool confirmDiscard();
    QString shortcutFor(const QString &id, const QString &defaultKey) const;
};
