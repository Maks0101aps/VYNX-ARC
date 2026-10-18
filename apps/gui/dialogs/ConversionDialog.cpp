#include "ConversionDialog.h"
#include "../SecretUtf8.h"
#include <QtConcurrent>
#include <QtWidgets>
static QString value(const rust::String &s) {
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}
ConversionDialog::ConversionDialog(const QStringList &paths, QWidget *parent)
    : QDialog(parent), paths_(paths) {
    setWindowTitle(tr("Convert archives"));
    resize(860, 600);
    setMinimumSize(540, 440);
    auto *layout = new QVBoxLayout(this);
    auto *formWidget = new QWidget;
    auto *form = new QFormLayout(formWidget);
    auto *destinationRow = new QWidget;
    auto *row = new QHBoxLayout(destinationRow);
    row->setContentsMargins(0, 0, 0, 0);
    destination =
        new QLineEdit(paths.isEmpty() ? QDir::homePath() : QFileInfo(paths.first()).absolutePath());
    destination->setAccessibleName(tr("Output folder"));
    auto *browse = new QPushButton(tr("Browse..."));
    row->addWidget(destination, 1);
    row->addWidget(browse);
    form->addRow(tr("Output folder"), destinationRow);
    outputName = new QLineEdit;
    outputName->setPlaceholderText(tr("Automatic name"));
    outputName->setEnabled(paths_.size() == 1);
    form->addRow(tr("Name without extension"), outputName);
    format = new QComboBox;
    format->addItems({"ZIP", "7Z"});
    form->addRow(tr("Output format"), format);
    preset = new QComboBox;
    preset->addItems({tr("Store"), tr("Fast"), tr("Balanced"), tr("Maximum")});
    preset->setCurrentIndex(2);
    form->addRow(tr("Compression"), preset);
    resource = new QComboBox;
    resource->addItems({tr("Eco"), tr("Balanced"), tr("Maximum")});
    resource->setCurrentIndex(1);
    form->addRow(tr("Resources"), resource);
    conflict = new QComboBox;
    conflict->addItems({tr("Refuse existing destinations"), tr("Choose unused names")});
    form->addRow(tr("Name conflicts"), conflict);
    sourcePassword = new QLineEdit;
    sourcePassword->setEchoMode(QLineEdit::Password);
    form->addRow(tr("Source password (if needed)"), sourcePassword);
    encrypt = new QCheckBox(tr("Encrypt destination archives"));
    encrypt->setChecked(true);
    form->addRow(encrypt);
    outputPassword = new QLineEdit;
    outputPassword->setEchoMode(QLineEdit::Password);
    form->addRow(tr("Destination password"), outputPassword);
    allowDecrypt =
        new QCheckBox(tr("I explicitly choose unencrypted output, including decrypted sources"));
    allowDecrypt->setText(tr("I choose unencrypted output"));
    allowDecrypt->setEnabled(false);
    form->addRow(allowDecrypt);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setWidget(formWidget);
    scroll->setMaximumHeight(310);
    scroll->setMinimumHeight(120);
    scroll->setFrameShape(QFrame::NoFrame);
    layout->addWidget(scroll);
    auto *notice = new QLabel(
        tr("Originals are kept. Names, file contents, folders and representable modified times are "
           "transferred. Comments, ACLs, ownership and extra metadata are not transferred. ZIP "
           "times have two-second precision. A temporary output archive needs space beside the "
           "destination; no loose plaintext files are created."));
    notice->setWordWrap(true);
    layout->addWidget(notice);
    table_ = new QTableWidget(0, 4);
    table_->setHorizontalHeaderLabels(
        {tr("Source"), tr("Destination"), tr("Decoded size"), tr("Plan / error")});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setAccessibleName(tr("Conversion plan"));
    layout->addWidget(table_, 1);
    summary_ = new QLabel;
    summary_->setWordWrap(true);
    layout->addWidget(summary_);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    scan_ = buttons->addButton(tr("Read archive plan"), QDialogButtonBox::ActionRole);
    start_ = buttons->addButton(tr("Queue conversion"), QDialogButtonBox::AcceptRole);
    start_->setObjectName("queueConversion");
    scan_->setObjectName("readConversionPlan");
    start_->setEnabled(false);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(start_, &QPushButton::clicked, this, &QDialog::accept);
    connect(scan_, &QPushButton::clicked, this, &ConversionDialog::scan);
    connect(browse, &QPushButton::clicked, this, [this] {
        const auto path =
            QFileDialog::getExistingDirectory(this, tr("Output folder"), destination->text());
        if (!path.isEmpty())
            destination->setText(path);
    });
    connect(outputName, &QLineEdit::textChanged, this, &ConversionDialog::updatePlan);
    connect(destination, &QLineEdit::textChanged, this, &ConversionDialog::updatePlan);
    connect(format, &QComboBox::currentIndexChanged, this, &ConversionDialog::updatePlan);
    connect(conflict, &QComboBox::currentIndexChanged, this, &ConversionDialog::updatePlan);
    connect(sourcePassword, &QLineEdit::textChanged, this, &ConversionDialog::invalidate);
    connect(resource, &QComboBox::currentIndexChanged, this, &ConversionDialog::invalidate);
    connect(outputPassword, &QLineEdit::textChanged, this, &ConversionDialog::updatePlan);
    connect(encrypt, &QCheckBox::toggled, this, [this](bool enabled) {
        outputPassword->setEnabled(enabled);
        allowDecrypt->setEnabled(!enabled);
        if (enabled)
            allowDecrypt->setChecked(false);
        updatePlan();
    });
    connect(allowDecrypt, &QCheckBox::toggled, this, &ConversionDialog::updatePlan);
    connect(&watcher_, &QFutureWatcher<QVector<Scan>>::finished, this, [this] {
        const auto results = watcher_.result();
        scan_->setEnabled(true);
        sourcePassword->setEnabled(true);
        resource->setEnabled(true);
        quint64 total = 0;
        int errors = 0;
        for (int i = 0; i < results.size(); ++i) {
            const auto &r = results[i];
            total += r.bytes;
            errors += !r.error.isEmpty();
            table_->setItem(i, 2, new QTableWidgetItem(QLocale().formattedDataSize(r.bytes)));
            table_->setItem(
                i, 3,
                new QTableWidgetItem(r.error.isEmpty()
                                         ? tr("%1, %2 entries").arg(r.format).arg(r.entries)
                                         : r.error));
            table_->item(i, 3)->setToolTip(r.error);
        }
        summary_->setText(tr("%1 archives; %2 decoded; %3 plan errors. Space is checked again at "
                             "execution; an estimate does not reserve disk space.")
                              .arg(results.size())
                              .arg(QLocale().formattedDataSize(total))
                              .arg(errors));
        scanned_ = true;
        updatePlan();
    });
    updatePlan();
    QTimer::singleShot(0, this, &ConversionDialog::scan);
}
ConversionDialog::~ConversionDialog() {
    if (operation_)
        vynx::cancel(**operation_);
    watcher_.waitForFinished();
}
void ConversionDialog::invalidate() {
    scanned_ = false;
    start_->setEnabled(false);
    summary_->setText(tr("Source settings changed. Read the archive plan again."));
}
QVector<ConversionDialog::Plan> ConversionDialog::plan() const {
    QVector<Plan> result;
    QSet<QString> reserved;
    for (const auto &source : paths_) {
        const auto stem = paths_.size() == 1 && !outputName->text().trimmed().isEmpty()
                              ? outputName->text().trimmed()
                              : QFileInfo(source).completeBaseName() + " - converted";
        const auto ext = format->currentIndex() == 0 ? ".zip" : ".7z";
        auto output = QDir(destination->text()).absoluteFilePath(stem + ext);
        if (conflict->currentIndex() == 1) {
            int n = 2;
            while (QFileInfo::exists(output) || reserved.contains(output.toCaseFolded()))
                output = QDir(destination->text())
                             .absoluteFilePath(stem + QString(" (%1)").arg(n++) + ext);
        }
        reserved.insert(output.toCaseFolded());
        result.append({source, output});
    }
    return result;
}
void ConversionDialog::updatePlan() {
    const auto entries = plan();
    if (table_->rowCount() != entries.size())
        table_->setRowCount(entries.size());
    bool valid = !entries.isEmpty() && QFileInfo(destination->text()).isDir() &&
                 (outputName->text().isEmpty() ||
                  QFileInfo(outputName->text()).fileName() == outputName->text());
    QSet<QString> outputs;
    for (int i = 0; i < entries.size(); ++i) {
        const auto &p = entries[i];
        for (int c = 0; c < 2; ++c) {
            const auto text = c == 0 ? p.source : p.output;
            auto *item = new QTableWidgetItem(QFileInfo(text).fileName());
            item->setToolTip(text);
            table_->setItem(i, c, item);
        }
        if (QFileInfo::exists(p.output) || outputs.contains(p.output.toCaseFolded()))
            valid = false;
        outputs.insert(p.output.toCaseFolded());
    }
    valid &= encrypt->isChecked() ? !outputPassword->text().isEmpty() : allowDecrypt->isChecked();
    start_->setEnabled(scanned_ && valid && !watcher_.isRunning());
}
void ConversionDialog::scan() {
    if (watcher_.isRunning())
        return;
    scanned_ = false;
    start_->setEnabled(false);
    scan_->setEnabled(false);
    sourcePassword->setEnabled(false);
    resource->setEnabled(false);
    summary_->setText(tr("Reading source archives..."));
    operation_ = std::make_shared<rust::Box<vynx::Operation>>(vynx::new_operation());
    vynx::configure_operation(**operation_, 2, uint8_t(resource->currentIndex()));
    const auto op = operation_;
    const auto paths = paths_;
    const auto pw = secret(sourcePassword->text());
    watcher_.setFuture(QtConcurrent::run([paths, pw, op] {
        QVector<Scan> results;
        for (const auto &path : paths) {
            Scan r;
            try {
                if (vynx::cancelled(**op))
                    break;
                auto archive = vynx::open_archive(path.toUtf8().toStdString(), pw->bytes(), **op);
                r.format = value(vynx::archive_format(*archive));
                const auto entries = vynx::list_entries(*archive);
                r.entries = qsizetype(entries.size());
                for (const auto &e : entries)
                    r.bytes += e.size;
            } catch (const std::exception &e) {
                r.error = QString::fromUtf8(e.what());
            }
            results.append(r);
        }
        return results;
    }));
}
