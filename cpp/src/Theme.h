#pragma once
#include <QString>

namespace Theme {

inline QString darkSheet() {
    return QStringLiteral(R"(
/* ── Global ───────────────────────────────────────── */
* { font-family: "Segoe UI", system-ui; font-size: 13px; }
QMainWindow { background: #1E1E1E; color: #D4D4D4; }
QDialog     { background: #252526; color: #D4D4D4; }
QWidget     { background: transparent; color: #D4D4D4; }

/* ── Menu Bar ─────────────────────────────────────── */
QMenuBar {
    background: #2D2D2D;
    color: #CCCCCC;
    border-bottom: 1px solid #1A1A1A;
    padding: 0 4px;
    spacing: 0;
}
QMenuBar::item {
    padding: 7px 12px;
    background: transparent;
    border-radius: 4px;
}
QMenuBar::item:selected { background: #3C3C3C; color: #FFFFFF; }
QMenuBar::item:pressed  { background: #0078D4; color: #FFFFFF; }

/* ── Menus ────────────────────────────────────────── */
QMenu {
    background: #2D2D2D;
    color: #D4D4D4;
    border: 1px solid #3C3C3C;
    border-radius: 8px;
    padding: 4px;
}
QMenu::item {
    padding: 7px 32px 7px 14px;
    border-radius: 4px;
}
QMenu::item:selected { background: #0078D4; color: #FFFFFF; }
QMenu::item:disabled { color: #555555; }
QMenu::separator {
    height: 1px;
    background: #3C3C3C;
    margin: 4px 8px;
}
QMenu::indicator { width: 16px; margin-left: 4px; }

/* ── Tool Bars ────────────────────────────────────── */
QToolBar {
    background: #2D2D2D;
    border: none;
    border-bottom: 1px solid #1A1A1A;
    padding: 5px 10px;
    spacing: 2px;
}
QToolBar::separator {
    width: 1px;
    background: #444444;
    margin: 4px 8px;
}

/* ── Tool Buttons ─────────────────────────────────── */
QToolButton {
    background: transparent;
    color: #C8C8C8;
    border: none;
    border-radius: 6px;
    padding: 5px 12px;
    font-size: 13px;
    min-height: 28px;
}
QToolButton:hover {
    background: #3A3A3A;
    color: #FFFFFF;
}
QToolButton:pressed {
    background: #252525;
    color: #FFFFFF;
}
QToolButton:checked {
    background: #0078D4;
    color: #FFFFFF;
    font-weight: 600;
}
QToolButton:checked:hover { background: #1088E4; }
QToolButton:disabled      { color: #555555; }

/* ── Scroll Area ──────────────────────────────────── */
QScrollArea {
    background: #181818;
    border: none;
}
QAbstractScrollArea > QWidget { background: #181818; }

/* ── Scrollbars ───────────────────────────────────── */
QScrollBar:vertical {
    background: transparent;
    width: 10px;
    margin: 2px 0;
}
QScrollBar::handle:vertical {
    background: #4A4A4A;
    min-height: 32px;
    border-radius: 5px;
    margin: 0 2px;
}
QScrollBar::handle:vertical:hover   { background: #6A6A6A; }
QScrollBar::handle:vertical:pressed { background: #0078D4; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }

QScrollBar:horizontal {
    background: transparent;
    height: 10px;
    margin: 0 2px;
}
QScrollBar::handle:horizontal {
    background: #4A4A4A;
    min-width: 32px;
    border-radius: 5px;
    margin: 2px 0;
}
QScrollBar::handle:horizontal:hover   { background: #6A6A6A; }
QScrollBar::handle:horizontal:pressed { background: #0078D4; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }

/* ── Splitter ─────────────────────────────────────── */
QSplitter { background: #1A1A1A; }
QSplitter::handle { background: #2A2A2A; }
QSplitter::handle:horizontal { width: 1px; }
QSplitter::handle:vertical   { height: 1px; }

/* ── Status Bar ───────────────────────────────────── */
QStatusBar {
    background: #007ACC;
    color: #FFFFFF;
    font-size: 12px;
    border-top: none;
}
QStatusBar::item { border: none; }
QStatusBar QLabel { color: #FFFFFF; padding: 0 10px; font-size: 12px; }

/* ── Slider ───────────────────────────────────────── */
QSlider::groove:horizontal {
    background: #3C3C3C;
    height: 4px;
    border-radius: 2px;
}
QSlider::handle:horizontal {
    background: #FFFFFF;
    width: 14px;
    height: 14px;
    margin: -5px 0;
    border-radius: 7px;
    border: 2px solid #888888;
}
QSlider::handle:horizontal:hover {
    background: #60CDFF;
    border-color: #60CDFF;
}
QSlider::handle:horizontal:pressed {
    background: #0078D4;
    border-color: #0078D4;
}
QSlider::sub-page:horizontal { background: #0078D4; border-radius: 2px; }

/* ── Labels ───────────────────────────────────────── */
QLabel { color: #D4D4D4; background: transparent; }

/* ── Buttons ──────────────────────────────────────── */
QPushButton {
    background: #0E639C;
    color: #FFFFFF;
    border: none;
    border-radius: 6px;
    padding: 7px 20px;
    font-size: 13px;
    min-width: 80px;
}
QPushButton:hover   { background: #1177BB; }
QPushButton:pressed { background: #094771; }
QPushButton:default { background: #0078D4; border: 1px solid #1097F3; }
QPushButton:default:hover { background: #1088E4; }
QPushButton:disabled { background: #2A2A2A; color: #555555; }

/* ── Input fields ─────────────────────────────────── */
QLineEdit {
    background: #3C3C3C;
    color: #CCCCCC;
    border: 1px solid #3F3F3F;
    border-radius: 5px;
    padding: 5px 9px;
    selection-background-color: #0078D4;
}
QLineEdit:focus { border-color: #007ACC; }

QTextEdit {
    background: #252526;
    color: #CCCCCC;
    border: 1px solid #3C3C3C;
    border-radius: 5px;
    padding: 6px;
    selection-background-color: #0078D4;
}
QTextEdit:focus { border-color: #007ACC; }

/* ── List widgets ─────────────────────────────────── */
QListWidget {
    background: #252526;
    color: #CCCCCC;
    border: 1px solid #3C3C3C;
    border-radius: 6px;
    outline: none;
    padding: 3px;
}
QListWidget::item { padding: 7px 10px; border-radius: 5px; }
QListWidget::item:hover    { background: #2A2D2E; }
QListWidget::item:selected { background: #094771; color: #FFFFFF; }

QTreeWidget {
    background: #252526;
    color: #CCCCCC;
    border: 1px solid #3C3C3C;
    border-radius: 4px;
    outline: none;
}
QTreeWidget::item:hover    { background: #2A2D2E; }
QTreeWidget::item:selected { background: #094771; color: #FFFFFF; }
QHeaderView::section {
    background: #2D2D2D;
    color: #AAAAAA;
    padding: 6px 8px;
    border: none;
    border-bottom: 1px solid #1A1A1A;
    border-right: 1px solid #3C3C3C;
    font-size: 12px;
    font-weight: 600;
}

/* ── Combo Box ────────────────────────────────────── */
QComboBox {
    background: #3C3C3C;
    color: #CCCCCC;
    border: 1px solid #3F3F3F;
    border-radius: 5px;
    padding: 5px 9px;
}
QComboBox:hover { border-color: #007ACC; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView {
    background: #2D2D2D;
    color: #CCCCCC;
    border: 1px solid #454545;
    border-radius: 4px;
    selection-background-color: #0078D4;
    outline: none;
}

/* ── Tab Widget ───────────────────────────────────── */
QTabWidget::pane {
    background: #252526;
    border: 1px solid #3C3C3C;
    border-radius: 0 6px 6px 6px;
}
QTabBar::tab {
    background: #2D2D2D;
    color: #AAAAAA;
    border: 1px solid #3C3C3C;
    border-bottom: none;
    padding: 7px 18px;
    border-radius: 5px 5px 0 0;
    margin-right: 2px;
}
QTabBar::tab:selected { background: #252526; color: #FFFFFF; }
QTabBar::tab:hover:!selected { background: #3A3A3A; color: #CCCCCC; }

/* ── Tool Tips ────────────────────────────────────── */
QToolTip {
    background: #2D2D2D;
    color: #CCCCCC;
    border: 1px solid #454545;
    border-radius: 5px;
    padding: 5px 10px;
    font-size: 12px;
}

/* ── KeySequenceEdit ──────────────────────────────── */
QKeySequenceEdit {
    background: #3C3C3C;
    color: #CCCCCC;
    border: 1px solid #3C3C3C;
    border-radius: 5px;
    padding: 4px 8px;
}

/* ── Checkboxes ───────────────────────────────────── */
QCheckBox { color: #D4D4D4; spacing: 8px; }
QCheckBox::indicator {
    width: 16px; height: 16px;
    background: #3C3C3C;
    border: 1px solid #555555;
    border-radius: 3px;
}
QCheckBox::indicator:checked { background: #0078D4; border-color: #0078D4; }

/* ── Message Box ──────────────────────────────────── */
QMessageBox { background: #252526; }
QMessageBox QLabel { color: #D4D4D4; }
QDialogButtonBox QPushButton { min-width: 80px; }
)");
}

inline QString lightSheet() {
    return QStringLiteral(R"(
* { font-family: "Segoe UI", system-ui; font-size: 13px; }
QMainWindow { background: #F3F3F3; color: #1C1C1C; }
QDialog     { background: #FFFFFF; color: #1C1C1C; }
QWidget     { background: transparent; color: #1C1C1C; }
QMenuBar {
    background: #FFFFFF; color: #1C1C1C;
    border-bottom: 1px solid #E0E0E0; padding: 0 4px;
}
QMenuBar::item { padding: 7px 12px; border-radius: 4px; }
QMenuBar::item:selected { background: #E8E8E8; }
QMenuBar::item:pressed  { background: #0078D4; color: #FFFFFF; }
QMenu {
    background: #FFFFFF; color: #1C1C1C;
    border: 1px solid #D0D0D0; border-radius: 8px; padding: 4px;
}
QMenu::item { padding: 7px 32px 7px 14px; border-radius: 4px; }
QMenu::item:selected { background: #0078D4; color: #FFFFFF; }
QMenu::separator { height: 1px; background: #E0E0E0; margin: 4px 8px; }
QToolBar {
    background: #F9F9F9; border-bottom: 1px solid #E0E0E0; padding: 5px 10px;
}
QToolBar::separator { width: 1px; background: #D0D0D0; margin: 4px 8px; }
QToolButton {
    background: transparent; color: #1C1C1C; border: none;
    border-radius: 6px; padding: 5px 12px; min-height: 28px;
}
QToolButton:hover   { background: #E5E5E5; }
QToolButton:checked { background: #0078D4; color: #FFFFFF; font-weight: 600; }
QScrollArea { background: #E8E8E8; border: none; }
QScrollBar:vertical { background: transparent; width: 10px; }
QScrollBar::handle:vertical { background: #BBBBBB; min-height: 32px; border-radius: 5px; margin: 0 2px; }
QScrollBar::handle:vertical:hover { background: #999999; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QStatusBar { background: #0078D4; color: #FFFFFF; font-size: 12px; }
QStatusBar::item { border: none; }
QStatusBar QLabel { color: #FFFFFF; padding: 0 10px; }
QPushButton {
    background: #0078D4; color: #FFFFFF; border: none;
    border-radius: 6px; padding: 7px 20px; min-width: 80px;
}
QPushButton:hover { background: #1088E4; }
QPushButton:pressed { background: #006CBE; }
QLineEdit {
    background: #FFFFFF; color: #1C1C1C; border: 1px solid #CCCCCC;
    border-radius: 5px; padding: 5px 9px; selection-background-color: #0078D4;
}
QLineEdit:focus { border-color: #0078D4; }
QLabel { color: #1C1C1C; background: transparent; }
QSlider::groove:horizontal { background: #D0D0D0; height: 4px; border-radius: 2px; }
QSlider::handle:horizontal {
    background: #FFFFFF; width: 14px; height: 14px; margin: -5px 0;
    border-radius: 7px; border: 2px solid #AAAAAA;
}
QSlider::handle:horizontal:hover { background: #0078D4; border-color: #0078D4; }
QSlider::sub-page:horizontal { background: #0078D4; border-radius: 2px; }
QSplitter::handle { background: #E0E0E0; }
QSplitter::handle:horizontal { width: 1px; }
QToolTip { background: #FFFFFF; color: #1C1C1C; border: 1px solid #CCCCCC; border-radius: 5px; padding: 5px 10px; }
QKeySequenceEdit { background: #FFFFFF; color: #1C1C1C; border: 1px solid #CCCCCC; border-radius: 5px; padding: 4px 8px; }
QDialogButtonBox QPushButton { min-width: 80px; }
)");
}

} // namespace Theme
