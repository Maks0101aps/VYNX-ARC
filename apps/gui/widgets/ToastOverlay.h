#pragma once
#include <QLabel>
class QTimer;
class ToastOverlay : public QLabel {
    Q_OBJECT
  public:
    explicit ToastOverlay(QWidget *parent);
    void notify(const QString &message);

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    void place();
    QTimer *timer_;
};
