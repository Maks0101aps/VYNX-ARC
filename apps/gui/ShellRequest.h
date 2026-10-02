#pragma once
#include <QStringList>
struct ShellRequest {
    quint32 action = 0;
    QStringList paths;
};
ShellRequest consumeShellRequest(const QString &path);
ShellRequest decodeShellRequest(const QByteArray &data);
