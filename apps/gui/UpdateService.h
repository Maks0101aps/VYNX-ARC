#pragma once

#include <QByteArray>
#include <QFuture>
#include <QList>
#include <QMap>
#include <QMetaType>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QUrl>
#include <atomic>
#include <memory>

#ifndef VYNX_VERSION
#define VYNX_VERSION "1.0.0"
#endif

struct ReleaseInfo {
    QString version;
    QString title;
    QString notes;
    QString changelog;
    QString setupUrl;
    QString setupDownloadUrl;
    QString portableUrl;
    QString portableDownloadUrl;
    QString sha256Url;
    QString sha256SumsUrl;
    bool isNewerThanCurrent = false;
    QMap<QString, QString> assetUrls;

    bool operator==(const ReleaseInfo &other) const {
        return version == other.version &&
               title == other.title &&
               notes == other.notes &&
               setupUrl == other.setupUrl &&
               portableUrl == other.portableUrl &&
               sha256Url == other.sha256Url &&
               isNewerThanCurrent == other.isNewerThanCurrent;
    }
};

Q_DECLARE_METATYPE(ReleaseInfo)

class UpdateService : public QObject {
    Q_OBJECT

public:
    explicit UpdateService(QObject *parent = nullptr);
    ~UpdateService() override;

    // Version comparison
    static bool isVersionNewer(const QString &currentVersion, const QString &candidateVersion);

    // SHA-256 integrity validation
    static bool verifySha256(const QString &filePath, const QString &expectedHash);
    static QMap<QString, QString> parseSha256Sums(const QString &sha256SumsText);

    // Release JSON parsing
    static ReleaseInfo parseReleaseJson(const QByteArray &jsonData, const QString &currentVersion = QString());

    // 24-hour rate limit helper
    static bool shouldCheckForUpdates(bool checkOnStartup, qint64 lastCheckTime, qint64 currentTime);

    // API query & downloads
    void checkForUpdates(const QString &currentVersion = QString(), const QString &apiUrl = QString());
    void checkForUpdates(bool interactive);
    void downloadAsset(const QUrl &url, const QString &destinationFilePath);
    void downloadAndVerify(const ReleaseInfo &release, bool isPortable, const QString &targetDirectory = QString());
    void cancelDownload();

    bool isCancelled() const { return m_cancelled.load(); }

signals:
    void updateAvailable(const ReleaseInfo &release);
    void noUpdateAvailable();
    void updateCheckError(const QString &error);
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void downloadComplete(const QString &filePath);
    void downloadError(const QString &error);

    // Aliases for compatibility
    void updateReady(const QString &filePath);
    void updateError(const QString &error);

private:
    void trackFuture(const QFuture<void> &future);

    std::atomic<bool> m_cancelled{false};
    bool m_interactiveCheck{false};
    mutable QMutex m_futuresMutex;
    QList<QFuture<void>> m_activeFutures;
};
