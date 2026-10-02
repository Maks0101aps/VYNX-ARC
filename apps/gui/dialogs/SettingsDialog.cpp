#include "SettingsDialog.h"
#include <QtWidgets>
SettingsDialog::SettingsDialog(const QString &themeId, const QString &languageId, bool remember,
                               QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("Settings"));
    resize(520, 340);
    auto *layout = new QVBoxLayout(this);
    auto *tabs = new QTabWidget;
    layout->addWidget(tabs);
    auto *general = new QWidget;
    auto *form = new QFormLayout(general);
    language = new QComboBox;
    language->addItems({"English", QString::fromUtf8("Українська"), QString::fromUtf8("Русский")});
    language->setCurrentIndex(qMax(0, QStringList{"en", "uk", "ru"}.indexOf(languageId)));
    form->addRow(tr("Language (restart required)"), language);
    history = new QCheckBox(tr("Remember recent archives locally"));
    history->setChecked(remember);
    form->addRow(history);
    tabs->addTab(general, tr("General"));
    auto *appearance = new QWidget;
    auto *appearanceForm = new QFormLayout(appearance);
    theme = new QComboBox;
    theme->addItems({tr("Follow system"), tr("Light"), tr("Dark")});
    theme->setCurrentIndex(qMax(0, QStringList{"system", "light", "dark"}.indexOf(themeId)));
    appearanceForm->addRow(tr("Appearance"), theme);
    tabs->addTab(appearance, tr("Appearance"));
    auto *security = new QWidget;
    auto *securityLayout = new QVBoxLayout(security);
    auto *notice = new QLabel(
        tr("Path protection and link blocking are always enabled.\nNo telemetry or background "
           "services.\nExplorer integration requires a signed identity package."));
    notice->setWordWrap(true);
    securityLayout->addWidget(notice);
    securityLayout->addStretch();
    tabs->addTab(security, tr("Security"));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setObjectName("primary");
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
