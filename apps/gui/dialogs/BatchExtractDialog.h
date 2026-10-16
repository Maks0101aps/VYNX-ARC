#pragma once
#include <QDialog>
#include <QStringList>
class QListWidget;
class QLineEdit;
class QComboBox;
class BatchExtractDialog : public QDialog {
    Q_OBJECT
  public:
    explicit BatchExtractDialog(QWidget *parent = nullptr);
    QStringList archives() const;
    QListWidget *files;
    QLineEdit *destination, *password;
    QComboBox *conflicts;
};
QStringList batchDestinations(const QStringList &archives, const QString &destination);
