#pragma once
#include <QItemSelection>
#include <QObject>
#include <QPoint>

class QTableView;
class QRubberBand;

class TableRubberBand : public QObject {
    Q_OBJECT
  public:
    explicit TableRubberBand(QTableView *table, QObject *parent = nullptr);

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    void updateSelection(const QRect &normalizedRect);
    void applySelection(const QItemSelection &selection);

    QTableView *table_ = nullptr;
    QRubberBand *rubberBand_ = nullptr;
    QPoint origin_;
    bool isDragging_ = false;
    bool dragDropCandidate_ = false;
    Qt::KeyboardModifiers modifiers_ = Qt::NoModifier;
    QItemSelection initialSelection_;
};
