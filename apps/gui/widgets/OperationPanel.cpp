#include "OperationPanel.h"
#include "../ArchiveModel.h"
#include <QtWidgets>
#include <cmath>
OperationPanel::OperationPanel(QWidget *parent) : QWidget(parent) {
    setObjectName("operation");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(8);
    title_ = new QLabel;
    title_->setTextFormat(Qt::PlainText);
    layout->addWidget(title_);
    phase_ = new QLabel;
    phase_->setTextFormat(Qt::PlainText);
    layout->addWidget(phase_);
    details_ = new QLabel;
    details_->setTextFormat(Qt::PlainText);
    details_->setWordWrap(true);
    layout->addWidget(details_);
    current = new QLabel;
    current->setTextFormat(Qt::PlainText);
    current->setObjectName("muted");
    layout->addWidget(current);
    auto *row = new QHBoxLayout;
    progress = new QProgressBar;
    progress->setAccessibleName(tr("Operation progress"));
    progress->setFormat("%p%");
    cancel = new QPushButton(tr("Cancel"));
    row->addWidget(progress, 1);
    row->addWidget(cancel);
    layout->addLayout(row);
    rate = new QLabel;
    rate->setObjectName("muted");
    layout->addWidget(rate);
    hide();
}
void OperationPanel::begin(const QString &title) {
    title_->setText(title);
    current->clear();
    phase_->clear();
    details_->clear();
    previousPhase_.clear();
    rate->clear();
    previousDone_ = previousTotal_ = 0;
    previousTime_ = startTime_ = 0;
    speed_ = 0;
    progress->setRange(0, 0);
    cancel->setEnabled(true);
    show();
}
void OperationPanel::updateProgress(quint64 done, quint64 total, const QString &path,
                                    qint64 elapsed, const QString &phase, const QString &details,
                                    bool cancelling) {
    phase_->setText(cancelling ? tr("Cancelling…") : phase);
    details_->setText(details);
    details_->setVisible(!details.isEmpty());
    if (cancelling)
        cancel->setEnabled(false);
    current->setToolTip(path);
    current->setText(
        current->fontMetrics().elidedText(path, Qt::ElideMiddle, qMax(120, current->width())));
    if (total) {
        progress->setRange(0, 1000);
        progress->setValue(int(qMin(1000.0, double(done) / double(total) * 1000.0)));
    } else
        progress->setRange(0, 0);
    if (done < previousDone_ || total != previousTotal_ || phase != previousPhase_) {
        speed_ = 0;
        startTime_ = elapsed;
        previousDone_ = done;
        previousTime_ = elapsed;
    }
    double dt = (elapsed - previousTime_) / 1000.0;
    if (dt >= 0.2) {
        double sample = double(done - previousDone_) / dt;
        double weight = 1 - std::exp(-dt / 2.0);
        speed_ = speed_ ? speed_ + (sample - speed_) * weight : sample;
        previousTime_ = elapsed;
        previousDone_ = done;
    }
    previousTotal_ = total;
    previousPhase_ = phase;
    auto report = tr("%1 / %2 · %3/s · %4 s elapsed")
                      .arg(displaySize(done), total ? displaySize(total) : tr("unknown"),
                           displaySize(quint64(qMax(0.0, speed_))),
                           QString::number(elapsed / 1000.0, 'f', 1));
    if (total && done < total && done > total / 100 && elapsed - startTime_ >= 3000 && speed_ > 0) {
        double eta = 1.25 * (total - done) / speed_;
        if (eta >= 1 && eta <= 3600)
            report += tr(" · ~%1 s remaining").arg(qRound(eta));
    }
    rate->setText(report);
}
