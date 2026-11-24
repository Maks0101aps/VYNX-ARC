#include "ArchivePage.h"
#include "../widgets/BreadcrumbBar.h"
#include "../widgets/IconProvider.h"
#include "../widgets/TableRubberBand.h"
#include <QtWidgets>

ArchivePage::ArchivePage(QAction *extract, QAction *add, QAction *test, QAction *remove,
                         QAction *rename, QAction *hash, const QList<QAction *> &overflow,
                         QAction *view, QAction *find, QAction *wizard, QAction *info,
                         QAction *repair, QWidget *parent)
    : QWidget(parent),
      extractAction(extract),
      addAction(add),
      testAction(test),
      removeAction(remove),
      renameAction(rename),
      hashAction(hash),
      viewAction(view),
      findAction(find),
      wizardAction(wizard),
      infoAction(info),
      repairAction(repair ? repair : hash),
      overflowActions_(overflow) {

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    // Fallbacks if optional actions were null
    if (!viewAction)
        viewAction = new QAction(tr("View"), this);
    if (!findAction) {
        findAction = new QAction(tr("Find"), this);
        connect(findAction, &QAction::triggered, this, [this] {
            if (search) {
                search->setFocus();
                search->selectAll();
            }
        });
    }
    if (!wizardAction)
        wizardAction = new QAction(tr("Wizard"), this);
    if (!infoAction)
        infoAction = new QAction(tr("Info"), this);
    if (!repairAction)
        repairAction = new QAction(tr("Repair"), this);

    // 1. Prominent Top Action Toolbar (Commands)
    auto *commands = new QHBoxLayout;
    commands->setContentsMargins(0, 0, 0, 0);
    commands->setSpacing(4);

    auto createToolbarButton = [this, commands](QAction *action, const QString &label,
                                                const QString &iconName,
                                                const QString &objectName = {}) {
        auto *btn = new QToolButton;
        action->setText(label);
        btn->setDefaultAction(action);
        const auto icon = Icons::get(iconName);
        action->setIcon(icon);
        btn->setIcon(icon);
        btn->setText(label);
        btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        btn->setIconSize({32, 32});
        btn->setMinimumSize(60, 56);
        btn->setProperty("toolbarAction", true);
        btn->setToolTip(!action->toolTip().isEmpty() ? action->toolTip() : label);
        btn->setAccessibleName(label);
        if (!objectName.isEmpty())
            btn->setObjectName(objectName);
        connect(action, &QAction::changed, btn, [btn, label, icon] {
            btn->setText(label);
            btn->setIcon(icon);
        });
        commands->addWidget(btn);
        return btn;
    };

    // Cluster 1 (Core Archiving): Add, Extract To, Test, View, Delete, Rename
    addButton = createToolbarButton(addAction, tr("Add"), "tb_add");
    extractButton = createToolbarButton(extractAction, tr("Extract To"), "tb_extract");
    testButton = createToolbarButton(testAction, tr("Test"), "tb_test");
    viewButton = createToolbarButton(viewAction, tr("View"), "tb_view");
    deleteButton = createToolbarButton(removeAction, tr("Delete"), "tb_delete");
    renameButton = createToolbarButton(renameAction, tr("Rename"), "tb_rename");

    mutations_ = {addButton, deleteButton, renameButton};
    test_ = testButton;

    // Separator between Cluster 1 and Cluster 2
    separator1_ = new QFrame;
    separator1_->setFrameShape(QFrame::VLine);
    separator1_->setFrameShadow(QFrame::Sunken);
    separator1_->setObjectName("toolbarSeparator");
    commands->addWidget(separator1_);

    // Cluster 2 (Inspection & Navigation): Find, Wizard, Info
    findButton = createToolbarButton(findAction, tr("Find"), "tb_find");
    wizardButton = createToolbarButton(wizardAction, tr("Wizard"), "tb_wizard");
    infoButton = createToolbarButton(infoAction, tr("Info"), "tb_info");

    // Separator between Cluster 2 and Cluster 3
    separator2_ = new QFrame;
    separator2_->setFrameShape(QFrame::VLine);
    separator2_->setFrameShadow(QFrame::Sunken);
    separator2_->setObjectName("toolbarSeparator");
    commands->addWidget(separator2_);

    // Cluster 3 (Tools & Verification): Repair
    repairButton = createToolbarButton(repairAction, tr("Repair"), "tb_repair");
    hash_ = repairButton;

    commands->addStretch();

    // Overflow button
    moreButton = new QToolButton;
    moreButton->setIcon(Icons::get("more"));
    moreButton->setIconSize({20, 20});
    moreButton->setFixedSize(36, 36);
    moreButton->setToolTip(tr("More actions"));
    moreButton->setAccessibleName(tr("More actions"));
    moreButton->setObjectName("ghost");
    moreMenu_ = new QMenu(moreButton);
    for (auto *action : overflowActions_)
        moreMenu_->addAction(action);
    moreButton->setMenu(moreMenu_);
    moreButton->setPopupMode(QToolButton::InstantPopup);
    commands->addWidget(moreButton);

    layout->addLayout(commands);

    // 2. Sub-navigation bar below toolbar
    auto *navigation = new QHBoxLayout;
    navigation->setContentsMargins(0, 0, 0, 0);
    navigation->setSpacing(4);

    back = new QToolButton;
    back->setIcon(Icons::get("back"));
    back->setIconSize({20, 20});
    back->setObjectName("ghost");
    back->setFixedSize(32, 32);
    back->setToolTip(tr("Back (Alt+Left)"));
    back->setAccessibleName(tr("Back"));
    navigation->addWidget(back);

    up = new QToolButton;
    up->setIcon(Icons::get("up"));
    up->setIconSize({20, 20});
    up->setObjectName("ghost");
    up->setFixedSize(32, 32);
    up->setToolTip(tr("Up (Alt+Up)"));
    up->setAccessibleName(tr("Up"));
    navigation->addWidget(up);

    breadcrumb = new BreadcrumbBar;
    navigation->addWidget(breadcrumb, 3);

    search = new QLineEdit;
    search->setPlaceholderText(tr("Search entire archive"));
    search->setAccessibleName(tr("Search entire archive"));
    search->setClearButtonEnabled(true);
    search->setMinimumWidth(170);
    search->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    navigation->addWidget(search, 2);

    layout->addLayout(navigation);

    // 3. Table below navigation
    table = new QTableView;
    table->setMouseTracking(true);
    table->setDragEnabled(true);
    table->setAcceptDrops(true);
    table->setDropIndicatorShown(true);
    table->setDragDropMode(QAbstractItemView::DragDrop);
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
    new TableRubberBand(table, table);
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
    if (table && table->model()) {
        for (int column : {2, 3, 4}) {
            table->setColumnHidden(column, columnPreferences_.contains(column)
                                               ? !columnPreferences_.value(column)
                                               : width() < (column == 3 ? 900 : 800));
        }
    }

    // Cluster 1 mutations:
    if (addButton)
        addButton->setVisible(writable_);

    const bool showWideMutations = writable_ && (width() >= 820);
    if (deleteButton)
        deleteButton->setVisible(showWideMutations);
    if (renameButton)
        renameButton->setVisible(showWideMutations);

    // Secondary clusters fold when width < 780
    const bool showSecondary = (width() >= 780);
    if (findButton)
        findButton->setVisible(showSecondary);
    if (wizardButton)
        wizardButton->setVisible(showSecondary);
    if (infoButton)
        infoButton->setVisible(showSecondary);
    if (repairButton)
        repairButton->setVisible(showSecondary);

    if (separator1_)
        separator1_->setVisible(showSecondary);
    if (separator2_)
        separator2_->setVisible(showSecondary);

    // Dynamically update moreMenu_ actions
    if (moreMenu_) {
        moreMenu_->clear();
        if (!showSecondary) {
            if (findAction)
                moreMenu_->addAction(findAction);
            if (wizardAction)
                moreMenu_->addAction(wizardAction);
            if (infoAction)
                moreMenu_->addAction(infoAction);
            if (repairAction)
                moreMenu_->addAction(repairAction);
            moreMenu_->addSeparator();
        }
        if (writable_ && width() < 820) {
            if (removeAction)
                moreMenu_->addAction(removeAction);
            if (renameAction)
                moreMenu_->addAction(renameAction);
            moreMenu_->addSeparator();
        }
        for (auto *action : overflowActions_) {
            if (!moreMenu_->actions().contains(action))
                moreMenu_->addAction(action);
        }
    }
}

void ArchivePage::setColumnPreference(int column, bool visible) {
    columnPreferences_[column] = visible;
    table->setColumnHidden(column, !visible);
    adapt();
}
