#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

enum class JobKind {
    Unknown,
    Open,
    Extract,
    Create,
    Modify,
    Convert,
    EditSave,
    Test,
    Hash,
    Preview,
    CopyEntries
};

enum class JobState {
    Pending,
    Running,
    Decision,
    Verifying,
    Completed,
    Failed,
    Cancelled,
    Interrupted,
    Unknown
};

struct ArchiveJobSpec {
    JobKind kind = JobKind::Unknown;
    QString id;
    QString title;
    QString destination;
    QString folder;
    QString newName;
    QString editId;
    QString batchId;
    QString retryOf;
    QString archiveHash;
    QString inputHash;
    QString sourceSize;
    QString sourceModified;

    int action = 0;
    int format = 0;
    int preset = 0;
    int resource = 1;
    int conflict = 0; // 0: Refuse, 1: Replace, 2: Rename, 3: Skip, 4: Ask, 5: Newer
    quint64 volumeSize = 0;

    QStringList sources;
    QStringList names;

    bool smart = false;
    bool needsPassword = false;
    bool needsOutputPassword = false;
    bool allowDecrypt = false;
    bool incompleteSpec = false;

    // 0.7 extensions:
    QString sourceArchive;
    QString targetArchive;
    QJsonObject recoveryPlan;

    QJsonObject toJson() const;
    static ArchiveJobSpec fromJson(const QJsonObject &json);
    static QString kindToString(JobKind kind);
    static JobKind stringToKind(const QString &str);
    static QString stateToString(JobState state);
    static JobState stringToState(const QString &str);
};

