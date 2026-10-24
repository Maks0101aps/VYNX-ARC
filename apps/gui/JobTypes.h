#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

enum class JobKind {
    Open,
    Extract,
    Create,
    Modify,
    Convert,
    EditSave,
    Test,
    Hash,
    Preview,
    CopyEntries,
    Shell,
    Read,
    Reconcile,
    Unknown
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

enum class ModifyAction { AddFiles = 0, DeleteEntries = 1, RenameEntry = 2 };

enum class ArchiveFormat {
    Zip = 0,
    SevenZ = 1,
    Tar = 2,
    TarGz = 3,
    Gzip = 4,
    Xz = 5,
    Bzip2 = 6,
    Zstd = 7,
    Lzma = 8,
    TarXz = 9,
    TarBz2 = 10,
    TarZst = 11,
    Rar = 12
};

enum class CompressionPreset { Store = 0, Fast = 1, Balanced = 2, Maximum = 3 };

enum class ResourceMode { Eco = 0, Balanced = 1, Maximum = 2 };

// Wire values match core/src/ffi.rs and core/src/worker/mod.rs.
enum class ConflictPolicy {
    Refuse = 0,
    Skip = 1,
    Replace = 2,
    Ask = 3,
    Rename = 4,
    Newer = 5
};

struct ArchiveJobSpec {
    bool rarSolid = false, rarHeaders = false;
    quint64 rarDictionary = 0;
    int rarRecovery = 0;
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

    ModifyAction action = ModifyAction::AddFiles;
    ArchiveFormat format = ArchiveFormat::Zip;
    CompressionPreset preset = CompressionPreset::Store;
    ResourceMode resource = ResourceMode::Balanced;
    ConflictPolicy conflict = ConflictPolicy::Refuse;
    quint64 volumeSize = 0;
    quint32 shellAction = 0;

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
    QString validationError() const;
    static ArchiveJobSpec fromJson(const QJsonObject &json);
    static QString kindToString(JobKind kind);
    static JobKind stringToKind(const QString &str);
    static QString stateToString(JobState state);
    static JobState stringToState(const QString &str);
    static bool isWriteKind(JobKind kind);
    static ModifyAction actionFromInt(int value);
    static ArchiveFormat formatFromInt(int value);
    static CompressionPreset presetFromInt(int value);
    static ResourceMode resourceFromInt(int value);
    static ConflictPolicy conflictFromInt(int value);
};

