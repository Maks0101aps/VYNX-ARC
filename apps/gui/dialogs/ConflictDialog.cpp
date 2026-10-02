#include "ConflictDialog.h"
#include "../ArchiveModel.h"
#include <QtWidgets>
ConflictDialog::ConflictDialog(const QString &existing, quint64 existingSize, quint64 existingTime,
                               const QString &incoming, quint64 incomingSize, quint64 incomingTime,
                               QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("File already exists"));
    setMinimumWidth(500);
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(12);
    auto *heading = new QLabel(tr("Choose how to handle this file"));
    layout->addWidget(heading);
    auto *comparison = new QGridLayout;
    auto card = [&](const QString &title, const QString &path, quint64 size, quint64 timestamp,
                    int column) {
        auto *group = new QGroupBox(title);
        auto *form = new QFormLayout(group);
        auto *name = new QLabel(path);
        name->setTextFormat(Qt::PlainText);
        name->setWordWrap(true);
        name->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(name);
        form->addRow(tr("Size"), new QLabel(displaySize(size)));
        form->addRow(tr("Modified"),
                     new QLabel(timestamp ? QLocale().toString(
                                                QDateTime::fromSecsSinceEpoch(qint64(timestamp)),
                                                QLocale::ShortFormat)
                                          : tr("Unknown")));
        comparison->addWidget(group, 0, column);
    };
    card(tr("Existing file"), existing, existingSize, existingTime, 0);
    card(tr("Incoming file"), incoming, incomingSize, incomingTime, 1);
    layout->addLayout(comparison);
    auto *all = new QCheckBox(tr("Apply this choice to all conflicts"));
    layout->addWidget(all);
    auto *buttons = new QDialogButtonBox;
    layout->addWidget(buttons);
    auto add = [&](const QString &label, quint8 choice, QDialogButtonBox::ButtonRole role) {
        auto *button = buttons->addButton(label, role);
        connect(button, &QPushButton::clicked, this, [this, all, choice] {
            emit decision(choice, all->isChecked());
            accept();
        });
    };
    add(tr("Replace"), 2, QDialogButtonBox::DestructiveRole);
    add(tr("Skip"), 1, QDialogButtonBox::ActionRole);
    add(tr("Keep both"), 4, QDialogButtonBox::ActionRole);
    add(tr("Cancel"), 0, QDialogButtonBox::RejectRole);
}
