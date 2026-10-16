#include "BatchExtractDialog.h"
#include <QtWidgets>
QStringList batchDestinations(const QStringList &archives, const QString &destination) {
    QStringList result;
    QSet<QString> used;
    for (const auto &path : archives) {
        QString base = QFileInfo(path).fileName();
        for (const auto &suffix :
             {".tar.gz", ".tar.xz", ".tar.bz2", ".tar.zst", ".7z.001", ".zip", ".7z", ".rar",
              ".tar", ".tgz", ".gz", ".xz", ".bz2", ".zst", ".lzma"}) {
            if (base.endsWith(suffix, Qt::CaseInsensitive)) {
                base.chop(qsizetype(strlen(suffix)));
                break;
            }
        }
        for (auto &c : base)
            if (c.unicode() < 32 || QStringLiteral("<>:\"/\\|?*").contains(c))
                c = '_';
        while (base.endsWith(' ') || base.endsWith('.'))
            base.chop(1);
        if (base.isEmpty())
            base = "Archive";
        if (QRegularExpression("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$",
                               QRegularExpression::CaseInsensitiveOption)
                .match(base)
                .hasMatch())
            base.prepend('_');
        const auto original = base;
        int suffix = 2;
        while (used.contains(base.toCaseFolded()))
            base = original + QString(" (%1)").arg(suffix++);
        used.insert(base.toCaseFolded());
        result << QDir(destination).absoluteFilePath(base);
    }
    return result;
}
BatchExtractDialog::BatchExtractDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Batch extraction"));
    resize(650, 480);
    auto *layout = new QVBoxLayout(this);
    auto *hint = new QLabel(tr("Extract each archive into its own folder. Failed archives are "
                               "reported in the queue; remaining jobs continue."));
    hint->setWordWrap(true);
    layout->addWidget(hint);
    files = new QListWidget;
    files->setObjectName("batchArchives");
    files->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layout->addWidget(files, 1);
    auto *controls = new QHBoxLayout;
    auto *add = new QPushButton(tr("Add archives..."));
    auto *remove = new QPushButton(tr("Remove selected"));
    controls->addWidget(add);
    controls->addWidget(remove);
    controls->addStretch();
    layout->addLayout(controls);
    connect(add, &QPushButton::clicked, this, [this] {
        const auto paths =
            QFileDialog::getOpenFileNames(this, tr("Choose archives"), {},
                                          tr("Archives (*.zip *.7z *.rar *.tar *.tgz *.gz *.xz "
                                             "*.bz2 *.zst *.lzma *.001);;All files (*)"));
        for (const auto &path : paths) {
            if (files->count() >= 32)
                break;
            if (files->findItems(path, Qt::MatchExactly).isEmpty())
                files->addItem(path);
        }
    });
    connect(remove, &QPushButton::clicked, this, [this] { qDeleteAll(files->selectedItems()); });
    auto *form = new QFormLayout;
    destination = new QLineEdit(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    auto *row = new QHBoxLayout;
    auto *browse = new QPushButton(tr("Browse..."));
    row->addWidget(destination, 1);
    row->addWidget(browse);
    connect(browse, &QPushButton::clicked, this, [this] {
        auto path = QFileDialog::getExistingDirectory(this, tr("Extraction destination"),
                                                      destination->text());
        if (!path.isEmpty())
            destination->setText(path);
    });
    form->addRow(tr("Destination"), row);
    conflicts = new QComboBox;
    conflicts->addItems(
        {tr("Keep both"), tr("Skip existing"), tr("Replace existing"), tr("Stop on conflict")});
    form->addRow(tr("Existing files"), conflicts);
    password = new QLineEdit;
    password->setEchoMode(QLineEdit::Password);
    form->addRow(tr("Shared password, if needed"), password);
    layout->addLayout(form);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Add to queue"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName("primary");
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (files->count() == 0 || destination->text().trimmed().isEmpty()) {
            QMessageBox::information(this, tr("Batch extraction"),
                                     tr("Choose archives and a destination folder."));
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
QStringList BatchExtractDialog::archives() const {
    QStringList result;
    for (int i = 0; i < files->count(); ++i)
        result << files->item(i)->text();
    return result;
}
