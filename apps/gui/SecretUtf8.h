#pragma once
#include <QByteArray>
#include <QString>
#include <memory>
#include <string>

// One owned byte buffer is shared by jobs; std::function copies only the pointer.
// Qt widgets/framework/backend copies are separately documented limitations.
class SecretUtf8 final {
    std::string value_;

  public:
    explicit SecretUtf8(const QByteArray &value)
        : value_(value.constData(), size_t(value.size())) {}
    SecretUtf8(const SecretUtf8 &) = delete;
    SecretUtf8 &operator=(const SecretUtf8 &) = delete;
    ~SecretUtf8() {
        volatile char *bytes = value_.data();
        for (size_t i = 0; i < value_.size(); ++i)
            bytes[i] = 0;
    }
    const std::string &bytes() const { return value_; }
    QString text() const { return QString::fromUtf8(value_.data(), qsizetype(value_.size())); }
};
inline std::shared_ptr<SecretUtf8> secret(const QString &text) {
    auto bytes = text.toUtf8();
    auto value = std::make_shared<SecretUtf8>(bytes);
    bytes.fill('\0');
    return value;
}
