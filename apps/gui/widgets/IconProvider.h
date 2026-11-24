#pragma once
#include <QIcon>
#include <QString>
namespace Icons {
QIcon get(const QString &kind);
QIcon file(const QString &name, bool folder = false, bool isUp = false);
} // namespace Icons
