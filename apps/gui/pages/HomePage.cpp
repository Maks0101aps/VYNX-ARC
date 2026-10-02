#include "HomePage.h"
#include "../widgets/IconProvider.h"
#include "../widgets/RecentDelegate.h"
#include <QtWidgets>
HomePage::HomePage(QAction *open, QAction *create, QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 16);
    layout->setSpacing(12);
    auto *heading = new QHBoxLayout;
    auto *logo = new QLabel;
    logo->setPixmap(QIcon(":/assets/icons/vynx-arc.png").pixmap(40, 40));
    auto *brand = new QLabel(QStringLiteral("VYNX ARC"));
    brand->setObjectName("brand");
    heading->addWidget(logo);
    heading->addWidget(brand);
    heading->addStretch();
    layout->addLayout(heading);
    auto *subtitle = new QLabel(tr("Fast. Secure. Lightweight."));
    subtitle->setObjectName("muted");
    layout->addWidget(subtitle);
    auto *actions = new QHBoxLayout;
    for (auto *action : {open, create}) {
        auto *button = new QToolButton;
        button->setDefaultAction(action);
        button->setText(action == open ? tr("Open archive") : tr("Create archive"));
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setObjectName(action == open ? "primary" : "secondary");
        actions->addWidget(button);
    }
    actions->addStretch();
    layout->addLayout(actions);
    auto *drop = new QLabel(tr("Drop an archive here"));
    drop->setObjectName("dropTarget");
    drop->setAlignment(Qt::AlignCenter);
    drop->setMinimumHeight(64);
    layout->addWidget(drop);
    auto *recentHeader = new QHBoxLayout;
    recentHeader->addWidget(new QLabel(tr("Recent archives")));
    recentHeader->addStretch();
    auto *clear = new QToolButton;
    clear->setText(tr("Clear history"));
    clear->setObjectName("ghost");
    recentHeader->addWidget(clear);
    layout->addLayout(recentHeader);
    connect(clear, &QToolButton::clicked, this, &HomePage::clearHistory);
    recent = new QListWidget;
    recent->setItemDelegate(new RecentDelegate(recent));
    recent->setObjectName("recent");
    recent->setAccessibleName(tr("Recent archives"));
    layout->addWidget(recent, 1);
    connect(recent, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        emit openRequested(item->data(Qt::UserRole).toString());
    });
    recent->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(recent, &QWidget::customContextMenuRequested, this, [this](QPoint point) {
        auto *item = recent->itemAt(point);
        if (!item)
            return;
        auto path = item->data(Qt::UserRole).toString();
        QMenu menu;
        connect(menu.addAction(tr("Open archive")), &QAction::triggered, this,
                [this, path] { emit openRequested(path); });
        connect(menu.addAction(tr("Show in Explorer")), &QAction::triggered, this, [path] {
            QProcess::startDetached("explorer.exe",
                                    {QStringLiteral("/select,"), QDir::toNativeSeparators(path)});
        });
        connect(menu.addAction(tr("Remove from recent list")), &QAction::triggered, this,
                [this, path] { emit removeRecent(path); });
        menu.exec(recent->viewport()->mapToGlobal(point));
    });
}
