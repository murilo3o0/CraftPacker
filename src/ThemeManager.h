#pragma once
#include <QString>
#include <QPalette>
#include <QApplication>

namespace ThemeManager {

inline const QString& DarkStylesheet() {
    static const QString ss = R"(
        /* === CRAFTPACKER V3 FLUENT DARK THEME === */
        QMainWindow, QDialog, QWidget#centralWidget {
            background-color: #1a1b2e;
            color: #e0e0e0;
        }
        QMenuBar {
            background-color: #14152a;
            color: #b0b0b0;
            border-bottom: 1px solid #2a2b4a;
            padding: 2px;
            font-weight: 500;
        }
        QMenuBar::item:selected {
            background-color: #3a3b6e;
            border-radius: 4px;
        }
        QMenu {
            background-color: #1e1f36;
            border: 1px solid #3a3b5e;
            border-radius: 6px;
            padding: 4px;
        }
        QMenu::item {
            padding: 6px 24px;
            border-radius: 4px;
        }
        QMenu::item:selected {
            background-color: #4a4b8a;
        }
        QGroupBox {
            border: 1px solid #2e2f52;
            border-radius: 8px;
            margin-top: 12px;
            padding: 16px 12px 12px 12px;
            font-weight: bold;
            color: #c0c0d0;
            background-color: rgba(20, 21, 42, 0.5);
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            padding: 2px 8px;
            background-color: #1a1b2e;
            border: 1px solid #3a3b6e;
            border-radius: 4px;
            left: 12px;
        }
        QLabel {
            color: #d0d0e0;
            font-size: 10pt;
        }
        QLineEdit, QTextEdit, QPlainTextEdit, QSpinBox {
            background-color: #1e1f38;
            color: #e0e0f0;
            border: 1px solid #3a3b5e;
            border-radius: 6px;
            padding: 8px 10px;
            font-size: 10pt;
            selection-background-color: #4a6cf7;
        }
        QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QSpinBox:focus {
            border: 1px solid #4a6cf7;
            background-color: #222342;
        }
        QComboBox {
            background-color: #1e1f38;
            color: #e0e0f0;
            border: 1px solid #3a3b5e;
            border-radius: 6px;
            padding: 8px 10px;
            font-size: 10pt;
        }
        QComboBox:focus {
            border: 1px solid #4a6cf7;
        }
        QComboBox::drop-down {
            border: none;
            width: 24px;
        }
        QComboBox QAbstractItemView {
            background-color: #1e1f38;
            color: #e0e0f0;
            border: 1px solid #3a3b5e;
            border-radius: 4px;
            selection-background-color: #4a6cf7;
        }
        QListWidget, QTreeWidget {
            background-color: #1a1b30;
            color: #e0e0e0;
            border: 1px solid #2e2f52;
            border-radius: 8px;
            alternate-background-color: #1e1f38;
            outline: none;
        }
        QListWidget::item, QTreeWidget::item {
            padding: 6px 8px;
            border-radius: 4px;
        }
        QListWidget::item:hover, QTreeWidget::item:hover {
            background-color: #2a2b4a;
        }
        QListWidget::item:selected, QTreeWidget::item:selected {
            background-color: #3a4b8a;
        }
        QHeaderView::section {
            background-color: #22234a;
            color: #b0b0c0;
            padding: 6px;
            border: 1px solid #2e2f52;
            font-weight: bold;
        }
        QPushButton {
            background-color: #3a4b8a;
            color: white;
            border: none;
            padding: 8px 18px;
            border-radius: 6px;
            font-size: 10pt;
            font-weight: 600;
        }
        QPushButton:hover {
            background-color: #4a6cf7;
        }
        QPushButton:pressed {
            background-color: #2a3b7a;
        }
        QPushButton:disabled {
            background-color: #2a2b44;
            color: #5a5a7a;
        }
        QPushButton#downloadSelectedButton, QPushButton#downloadAllButton {
            background-color: #2a7a3a;
        }
        QPushButton#downloadSelectedButton:hover, QPushButton#downloadAllButton:hover {
            background-color: #3a9a4a;
        }
        QScrollBar:vertical {
            background: #1a1b30;
            width: 10px;
            margin: 0;
            border-radius: 5px;
        }
        QScrollBar::handle:vertical {
            background: #3a3b5e;
            min-height: 30px;
            border-radius: 5px;
        }
        QScrollBar::handle:vertical:hover {
            background: #4a6cf7;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0;
        }
        QScrollBar:horizontal {
            background: #1a1b30;
            height: 10px;
            border-radius: 5px;
        }
        QScrollBar::handle:horizontal {
            background: #3a3b5e;
            min-width: 30px;
            border-radius: 5px;
        }
        QSplitter::handle {
            background-color: #2a2b4a;
        }
        QSplitter::handle:horizontal {
            width: 3px;
        }
        QSplitter::handle:vertical {
            height: 3px;
        }
        QStatusBar {
            background-color: #14152a;
            color: #8a8aaa;
            border-top: 1px solid #2a2b4a;
            font-size: 9pt;
        }
        QStatusBar QPushButton {
            background-color: transparent;
            border: 1px solid #3a3b5e;
            border-radius: 4px;
            padding: 2px 10px;
            font-size: 8pt;
            color: #8a8aaa;
        }
        QStatusBar QPushButton:hover {
            background-color: #2a2b4a;
            color: #e0e0e0;
        }
        QProgressBar {
            background-color: #1e1f38;
            border: 1px solid #3a3b5e;
            border-radius: 4px;
            text-align: center;
            color: white;
            height: 20px;
        }
        QProgressBar::chunk {
            background-color: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #4a6cf7, stop:0.5 #6a8cf7, stop:1 #4a6cf7);
            border-radius: 3px;
        }
        QDialog {
            background-color: #1a1b2e;
        }
        QDialogButtonBox QPushButton {
            min-width: 80px;
        }
    )";
    return ss;
}

// Accent colors for Modrinth (Green) and CurseForge (Orange)
namespace Colors {
    inline const QColor ModrinthGreen() { return QColor("#1bd96a"); }
    inline const QColor CurseForgeOrange() { return QColor("#f16436"); }
    inline const QColor DependencyPurple() { return QColor("#8e44ad"); }
    inline const QColor ErrorRed() { return QColor("#e74c3c"); }
    inline const QColor SuccessGreen() { return QColor("#2ecc71"); }
    inline const QColor WarningYellow() { return QColor("#f39c12"); }
    inline const QColor UpdateAvailable() { return QColor("#3498db"); }
}

inline void ApplyTheme(QApplication* app) {
    app->setStyleSheet(DarkStylesheet());
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor("#1a1b2e"));
    darkPalette.setColor(QPalette::WindowText, QColor("#e0e0e0"));
    darkPalette.setColor(QPalette::Base, QColor("#1e1f38"));
    darkPalette.setColor(QPalette::AlternateBase, QColor("#222342"));
    darkPalette.setColor(QPalette::ToolTipBase, QColor("#2a2b4a"));
    darkPalette.setColor(QPalette::ToolTipText, QColor("#e0e0e0"));
    darkPalette.setColor(QPalette::Text, QColor("#e0e0f0"));
    darkPalette.setColor(QPalette::Button, QColor("#3a3b6e"));
    darkPalette.setColor(QPalette::ButtonText, QColor("#e0e0e0"));
    darkPalette.setColor(QPalette::BrightText, QColor("#ff0000"));
    darkPalette.setColor(QPalette::Highlight, QColor("#4a6cf7"));
    darkPalette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    app->setPalette(darkPalette);
}

} // namespace ThemeManager