#include "UpdateInstaller.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>

namespace UpdateInstaller {

QStringList installerArgs() {
    return QStringList{
        QStringLiteral("/SILENT"),
        QStringLiteral("/CLOSEAPPLICATIONS"),
        QStringLiteral("/RESTARTAPPLICATIONS")
    };
}

QString generatePortableScriptContent() {
    return QStringLiteral(
        "@echo off\r\n"
        "setlocal enabledelayedexpansion\r\n"
        ":waitloop\r\n"
        "tasklist /FI \"PID eq %1\" 2>nul | findstr /I \"%1\" >nul\r\n"
        "if not errorlevel 1 (\r\n"
        "    timeout /t 1 /nobreak >nul\r\n"
        "    goto waitloop\r\n"
        ")\r\n"
        "timeout /t 1 /nobreak >nul\r\n"
        "tar.exe -xf \"%~2\" -C \"%~3\"\r\n"
        "if errorlevel 1 (\r\n"
        "    echo Update extraction failed.\r\n"
        "    pause\r\n"
        "    exit /b 1\r\n"
        ")\r\n"
        "start \"\" \"%~3\\VynxArc.exe\"\r\n"
        "(goto) 2>nul & del \"%~f0\"\r\n"
    );
}

QString portableScriptPath() {
    return QDir(QDir::tempPath()).filePath(QStringLiteral("vynx-update-portable.bat"));
}

QString launchAndExit(const QString &packagePath, bool isPortable, qint64 currentPid, const QString &appDir, bool autoQuit) {
    if (packagePath.isEmpty() || !QFileInfo::exists(packagePath)) {
        return QObject::tr("Update package not found: %1").arg(packagePath);
    }

    if (!isPortable) {
        const QStringList args = installerArgs();
        if (!QProcess::startDetached(packagePath, args)) {
            return QObject::tr("Failed to launch installer process: %1").arg(packagePath);
        }
    } else {
        if (appDir.isEmpty() || !QDir(appDir).exists()) {
            return QObject::tr("Application directory not found: %1").arg(appDir);
        }

        const QString scriptPath = portableScriptPath();
        QFile scriptFile(scriptPath);
        if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            return QObject::tr("Failed to create portable update script: %1").arg(scriptFile.errorString());
        }

        const QString scriptContent = generatePortableScriptContent();
        scriptFile.write(scriptContent.toUtf8());
        scriptFile.close();

        QProcess process;
        process.setProgram(QStringLiteral("cmd.exe"));
        process.setArguments(QStringList{
            QStringLiteral("/c"),
            QDir::toNativeSeparators(scriptPath),
            QString::number(currentPid),
            QDir::toNativeSeparators(packagePath),
            QDir::toNativeSeparators(appDir)
        });

#if defined(Q_OS_WIN)
        process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
            args->flags |= 0x08000000; // CREATE_NO_WINDOW
        });
#endif

        if (!process.startDetached()) {
            return QObject::tr("Failed to launch portable update process.");
        }
    }

    if (autoQuit && qApp) {
        qApp->quit();
    }
    return QString();
}

} // namespace UpdateInstaller
