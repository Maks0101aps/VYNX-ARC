#pragma once
#include <QWidget>
class QHBoxLayout;
class BreadcrumbBar : public QWidget {
    Q_OBJECT
  public:
    explicit BreadcrumbBar(QWidget *parent = nullptr);
    void setPath(const QString &archive, const QString &folder);
    QSize minimumSizeHint() const override { return {80, 36}; }
    QSize sizeHint() const override { return {340, 36}; }
  signals:
    void navigateRequested(const QString &folder);

  protected:
    void resizeEvent(QResizeEvent *event) override;

  private:
    void rebuild();
    QString archive_, folder_;
    QHBoxLayout *layout_;
};
