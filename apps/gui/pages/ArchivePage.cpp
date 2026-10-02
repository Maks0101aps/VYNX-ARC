#include "ArchivePage.h"
#include "../widgets/BreadcrumbBar.h"
#include "../widgets/IconProvider.h"
#include <QtWidgets>
ArchivePage::ArchivePage(QAction *extract, QAction *add, QAction *test, QAction *remove,
                         QAction *rename, QAction *hash, const QList<QAction *> &overflow,
                         QWidget *parent)
    : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    auto *navigation = new QHBoxLayout;
    navigation->setSpacing(4);
    back = new QToolButton;
    up = new QToolButton;
    for (auto pair : {qMakePair(back, QString("back")), qMakePair(up, QString("up"))}) {
        pair.first->setIcon(Icons::get(pair.second));
        pair.first->setIconSize({20, 20});
        pair.first->setObjectName("ghost");
        pair.first->setFixedSize(36, 36);
        navigation->addWidget(pair.first);
    }
    back->setToolTip(tr("Back (Alt+Left)"));
    back->setAccessibleName(tr("Back"));
    up->setToolTip(tr("Up (Alt+Up)"));
    up->setAccessibleName(tr("Up"));
    breadcrumb = new BreadcrumbBar;
    navigation->addWidget(breadcrumb, 3);
    search = new QLineEdit;
    search->setPlaceholderText(tr("Search filenames"));
    search->setAccessibleName(tr("Search filenames"));
    search->setClearButtonEnabled(true);
    search->setMinimumWidth(170);
    search->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    navigation->addWidget(search, 2);
    layout->addLayout(navigation);
    auto *commands = new QHBoxLayout;
    commands->setSpacing(6);
    auto button = [&](QAction *action, const QString &label) {
        auto *control = new QToolButton;
        control->setDefaultAction(action);
        control->setText(label);
        connect(action, &QAction::changed, control, [control, label] { control->setText(label); });
        control->setToolButtonStyle(Qt::ToolButtonTextOnly);
        control->setToolTip(action->text());
        control->setAccessibleName(label);
        commands->addWidget(control);
        return control;
    };
    button(extract, tr("Extract"))->setObjectName("primary");
    mutations_.append(button(add, tr("Add")));
    test_ = button(test, tr("Test"));
    mutations_.append(button(remove, tr("Delete")));
    mutations_.append(button(rename, tr("Rename")));
    hash_ = button(hash, tr("Hash"));
    commands->addStretch();
    auto *more = new QToolButton;
    more->setIcon(Icons::get("more"));
    more->setFixedSize(36, 36);
    more->setToolTip(tr("More actions"));
    more->setAccessibleName(tr("More actions"));
    auto *menu = new QMenu(more);
    for (auto *action : overflow)
        menu->addAction(action);
    more->setMenu(menu);
    more->setPopupMode(QToolButton::InstantPopup);
    commands->addWidget(more);
    layout->addLayout(commands);
    table = new QTableView;
    table->setMouseTracking(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setSortingEnabled(true);
    table->setAlternatingRowColors(false);
    table->setShowGrid(false);
    table->verticalHeader()->hide();
    table->verticalHeader()->setDefaultSectionSize(36);
    table->setIconSize({20, 20});
    table->horizontalHeader()->setStretchLastSection(false);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->setAccessibleName(tr("Archive contents"));
    layout->addWidget(table, 1);
}
void ArchivePage::setWritable(bool writable) {
    writable_ = writable;
    adapt();
}
void ArchivePage::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    adapt();
}
void ArchivePage::adapt() {
    if (table && table->model())
        for (int column : {2, 3, 4})
            table->setColumnHidden(column, columnPreferences_.contains(column)
                                               ? !columnPreferences_.value(column)
                                               : width() < (column == 3 ? 900 : 800));
    for (int i = 0; i < mutations_.size(); ++i)
        mutations_[i]->setVisible(writable_ && (i == 0 || width() >= 820));
    hash_->setVisible(!writable_);
    test_->setVisible(true);
}
void ArchivePage::setColumnPreference(int column, bool visible) {
    columnPreferences_[column] = visible;
    table->setColumnHidden(column, !visible);
    adapt();
}
