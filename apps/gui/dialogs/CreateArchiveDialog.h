#pragma once
#include <QDialog>
#include <QStringList>
class QListWidget;
class QComboBox;
class QLineEdit;
class QGroupBox;
class QSpinBox;
class CreateArchiveDialog : public QDialog {
    Q_OBJECT
  public:
    CreateArchiveDialog(const QStringList &initial, int initialFormat, QWidget *parent = nullptr);
    QListWidget *inputs;
    QComboBox *format, *split, *preset, *resource;
    QLineEdit *output, *password;
    QGroupBox *advanced;
    QSpinBox *customSplit;
};
