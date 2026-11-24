#include "UpdateService.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QtConcurrent/QtConcurrent>
#include <vector>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#endif

namespace {

struct SemVer {
    int major = 0;
    int minor = 0;
    int patch = 0;
    int build = 0;
    QString prerelease;
    bool hasPrerelease = false;

    static SemVer parse(const QString &verStr) {
        SemVer v;
        QString s = verStr.trimmed();
        if (s.startsWith('v', Qt::CaseInsensitive)) {
            s.remove(0, 1);
        }
        int plusIdx = s.indexOf('+');
        if (plusIdx >= 0) {
            s = s.left(plusIdx);
        }
        int dashIdx = s.indexOf('-');
        if (dashIdx >= 0) {
            v.prerelease = s.mid(dashIdx + 1).trimmed();
            v.hasPrerelease = !v.prerelease.isEmpty();
            s = s.left(dashIdx);
        }
        const auto parts = s.split('.', Qt::SkipEmptyParts);
        if (!parts.isEmpty()) v.major = parts.value(0).toInt();
        if (parts.size() > 1) v.minor = parts.value(1).toInt();
        if (parts.size() > 2) v.patch = parts.value(2).toInt();
        if (parts.size() > 3) v.build = parts.value(3).toInt();
        return v;
    }

    int compare(const SemVer &other) const {
        if (major != other.major) return major < other.major ? -1 : 1;
        if (minor != other.minor) return minor < other.minor ? -1 : 1;
        if (patch != other.patch) return patch < other.patch ? -1 : 1;
        if (build != other.build) return build < other.build ? -1 : 1;

        if (hasPrerelease != other.hasPrerelease) {
            return hasPrerelease ? -1 : 1;
        }
        if (hasPrerelease && other.hasPrerelease) {
            return comparePrerelease(prerelease, other.prerelease);
        }
        return 0;
    }

    static int comparePrerelease(const QString &a, const QString &b) {
        const auto aParts = a.split('.', Qt::SkipEmptyParts);
        const auto bParts = b.split('.', Qt::SkipEmptyParts);
        int len = std::min(aParts.size(), bParts.size());
        for (int i = 0; i < len; ++i) {
            const QString &ap = aParts[i];
            const QString &bp = bParts[i];
            bool aIsNum = false, bIsNum = false;
            int aNum = ap.toInt(&aIsNum);
            int bNum = bp.toInt(&bIsNum);
            if (aIsNum && bIsNum) {
                if (aNum != bNum) return aNum < bNum ? -1 : 1;
            } else if (aIsNum) {
                return -1;
            } else if (bIsNum) {
                return 1;
            } else {
                int cmp = ap.compare(bp, Qt::CaseInsensitive);
                if (cmp != 0) return cmp < 0 ? -1 : 1;
            }
        }
        if (aParts.size() != bParts.size()) {
            return aParts.size() < bParts.size() ? -1 : 1;
        }
        return 0;
    }
};

#if defined(Q_OS_WIN)
bool executeWinHttpRequest(
    const QUrl &initialUrl,
    const QString &destinationFilePath,
    QByteArray *outBody,
    int *outStatusCode,
    QString *outError,
    const std::function<void(qint64, qint64)> &progressCallback,
    const std::atomic<bool> *cancelFlag)
{
    HINTERNET hSession = WinHttpOpen(
        L"VYNX-ARC-Updater/1.0.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!hSession) {
        hSession = WinHttpOpen(
            L"VYNX-ARC-Updater/1.0.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0);
    }
    if (!hSession) {
        if (outError) *outError = QStringLiteral("Failed to initialize WinHTTP session (code %1)").arg(GetLastError());
        return false;
    }

    WinHttpSetTimeouts(hSession, 10000, 10000, 15000, 30000);

    QUrl currentUrl = initialUrl;
    int redirectCount = 0;
    const int maxRedirects = 10;

    while (redirectCount < maxRedirects) {
        if (cancelFlag && cancelFlag->load()) {
            if (outError) *outError = QStringLiteral("Download cancelled");
            WinHttpCloseHandle(hSession);
            return false;
        }

        bool isHttps = (currentUrl.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0);
        if (!isHttps) {
            if (outError) *outError = QStringLiteral("Insecure HTTP connections are not permitted.");
            WinHttpCloseHandle(hSession);
            return false;
        }

        QString host = currentUrl.host();
        int port = currentUrl.port(INTERNET_DEFAULT_HTTPS_PORT);

        HINTERNET hConnect = WinHttpConnect(
            hSession,
            reinterpret_cast<LPCWSTR>(host.utf16()),
            static_cast<INTERNET_PORT>(port),
            0);
        if (!hConnect) {
            if (outError) *outError = QStringLiteral("WinHttpConnect failed: %1").arg(GetLastError());
            WinHttpCloseHandle(hSession);
            return false;
        }

        QString pathAndQuery = currentUrl.path();
        if (pathAndQuery.isEmpty()) pathAndQuery = QStringLiteral("/");
        if (currentUrl.hasQuery()) pathAndQuery += QStringLiteral("?") + currentUrl.query();

        DWORD openFlags = isHttps ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET hRequest = WinHttpOpenRequest(
            hConnect,
            L"GET",
            reinterpret_cast<LPCWSTR>(pathAndQuery.utf16()),
            NULL,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            openFlags);

        if (!hRequest) {
            if (outError) *outError = QStringLiteral("WinHttpOpenRequest failed: %1").arg(GetLastError());
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
        }

        DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

        LPCWSTR headers = L"Accept: application/vnd.github.v3+json, application/octet-stream, */*\r\n";
        BOOL sent = WinHttpSendRequest(hRequest, headers, static_cast<DWORD>(-1L), NULL, 0, 0, 0);
        if (!sent) {
            DWORD err = GetLastError();
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            if (outError) *outError = QStringLiteral("WinHttpSendRequest failed: %1").arg(err);
            return false;
        }

        if (!WinHttpReceiveResponse(hRequest, NULL)) {
            DWORD err = GetLastError();
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            if (outError) *outError = QStringLiteral("WinHttpReceiveResponse failed: %1").arg(err);
            return false;
        }

        DWORD statusCode = 0;
        DWORD scSize = sizeof(statusCode);
        WinHttpQueryHeaders(
            hRequest,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &statusCode,
            &scSize,
            WINHTTP_NO_HEADER_INDEX);

        if (outStatusCode) *outStatusCode = static_cast<int>(statusCode);

        if (statusCode == 301 || statusCode == 302 || statusCode == 303 || statusCode == 307 || statusCode == 308) {
            DWORD locSize = 0;
            WinHttpQueryHeaders(
                hRequest,
                WINHTTP_QUERY_LOCATION,
                WINHTTP_HEADER_NAME_BY_INDEX,
                WINHTTP_NO_OUTPUT_BUFFER,
                &locSize,
                WINHTTP_NO_HEADER_INDEX);

            if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && locSize > 0) {
                std::vector<wchar_t> locBuf(locSize / sizeof(wchar_t) + 1, 0);
                if (WinHttpQueryHeaders(
                        hRequest,
                        WINHTTP_QUERY_LOCATION,
                        WINHTTP_HEADER_NAME_BY_INDEX,
                        locBuf.data(),
                        &locSize,
                        WINHTTP_NO_HEADER_INDEX)) {
                    QString newUrlStr = QString::fromWCharArray(locBuf.data());
                    currentUrl = currentUrl.resolved(QUrl(newUrlStr));
                    WinHttpCloseHandle(hRequest);
                    WinHttpCloseHandle(hConnect);
                    redirectCount++;
                    continue;
                }
            }
        }

        if (statusCode != 200) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            if (outError) {
                if (statusCode == 403) {
                    *outError = QStringLiteral("GitHub API rate limit exceeded or access forbidden (HTTP 403)");
                } else if (statusCode == 404) {
                    *outError = QStringLiteral("Release or asset not found (HTTP 404)");
                } else {
                    *outError = QStringLiteral("HTTP request failed with status %1").arg(statusCode);
                }
            }
            return false;
        }

        DWORD contentLength = 0;
        DWORD clSize = sizeof(contentLength);
        qint64 totalBytes = -1;
        if (WinHttpQueryHeaders(
                hRequest,
                WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX,
                &contentLength,
                &clSize,
                WINHTTP_NO_HEADER_INDEX)) {
            totalBytes = static_cast<qint64>(contentLength);
        }

        std::unique_ptr<QFile> destFile;
        if (!destinationFilePath.isEmpty()) {
            destFile = std::make_unique<QFile>(destinationFilePath);
            if (!destFile->open(QIODevice::WriteOnly)) {
                if (outError) *outError = QStringLiteral("Failed to open file for writing: %1").arg(destinationFilePath);
                WinHttpCloseHandle(hRequest);
                WinHttpCloseHandle(hConnect);
                WinHttpCloseHandle(hSession);
                return false;
            }
        }

        qint64 bytesReceived = 0;
        if (progressCallback) {
            progressCallback(0, totalBytes);
        }

        const DWORD bufSize = 65536;
        std::vector<char> readBuffer(bufSize);
        bool readSuccess = true;

        while (true) {
            if (cancelFlag && cancelFlag->load()) {
                readSuccess = false;
                if (outError) *outError = QStringLiteral("Download cancelled");
                break;
            }

            DWORD bytesAvailable = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &bytesAvailable)) {
                readSuccess = false;
                if (outError) *outError = QStringLiteral("WinHttpQueryDataAvailable failed: %1").arg(GetLastError());
                break;
            }

            if (bytesAvailable == 0) {
                break;
            }

            DWORD toRead = std::min<DWORD>(bytesAvailable, bufSize);
            DWORD bytesRead = 0;
            if (!WinHttpReadData(hRequest, readBuffer.data(), toRead, &bytesRead)) {
                readSuccess = false;
                if (outError) *outError = QStringLiteral("WinHttpReadData failed: %1").arg(GetLastError());
                break;
            }

            if (bytesRead == 0) {
                break;
            }

            if (destFile) {
                if (destFile->write(readBuffer.data(), static_cast<qint64>(bytesRead)) != static_cast<qint64>(bytesRead)) {
                    readSuccess = false;
                    if (outError) *outError = QStringLiteral("Failed to write data to destination file");
                    break;
                }
            } else if (outBody) {
                outBody->append(readBuffer.data(), static_cast<int>(bytesRead));
            }

            bytesReceived += bytesRead;
            if (progressCallback) {
                progressCallback(bytesReceived, totalBytes);
            }
        }

        if (destFile) {
            destFile->flush();
            destFile->close();
            if (!readSuccess) {
                destFile->remove();
            }
        }

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return readSuccess;
    }

    WinHttpCloseHandle(hSession);
    if (outError) *outError = QStringLiteral("Too many redirects");
    return false;
}
#else
bool executeWinHttpRequest(
    const QUrl &,
    const QString &,
    QByteArray *,
    int *outStatusCode,
    QString *outError,
    const std::function<void(qint64, qint64)> &,
    const std::atomic<bool> *)
{
    if (outStatusCode) *outStatusCode = 501;
    if (outError) *outError = QStringLiteral("WinHTTP is only supported on Windows");
    return false;
}
#endif

} // namespace

UpdateService::UpdateService(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<ReleaseInfo>("ReleaseInfo");
}

UpdateService::~UpdateService() {
    m_cancelled.store(true);
    QList<QFuture<void>> futures;
    {
        QMutexLocker locker(&m_futuresMutex);
        futures = m_activeFutures;
    }
    for (auto &f : futures) {
        f.waitForFinished();
    }
}

void UpdateService::trackFuture(const QFuture<void> &future) {
    QMutexLocker locker(&m_futuresMutex);
    m_activeFutures.removeIf([](const QFuture<void> &f) { return f.isFinished(); });
    m_activeFutures.append(future);
}

bool UpdateService::shouldCheckForUpdates(bool checkOnStartup, qint64 lastCheckTime, qint64 currentTime) {
    if (!checkOnStartup) {
        return false;
    }
    if (lastCheckTime <= 0) {
        return true;
    }
    return (currentTime - lastCheckTime) >= 86400;
}

bool UpdateService::isVersionNewer(const QString &currentVersion, const QString &candidateVersion) {
    const auto cur = SemVer::parse(currentVersion);
    const auto cand = SemVer::parse(candidateVersion);
    return cand.compare(cur) > 0;
}

bool UpdateService::verifySha256(const QString &filePath, const QString &expectedHash) {
    if (filePath.isEmpty() || expectedHash.trimmed().isEmpty()) {
        return false;
    }
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    QCryptographicHash hasher(QCryptographicHash::Sha256);
    if (!hasher.addData(&file)) {
        return false;
    }
    const QString actualHex = QString::fromLatin1(hasher.result().toHex());
    return actualHex.compare(expectedHash.trimmed(), Qt::CaseInsensitive) == 0;
}

QMap<QString, QString> UpdateService::parseSha256Sums(const QString &sha256SumsText) {
    QMap<QString, QString> result;
    const auto lines = sha256SumsText.split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
    const QRegularExpression rx(QStringLiteral("^([a-fA-F0-9]{64})\\s+\\*?(.*)$"));

    for (const auto &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith('#')) {
            continue;
        }
        auto match = rx.match(line);
        if (match.hasMatch()) {
            const QString hash = match.captured(1).toLower();
            QString filename = match.captured(2).trimmed();
            if (filename.startsWith(QStringLiteral("./")) || filename.startsWith(QStringLiteral(".\\"))) {
                filename.remove(0, 2);
            }
            if (!filename.isEmpty()) {
                result.insert(filename, hash);
            }
        }
    }
    return result;
}

ReleaseInfo UpdateService::parseReleaseJson(const QByteArray &jsonData, const QString &currentVersion) {
    ReleaseInfo info;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonData, &err);
    if (doc.isNull() || !doc.isObject()) {
        return info;
    }

    const QJsonObject obj = doc.object();
    info.version = obj.value(QStringLiteral("tag_name")).toString();
    info.title = obj.value(QStringLiteral("name")).toString();
    info.notes = obj.value(QStringLiteral("body")).toString();
    info.changelog = info.notes;

    if (!currentVersion.isEmpty()) {
        info.isNewerThanCurrent = isVersionNewer(currentVersion, info.version);
    }

    const QJsonArray assets = obj.value(QStringLiteral("assets")).toArray();
    for (const auto &val : assets) {
        if (!val.isObject()) continue;
        const QJsonObject assetObj = val.toObject();
        const QString name = assetObj.value(QStringLiteral("name")).toString();
        const QString downloadUrl = assetObj.value(QStringLiteral("browser_download_url")).toString();
        if (name.isEmpty() || downloadUrl.isEmpty()) continue;

        info.assetUrls.insert(name, downloadUrl);

        if (name.compare(QStringLiteral("VYNX-ARC-Setup-x64.exe"), Qt::CaseInsensitive) == 0 ||
            (name.contains(QStringLiteral("Setup"), Qt::CaseInsensitive) && name.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive))) {
            info.setupUrl = downloadUrl;
            info.setupDownloadUrl = downloadUrl;
        } else if (name.compare(QStringLiteral("VYNX-ARC-Portable-x64.zip"), Qt::CaseInsensitive) == 0 ||
                   (name.contains(QStringLiteral("Portable"), Qt::CaseInsensitive) && name.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive))) {
            info.portableUrl = downloadUrl;
            info.portableDownloadUrl = downloadUrl;
        } else if (name.compare(QStringLiteral("SHA256SUMS.txt"), Qt::CaseInsensitive) == 0) {
            info.sha256Url = downloadUrl;
            info.sha256SumsUrl = downloadUrl;
        }
    }

    return info;
}

void UpdateService::checkForUpdates(bool interactive) {
    m_interactiveCheck = interactive;
    checkForUpdates(QString(), QString());
}

void UpdateService::checkForUpdates(const QString &currentVersion, const QString &apiUrl) {
    m_cancelled.store(false);
    const QString ver = currentVersion.isEmpty() ? QStringLiteral(VYNX_VERSION) : currentVersion;
    const QString endpoint = apiUrl.isEmpty() ?
        QStringLiteral("https://api.github.com/repos/Maks0101aps/VYNX-ARC/releases/latest") : apiUrl;

    const QUrl checkUrl(endpoint);
    if (checkUrl.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0) {
        emit updateCheckError(tr("Insecure HTTP connections are not permitted."));
        return;
    }

    auto future = QtConcurrent::run([this, ver, endpoint]() {
        QByteArray body;
        int statusCode = 0;
        QString error;
        bool ok = executeWinHttpRequest(QUrl(endpoint), QString(), &body, &statusCode, &error, nullptr, &m_cancelled);
        if (!ok) {
            emit updateCheckError(error.isEmpty() ? tr("Failed to fetch update information") : error);
            return;
        }

        ReleaseInfo info = parseReleaseJson(body, ver);
        if (info.version.isEmpty()) {
            emit updateCheckError(tr("Invalid release response from GitHub"));
            return;
        }

        if (info.isNewerThanCurrent) {
            emit updateAvailable(info);
        } else {
            emit noUpdateAvailable();
        }
    });
    trackFuture(future);
}

void UpdateService::downloadAsset(const QUrl &url, const QString &destinationFilePath) {
    m_cancelled.store(false);
    if (url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0) {
        const QString err = tr("Insecure HTTP connections are not permitted.");
        emit downloadError(err);
        emit updateError(err);
        return;
    }

    auto future = QtConcurrent::run([this, url, destinationFilePath]() {
        int statusCode = 0;
        QString error;
        auto progressCb = [this](qint64 received, qint64 total) {
            emit downloadProgress(received, total);
        };
        bool ok = executeWinHttpRequest(url, destinationFilePath, nullptr, &statusCode, &error, progressCb, &m_cancelled);
        if (ok) {
            emit downloadComplete(destinationFilePath);
            emit updateReady(destinationFilePath);
        } else {
            const QString err = error.isEmpty() ? tr("Asset download failed") : error;
            emit downloadError(err);
            emit updateError(err);
        }
    });
    trackFuture(future);
}

void UpdateService::downloadAndVerify(const ReleaseInfo &release, bool isPortable, const QString &targetDirectory) {
    m_cancelled.store(false);
    const QString dir = targetDirectory.isEmpty() ? QDir::tempPath() : targetDirectory;
    const QString assetUrlStr = isPortable ? release.portableUrl : release.setupUrl;
    const QString filename = isPortable ? QStringLiteral("VYNX-ARC-Portable-x64.zip") : QStringLiteral("VYNX-ARC-Setup-x64.exe");
    const QString destFilePath = QDir(dir).filePath(filename);

    const QString failClosedError = tr("Cryptographic release integrity verification failed: checksum missing or invalid.");

    if (assetUrlStr.isEmpty()) {
        const QString err = QStringLiteral("Download URL not found in release assets for %1").arg(filename);
        emit downloadError(err);
        emit updateError(err);
        return;
    }

    const QUrl assetUrl(assetUrlStr);
    if (assetUrl.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0) {
        QFile::remove(destFilePath);
        const QString err = tr("Insecure HTTP connections are not permitted.");
        emit downloadError(err);
        emit updateError(err);
        return;
    }

    const QString sha256UrlStr = release.sha256Url;
    if (sha256UrlStr.isEmpty()) {
        QFile::remove(destFilePath);
        emit downloadError(failClosedError);
        emit updateError(failClosedError);
        return;
    }

    const QUrl sha256Url(sha256UrlStr);
    if (sha256Url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0) {
        QFile::remove(destFilePath);
        const QString err = tr("Insecure HTTP connections are not permitted.");
        emit downloadError(err);
        emit updateError(err);
        return;
    }

    auto future = QtConcurrent::run([this, assetUrlStr, sha256UrlStr, destFilePath, filename, failClosedError]() {
        // Step 1: Download SHA256SUMS.txt - must succeed and contain expected hash
        QByteArray shaBody;
        int sc = 0;
        QString err;
        if (!executeWinHttpRequest(QUrl(sha256UrlStr), QString(), &shaBody, &sc, &err, nullptr, &m_cancelled)) {
            QFile::remove(destFilePath);
            emit downloadError(failClosedError);
            emit updateError(failClosedError);
            return;
        }

        auto sums = parseSha256Sums(QString::fromUtf8(shaBody));
        const QString expectedHash = sums.value(filename);
        if (expectedHash.isEmpty()) {
            QFile::remove(destFilePath);
            emit downloadError(failClosedError);
            emit updateError(failClosedError);
            return;
        }

        // Step 2: Download asset
        int statusCode = 0;
        QString error;
        auto progressCb = [this](qint64 received, qint64 total) {
            emit downloadProgress(received, total);
        };
        bool ok = executeWinHttpRequest(QUrl(assetUrlStr), destFilePath, nullptr, &statusCode, &error, progressCb, &m_cancelled);
        if (!ok) {
            QFile::remove(destFilePath);
            const QString downloadErr = error.isEmpty() ? tr("Asset download failed") : error;
            emit downloadError(downloadErr);
            emit updateError(downloadErr);
            return;
        }

        // Step 3: Verify SHA-256 integrity
        if (!verifySha256(destFilePath, expectedHash)) {
            QFile::remove(destFilePath);
            emit downloadError(failClosedError);
            emit updateError(failClosedError);
            return;
        }

        emit downloadComplete(destFilePath);
        emit updateReady(destFilePath);
    });
    trackFuture(future);
}

void UpdateService::cancelDownload() {
    m_cancelled.store(true);
}
