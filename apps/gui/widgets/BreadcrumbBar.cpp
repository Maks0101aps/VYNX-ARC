#include "BreadcrumbBar.h"
#include <QtWidgets>

BreadcrumbBar::BreadcrumbBar(QWidget *parent) : QWidget(parent), layout_(new QHBoxLayout(this)) {
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(0);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAccessibleName(tr("Archive path"));
}

void BreadcrumbBar::setPath(const QString &archive, const QString &folder, const QStringList &parentChain) {
    archive_ = archive;
    folder_ = folder;
    parentChain_ = parentChain;
    QString fullToolTip;
    for (const auto &p : parentChain)
        fullToolTip += p + QStringLiteral(" › ");
    fullToolTip += archive;
    if (!folder.isEmpty())
        fullToolTip += QStringLiteral(" / ") + folder;
    setToolTip(fullToolTip);
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

    struct Segment {
        QString label;
        bool isParent = false;
        int parentLevel = 0;
        QString folderTarget;
    };

    QVector<Segment> segments;
    for (int i = 0; i < parentChain_.size(); ++i) {
        Segment s;
        s.label = parentChain_[i];
        s.isParent = true;
        s.parentLevel = i;
        segments.append(s);
    }

    Segment currentArchive;
    currentArchive.label = archive_;
    currentArchive.folderTarget = QString();
    segments.append(currentArchive);

    QString path;
    for (const auto &seg : folder_.split('/', Qt::SkipEmptyParts)) {
        if (!path.isEmpty())
            path += '/';
        path += seg;
        Segment s;
        s.label = seg;
        s.folderTarget = path;
        segments.append(s);
    }

    int available = qMax(60, width() - 12);
    int required = 0;
    for (const auto &seg : segments)
        required += fontMetrics().horizontalAdvance(seg.label) + 32;

    QList<int> visible{0};
    if (required > available && segments.size() > 2)
        visible.append(-1);
    else
        for (int i = 1; i < segments.size() - 1; ++i)
            visible.append(i);
    if (segments.size() > 1)
        visible.append(segments.size() - 1);

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
            for (int j = 1; j < segments.size() - 1; ++j) {
                const auto &seg = segments[j];
                auto *act = menu->addAction(seg.label);
                if (seg.isParent) {
                    connect(act, &QAction::triggered, this,
                            [this, level = seg.parentLevel] { emit parentArchiveRequested(level); });
                } else {
                    connect(act, &QAction::triggered, this,
                            [this, target = seg.folderTarget] { emit navigateRequested(target); });
                }
            }
            button->setMenu(menu);
            button->setPopupMode(QToolButton::InstantPopup);
        } else {
            const auto &seg = segments[i];
            button->setText(fontMetrics().elidedText(seg.label, Qt::ElideMiddle, budget - 12));
            button->setToolTip(seg.label);
            button->setAccessibleName(seg.label);
            if (seg.isParent) {
                connect(button, &QToolButton::clicked, this,
                        [this, level = seg.parentLevel] { emit parentArchiveRequested(level); });
            } else {
                connect(button, &QToolButton::clicked, this,
                        [this, target = seg.folderTarget] { emit navigateRequested(target); });
            }
        }
        button->setFixedHeight(36);
        button->setMaximumWidth(budget);
        layout_->addWidget(button);
    }
    layout_->addStretch();
}
