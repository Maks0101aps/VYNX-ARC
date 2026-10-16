#pragma once
#include <QString>
#include <QStringList>
inline QString outputForFormat(QString path, int format) {
    const QStringList extensions{"zip", "7z",  "tar",  "tar.gz", "gz",      "xz",
                                 "bz2", "zst", "lzma", "tar.xz", "tar.bz2", "tar.zst"};
    for (const auto &ext : QStringList{"tar.gz", "tar.xz", "tar.bz2", "tar.zst", "zip", "7z", "tar",
                                       "tgz", "gz", "xz", "bz2", "zst", "lzma"}) {
        if (path.endsWith("." + ext, Qt::CaseInsensitive)) {
            path.chop(ext.size() + 1);
            break;
        }
    }
    return path + "." + extensions.at(format);
}
