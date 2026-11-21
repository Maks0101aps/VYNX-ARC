#pragma once

#include <QString>
#include <QStringList>

namespace UpdateInstaller {

// Returns the command-line arguments for Inno Setup installer.
QStringList installerArgs();

// Returns the generated batch script content for portable mode upgrade.
QString generatePortableScriptContent();

// Returns the path to the temporary portable update batch script.
QString portableScriptPath();

// Returns empty QString on success (process launched and app can exit),
// or error string if launch fails.
// When autoQuit is true and process launches successfully, qApp->quit() is called.
QString launchAndExit(const QString &packagePath, bool isPortable, qint64 currentPid, const QString &appDir, bool autoQuit = true);

} // namespace UpdateInstaller
