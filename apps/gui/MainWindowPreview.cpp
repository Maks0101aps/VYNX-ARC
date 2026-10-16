#include "MainWindow.h"
#include "SecretUtf8.h"
#include <QtWidgets>

void MainWindow::previewSelected() {
    if (busy_ || !archive_)
        return;
    const auto selection = table_->selectionModel()->selectedRows();
    if (selection.size() != 1)
        return;
    const auto &row = model_->row(filter_->mapToSource(selection.first()).row());
    if (row.folder)
        return;
    const auto id = row.id;
    const auto name = row.fullPath;
    auto archive = archive_;
    auto pw = password_ ? password_ : secret({});
    auto result = std::make_shared<QString>();
    beginOperation();
    auto op = operation_;
    runJob(
        tr("Reading text preview…"),
        [archive, pw, op, result, id] {
            const auto value = vynx::preview_text(**archive, id, pw->bytes(), **op);
            *result = QString::fromUtf8(value.data(), qsizetype(value.size()));
            return QString();
        },
        [this, result, name] {
            QDialog dialog(this);
            dialog.setWindowTitle(tr("Text preview"));
            auto *layout = new QVBoxLayout(&dialog);
            auto *path = new QLabel(name);
            path->setTextFormat(Qt::PlainText);
            path->setTextInteractionFlags(Qt::TextSelectableByMouse);
            path->setWordWrap(true);
            layout->addWidget(path);
            auto *text = new QPlainTextEdit;
            text->setReadOnly(true);
            text->setAccessibleName(tr("Text preview"));
            text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
            text->setPlainText(*result);
            layout->addWidget(text);
            auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            layout->addWidget(buttons);
            dialog.resize(800, 600);
            dialog.exec();
        });
}
