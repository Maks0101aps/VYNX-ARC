#include "JobTypes.h"

static int boundedValue(int value, int maximum, int fallback) {
    return value < 0 || value > maximum ? fallback : value;
}

QString ArchiveJobSpec::kindToString(JobKind kind) {
    switch (kind) {
    case JobKind::Open:
        return QStringLiteral("open");
    case JobKind::Extract:
        return QStringLiteral("extract");
    case JobKind::Create:
        return QStringLiteral("create");
    case JobKind::Modify:
        return QStringLiteral("modify");
    case JobKind::Convert:
        return QStringLiteral("convert");
    case JobKind::EditSave:
        return QStringLiteral("edit");
    case JobKind::Test:
        return QStringLiteral("test");
    case JobKind::Hash:
        return QStringLiteral("hash");
    case JobKind::Preview:
        return QStringLiteral("preview");
    case JobKind::CopyEntries:
        return QStringLiteral("copy");
    case JobKind::Shell:
        return QStringLiteral("shell");
    case JobKind::Read:
        return QStringLiteral("read");
    case JobKind::Reconcile:
        return QStringLiteral("reconcile");
    case JobKind::Unknown:
    default:
        return QStringLiteral("unknown");
    }
}

JobKind ArchiveJobSpec::stringToKind(const QString &str) {
    if (str == QLatin1String("open"))
        return JobKind::Open;
    if (str == QLatin1String("extract"))
        return JobKind::Extract;
    if (str == QLatin1String("create"))
        return JobKind::Create;
    if (str == QLatin1String("modify"))
        return JobKind::Modify;
    if (str == QLatin1String("convert"))
        return JobKind::Convert;
    if (str == QLatin1String("edit"))
        return JobKind::EditSave;
    if (str == QLatin1String("test"))
        return JobKind::Test;
    if (str == QLatin1String("hash"))
        return JobKind::Hash;
    if (str == QLatin1String("preview"))
        return JobKind::Preview;
    if (str == QLatin1String("copy"))
        return JobKind::CopyEntries;
    if (str == QLatin1String("shell"))
        return JobKind::Shell;
    if (str == QLatin1String("read"))
        return JobKind::Read;
    if (str == QLatin1String("reconcile"))
        return JobKind::Reconcile;
    return JobKind::Unknown;
}

bool ArchiveJobSpec::isWriteKind(JobKind kind) {
    switch (kind) {
    case JobKind::Create:
    case JobKind::Extract:
    case JobKind::Modify:
    case JobKind::Convert:
    case JobKind::EditSave:
    case JobKind::CopyEntries:
    case JobKind::Shell:
        return true;
    default:
        return false;
    }
}

ModifyAction ArchiveJobSpec::actionFromInt(int value) {
    return static_cast<ModifyAction>(boundedValue(value, 2, int(ModifyAction::AddFiles)));
}

ArchiveFormat ArchiveJobSpec::formatFromInt(int value) {
    return static_cast<ArchiveFormat>(boundedValue(value, 11, int(ArchiveFormat::Zip)));
}

CompressionPreset ArchiveJobSpec::presetFromInt(int value) {
    return static_cast<CompressionPreset>(
        boundedValue(value, 3, int(CompressionPreset::Store)));
}

ResourceMode ArchiveJobSpec::resourceFromInt(int value) {
    return static_cast<ResourceMode>(boundedValue(value, 2, int(ResourceMode::Balanced)));
}

ConflictPolicy ArchiveJobSpec::conflictFromInt(int value) {
    return static_cast<ConflictPolicy>(boundedValue(value, 5, int(ConflictPolicy::Refuse)));
}

QString ArchiveJobSpec::stateToString(JobState state) {
    switch (state) {
    case JobState::Pending:
        return QStringLiteral("pending");
    case JobState::Running:
        return QStringLiteral("running");
    case JobState::Decision:
        return QStringLiteral("decision");
    case JobState::Verifying:
        return QStringLiteral("verifying");
    case JobState::Completed:
        return QStringLiteral("completed");
    case JobState::Failed:
        return QStringLiteral("failed");
    case JobState::Cancelled:
        return QStringLiteral("cancelled");
    case JobState::Interrupted:
        return QStringLiteral("interrupted");
    case JobState::Unknown:
    default:
        return QStringLiteral("unknown");
    }
}

JobState ArchiveJobSpec::stringToState(const QString &str) {
    if (str == QLatin1String("pending"))
        return JobState::Pending;
    if (str == QLatin1String("running"))
        return JobState::Running;
    if (str == QLatin1String("decision"))
        return JobState::Decision;
    if (str == QLatin1String("verifying"))
        return JobState::Verifying;
    if (str == QLatin1String("completed"))
        return JobState::Completed;
    if (str == QLatin1String("failed"))
        return JobState::Failed;
    if (str == QLatin1String("cancelled"))
        return JobState::Cancelled;
    if (str == QLatin1String("interrupted"))
        return JobState::Interrupted;
    return JobState::Unknown;
}

QJsonObject ArchiveJobSpec::toJson() const {
    QJsonObject obj;
    obj[QStringLiteral("kind")] = kindToString(kind);
    if (!id.isEmpty())
        obj[QStringLiteral("id")] = id;
    if (!destination.isEmpty())
        obj[QStringLiteral("destination")] = destination;
    if (!folder.isEmpty())
        obj[QStringLiteral("folder")] = folder;
    if (!newName.isEmpty())
        obj[QStringLiteral("newName")] = newName;
    if (!editId.isEmpty())
        obj[QStringLiteral("editId")] = editId;
    if (!batchId.isEmpty())
        obj[QStringLiteral("batchId")] = batchId;
    if (!retryOf.isEmpty())
        obj[QStringLiteral("retryOf")] = retryOf;
    if (!archiveHash.isEmpty())
        obj[QStringLiteral("archiveHash")] = archiveHash;
    if (!inputHash.isEmpty())
        obj[QStringLiteral("inputHash")] = inputHash;
    if (!sourceSize.isEmpty())
        obj[QStringLiteral("sourceSize")] = sourceSize;
    if (!sourceModified.isEmpty())
        obj[QStringLiteral("sourceModified")] = sourceModified;

    obj[QStringLiteral("action")] = kind == JobKind::Shell ? double(shellAction) : int(action);
    obj[QStringLiteral("format")] = int(format);
    obj[QStringLiteral("preset")] = int(preset);
    obj[QStringLiteral("resource")] = int(resource);
    obj[QStringLiteral("conflict")] = int(conflict);
    if (volumeSize > 0)
        obj[QStringLiteral("volumeSize")] = double(volumeSize);

    if (!sources.isEmpty()) {
        QJsonArray arr;
        for (const auto &s : sources)
            arr.append(s);
        obj[QStringLiteral("sources")] = arr;
    }
    if (!names.isEmpty()) {
        QJsonArray arr;
        for (const auto &n : names)
            arr.append(n);
        obj[QStringLiteral("names")] = arr;
    }

    if (smart)
        obj[QStringLiteral("smart")] = true;
    if (needsPassword)
        obj[QStringLiteral("needsPassword")] = true;
    if (needsOutputPassword)
        obj[QStringLiteral("needsOutputPassword")] = true;
    if (allowDecrypt)
        obj[QStringLiteral("allowDecrypt")] = true;
    if (incompleteSpec)
        obj[QStringLiteral("incompleteSpec")] = true;

    if (!sourceArchive.isEmpty())
        obj[QStringLiteral("sourceArchive")] = sourceArchive;
    if (!targetArchive.isEmpty())
        obj[QStringLiteral("targetArchive")] = targetArchive;
    if (!recoveryPlan.isEmpty())
        obj[QStringLiteral("recoveryPlan")] = recoveryPlan;

    return obj;
}

ArchiveJobSpec ArchiveJobSpec::fromJson(const QJsonObject &json) {
    ArchiveJobSpec spec;
    spec.kind = stringToKind(json[QStringLiteral("kind")].toString());
    spec.id = json[QStringLiteral("id")].toString();
    spec.destination = json[QStringLiteral("destination")].toString();
    spec.folder = json[QStringLiteral("folder")].toString();
    spec.newName = json[QStringLiteral("newName")].toString();
    spec.editId = json[QStringLiteral("editId")].toString();
    spec.batchId = json[QStringLiteral("batchId")].toString();
    spec.retryOf = json[QStringLiteral("retryOf")].toString();
    spec.archiveHash = json[QStringLiteral("archiveHash")].toString();
    spec.inputHash = json[QStringLiteral("inputHash")].toString();
    spec.sourceSize = json[QStringLiteral("sourceSize")].toString();
    spec.sourceModified = json[QStringLiteral("sourceModified")].toString();

    spec.action = actionFromInt(json[QStringLiteral("action")].toInt());
    spec.format = formatFromInt(json[QStringLiteral("format")].toInt());
    spec.preset = presetFromInt(json[QStringLiteral("preset")].toInt(2));
    spec.resource =
        resourceFromInt(json[QStringLiteral("resource")].toInt(int(ResourceMode::Balanced)));
    spec.conflict = conflictFromInt(json[QStringLiteral("conflict")].toInt());
    spec.volumeSize = quint64(json[QStringLiteral("volumeSize")].toDouble());
    spec.shellAction = quint32(json[QStringLiteral("action")].toInt());

    for (const auto &val : json[QStringLiteral("sources")].toArray())
        spec.sources.append(val.toString());
    for (const auto &val : json[QStringLiteral("names")].toArray())
        spec.names.append(val.toString());

    spec.smart = json[QStringLiteral("smart")].toBool();
    spec.needsPassword = json[QStringLiteral("needsPassword")].toBool();
    spec.needsOutputPassword = json[QStringLiteral("needsOutputPassword")].toBool();
    spec.allowDecrypt = json[QStringLiteral("allowDecrypt")].toBool();
    spec.incompleteSpec = json[QStringLiteral("incompleteSpec")].toBool();

    spec.sourceArchive = json[QStringLiteral("sourceArchive")].toString();
    spec.targetArchive = json[QStringLiteral("targetArchive")].toString();
    spec.recoveryPlan = json[QStringLiteral("recoveryPlan")].toObject();

    return spec;
}

QString ArchiveJobSpec::validationError() const {
    switch (kind) {
    case JobKind::Open:
    case JobKind::Test:
    case JobKind::Hash:
    case JobKind::Preview:
    case JobKind::Read:
        if (sources.size() != 1)
            return QStringLiteral("OPTIONS: This operation requires exactly one source archive");
        break;
    case JobKind::Extract:
        if (sources.size() != 1 || destination.isEmpty())
            return QStringLiteral(
                "OPTIONS: Extraction requires one source archive and a destination");
        break;
    case JobKind::Create:
        if (sources.isEmpty() || destination.isEmpty())
            return QStringLiteral("OPTIONS: Archive creation requires inputs and a destination");
        break;
    case JobKind::Modify:
        if (sources.size() != 1 || names.isEmpty())
            return QStringLiteral(
                "OPTIONS: Modification requires one archive and selected entries");
        if (action == ModifyAction::RenameEntry && newName.isEmpty())
            return QStringLiteral("OPTIONS: Renaming requires a new archive path");
        break;
    case JobKind::Shell:
        if (sources.isEmpty())
            return QStringLiteral("OPTIONS: Shell integration requires at least one path");
        break;
    case JobKind::CopyEntries:
        return QStringLiteral("OPTIONS: Repeat inter-archive copy from its source tab");
    case JobKind::Convert:
        if (sources.size() != 1 || destination.isEmpty())
            return QStringLiteral("OPTIONS: Conversion requires one source and a destination");
        break;
    default:
        return QStringLiteral("OPTIONS: Unsupported restored operation");
    }
    return {};
}

