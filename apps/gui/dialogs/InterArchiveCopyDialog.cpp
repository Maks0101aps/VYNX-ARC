#include "InterArchiveCopyDialog.h"
#include "../ArchiveModel.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

InterArchiveCopyDialog::InterArchiveCopyDialog(const QString &sourceArchive,
                                               const QString &targetArchive,
                                               const QString &targetFolder,
                                               const QVector<CopyCandidateItem> &items,
                                               quint64 availableSpace,
                                               QWidget *parent)
    : QDialog(parent), sourceArchive_(sourceArchive), targetArchive_(targetArchive),
      targetFolder_(targetFolder), items_(items), availableSpace_(availableSpace) {
    setWindowTitle(tr("Copy files between archives"));
    resize(780, 460);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);

    // Header info
    auto *headerLabel = new QLabel(this);
    headerLabel->setWordWrap(true);
    headerLabel->setText(tr("<b>Source:</b> %1<br><b>Destination:</b> %2 (folder: '%3')")
                             .arg(QFileInfo(sourceArchive_).fileName(),
                                  QFileInfo(targetArchive_).fileName(),
                                  targetFolder_.isEmpty() ? QStringLiteral("/") : targetFolder_));
    mainLayout->addWidget(headerLabel);

    // Conflict policy selection
    int conflictCount = 0;
    quint64 totalRequired = 0;
    for (const auto &it : items_) {
        if (it.hasConflict)
            ++conflictCount;
        totalRequired += it.size;
    }

    auto *policyLayout = new QHBoxLayout;
    policyLayout->addWidget(new QLabel(tr("If destination file already exists:")));
    policyCombo_ = new QComboBox;
    policyCombo_->setObjectName("copyPolicyCombo");
    policyCombo_->setAccessibleName(tr("Conflict resolution policy"));
    policyCombo_->addItem(tr("Replace existing files"), int(CopyConflictPolicy::Replace));
    policyCombo_->addItem(tr("Rename new files (keep both)"), int(CopyConflictPolicy::Rename));
    policyCombo_->addItem(tr("Skip existing files"), int(CopyConflictPolicy::Skip));
    policyCombo_->setCurrentIndex(conflictCount > 0 ? 1 : 0); // Default to Rename if conflicts exist
    connect(policyCombo_, &QComboBox::currentIndexChanged, this, [this] { updateTable(); });
    policyLayout->addWidget(policyCombo_);
    policyLayout->addStretch();
    mainLayout->addLayout(policyLayout);

    // Table
    table_ = new QTableWidget(0, 4, this);
    table_->setObjectName("copyCandidateTable");
    table_->setAccessibleName(tr("Files to copy"));
    table_->setHorizontalHeaderLabels({tr("File"), tr("Size"), tr("Target path"), tr("Status")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    table_->verticalHeader()->hide();
    mainLayout->addWidget(table_, 1);

    // Space label
    spaceLabel_ = new QLabel(this);
    spaceLabel_->setText(tr("Required space: <b>%1</b> (available: %2). Conflicts: <b>%3</b>.")
                             .arg(displaySize(totalRequired), displaySize(availableSpace_), QString::number(conflictCount)));
    mainLayout->addWidget(spaceLabel_);

    // Buttons
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttonBox->button(QDialogButtonBox::Ok)->setText(tr("Copy"));
    buttonBox->button(QDialogButtonBox::Ok)->setObjectName("primary");
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);

    updateTable();
}

CopyConflictPolicy InterArchiveCopyDialog::policy() const {
    return CopyConflictPolicy(policyCombo_->currentData().toInt());
}

void InterArchiveCopyDialog::updateTable() {
    table_->setRowCount(0);
    const auto pol = policy();

    for (const auto &item : items_) {
        const int r = table_->rowCount();
        table_->insertRow(r);

        table_->setItem(r, 0, new QTableWidgetItem(item.name));
        table_->setItem(r, 1, new QTableWidgetItem(displaySize(item.size)));
        table_->setItem(r, 2, new QTableWidgetItem(item.targetPath));

        QString statusText;
        if (!item.hasConflict) {
            statusText = tr("New file");
        } else {
            if (pol == CopyConflictPolicy::Replace)
                statusText = tr("Conflict: Will overwrite");
            else if (pol == CopyConflictPolicy::Rename)
                statusText = tr("Conflict: Will rename");
            else
                statusText = tr("Conflict: Will skip");
        }

        auto *statusItem = new QTableWidgetItem(statusText);
        if (item.hasConflict)
            statusItem->setForeground(QBrush(QColor(220, 100, 0)));
        else
            statusItem->setForeground(QBrush(QColor(0, 150, 50)));

        table_->setItem(r, 3, statusItem);
    }
}

QVector<CopyCandidateItem> InterArchiveCopyDialog::resolvedItems() const {
    QVector<CopyCandidateItem> res;
    const auto pol = policy();

    for (auto it : items_) {
        if (it.hasConflict && pol == CopyConflictPolicy::Skip)
            continue;
        res.append(it);
    }
    return res;
}
