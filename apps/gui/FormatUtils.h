#pragma once
#include <QString>
#include <QStringList>
inline QString outputForFormat(QString path, int format) {
    const QStringList extensions{"zip", "7z", "tar", "tar.gz"};
    for (const auto &ext : QStringList{"tar.gz", "zip", "7z", "tar", "tgz"}) {
        if (path.endsWith("." + ext, Qt::CaseInsensitive)) {
            path.chop(ext.size() + 1);
            break;
        }
    }
    return path + "." + extensions.at(format);
}
