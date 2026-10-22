#include "TemporaryStorageDialog.h"
#include "../ArchiveModel.h"
#include "../TempStore.h"
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QComboBox>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

TemporaryStorageDialog::TemporaryStorageDialog(TempStore *store, const QSet<QString> &activeNestedIds, QWidget *parent)
    : QDialog(parent), store_(store), activeNestedIds_(activeNestedIds) {
    setWindowTitle(tr("Temporary storage & cache"));
    resize(880, 500);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);

    // Summary header
    auto *summaryLayout = new QHBoxLayout;
    summaryLayout->setSpacing(12);

    auto createSummaryCard = [](const QString &title, QLabel *&valLabel) {
        auto *frame = new QWidget;
        auto *v = new QVBoxLayout(frame);
        v->setContentsMargins(10, 8, 10, 8);
        v->setSpacing(2);
        auto *lblTitle = new QLabel(title);
        lblTitle->setObjectName("muted");
        lblTitle->setStyleSheet("font-size: 11px; font-weight: 500;");
        valLabel = new QLabel(QStringLiteral("0 B (0 items)"));
        valLabel->setStyleSheet("font-size: 14px; font-weight: bold;");
        v->addWidget(lblTitle);
        v->addWidget(valLabel);
        return frame;
    };

    summaryLayout->addWidget(createSummaryCard(tr("Previews & views"), previewSummary_));
    summaryLayout->addWidget(createSummaryCard(tr("Nested archives"), nestedSummary_));
    summaryLayout->addWidget(createSummaryCard(tr("Editable working copies"), editSummary_));
    mainLayout->addLayout(summaryLayout);

    // Filter bar
    auto *filterLayout = new QHBoxLayout;
    filterLayout->addWidget(new QLabel(tr("Category:")));
    categoryFilter_ = new QComboBox;
    categoryFilter_->setObjectName("tempCategoryFilter");
    categoryFilter_->setAccessibleName(tr("Filter temporary files by category"));
    categoryFilter_->addItems({tr("All categories"), tr("Previews"), tr("Nested archives"), tr("Working copies")});
    connect(categoryFilter_, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
    filterLayout->addWidget(categoryFilter_);
    filterLayout->addStretch();
    mainLayout->addLayout(filterLayout);

    // Table
    table_ = new QTableWidget(0, 6, this);
    table_->setObjectName("tempStorageTable");
    table_->setAccessibleName(tr("Temporary items table"));
    table_->setHorizontalHeaderLabels({tr("Category"), tr("Name"), tr("Size"), tr("Status"), tr("Retention reason"), tr("Location")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    table_->verticalHeader()->hide();
    mainLayout->addWidget(table_, 1);

    // Controls
    auto *controls = new QHBoxLayout;
    cleanupButton_ = new QPushButton(tr("Safe cleanup... (Alt+C)"));
    cleanupButton_->setObjectName("primary");
    cleanupButton_->setShortcut(QKeySequence("Alt+C"));
    cleanupButton_->setToolTip(tr("Preview and clean disposable views and closed nested archives"));
    cleanupButton_->setAccessibleName(tr("Safe cleanup"));

    openButton_ = new QPushButton(tr("Open folder (Alt+O)"));
    openButton_->setShortcut(QKeySequence("Alt+O"));
    openButton_->setAccessibleName(tr("Open file location in file explorer"));

    deleteButton_ = new QPushButton(tr("Delete copy..."));
    deleteButton_->setAccessibleName(tr("Explicitly delete selected copy"));

    auto *refreshBtn = new QPushButton(tr("Refresh (F5)"));
    refreshBtn->setShortcut(QKeySequence("F5"));
    refreshBtn->setAccessibleName(tr("Refresh storage data"));

    auto *closeBtn = new QPushButton(tr("Close"));
    closeBtn->setAccessibleName(tr("Close dialog"));

    controls->addWidget(cleanupButton_);
    controls->addWidget(openButton_);
    controls->addWidget(deleteButton_);
    controls->addStretch();
    controls->addWidget(refreshBtn);
    controls->addWidget(closeBtn);
    mainLayout->addLayout(controls);

    connect(refreshBtn, &QPushButton::clicked, this, &TemporaryStorageDialog::refresh);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(cleanupButton_, &QPushButton::clicked, this, &TemporaryStorageDialog::safeCleanup);

    connect(openButton_, &QPushButton::clicked, this, [this] {
        const int row = table_->currentRow();
        if (row < 0)
            return;
        const auto path = table_->item(row, 5)->text();
        QFileInfo fi(path);
        const auto dir = fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath();
        if (QDir(dir).exists())
            QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });

    connect(deleteButton_, &QPushButton::clicked, this, [this] {
        const int row = table_->currentRow();
        if (row < 0 || !store_)
            return;
        if (!table_->item(row, 0)->data(Qt::UserRole + 2).toBool()) {
            QMessageBox::information(this, tr("Temporary files"),
                                     tr("This item is in use or is a working copy. Manage edits in Working copies."));
            return;
        }
        const auto category = table_->item(row, 0)->text();
        const auto name = table_->item(row, 1)->text();
        const auto path = table_->item(row, 5)->text();
        const auto status = table_->item(row, 3)->text();

        QString warning = tr("Are you sure you want to delete '%1'?").arg(name);
        if (category == tr("Working copy") || status.contains(tr("Unsaved"))) {
            warning += QStringLiteral("\n\n") + tr("WARNING: This copy may contain unsaved changes. Deleting it will permanently discard them!");
        }

        if (QMessageBox::question(this, tr("Confirm deletion"), warning,
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
            TempStore::TempItem item;
            item.category = table_->item(row, 0)->data(Qt::UserRole + 1).toString();
            if (item.category.isEmpty())
                item.category = category;
            item.path = path;
            item.id = table_->item(row, 0)->data(Qt::UserRole).toString();
            item.canClean = true;
            store_->cleanItems({item});
            refresh();
        }
    });

    refresh();
}

void TemporaryStorageDialog::refresh() {
    if (!store_)
        return;

    const auto prev = store_->previewUsage();
    previewSummary_->setText(tr("%1 (%2 items)").arg(displaySize(prev.bytes)).arg(prev.count));

    const auto nest = store_->nestedUsage();
    nestedSummary_->setText(tr("%1 (%2 items)").arg(displaySize(nest.bytes)).arg(nest.count));

    const auto edit = store_->editUsage();
    editSummary_->setText(tr("%1 (%2 items)").arg(displaySize(edit.bytes)).arg(edit.count));

    const auto all = store_->allItems(activeNestedIds_);
    const int filterIdx = categoryFilter_->currentIndex();

    table_->setRowCount(0);
    for (const auto &item : all) {
        if (filterIdx == 1 && item.category != QLatin1String("Preview"))
            continue;
        if (filterIdx == 2 && item.category != QLatin1String("Nested"))
            continue;
        if (filterIdx == 3 && item.category != QLatin1String("WorkingCopy"))
            continue;

        const int r = table_->rowCount();
        table_->insertRow(r);

        QString catDisplay = item.category;
        if (item.category == QLatin1String("Preview"))
            catDisplay = tr("Preview");
        else if (item.category == QLatin1String("Nested"))
            catDisplay = tr("Nested archive");
        else if (item.category == QLatin1String("WorkingCopy"))
            catDisplay = tr("Working copy");

        auto *catItem = new QTableWidgetItem(catDisplay);
        catItem->setData(Qt::UserRole, item.id);
        catItem->setData(Qt::UserRole + 2, item.canClean);
        catItem->setData(Qt::UserRole + 1, item.category);
        table_->setItem(r, 0, catItem);
        table_->setItem(r, 1, new QTableWidgetItem(item.name));
        table_->setItem(r, 2, new QTableWidgetItem(displaySize(item.bytes)));
        table_->setItem(r, 3, new QTableWidgetItem(item.status));
        table_->setItem(r, 4, new QTableWidgetItem(item.reason));
        table_->setItem(r, 5, new QTableWidgetItem(item.path));
    }

    if (table_->rowCount() > 0)
        table_->selectRow(0);
}

void TemporaryStorageDialog::safeCleanup() {
    if (!store_)
        return;

    const auto toClean = store_->previewCleanupList(activeNestedIds_);
    if (toClean.isEmpty()) {
        QMessageBox::information(this, tr("Safe cleanup"),
                                 tr("No disposable temporary items found. Working copies and active tabs are preserved."));
        return;
    }

    quint64 totalBytes = 0;
    QStringList itemsSummary;
    for (const auto &item : toClean) {
        totalBytes += item.bytes;
        if (itemsSummary.size() < 10)
            itemsSummary.append(QStringLiteral("• [%1] %2 (%3)").arg(item.category, item.name, displaySize(item.bytes)));
    }
    if (toClean.size() > 10)
        itemsSummary.append(tr("... and %1 more items").arg(toClean.size() - 10));

    const QString prompt = tr("Safe cleanup will remove %1 disposable items totaling %2:\n\n%3\n\nNote: All working copies and active sessions are protected and will NOT be touched.\nProceed with cleanup?")
                               .arg(toClean.size())
                               .arg(displaySize(totalBytes))
                               .arg(itemsSummary.join(QLatin1Char('\n')));

    if (QMessageBox::question(this, tr("Confirm safe cleanup"), prompt,
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes) {
        const int cleaned = store_->cleanItems(toClean);
        QMessageBox::information(this, tr("Cleanup complete"),
                                 tr("Successfully cleaned %1 temporary items and freed %2.").arg(cleaned).arg(displaySize(totalBytes)));
        refresh();
    }
}
