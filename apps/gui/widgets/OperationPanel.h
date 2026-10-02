#pragma once
#include <QWidget>
class QLabel;
class QProgressBar;
class QPushButton;
class OperationPanel : public QWidget {
    Q_OBJECT
  public:
    explicit OperationPanel(QWidget *parent = nullptr);
    QLabel *current, *rate;
    QProgressBar *progress;
    QPushButton *cancel;
    void begin(const QString &title);
    void updateProgress(quint64 done, quint64 total, const QString &path, qint64 elapsed);

  private:
    QLabel *title_;
    quint64 previousDone_ = 0, previousTotal_ = 0;
    qint64 previousTime_ = 0, startTime_ = 0;
    double speed_ = 0;
};
