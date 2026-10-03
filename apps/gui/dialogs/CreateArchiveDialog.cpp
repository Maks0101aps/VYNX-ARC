#include "CreateArchiveDialog.h"
#include "../ArchiveModel.h"
#include "../FormatUtils.h"
#include <QtWidgets>
CreateArchiveDialog::CreateArchiveDialog(const QStringList &initial, int initialFormat,
                                         QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("Create archive"));
    auto *form = new QFormLayout(this);
    inputs = new QListWidget;
    inputs->addItems(initial);
    inputs->setMinimumWidth(380);
    inputs->setMaximumHeight(110);
    auto *sourceButtons = new QHBoxLayout;
    auto *files = new QPushButton(tr("Add files…"));
    auto *folder = new QPushButton(tr("Add folder…"));
    auto *remove = new QPushButton(tr("Remove"));
    sourceButtons->addWidget(files);
    sourceButtons->addWidget(folder);
    sourceButtons->addWidget(remove);
    connect(files, &QPushButton::clicked, this,
            [this] { inputs->addItems(QFileDialog::getOpenFileNames(this, tr("Source files"))); });
    connect(folder, &QPushButton::clicked, this, [this] {
        auto p = QFileDialog::getExistingDirectory(this, tr("Source folder"));
        if (!p.isEmpty())
            inputs->addItem(p);
    });
    connect(remove, &QPushButton::clicked, this,
            [this] { delete inputs->takeItem(inputs->currentRow()); });
    format = new QComboBox;
    format->addItems({"ZIP", "7Z", "TAR", "TAR.GZ", "GZIP", "XZ", "BZIP2", "ZSTD", "LZMA", "TAR.XZ",
                      "TAR.BZ2", "TAR.ZST"});
    format->setCurrentIndex(initialFormat);
    output = new QLineEdit;
    output->setPlaceholderText(tr("Archive name or full path"));
    if (!initial.isEmpty())
        output->setText(outputForFormat(
            QFileInfo(initial.first()).absolutePath() + "/" +
                (initial.size() == 1 ? QFileInfo(initial.first()).completeBaseName() : "Archive"),
            initialFormat));
    auto *save = new QPushButton(tr("Browse…"));
    auto *outLine = new QHBoxLayout;
    outLine->addWidget(output);
    outLine->addWidget(save);
    form->addRow(tr("Output archive"), outLine);
    form->addRow(tr("Format"), format);
    preset = new QComboBox;
    preset->setObjectName("compressionPreset");
    preset->addItems({tr("Store / None"), tr("Fast"), tr("Balanced"), tr("Maximum")});
    preset->setCurrentIndex(2);
    resource = new QComboBox;
    resource->setObjectName("resourceMode");
    resource->addItems({tr("Eco"), tr("Balanced"), tr("Maximum")});
    resource->setCurrentIndex(1);
    form->addRow(tr("Compression"), preset);
    form->addRow(tr("Resources"), resource);
    auto *effective = new QLabel;
    effective->setObjectName("effectiveCompression");
    effective->setWordWrap(true);
    effective->setMaximumWidth(480);
    form->addRow(tr("Effective settings"), effective);
    auto refresh = [this, effective] {
        if (format->currentIndex() == 2)
            preset->setCurrentIndex(0);
        if (format->currentIndex() >= 5 && preset->currentIndex() == 0)
            preset->setCurrentIndex(2);
        auto *model = qobject_cast<QStandardItemModel *>(preset->model());
        if (model)
            model->item(0)->setEnabled(format->currentIndex() < 5);
        preset->setEnabled(format->currentIndex() != 2);
        try {
            auto s = vynx::compression_details(uint8_t(format->currentIndex()),
                                               uint8_t(preset->currentIndex()),
                                               uint8_t(resource->currentIndex()));
            effective->setText(QString::fromUtf8(s.data(), qsizetype(s.size())));
        } catch (const std::exception &e) {
            effective->setText(QString::fromUtf8(e.what()));
        }
    };
    connect(format, &QComboBox::currentIndexChanged, this, refresh);
    connect(preset, &QComboBox::currentIndexChanged, this, refresh);
    connect(resource, &QComboBox::currentIndexChanged, this, refresh);
    refresh();
    connect(save, &QPushButton::clicked, this, [this] {
        const QStringList extensions{"zip", "7z",  "tar",  "tar.gz", "gz",      "xz",
                                     "bz2", "zst", "lzma", "tar.xz", "tar.bz2", "tar.zst"};
        auto p = QFileDialog::getSaveFileName(this, tr("Create archive"),
                                              "Archive." + extensions[format->currentIndex()],
                                              tr("All files (*)"));
        if (!p.isEmpty())
            output->setText(p);
    });
    password = new QLineEdit;
    password->setEchoMode(QLineEdit::Password);
    auto *addPassword = new QCheckBox(tr("Add password"));
    form->addRow(addPassword);
    password->hide();
    connect(addPassword, &QCheckBox::toggled, password, &QWidget::setVisible);
    connect(addPassword, &QCheckBox::toggled, this, [this](bool enabled) {
        if (!enabled)
            password->clear();
    });
    form->addRow(tr("Password (optional)"), password);
    auto *passwordLabel = form->labelForField(password);
    passwordLabel->hide();
    connect(addPassword, &QCheckBox::toggled, passwordLabel, &QWidget::setVisible);
    advanced = new QGroupBox(tr("Advanced: split 7Z volumes"));
    advanced->setCheckable(true);
    advanced->setChecked(false);
    advanced->setMaximumHeight(advanced->fontMetrics().height() + 18);
    advanced->setVisible(initialFormat == 1);
    auto *splitForm = new QFormLayout(advanced);
    split = new QComboBox;
    split->addItems({tr("No split"), "100 MiB", "500 MiB", "1 GiB", "4 GiB", tr("Custom")});
    customSplit = new QSpinBox;
    customSplit->setRange(1, 1048576);
    customSplit->setValue(100);
    customSplit->setSuffix(" MiB");
    customSplit->setVisible(false);
    splitForm->addRow(tr("Volume size"), split);
    splitForm->addRow(customSplit);
    auto *splitLabel = splitForm->labelForField(split);
    splitLabel->hide();
    split->hide();
    connect(advanced, &QGroupBox::toggled, this, [this, splitLabel](bool expanded) {
        advanced->setMaximumHeight(expanded ? QWIDGETSIZE_MAX
                                            : advanced->fontMetrics().height() + 18);
        split->setVisible(expanded);
        splitLabel->setVisible(expanded);
        customSplit->setVisible(expanded && split->currentIndex() == 5);
        adjustSize();
    });
    connect(split, &QComboBox::currentIndexChanged, this,
            [this](int index) { customSplit->setVisible(index == 5 && advanced->isChecked()); });
    form->addRow(advanced);
    auto *encryptionLabel = new QLabel(tr("ZIP uses AES-256. 7Z encrypts data and filenames."));
    encryptionLabel->setWordWrap(true);
    form->addRow(encryptionLabel);
    connect(format, &QComboBox::currentIndexChanged, this,
            [this, encryptionLabel, addPassword](int i) {
                addPassword->setEnabled(i < 2);
                if (i >= 2)
                    addPassword->setChecked(false);
                password->setEnabled(i < 2);
                if (!output->text().isEmpty())
                    output->setText(outputForFormat(output->text(), i));
                if (i >= 2)
                    password->clear();
                encryptionLabel->setVisible(i < 2);
                advanced->setVisible(i == 1);
            });
    password->setEnabled(initialFormat < 2);
    addPassword->setEnabled(initialFormat < 2);
    encryptionLabel->setVisible(initialFormat < 2);
    form->addRow(tr("Sources"), inputs);
    form->addRow(sourceButtons);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Create"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName("primary");
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (inputs->count() == 0 || output->text().isEmpty()) {
            QMessageBox::information(this, tr("Create archive"),
                                     tr("Choose source files and an output archive."));
            return;
        }
        output->setText(outputForFormat(output->text(), format->currentIndex()));
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
