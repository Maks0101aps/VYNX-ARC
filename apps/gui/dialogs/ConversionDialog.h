#pragma once
#include "vynx-arc-core/src/ffi.rs.h"
#include <QDialog>
#include <QFutureWatcher>
#include <QStringList>
#include <memory>
class QLineEdit;
class QComboBox;
class QCheckBox;
class QTableWidget;
class QPushButton;
class QLabel;
class ConversionDialog : public QDialog {
    Q_OBJECT
  public:
    struct Plan {
        QString source, output;
    };
    explicit ConversionDialog(const QStringList &paths, QWidget *parent = nullptr);
    ~ConversionDialog() override;
    QLineEdit *destination, *sourcePassword, *outputPassword, *outputName;
    QComboBox *format, *preset, *resource, *conflict;
    QCheckBox *encrypt, *allowDecrypt;
    QStringList sources() const { return paths_; }
    QVector<Plan> plan() const;

  private:
    struct Scan {
        QString format, error;
        quint64 bytes = 0;
        qsizetype entries = 0;
    };
    QStringList paths_;
    QTableWidget *table_;
    QPushButton *scan_, *start_;
    QLabel *summary_;
    QFutureWatcher<QVector<Scan>> watcher_;
    std::shared_ptr<rust::Box<vynx::Operation>> operation_;
    bool scanned_ = false;
    void updatePlan();
    void scan();
    void invalidate();
};
