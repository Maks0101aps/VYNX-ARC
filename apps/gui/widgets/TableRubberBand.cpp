#include "TableRubberBand.h"
#include <QAbstractItemModel>
#include <QApplication>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QRubberBand>
#include <QTableView>
#include <algorithm>

TableRubberBand::TableRubberBand(QTableView *table, QObject *parent)
    : QObject(parent ? parent : table), table_(table) {
    if (table_ && table_->viewport()) {
        table_->viewport()->installEventFilter(this);
        rubberBand_ = new QRubberBand(QRubberBand::Rectangle, table_->viewport());
        rubberBand_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        rubberBand_->hide();
    }
}

bool TableRubberBand::eventFilter(QObject *watched, QEvent *event) {
    if (!table_ || watched != table_->viewport())
        return QObject::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton) {
            origin_ = me->pos();
            modifiers_ = me->modifiers();
            isDragging_ = false;

            QModelIndex idx = table_->indexAt(origin_);
            bool clickedSelected = idx.isValid() && table_->selectionModel() &&
                                   table_->selectionModel()->isSelected(idx);

            if (clickedSelected && table_->dragEnabled()) {
                dragDropCandidate_ = true;
                return false;
            }

            dragDropCandidate_ = false;
            if (table_->selectionModel()) {
                initialSelection_ = table_->selectionModel()->selection();
            } else {
                initialSelection_ = QItemSelection();
            }

            if (!idx.isValid() && !(modifiers_ & (Qt::ControlModifier | Qt::ShiftModifier))) {
                if (table_->selectionModel()) {
                    table_->selectionModel()->clearSelection();
                    initialSelection_ = QItemSelection();
                }
            }

            return false;
        }
        break;
    }

    case QEvent::MouseMove: {
        auto *me = static_cast<QMouseEvent *>(event);
        if (dragDropCandidate_)
            return false;

        if (me->buttons() & Qt::LeftButton) {
            if (!isDragging_) {
                if ((me->pos() - origin_).manhattanLength() >= QApplication::startDragDistance()) {
                    isDragging_ = true;
                    if (!rubberBand_) {
                        rubberBand_ = new QRubberBand(QRubberBand::Rectangle, table_->viewport());
                        rubberBand_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
                    }
                    QRect rect = QRect(origin_, me->pos()).normalized();
                    rubberBand_->setGeometry(rect);
                    rubberBand_->show();
                    updateSelection(rect);
                    return true;
                }
            } else {
                QRect rect = QRect(origin_, me->pos()).normalized();
                if (rubberBand_) {
                    rubberBand_->setGeometry(rect);
                }
                updateSelection(rect);
                return true;
            }
        }
        break;
    }

    case QEvent::MouseButtonRelease: {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton) {
            dragDropCandidate_ = false;
            if (isDragging_) {
                isDragging_ = false;
                if (rubberBand_) {
                    rubberBand_->hide();
                }
                return true;
            }
        }
        break;
    }

    case QEvent::KeyPress: {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Escape && isDragging_) {
            isDragging_ = false;
            if (rubberBand_) {
                rubberBand_->hide();
            }
            if (table_->selectionModel()) {
                table_->selectionModel()->select(initialSelection_,
                                                  QItemSelectionModel::ClearAndSelect |
                                                      QItemSelectionModel::Rows);
            }
            return true;
        }
        break;
    }

    default:
        break;
    }

    return QObject::eventFilter(watched, event);
}

void TableRubberBand::updateSelection(const QRect &normalizedRect) {
    if (!table_ || !table_->model() || !table_->selectionModel())
        return;

    int totalRows = table_->model()->rowCount();
    if (totalRows <= 0) {
        applySelection(QItemSelection());
        return;
    }

    int lastRow = totalRows - 1;
    int firstRowTop = table_->rowViewportPosition(0);
    int lastRowBottom = table_->rowViewportPosition(lastRow) + table_->rowHeight(lastRow);

    if (normalizedRect.bottom() < firstRowTop || normalizedRect.top() >= lastRowBottom) {
        applySelection(QItemSelection());
        return;
    }

    int topRow = table_->rowAt(normalizedRect.top());
    if (topRow < 0) {
        topRow = (normalizedRect.top() < firstRowTop) ? 0 : lastRow;
    }

    int bottomRow = table_->rowAt(normalizedRect.bottom());
    if (bottomRow < 0) {
        bottomRow = (normalizedRect.bottom() >= lastRowBottom) ? lastRow : 0;
    }

    if (topRow > bottomRow)
        std::swap(topRow, bottomRow);

    topRow = std::max(0, std::min(topRow, lastRow));
    bottomRow = std::max(0, std::min(bottomRow, lastRow));

    int lastCol = std::max(0, table_->model()->columnCount() - 1);
    QModelIndex topLeft = table_->model()->index(topRow, 0);
    QModelIndex bottomRight = table_->model()->index(bottomRow, lastCol);
    QItemSelection selection(topLeft, bottomRight);

    applySelection(selection);
}

void TableRubberBand::applySelection(const QItemSelection &selection) {
    if (!table_ || !table_->selectionModel())
        return;

    if (modifiers_ & Qt::ControlModifier) {
        QItemSelection result = initialSelection_;
        result.merge(selection, QItemSelectionModel::Toggle);
        table_->selectionModel()->select(result,
                                          QItemSelectionModel::ClearAndSelect |
                                              QItemSelectionModel::Rows);
    } else if (modifiers_ & Qt::ShiftModifier) {
        QItemSelection result = initialSelection_;
        result.merge(selection, QItemSelectionModel::Select);
        table_->selectionModel()->select(result,
                                          QItemSelectionModel::ClearAndSelect |
                                              QItemSelectionModel::Rows);
    } else {
        table_->selectionModel()->select(selection,
                                          QItemSelectionModel::ClearAndSelect |
                                              QItemSelectionModel::Rows);
    }
}
