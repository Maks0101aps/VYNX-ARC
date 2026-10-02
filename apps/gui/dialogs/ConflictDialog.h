#pragma once
#include <QDialog>
class ConflictDialog : public QDialog {
    Q_OBJECT
  public:
    ConflictDialog(const QString &existing, quint64 existingSize, quint64 existingTime,
                   const QString &incoming, quint64 incomingSize, quint64 incomingTime,
                   QWidget *parent = nullptr);
  signals:
    void decision(quint8 choice, bool all);
};
