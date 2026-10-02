#include "IconProvider.h"
#include <QApplication>
#include <QFileInfo>
#include <QHash>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>

namespace {
class VectorIcon : public QIconEngine {
    QString kind_;

  public:
    explicit VectorIcon(QString kind) : kind_(std::move(kind)) {}
    QIconEngine *clone() const override { return new VectorIcon(kind_); }
    void paint(QPainter *p, const QRect &rect, QIcon::Mode mode, QIcon::State) override {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        p->translate(rect.x(), rect.y());
        p->scale(rect.width() / 24.0, rect.height() / 24.0);
        QColor color = qApp->palette().color(
            mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active, QPalette::Text);
        p->setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->setBrush(Qt::NoBrush);
        QPainterPath path;
        if (kind_ == "back") {
            path.moveTo(14, 5);
            path.lineTo(7, 12);
            path.lineTo(14, 19);
        } else if (kind_ == "up") {
            path.moveTo(5, 11);
            path.lineTo(12, 4);
            path.lineTo(19, 11);
            path.moveTo(12, 4);
            path.lineTo(12, 20);
        } else if (kind_ == "more") {
            p->setBrush(color);
            for (int x : {5, 12, 19})
                p->drawEllipse(QPointF(x, 12), 1.5, 1.5);
        } else if (kind_ == "folder") {
            path.moveTo(3, 7);
            path.lineTo(10, 7);
            path.lineTo(12, 9);
            path.lineTo(21, 9);
            path.lineTo(21, 19);
            path.lineTo(3, 19);
            path.closeSubpath();
        } else {
            path.moveTo(5, 3);
            path.lineTo(14, 3);
            path.lineTo(19, 8);
            path.lineTo(19, 21);
            path.lineTo(5, 21);
            path.closeSubpath();
            path.moveTo(14, 3);
            path.lineTo(14, 8);
            path.lineTo(19, 8);
            if (kind_ == "text") {
                path.moveTo(8, 12);
                path.lineTo(16, 12);
                path.moveTo(8, 16);
                path.lineTo(14, 16);
            }
            if (kind_ == "archive") {
                path.moveTo(11, 4);
                path.lineTo(11, 17);
                path.lineTo(14, 17);
                path.lineTo(14, 20);
                path.lineTo(11, 20);
            }
            if (kind_ == "image") {
                path.moveTo(7, 18);
                path.lineTo(11, 13);
                path.lineTo(14, 16);
                path.lineTo(16, 14);
                path.lineTo(17, 18);
            }
            if (kind_ == "executable") {
                path.moveTo(10, 11);
                path.lineTo(15, 14);
                path.lineTo(10, 17);
                path.closeSubpath();
            }
        }
        p->drawPath(path);
        p->restore();
    }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override {
        QPixmap result(size);
        result.fill(Qt::transparent);
        QPainter p(&result);
        paint(&p, QRect(QPoint(), size), mode, state);
        return result;
    }
};
} // namespace
QIcon Icons::get(const QString &kind) {
    static QHash<QString, QIcon> cache;
    if (!cache.contains(kind))
        cache.insert(kind, QIcon(new VectorIcon(kind)));
    return cache.value(kind);
}
QIcon Icons::file(const QString &name, bool folder) {
    if (folder)
        return get("folder");
    auto ext = QFileInfo(name).suffix().toLower();
    if (QStringList{"zip", "7z", "rar", "tar", "gz"}.contains(ext))
        return get("archive");
    if (QStringList{"txt", "md", "cpp", "h", "rs", "js", "json", "xml", "py", "html", "css"}
            .contains(ext))
        return get("text");
    if (QStringList{"png", "jpg", "jpeg", "gif", "webp", "svg", "bmp"}.contains(ext))
        return get("image");
    if (QStringList{"exe", "dll", "msi", "com", "bat", "ps1"}.contains(ext))
        return get("executable");
    return get("file");
}
