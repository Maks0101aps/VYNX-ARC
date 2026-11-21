#pragma once
#include <QDialog>
class QComboBox;
class QCheckBox;
class SettingsDialog : public QDialog {
    Q_OBJECT
  public:
    SettingsDialog(const QString &themeId, const QString &languageId, bool remember,
                   bool checkUpdatesOnStartup = true,
                   QWidget *parent = nullptr);
    QComboBox *theme, *language, *resource;
    QCheckBox *history;
    QCheckBox *checkUpdates;
};
