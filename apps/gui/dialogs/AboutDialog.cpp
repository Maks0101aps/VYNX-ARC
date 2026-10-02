#include "AboutDialog.h"
#include <QtWidgets>
AboutDialog::AboutDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("About VYNX ARC"));
    setMinimumWidth(440);
    auto *layout = new QVBoxLayout(this);
    auto *brand = new QLabel("VYNX ARC " + QCoreApplication::applicationVersion());
    brand->setObjectName("brand");
    layout->addWidget(brand);
    layout->addWidget(new QLabel(tr("Windows x64 · Rust core")));
    layout->addWidget(
        new QLabel(tr("Qt runtime %1 · built with %2").arg(qVersion(), QT_VERSION_STR)));
#ifdef NDEBUG
    layout->addWidget(new QLabel(tr("Release build · development milestone")));
#else
    layout->addWidget(new QLabel(tr("Debug build · development milestone")));
#endif
    auto *details = new QLabel(tr("Offline archive operations. No telemetry.\nRead ZIP, 7Z, RAR, "
                                  "TAR and TAR.GZ.\nCreate ZIP, 7Z, TAR and TAR.GZ."));
    details->setWordWrap(true);
    layout->addWidget(details);
    auto *source = new QLabel("<a href=\"https://github.com/Maks0101aps/VYNX-ARC\">" +
                              tr("Source repository") + "</a>");
    source->setOpenExternalLinks(true);
    layout->addWidget(source);
    auto *licenses = new QPushButton(tr("Licenses"));
    layout->addWidget(licenses);
    connect(licenses, &QPushButton::clicked, this, [this] {
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Licenses"));
        auto *layout = new QVBoxLayout(&dialog);
        auto *report = new QPlainTextEdit;
        report->setReadOnly(true);
        QString text;
        for (const auto &name : QStringList{"LICENSE", "THIRD_PARTY_NOTICES.md"}) {
            QFile file(QCoreApplication::applicationDirPath() + "/" + name);
            if (file.open(QIODevice::ReadOnly))
                text += QString::fromUtf8(file.readAll()) + "\n\n";
        }
        report->setPlainText(text.isEmpty()
                                 ? tr("License notices are included with the packaged application.")
                                 : text);
        layout->addWidget(report);
        dialog.resize(640, 480);
        dialog.exec();
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
