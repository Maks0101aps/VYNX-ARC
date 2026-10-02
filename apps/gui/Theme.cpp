#include "Theme.h"
#include <QtWidgets>
void applyUiTheme(const QString &theme) {
    bool dark = theme == "dark" ||
                (theme == "system" && qApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark);
    struct Colors {
        QString window, surface, elevated, border, text, muted, hover, selection;
    };
    Colors c = dark ? Colors{"#13151a", "#191c22", "#20242c", "#303640",
                             "#f0f2f6", "#a5adbb", "#292f3a", "#253857"}
                    : Colors{"#f4f5f7", "#ffffff", "#edf0f4", "#d8dde5",
                             "#202632", "#5f6877", "#e5e9f0", "#dce8fb"};
    QPalette palette = qApp->style()->standardPalette();
    for (auto role :
         {QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::ToolTipText})
        palette.setColor(role, QColor(c.text));
    palette.setColor(QPalette::Window, QColor(c.window));
    palette.setColor(QPalette::Base, QColor(c.surface));
    palette.setColor(QPalette::Button, QColor(c.elevated));
    palette.setColor(QPalette::ToolTipBase, QColor(c.elevated));
    palette.setColor(QPalette::Highlight, QColor(c.selection));
    palette.setColor(QPalette::HighlightedText, QColor(c.text));
    palette.setColor(QPalette::PlaceholderText, QColor(c.muted));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(c.muted));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(c.muted));
    qApp->setPalette(palette);
    QFont font("Segoe UI Variable", 10);
    if (!QFontInfo(font).exactMatch())
        font.setFamily("Segoe UI");
    qApp->setFont(font);
    QString css =
        QString(
            "QWidget { font-size: 14px; } QLabel#brand {font-size:24pt;font-weight:600;} "
            "QLabel#muted {color:%1;} "
            "QPushButton,QToolButton {min-height:24px;padding:5px 12px;border:1px solid "
            "transparent;border-radius:8px;background:%2;} "
            "QPushButton:hover,QToolButton:hover {background:%3;} "
            "QPushButton:pressed,QToolButton:pressed {background:%4;} "
            "QPushButton:focus,QToolButton:focus,QLineEdit:focus,QTableView:focus {border:1px "
            "solid #2563eb;} "
            "QToolButton#ghost,QToolButton#breadcrumb {padding:0px 4px;background:transparent;} "
            "QToolButton#ghost:hover,QToolButton#breadcrumb:hover {background:%3;} "
            "QPushButton#primary,QToolButton#primary {background:#2563eb;color:white;} "
            "QPushButton#primary:hover,QToolButton#primary:hover {background:#3474f3;} "
            "QLineEdit,QComboBox,QSpinBox {padding:7px;border:1px solid %5;border-radius:8px;} "
            "QHeaderView::section {padding:7px 8px;border:0;background:%2;color:%1;} "
            "QTableView {border:1px solid %5;background:%6;} QTableView::item {padding:0px "
            "6px;border:0;} QTableView::item:hover {background:%3;} "
            "QTableView::item:selected {background:%4;} QListWidget#recent "
            "{border:0;background:transparent;} QListWidget#recent::item "
            "{padding:10px;border-radius:8px;} "
            "QLabel#dropTarget {border:1px dashed %5;border-radius:10px;color:%1;background:%6;} "
            "QWidget#operation,QLabel#toast {background:%2;border:1px solid "
            "%5;border-radius:10px;} QLabel#toast {padding:12px;} "
            "QProgressBar "
            "{min-height:14px;border:0;border-radius:4px;text-align:center;background:%3;} "
            "QProgressBar::chunk {background:#2563eb;border-radius:4px;} "
            "QStatusBar {border-top:1px solid %5;} QStatusBar::item {border:0;} "
            "QToolButton::menu-indicator {image:none;}")
            .arg(c.muted, c.elevated, c.hover, c.selection, c.border, c.surface);
    qApp->setStyleSheet(css);
}
