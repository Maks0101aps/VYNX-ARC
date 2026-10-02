#include "ToastOverlay.h"
#include <QtWidgets>
ToastOverlay::ToastOverlay(QWidget *parent) : QLabel(parent), timer_(new QTimer(this)) {
    setObjectName("toast");
    setTextFormat(Qt::PlainText);
    setWordWrap(true);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAccessibleName(tr("Notification"));
    timer_->setSingleShot(true);
    connect(timer_, &QTimer::timeout, this, &QWidget::hide);
    parent->installEventFilter(this);
    hide();
}
void ToastOverlay::place() {
    setFixedWidth(qMin(380, parentWidget()->width() - 32));
    adjustSize();
    move(parentWidget()->width() - width() - 16, parentWidget()->height() - height() - 16);
    raise();
}
void ToastOverlay::notify(const QString &message) {
    setText(message);
    place();
    show();
    timer_->start(3200);
}
bool ToastOverlay::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::Resize)
        place();
    return QLabel::eventFilter(watched, event);
}
