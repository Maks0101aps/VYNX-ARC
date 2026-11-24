#include "Theme.h"
#include <QtWidgets>

void applyUiTheme(const QString &theme) {
    bool dark = theme == "dark" ||
                (theme == "system" && qApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark);
    struct Colors {
        QString window, surface, elevated, border, text, muted, hover, selection, selectedText,
            rubberBandFill, rubberBandBorder;
    };
    Colors c = dark ? Colors{"#13151a", "#191c22", "#20242c", "#303640",
                             "#f0f2f6", "#a5adbb", "#292f3a", "#253857", "#ffffff",
                             "rgba(59, 130, 246, 0.28)", "#3b82f6"}
                    : Colors{"#f4f5f7", "#ffffff", "#edf0f4", "#d8dde5",
                             "#202632", "#5f6877", "#e5e9f0", "#dce8fb", "#1e40af",
                             "rgba(37, 99, 235, 0.22)", "#2563eb"};
    QPalette palette = qApp->style()->standardPalette();
    for (auto role :
         {QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::ToolTipText})
        palette.setColor(role, QColor(c.text));
    palette.setColor(QPalette::Window, QColor(c.window));
    palette.setColor(QPalette::Base, QColor(c.surface));
    palette.setColor(QPalette::Button, QColor(c.elevated));
    palette.setColor(QPalette::ToolTipBase, QColor(c.elevated));
    palette.setColor(QPalette::Highlight, QColor(c.selection));
    palette.setColor(QPalette::HighlightedText, QColor(c.selectedText));
    palette.setColor(QPalette::PlaceholderText, QColor(c.muted));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(c.muted));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(c.muted));
    qApp->setPalette(palette);

    QFont font("Segoe UI Variable", 10);
    if (!QFontInfo(font).exactMatch())
        font.setFamily("Segoe UI");
    qApp->setFont(font);

    QString css = QString(
        "QWidget { font-size: 14px; color: %1; } "
        "QLabel#brand { font-size: 24pt; font-weight: 600; } "
        "QLabel#muted { color: %2; } "
        "QPushButton { min-height: 24px; padding: 5px 12px; border: 1px solid transparent; border-radius: 8px; background: %3; color: %1; } "
        "QPushButton:hover { background: %4; } "
        "QPushButton:pressed { background: %5; } "
        "QPushButton:disabled { color: %2; } "
        "QToolButton { min-height: 24px; padding: 5px 8px; border: 1px solid transparent; border-radius: 8px; background: transparent; color: %1; } "
        "QToolButton:hover { background: %4; } "
        "QToolButton:pressed { background: %5; } "
        "QToolButton:disabled { color: %2; } "
        "QPushButton:focus, QToolButton:focus, QLineEdit:focus, QTableView:focus { border: 1px solid #2563eb; } "
        "QPushButton#primary, QToolButton#primary { background: #2563eb; color: #ffffff; border: 1px solid #1d4ed8; } "
        "QPushButton#primary:hover, QToolButton#primary:hover { background: #3474f3; } "
        "QPushButton#primary:pressed, QToolButton#primary:pressed { background: #1d4ed8; } "
        "QToolButton#ghost, QToolButton#breadcrumb { padding: 0px 4px; background: transparent; } "
        "QToolButton#ghost:hover, QToolButton#breadcrumb:hover { background: %4; } "
        "QFrame#toolbarSeparator { width: 1px; max-width: 1px; margin: 6px 4px; border: none; background: %6; } "
        "QLineEdit, QComboBox, QSpinBox { padding: 7px; border: 1px solid %6; border-radius: 8px; background: %7; color: %1; } "
        "QHeaderView { background: transparent; border: none; } "
        "QHeaderView::section { padding: 6px 8px; border: none; border-right: 1px solid %6; border-bottom: 1px solid %6; background: %3; color: %2; font-weight: 600; } "
        "QHeaderView::section:hover { background: %4; color: %1; } "
        "QTableView { border: 1px solid %6; background: %7; gridline-color: transparent; outline: none; } "
        "QTableView::item { padding: 0px 6px; border: none; } "
        "QTableView::item:hover { background: %4; } "
        "QTableView::item:selected { background: %5; color: %8; } "
        "QListWidget#recent { border: 0; background: transparent; } "
        "QListWidget#recent::item { padding: 10px; border-radius: 8px; } "
        "QLabel#dropTarget { border: 1px dashed %6; border-radius: 10px; color: %2; background: %7; } "
        "QWidget#operation, QLabel#toast { background: %3; border: 1px solid %6; border-radius: 10px; } "
        "QLabel#toast { padding: 12px; } "
        "QFrame#updateBanner { background: %3; border: 1px solid %6; border-radius: 8px; padding: 8px 12px; } "
        "QProgressBar { min-height: 14px; border: 0; border-radius: 4px; text-align: center; background: %4; } "
        "QProgressBar::chunk { background: #2563eb; border-radius: 4px; } "
        "QStatusBar { border-top: 1px solid %6; background: %9; } "
        "QStatusBar::item { border: none; } "
        "QLabel#encryptionLock, QLabel#encryptionIndicator { padding: 2px 4px; margin-right: 4px; } "
        "QRubberBand { background-color: %10; border: 1px solid %11; border-radius: 2px; } "
        "QToolButton::menu-indicator { image: none; }"
    ).arg(c.text)
     .arg(c.muted)
     .arg(c.elevated)
     .arg(c.hover)
     .arg(c.selection)
     .arg(c.border)
     .arg(c.surface)
     .arg(c.selectedText)
     .arg(c.window)
     .arg(c.rubberBandFill)
     .arg(c.rubberBandBorder);

    qApp->setStyleSheet(css);
}
