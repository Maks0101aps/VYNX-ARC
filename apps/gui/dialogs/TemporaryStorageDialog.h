#pragma once
#include <QDialog>
#include <QSet>
#include <memory>

class TempStore;
class QTableWidget;
class QLabel;
class QComboBox;
class QPushButton;

class TemporaryStorageDialog : public QDialog {
    Q_OBJECT
  public:
    TemporaryStorageDialog(TempStore *store, const QSet<QString> &activeNestedIds, QWidget *parent = nullptr);

  private:
    void refresh();
    void safeCleanup();

    TempStore *store_ = nullptr;
    QSet<QString> activeNestedIds_;
    QLabel *previewSummary_ = nullptr;
    QLabel *nestedSummary_ = nullptr;
    QLabel *editSummary_ = nullptr;
    QComboBox *categoryFilter_ = nullptr;
    QTableWidget *table_ = nullptr;
    QPushButton *cleanupButton_ = nullptr;
    QPushButton *openButton_ = nullptr;
    QPushButton *deleteButton_ = nullptr;
};

