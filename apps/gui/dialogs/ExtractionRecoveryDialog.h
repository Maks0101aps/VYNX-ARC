#pragma once
#include <QDialog>
#include <QJsonObject>
#include <QString>
#include <QVector>

struct RecoveryFileStatus {
    QString name;
    quint64 expectedSize = 0;
    qint64 expectedModified = 0;
    QString expectedCrc;
    enum State {
        Completed,
        Missing,
        ModifiedByUser,
        PartialOrCorrupt
    } state = Missing;
    QString details;
    bool willExtract = false;
};

class QTableWidget;
class QLabel;
class QPushButton;

class ExtractionRecoveryDialog : public QDialog {
    Q_OBJECT
  public:
    ExtractionRecoveryDialog(const QString &archivePath,
                             const QString &destinationRoot,
                             const QString &savedFingerprint,
                             const QJsonObject &recoveryPlan,
                             QWidget *parent = nullptr);

    bool canResume() const { return canResume_; }
    QVector<RecoveryFileStatus> filesToExtract() const;

  private:
    void reconcile();

    QString archivePath_;
    QString destinationRoot_;
    QString savedFingerprint_;
    QJsonObject recoveryPlan_;
    bool canResume_ = false;

    QVector<RecoveryFileStatus> files_;
    QLabel *summaryLabel_ = nullptr;
    QLabel *sourceStatusLabel_ = nullptr;
    QTableWidget *table_ = nullptr;
    QPushButton *resumeButton_ = nullptr;
};

