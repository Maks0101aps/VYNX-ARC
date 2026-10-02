#include "ExtractDialog.h"
#include <QtWidgets>
ExtractDialog::ExtractDialog(const QString &defaultDest, bool hasSelection, bool smart, bool here,
                             const QString &initialPassword, QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("Extract archive"));
    auto *form = new QFormLayout(this);
    destination = new QLineEdit(defaultDest);
    auto *browse = new QPushButton(tr("Browse…"));
    auto *destLayout = new QHBoxLayout;
    destLayout->addWidget(destination);
    destLayout->addWidget(browse);
    form->addRow(tr("Destination"), destLayout);
    connect(browse, &QPushButton::clicked, this, [this] {
        QString p = QFileDialog::getExistingDirectory(this, tr("Extraction destination"),
                                                      destination->text());
        if (!p.isEmpty())
            destination->setText(p);
    });
    scope = new QComboBox;
    scope->addItem(tr("All files"));
    if (hasSelection) {
        scope->addItem(tr("Selected files and folders"));
        scope->setCurrentIndex(1);
    }
    form->addRow(tr("Files"), scope);
    conflicts = new QComboBox;
    conflicts->addItems({tr("Ask for each conflict"), tr("Replace existing files"),
                         tr("Skip existing files"), tr("Keep both / rename incoming"),
                         tr("Replace if newer"), tr("Stop on conflict")});
    form->addRow(tr("Existing files"), conflicts);
    smartBox = new QCheckBox(tr("Smart Extract: avoid redundant nesting"));
    smartBox->setChecked(smart);
    form->addRow(smartBox);
    auto *security = new QLabel(tr("Unsafe paths, links and archive bombs are blocked."));
    security->setWordWrap(true);
    form->addRow(security);
    password = new QLineEdit(initialPassword);
    password->setEchoMode(QLineEdit::Password);
    form->addRow(tr("Password, if needed"), password);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Extract"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName("primary");
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    if (here) {
        smartBox->setChecked(false);
    }
}
