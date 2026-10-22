#include "ExtractionRecoveryDialog.h"
#include "../ArchiveModel.h"
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <optional>

static QString fileFingerprint(const QString &path) {
    QFileInfo fi(path);
    if (!fi.exists())
        return {};
    return QString("%1:%2").arg(fi.size()).arg(fi.lastModified().toMSecsSinceEpoch());
}

static std::optional<quint32> computeFileCrc32(const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    static quint32 table[256];
    static bool tableInit = false;
    if (!tableInit) {
        for (quint32 i = 0; i < 256; i++) {
            quint32 c = i;
            for (int j = 0; j < 8; j++) {
                c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
        tableInit = true;
    }
    quint32 crc = 0xFFFFFFFF;
    char buf[65536];
    while (!file.atEnd()) {
        qint64 read = file.read(buf, sizeof(buf));
        if (read < 0) return std::nullopt;
        if (read == 0) break;
        for (qint64 i = 0; i < read; i++) {
            crc = table[(crc ^ quint8(buf[i])) & 0xFF] ^ (crc >> 8);
        }
    }
    return crc ^ 0xFFFFFFFF;
}

ExtractionRecoveryDialog::ExtractionRecoveryDialog(const QString &archivePath,
                                                 const QString &destinationRoot,
                                                 const QString &savedFingerprint,
                                                 const QJsonObject &recoveryPlan,
                                                 QWidget *parent)
    : QDialog(parent), archivePath_(archivePath), destinationRoot_(destinationRoot),
      savedFingerprint_(savedFingerprint), recoveryPlan_(recoveryPlan) {
    setWindowTitle(tr("Extraction recovery and review"));
    resize(860, 500);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);

    sourceStatusLabel_ = new QLabel(this);
    sourceStatusLabel_->setWordWrap(true);
    mainLayout->addWidget(sourceStatusLabel_);

    summaryLabel_ = new QLabel(this);
    summaryLabel_->setWordWrap(true);
    mainLayout->addWidget(summaryLabel_);

    table_ = new QTableWidget(0, 4, this);
    table_->setObjectName("recoveryPlanTable");
    table_->setAccessibleName(tr("Extraction recovery plan table"));
    table_->setHorizontalHeaderLabels({tr("File (Resume Check)"), tr("Expected size"), tr("Current status"), tr("Action on resume")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setAlternatingRowColors(true);
    table_->verticalHeader()->hide();
    mainLayout->addWidget(table_, 1);

    auto *buttonBox = new QDialogButtonBox(this);
    resumeButton_ = buttonBox->addButton(tr("Resume extraction"), QDialogButtonBox::AcceptRole);
    resumeButton_->setObjectName("primary");
    buttonBox->addButton(tr("Cancel"), QDialogButtonBox::RejectRole);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);

    reconcile();


}

void ExtractionRecoveryDialog::reconcile() {
    files_.clear();
    canResume_ = false;

    if (!QFile::exists(archivePath_)) {
        sourceStatusLabel_->setText(tr("Source archive is missing: %1").arg(archivePath_));
        sourceStatusLabel_->setStyleSheet("color: red;");
        resumeButton_->setEnabled(false);
        return;
    }

    if (!savedFingerprint_.isEmpty()) {
        const auto curFp = fileFingerprint(archivePath_);
        if (curFp != savedFingerprint_) {
            sourceStatusLabel_->setText(tr("Archive has changed on disk since extraction was started! Verification required."));
            sourceStatusLabel_->setStyleSheet("color: red;");
            resumeButton_->setEnabled(false);
            return;
        }
    }

    sourceStatusLabel_->setText(tr("Source archive verified unchanged: %1").arg(QFileInfo(archivePath_).fileName()));
    sourceStatusLabel_->setStyleSheet("color: green;");

    const auto filesArray = recoveryPlan_["files"].toArray();
    const QDir destDir(destinationRoot_);

    int completedCount = 0;
    int missingCount = 0;
    int modifiedCount = 0;
    int partialCount = 0;

    for (const auto &val : filesArray) {
        const auto obj = val.toObject();
        RecoveryFileStatus item;
        item.name = obj["name"].toString();
        item.expectedSize = quint64(obj["size"].toDouble());
        item.expectedModified = qint64(obj["modified"].toDouble());
        item.expectedCrc = obj["crc"].toString().trimmed();

        const QString targetPath = destDir.filePath(item.name);
        const QFileInfo fi(targetPath);

        // Existing output has no durable ownership receipt. Never schedule it for
        // overwrite, even when its checksum, size or timestamp differs.
        if (fi.exists() || fi.isSymLink()) {
            bool matches = false;
            if (fi.isFile() && !fi.isSymLink() && !item.expectedCrc.isEmpty() &&
                fi.size() == qint64(item.expectedSize)) {
                const auto crc = computeFileCrc32(targetPath);
                matches = crc.has_value() &&
                    QString::asprintf("%08x", *crc).compare(item.expectedCrc, Qt::CaseInsensitive) == 0;
            }
            item.state = matches ? RecoveryFileStatus::Completed : RecoveryFileStatus::ModifiedByUser;
            item.details = matches ? tr("Matches archive CRC; preserved")
                                   : tr("Existing output has no ownership receipt; preserved");
            item.willExtract = false;
            if (matches) ++completedCount; else ++modifiedCount;
        } else {
            item.state = RecoveryFileStatus::Missing;
            item.details = tr("Missing; extract without replacing existing output");
            item.willExtract = true;
            ++missingCount;
        }

        files_.append(item);
    }

    summaryLabel_->setText(tr("Status breakdown: <b>%1</b> completed, <b>%2</b> missing, <b>%3</b> modified by user (preserved), <b>%4</b> partial/corrupt.")
                               .arg(completedCount).arg(missingCount).arg(modifiedCount).arg(partialCount));

    QSignalBlocker blocker(table_);
    table_->setRowCount(0);
    for (const auto &f : files_) {
        const int r = table_->rowCount();
        table_->insertRow(r);

        auto *nameItem = new QTableWidgetItem(f.name);
        nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        table_->setItem(r, 0, nameItem);

        table_->setItem(r, 1, new QTableWidgetItem(displaySize(f.expectedSize)));

        QString stateStr;
        QColor col;
        if (f.state == RecoveryFileStatus::Completed) {
            stateStr = tr("Completed");
            col = QColor(0, 150, 50);
        } else if (f.state == RecoveryFileStatus::Missing) {
            stateStr = tr("Missing");
            col = QColor(100, 100, 100);
        } else if (f.state == RecoveryFileStatus::ModifiedByUser) {
            stateStr = tr("Modified by user");
            col = QColor(0, 100, 200);
        } else {
            stateStr = tr("Partial / Corrupt");
            col = QColor(200, 100, 0);
        }

        auto *stateItem = new QTableWidgetItem(stateStr);
        stateItem->setForeground(QBrush(col));
        table_->setItem(r, 2, stateItem);

        QString actionStr;
        if (f.state == RecoveryFileStatus::Completed)
            actionStr = tr("Skip (already completed)");
        else if (f.state == RecoveryFileStatus::ModifiedByUser)
            actionStr = tr("Preserve user modifications (protected)");
        else
            actionStr = tr("Extract and replace");

        table_->setItem(r, 3, new QTableWidgetItem(actionStr));
    }

    canResume_ = (missingCount > 0 || partialCount > 0);
    resumeButton_->setEnabled(canResume_);
    if (!canResume_) {
        summaryLabel_->setText(summaryLabel_->text() + QStringLiteral(" ") + tr("All files are either completed or preserved. No further work needed."));
    }
}

QVector<RecoveryFileStatus> ExtractionRecoveryDialog::filesToExtract() const {
    QVector<RecoveryFileStatus> list;
    for (const auto &f : files_)
        if (f.willExtract)
            list.append(f);
    return list;
}
