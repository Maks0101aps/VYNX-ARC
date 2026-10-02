#pragma once
#include <QDialog>
class QLineEdit;
class QComboBox;
class QCheckBox;
class ExtractDialog : public QDialog {
    Q_OBJECT
  public:
    ExtractDialog(const QString &defaultDest, bool hasSelection, bool smart, bool here,
                  const QString &initialPassword, QWidget *parent = nullptr);
    QLineEdit *destination, *password;
    QComboBox *scope, *conflicts;
    QCheckBox *smartBox;
};
