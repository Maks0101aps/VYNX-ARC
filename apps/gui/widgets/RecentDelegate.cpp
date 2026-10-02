#include "RecentDelegate.h"
#include "IconProvider.h"
#include <QtWidgets>
void RecentDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                           const QModelIndex &index) const {
    auto styled = option;
    initStyleOption(&styled, index);
    styled.text.clear();
    styled.icon = {};
    auto *style = styled.widget ? styled.widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &styled, painter, styled.widget);
    auto path = index.data(Qt::UserRole).toString();
    auto rect = option.rect.adjusted(12, 6, -12, -6);
    Icons::get("archive").paint(painter, QRect(rect.left(), rect.top() + 12, 24, 24));
    rect.setLeft(rect.left() + 40);
    painter->save();
    painter->setPen(option.palette.color(QPalette::Text));
    painter->drawText(
        rect.adjusted(0, 0, 0, -24), Qt::AlignLeft | Qt::AlignVCenter,
        option.fontMetrics.elidedText(QFileInfo(path).fileName(), Qt::ElideMiddle, rect.width()));
    painter->setPen(option.palette.color(QPalette::PlaceholderText));
    QFont secondary = option.font;
    secondary.setPointSizeF(qMax(8.0, secondary.pointSizeF() - 1));
    painter->setFont(secondary);
    painter->drawText(rect.adjusted(0, 24, 0, 0), Qt::AlignLeft | Qt::AlignVCenter,
                      QFontMetrics(secondary).elidedText(path, Qt::ElideMiddle, rect.width()));
    painter->restore();
}
