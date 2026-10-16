#pragma once
#include <QByteArray>
#include <QDialog>
#include <QImage>
#include <optional>

struct PreviewContent {
    QByteArray bytes;
    QImage image;
    std::optional<QString> text;
    QString imageError;
};
class PreviewDialog : public QDialog {
    Q_OBJECT
  public:
    static PreviewContent decode(QByteArray bytes);
    PreviewDialog(const QString &name, const PreviewContent &content, bool allowExternal,
                  QWidget *parent = nullptr);
};
