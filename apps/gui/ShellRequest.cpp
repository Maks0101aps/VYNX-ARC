#include "ShellRequest.h"
#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <stdexcept>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

ShellRequest decodeShellRequest(const QByteArray &data) {
    auto fail = [] { throw std::runtime_error("Invalid or oversized Explorer request"); };
    if (data.size() < 12 || data.size() > 8 * 1024 * 1024)
        fail();
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::LittleEndian);
    quint32 magic = 0, count = 0;
    ShellRequest request;
    stream >> magic >> request.action >> count;
    if (magic != 0x52415856 || request.action < 1 || request.action > 8 || !count || count > 10000)
        fail();
    for (quint32 i = 0; i < count; ++i) {
        quint32 length = 0;
        stream >> length;
        if (!length || length > 32767 ||
            quint64(length) * 2 > quint64(data.size() - stream.device()->pos()))
            fail();
        QString path;
        path.reserve(length);
        for (quint32 j = 0; j < length; ++j) {
            quint16 code = 0;
            stream >> code;
            path.append(QChar(code));
        }
        if (path.contains(QChar(0)) || !QFileInfo(path).isAbsolute())
            fail();
        request.paths.append(path);
    }
    if (stream.status() != QDataStream::Ok || !stream.atEnd())
        fail();
    return request;
}
ShellRequest consumeShellRequest(const QString &path) {
    const QString inbox = QDir::cleanPath(qEnvironmentVariable("LOCALAPPDATA") + "/VynxArcShell");
    const QFileInfo file(path);
    static const QRegularExpression name("^\\{[0-9A-Fa-f-]{36}\\}\\.vxreq$");
    if (QDir::cleanPath(file.absolutePath()).compare(inbox, Qt::CaseInsensitive) ||
        !name.match(file.fileName()).hasMatch())
        throw std::runtime_error("Explorer request is outside the application inbox");
    const auto folder = inbox.toStdWString();
    const auto attrs = GetFileAttributesW(folder.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_REPARSE_POINT))
        throw std::runtime_error("Explorer inbox is missing or redirected");
    const auto native = QDir::toNativeSeparators(file.absoluteFilePath()).toStdWString();
    HANDLE handle = CreateFileW(native.c_str(), GENERIC_READ | DELETE, 0, nullptr, OPEN_EXISTING,
                                FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot open Explorer request");
    BY_HANDLE_FILE_INFORMATION info{};
    LARGE_INTEGER size{};
    QByteArray bytes;
    bool valid = GetFileInformationByHandle(handle, &info) &&
                 !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
                 GetFileSizeEx(handle, &size) && size.QuadPart >= 12 &&
                 size.QuadPart <= 8 * 1024 * 1024;
    if (valid) {
        bytes.resize(qsizetype(size.QuadPart));
        DWORD read = 0;
        valid = ReadFile(handle, bytes.data(), DWORD(bytes.size()), &read, nullptr) &&
                read == DWORD(bytes.size());
    }
    CloseHandle(handle); // Consumed requests cannot be replayed, including malformed ones.
    if (!valid)
        throw std::runtime_error("Invalid Explorer request file");
    return decodeShellRequest(bytes);
}
