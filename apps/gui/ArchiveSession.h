#pragma once
#include "ArchiveModel.h"
#include <QSet>
#include <QStringList>
#include <QUuid>
#include <memory>
class SecretUtf8;
// Document state never belongs to the active window or a queue row.
struct ArchiveSession {
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString path, search;
    QByteArray headerState;
    QStringList history;
    QSet<QString> selection;
    int scroll = 0;
    bool busy = false, closeWhenIdle = false;
    std::shared_ptr<rust::Box<vynx::Archive>> archive;
    std::shared_ptr<SecretUtf8> password;
    ArchiveModel *model = new ArchiveModel;
    ArchiveFilter *filter = new ArchiveFilter;
    ArchiveSession() {
        filter->setSourceModel(model);
        filter->setFilterCaseSensitivity(Qt::CaseInsensitive);
        filter->setFilterKeyColumn(0);
    }
    ~ArchiveSession() {
        delete filter;
        delete model;
    }
};
