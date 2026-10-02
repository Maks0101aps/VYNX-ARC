#include "BreadcrumbBar.h"
#include <QtWidgets>
BreadcrumbBar::BreadcrumbBar(QWidget *parent) : QWidget(parent), layout_(new QHBoxLayout(this)) {
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(0);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAccessibleName(tr("Archive path"));
}
void BreadcrumbBar::setPath(const QString &archive, const QString &folder) {
    archive_ = archive;
    folder_ = folder;
    setToolTip(archive + " / " + folder);
    rebuild();
}
void BreadcrumbBar::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    rebuild();
}
void BreadcrumbBar::rebuild() {
    while (auto *item = layout_->takeAt(0)) {
        if (item->widget()) {
            item->widget()->hide();
            item->widget()->deleteLater();
        }
        delete item;
    }
    QStringList labels{archive_};
    QStringList targets{QString()};
    QString path;
    for (const auto &segment : folder_.split('/', Qt::SkipEmptyParts)) {
        if (!path.isEmpty())
            path += '/';
        path += segment;
        labels.append(segment);
        targets.append(path);
    }
    int available = qMax(60, width() - 12);
    int required = 0;
    for (const auto &label : labels)
        required += fontMetrics().horizontalAdvance(label) + 32;
    QList<int> visible{0};
    if (required > available && labels.size() > 2)
        visible.append(-1);
    else
        for (int i = 1; i < labels.size() - 1; ++i)
            visible.append(i);
    if (labels.size() > 1)
        visible.append(labels.size() - 1);
    const int budget =
        qMax(28, (available - 16 * (visible.size() - 1)) / qMax(1, int(visible.size())));
    for (int n = 0; n < visible.size(); ++n) {
        if (n) {
            auto *separator = new QLabel(QStringLiteral("›"));
            separator->setObjectName("muted");
            layout_->addWidget(separator);
        }
        int i = visible[n];
        auto *button = new QToolButton;
        button->setObjectName("breadcrumb");
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        if (i < 0) {
            button->setText(QStringLiteral("…"));
            button->setToolTip(toolTip());
            button->setAccessibleName(tr("Hidden path segments"));
            auto *menu = new QMenu(button);
            for (int j = 1; j < labels.size() - 1; ++j)
                connect(menu->addAction(labels[j]), &QAction::triggered, this,
                        [this, target = targets[j]] { emit navigateRequested(target); });
            button->setMenu(menu);
            button->setPopupMode(QToolButton::InstantPopup);
        } else {
            button->setText(fontMetrics().elidedText(labels[i], Qt::ElideMiddle, budget - 12));
            button->setToolTip(labels[i]);
            button->setAccessibleName(labels[i]);
            connect(button, &QToolButton::clicked, this,
                    [this, target = targets[i]] { emit navigateRequested(target); });
        }
        button->setFixedHeight(36);
        button->setMaximumWidth(budget);
        layout_->addWidget(button);
    }
    layout_->addStretch();
}
