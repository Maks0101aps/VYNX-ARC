#include "IconProvider.h"
#include <QApplication>
#include <QFileInfo>
#include <QHash>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QRectF>

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

        const bool isDark = qApp->palette().color(QPalette::Window).lightness() < 128;
        const bool disabled = (mode == QIcon::Disabled);

        QColor textColor = qApp->palette().color(
            disabled ? QPalette::Disabled : QPalette::Active, QPalette::Text);
        QColor mutedColor = qApp->palette().color(
            disabled ? QPalette::Disabled : QPalette::Active, QPalette::PlaceholderText);

        QColor accent, accentSecondary;
        if (disabled) {
            accent = mutedColor;
            accentSecondary = mutedColor;
        } else {
            if (kind_ == "folder") {
                accent = isDark ? QColor("#fbbf24") : QColor("#d97706");
                accentSecondary = isDark ? QColor("#f59e0b") : QColor("#b45309");
            } else if (kind_ == "folder_up") {
                accent = isDark ? QColor("#fbbf24") : QColor("#d97706");
                accentSecondary = isDark ? QColor("#60a5fa") : QColor("#2563eb");
            } else if (kind_ == "archive" || kind_ == "tb_extract") {
                accent = isDark ? QColor("#60a5fa") : QColor("#2563eb");
                accentSecondary = isDark ? QColor("#93c5fd") : QColor("#1d4ed8");
            } else if (kind_ == "tb_add") {
                accent = isDark ? QColor("#60a5fa") : QColor("#2563eb");
                accentSecondary = isDark ? QColor("#34d399") : QColor("#059669");
            } else if (kind_ == "tb_test") {
                accent = isDark ? QColor("#34d399") : QColor("#059669");
                accentSecondary = isDark ? QColor("#6ee7b7") : QColor("#047857");
            } else if (kind_ == "tb_repair") {
                accent = isDark ? QColor("#34d399") : QColor("#059669");
                accentSecondary = isDark ? QColor("#f87171") : QColor("#dc2626");
            } else if (kind_ == "tb_delete") {
                accent = isDark ? QColor("#f87171") : QColor("#dc2626");
                accentSecondary = isDark ? QColor("#fca5a5") : QColor("#b91c1c");
            } else if (kind_ == "tb_view" || kind_ == "tb_find") {
                accent = isDark ? QColor("#38bdf8") : QColor("#0284c7");
                accentSecondary = isDark ? QColor("#7dd3fc") : QColor("#0369a1");
            } else if (kind_ == "tb_rename" || kind_ == "tb_wizard" || kind_ == "executable" || kind_ == "exe") {
                accent = isDark ? QColor("#a78bfa") : QColor("#7c3aed");
                accentSecondary = isDark ? QColor("#c4b5fd") : QColor("#6d28d9");
            } else if (kind_ == "tb_info") {
                accent = isDark ? QColor("#818cf8") : QColor("#4f46e5");
                accentSecondary = isDark ? QColor("#a5b4fc") : QColor("#4338ca");
            } else if (kind_ == "lock") {
                accent = isDark ? QColor("#fbbf24") : QColor("#d97706");
                accentSecondary = isDark ? QColor("#fef3c7") : QColor("#92400e");
            } else if (kind_ == "code") {
                accent = isDark ? QColor("#34d399") : QColor("#059669");
                accentSecondary = isDark ? QColor("#6ee7b7") : QColor("#047857");
            } else if (kind_ == "audio") {
                accent = isDark ? QColor("#f472b6") : QColor("#db2777");
                accentSecondary = isDark ? QColor("#fbcfe8") : QColor("#be185d");
            } else if (kind_ == "video") {
                accent = isDark ? QColor("#fb7185") : QColor("#e11d48");
                accentSecondary = isDark ? QColor("#fecdd3") : QColor("#be123c");
            } else if (kind_ == "pdf") {
                accent = isDark ? QColor("#f87171") : QColor("#dc2626");
                accentSecondary = isDark ? QColor("#fca5a5") : QColor("#b91c1c");
            } else if (kind_ == "doc") {
                accent = isDark ? QColor("#60a5fa") : QColor("#2563eb");
                accentSecondary = isDark ? QColor("#93c5fd") : QColor("#1d4ed8");
            } else if (kind_ == "iso") {
                accent = isDark ? QColor("#2dd4bf") : QColor("#0d9488");
                accentSecondary = isDark ? QColor("#99f6e4") : QColor("#0f766e");
            } else if (kind_ == "wim") {
                accent = isDark ? QColor("#fb923c") : QColor("#ea580c");
                accentSecondary = isDark ? QColor("#fed7aa") : QColor("#c2410c");
            } else if (kind_ == "text" || kind_ == "txt" || kind_ == "image") {
                accent = isDark ? QColor("#38bdf8") : QColor("#0284c7");
                accentSecondary = isDark ? QColor("#7dd3fc") : QColor("#0369a1");
            } else {
                accent = textColor;
                accentSecondary = mutedColor;
            }
        }

        QColor washColor = accent;
        washColor.setAlpha(disabled ? 0 : (isDark ? 42 : 30));

        if (kind_ == "back") {
            p->setPen(QPen(accent, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->setBrush(Qt::NoBrush);
            QPainterPath path;
            path.moveTo(15, 6);
            path.lineTo(8, 12);
            path.lineTo(15, 18);
            p->drawPath(path);
        } else if (kind_ == "up") {
            p->setPen(QPen(accent, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->setBrush(Qt::NoBrush);
            QPainterPath path;
            path.moveTo(5, 11);
            path.lineTo(12, 4);
            path.lineTo(19, 11);
            path.moveTo(12, 4);
            path.lineTo(12, 20);
            p->drawPath(path);
        } else if (kind_ == "more") {
            p->setPen(Qt::NoPen);
            p->setBrush(accent);
            for (int x : {5, 12, 19})
                p->drawEllipse(QPointF(x, 12), 1.6, 1.6);
        } else if (kind_ == "folder") {
            QPainterPath path;
            path.moveTo(3, 7);
            path.lineTo(9, 7);
            path.lineTo(11, 9);
            path.lineTo(21, 9);
            path.lineTo(21, 19);
            path.lineTo(3, 19);
            path.closeSubpath();
            p->setPen(QPen(accent, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->setBrush(washColor);
            p->drawPath(path);
            p->setPen(QPen(accentSecondary, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->drawLine(QPointF(3, 11), QPointF(21, 11));
        } else if (kind_ == "folder_up") {
            QPainterPath path;
            path.moveTo(3, 7);
            path.lineTo(9, 7);
            path.lineTo(11, 9);
            path.lineTo(21, 9);
            path.lineTo(21, 19);
            path.lineTo(3, 19);
            path.closeSubpath();
            p->setPen(QPen(accent, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->setBrush(washColor);
            p->drawPath(path);
            QPainterPath arrow;
            arrow.moveTo(12, 17);
            arrow.lineTo(12, 10.5);
            arrow.moveTo(9.5, 13);
            arrow.lineTo(12, 10.5);
            arrow.lineTo(14.5, 13);
            p->setPen(QPen(accentSecondary, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->setBrush(Qt::NoBrush);
            p->drawPath(arrow);
        } else if (kind_ == "tb_add") {
            p->setPen(QPen(accent, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->setBrush(washColor);
            p->drawRoundedRect(QRectF(3, 4, 11, 16), 1.5, 1.5);
            p->drawLine(QPointF(6.5, 4), QPointF(6.5, 20));
            p->drawLine(QPointF(3, 8.5), QPointF(6.5, 8.5));
            p->drawLine(QPointF(3, 15.5), QPointF(6.5, 15.5));
            p->setPen(Qt::NoPen);
            p->setBrush(accentSecondary);
            p->drawEllipse(QPointF(17, 15), 4.5, 4.5);
            p->setPen(QPen(Qt::white, 1.8, Qt::SolidLine, Qt::RoundCap));
            p->drawLine(QPointF(17, 12.5), QPointF(17, 17.5));
            p->drawLine(QPointF(14.5, 15), QPointF(19.5, 15));
        } else if (kind_ == "tb_extract") {
            QPainterPath folderPath;
            folderPath.moveTo(3, 8);
            folderPath.lineTo(8, 8);
            folderPath.lineTo(10, 10);
            folderPath.lineTo(21, 10);
            folderPath.lineTo(21, 20);
            folderPath.lineTo(3, 20);
            folderPath.closeSubpath();
            p->setPen(QPen(accent, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->setBrush(washColor);
            p->drawPath(folderPath);
            QPainterPath arrow;
            arrow.moveTo(12, 17);
            arrow.lineTo(12, 4);
            arrow.moveTo(8.5, 7.5);
            arrow.lineTo(12, 4);
            arrow.lineTo(15.5, 7.5);
            p->setPen(QPen(accentSecondary, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->setBrush(Qt::NoBrush);
            p->drawPath(arrow);
        } else if (kind_ == "tb_test") {
            QPainterPath board;
            board.moveTo(5, 5);
            board.lineTo(8, 5);
            board.moveTo(16, 5);
            board.lineTo(19, 5);
            board.lineTo(19, 21);
            board.lineTo(5, 21);
            board.closeSubpath();
            p->setPen(QPen(textColor, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->setBrush(washColor);
            p->drawPath(board);
            p->drawRoundedRect(QRectF(8, 2.5, 8, 3.5), 1.0, 1.0);
            QPainterPath check;
            check.moveTo(7.5, 13.5);
            check.lineTo(10.5, 16.5);
            check.lineTo(16.5, 9.5);
            p->setPen(QPen(accent, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->setBrush(Qt::NoBrush);
            p->drawPath(check);
        } else if (kind_ == "tb_view") {
            QPainterPath doc;
            doc.moveTo(4, 3);
            doc.lineTo(14, 3);
            doc.lineTo(19, 8);
            doc.lineTo(19, 21);
            doc.lineTo(4, 21);
            doc.closeSubpath();
            doc.moveTo(14, 3);
            doc.lineTo(14, 8);
            doc.lineTo(19, 8);
            p->setPen(QPen(textColor, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->setBrush(washColor);
            p->drawPath(doc);
            QPainterPath eye;
            eye.moveTo(6.5, 15);
            eye.quadTo(11.5, 11, 16.5, 15);
            eye.quadTo(11.5, 19, 6.5, 15);
            p->setPen(QPen(accent, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->setBrush(washColor);
            p->drawPath(eye);
            p->setPen(Qt::NoPen);
            p->setBrush(accent);
            p->drawEllipse(QPointF(11.5, 15), 1.6, 1.6);
        } else if (kind_ == "tb_delete") {
            p->setPen(QPen(textColor, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->drawLine(QPointF(4, 6), QPointF(20, 6));
            p->drawLine(QPointF(9, 6), QPointF(9, 4));
            p->drawLine(QPointF(9, 4), QPointF(15, 4));
            p->drawLine(QPointF(15, 4), QPointF(15, 6));
            QPainterPath bin;
            bin.moveTo(5.5, 6);
            bin.lineTo(7, 20);
            bin.lineTo(17, 20);
            bin.lineTo(18.5, 6);
            bin.closeSubpath();
            p->setPen(QPen(accent, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->setBrush(washColor);
            p->drawPath(bin);
            p->drawLine(QPointF(10, 10), QPointF(10, 16));
            p->drawLine(QPointF(14, 10), QPointF(14, 16));
        } else if (kind_ == "tb_rename") {
            QPainterPath doc;
            doc.moveTo(4, 3);
            doc.lineTo(14, 3);
            doc.lineTo(19, 8);
            doc.lineTo(19, 12);
            doc.moveTo(4, 3);
            doc.lineTo(4, 21);
            doc.lineTo(10, 21);
            doc.moveTo(14, 3);
            doc.lineTo(14, 8);
            doc.lineTo(19, 8);
            p->setPen(QPen(textColor, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->setBrush(washColor);
            p->drawPath(doc);
            QPainterPath pencil;
            pencil.moveTo(13, 20.5);
            pencil.lineTo(11, 21);
            pencil.lineTo(11.5, 19);
            pencil.lineTo(17.5, 13);
            pencil.lineTo(19.5, 15);
            pencil.closeSubpath();
            p->setPen(QPen(accent, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->setBrush(accentSecondary);
            p->drawPath(pencil);
        } else if (kind_ == "tb_find") {
            p->setPen(QPen(accent, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->setBrush(washColor);
            p->drawEllipse(QPointF(10.5, 10.5), 5.5, 5.5);
            p->setPen(QPen(accent, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->drawLine(QPointF(14.5, 14.5), QPointF(20, 20));
            p->setPen(QPen(accentSecondary, 1.2, Qt::SolidLine, Qt::RoundCap));
            QPainterPath glint;
            glint.moveTo(7.5, 9.5);
            glint.quadTo(8.5, 7.5, 10.5, 7.5);
            p->drawPath(glint);
        } else if (kind_ == "tb_wizard") {
            p->setPen(QPen(accent, 2.2, Qt::SolidLine, Qt::RoundCap));
            p->drawLine(QPointF(4, 20), QPointF(15, 9));
            p->setPen(QPen(accentSecondary, 1.6, Qt::SolidLine, Qt::RoundCap));
            p->drawLine(QPointF(18, 2), QPointF(18, 6));
            p->drawLine(QPointF(16, 4), QPointF(20, 4));
            p->drawLine(QPointF(9.5, 3), QPointF(9.5, 6));
            p->drawLine(QPointF(8, 4.5), QPointF(11, 4.5));
            p->drawLine(QPointF(20, 9), QPointF(20, 12));
            p->drawLine(QPointF(18.5, 10.5), QPointF(21.5, 10.5));
        } else if (kind_ == "tb_info") {
            p->setPen(QPen(accent, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->setBrush(washColor);
            p->drawEllipse(QPointF(12, 12), 8.5, 8.5);
            p->setPen(Qt::NoPen);
            p->setBrush(accent);
            p->drawEllipse(QPointF(12, 7.5), 1.2, 1.2);
            p->setPen(QPen(accent, 1.8, Qt::SolidLine, Qt::RoundCap));
            p->drawLine(QPointF(12, 11), QPointF(12, 16.5));
            p->drawLine(QPointF(10.5, 11.5), QPointF(12, 11));
            p->drawLine(QPointF(10.5, 16.5), QPointF(13.5, 16.5));
        } else if (kind_ == "tb_repair") {
            p->setPen(QPen(textColor, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->setBrush(washColor);
            p->drawRoundedRect(QRectF(3.5, 6, 17, 14), 2.0, 2.0);
            p->drawLine(QPointF(9, 6), QPointF(9, 4));
            p->drawLine(QPointF(9, 4), QPointF(15, 4));
            p->drawLine(QPointF(15, 4), QPointF(15, 6));
            p->setPen(QPen(accent, 2.2, Qt::SolidLine, Qt::RoundCap));
            p->drawLine(QPointF(12, 9.5), QPointF(12, 16.5));
            p->drawLine(QPointF(8.5, 13), QPointF(15.5, 13));
        } else if (kind_ == "lock") {
            p->setPen(QPen(accent, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->setBrush(washColor);
            p->drawRoundedRect(QRectF(5, 10.5, 14, 10.5), 2.0, 2.0);
            QPainterPath shackle;
            shackle.moveTo(8, 10.5);
            shackle.lineTo(8, 6.5);
            shackle.arcTo(QRectF(8, 3.5, 8, 6), 180, -180);
            shackle.lineTo(16, 10.5);
            p->setBrush(Qt::NoBrush);
            p->drawPath(shackle);
            p->setPen(Qt::NoPen);
            p->setBrush(accent);
            p->drawEllipse(QPointF(12, 14.5), 1.4, 1.4);
            p->setPen(QPen(accent, 1.5, Qt::SolidLine, Qt::RoundCap));
            p->drawLine(QPointF(12, 14.5), QPointF(12, 17.5));
        } else if (kind_ == "iso") {
            p->setPen(QPen(accent, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->setBrush(washColor);
            p->drawEllipse(QPointF(12, 12), 8.5, 8.5);
            p->drawEllipse(QPointF(12, 12), 2.8, 2.8);
            p->setPen(Qt::NoPen);
            p->setBrush(accent);
            p->drawEllipse(QPointF(12, 12), 1.0, 1.0);
            p->setPen(QPen(accentSecondary, 1.6, Qt::SolidLine, Qt::RoundCap));
            p->drawLine(QPointF(12, 3.5), QPointF(12, 5.5));
            p->drawLine(QPointF(12, 18.5), QPointF(12, 20.5));
            p->drawLine(QPointF(3.5, 12), QPointF(5.5, 12));
            p->drawLine(QPointF(18.5, 12), QPointF(20.5, 12));
        } else if (kind_ == "wim") {
            p->setPen(QPen(accent, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->setBrush(washColor);
            p->drawRoundedRect(QRectF(4, 4, 16, 16), 2.0, 2.0);
            p->setPen(QPen(accentSecondary, 1.5, Qt::SolidLine, Qt::RoundCap));
            p->drawLine(QPointF(4, 10), QPointF(20, 10));
            p->drawLine(QPointF(10, 4), QPointF(10, 20));
            p->setPen(Qt::NoPen);
            p->setBrush(accent);
            p->drawEllipse(QPointF(7, 7), 1.1, 1.1);
            p->drawEllipse(QPointF(17, 17), 1.1, 1.1);
        } else {
            // Document based items (archive, executable, text, code, audio, video, pdf, doc, image, file)
            QPainterPath doc;
            doc.moveTo(5, 3);
            doc.lineTo(14, 3);
            doc.lineTo(19, 8);
            doc.lineTo(19, 21);
            doc.lineTo(5, 21);
            doc.closeSubpath();
            doc.moveTo(14, 3);
            doc.lineTo(14, 8);
            doc.lineTo(19, 8);
            p->setPen(QPen(textColor, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->setBrush(washColor);
            p->drawPath(doc);

            if (kind_ == "archive") {
                p->setPen(QPen(accent, 1.6, Qt::SolidLine, Qt::RoundCap));
                p->drawLine(QPointF(9, 7), QPointF(15, 7));
                p->drawLine(QPointF(9, 10), QPointF(15, 10));
                p->drawLine(QPointF(9, 13), QPointF(15, 13));
                p->drawRoundedRect(QRectF(9, 15.5, 6, 3.5), 1.0, 1.0);
            } else if (kind_ == "executable" || kind_ == "exe") {
                p->setPen(QPen(accent, 1.6, Qt::SolidLine, Qt::RoundCap));
                p->setBrush(Qt::NoBrush);
                p->drawEllipse(QPointF(12, 14), 2.5, 2.5);
                p->drawLine(QPointF(12, 10), QPointF(12, 11.5));
                p->drawLine(QPointF(12, 16.5), QPointF(12, 18));
                p->drawLine(QPointF(8, 14), QPointF(9.5, 14));
                p->drawLine(QPointF(14.5, 14), QPointF(16, 14));
            } else if (kind_ == "text" || kind_ == "txt") {
                p->setPen(QPen(accent, 1.6, Qt::SolidLine, Qt::RoundCap));
                p->drawLine(QPointF(8, 11), QPointF(16, 11));
                p->drawLine(QPointF(8, 14), QPointF(15, 14));
                p->drawLine(QPointF(8, 17), QPointF(12, 17));
            } else if (kind_ == "code") {
                p->setPen(QPen(accent, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                QPainterPath cPath;
                cPath.moveTo(9.5, 12);
                cPath.lineTo(7.5, 14.5);
                cPath.lineTo(9.5, 17);
                cPath.moveTo(14.5, 12);
                cPath.lineTo(16.5, 14.5);
                cPath.lineTo(14.5, 17);
                cPath.moveTo(12.5, 12);
                cPath.lineTo(11.5, 17);
                p->drawPath(cPath);
            } else if (kind_ == "audio") {
                p->setPen(Qt::NoPen);
                p->setBrush(accent);
                p->drawEllipse(QPointF(9.5, 16.5), 1.8, 1.8);
                p->drawEllipse(QPointF(14.5, 14.5), 1.5, 1.5);
                p->setPen(QPen(accent, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                QPainterPath note;
                note.moveTo(11.3, 16.5);
                note.lineTo(11.3, 11);
                note.lineTo(16, 10);
                note.lineTo(16, 14.5);
                p->drawPath(note);
            } else if (kind_ == "video") {
                QPolygonF play{QPointF(10, 12), QPointF(15.5, 15), QPointF(10, 18)};
                p->setPen(QPen(accent, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                p->setBrush(accent);
                p->drawPolygon(play);
            } else if (kind_ == "pdf") {
                p->setPen(Qt::NoPen);
                p->setBrush(accent);
                p->drawRoundedRect(QRectF(7.5, 12, 9, 5.5), 1.0, 1.0);
                p->setPen(QPen(Qt::white, 1.2, Qt::SolidLine, Qt::RoundCap));
                p->drawLine(QPointF(9, 13.5), QPointF(10.5, 13.5));
                p->drawLine(QPointF(9, 14.8), QPointF(12.5, 14.8));
                p->drawLine(QPointF(9, 16.1), QPointF(15, 16.1));
            } else if (kind_ == "doc") {
                p->setPen(Qt::NoPen);
                p->setBrush(accent);
                p->drawRoundedRect(QRectF(8, 10.5, 8, 2.0), 0.5, 0.5);
                p->setPen(QPen(accent, 1.6, Qt::SolidLine, Qt::RoundCap));
                p->drawLine(QPointF(8, 14.5), QPointF(16, 14.5));
                p->drawLine(QPointF(8, 17), QPointF(13, 17));
            } else if (kind_ == "image") {
                p->setPen(Qt::NoPen);
                p->setBrush(accentSecondary);
                p->drawEllipse(QPointF(15, 10), 1.5, 1.5);
                p->setPen(QPen(accent, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                p->setBrush(Qt::NoBrush);
                QPainterPath mtn;
                mtn.moveTo(7, 18);
                mtn.lineTo(11, 13);
                mtn.lineTo(14, 16);
                mtn.lineTo(16, 14);
                mtn.lineTo(17, 18);
                p->drawPath(mtn);
            }
        }
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

QIcon Icons::file(const QString &name, bool folder, bool isUp) {
    if (isUp || (folder && (name == ".." || name.endsWith("/..") || name.endsWith("\\.."))))
        return get("folder_up");
    if (folder)
        return get("folder");

    const auto ext = QFileInfo(name).suffix().toLower();
    if (QStringList{"zip", "7z", "rar", "tar", "gz", "bz2", "xz", "zst", "tgz", "tbz2", "txz", "z", "lzma", "cab", "arj", "lzh"}.contains(ext))
        return get("archive");
    if (QStringList{"exe", "dll", "msi", "com", "bat", "cmd", "scr", "sys"}.contains(ext))
        return get("executable");
    if (QStringList{"c", "cpp", "cc", "cxx", "h", "hpp", "rs", "py", "js", "ts", "jsx", "tsx", "html", "htm", "css", "scss", "sass", "json", "xml", "yaml", "yml", "toml", "sh", "ps1", "go", "java", "cs", "rb", "php", "swift", "kt", "sql"}.contains(ext))
        return get("code");
    if (QStringList{"mp3", "flac", "wav", "ogg", "m4a", "aac", "wma", "opus", "mid", "midi", "aiff"}.contains(ext))
        return get("audio");
    if (QStringList{"mp4", "mkv", "avi", "mov", "wmv", "flv", "webm", "m4v", "mpeg", "mpg", "3gp"}.contains(ext))
        return get("video");
    if (ext == "pdf")
        return get("pdf");
    if (QStringList{"doc", "docx", "odt", "xls", "xlsx", "ods", "ppt", "pptx", "odp", "rtf", "csv"}.contains(ext))
        return get("doc");
    if (QStringList{"iso", "img", "bin", "cue", "mdf", "nrg", "vhd", "vhdx", "vmdk"}.contains(ext))
        return get("iso");
    if (QStringList{"wim", "esd", "swm"}.contains(ext))
        return get("wim");
    if (QStringList{"png", "jpg", "jpeg", "gif", "webp", "svg", "bmp", "ico", "tiff", "tif", "avif", "heic"}.contains(ext))
        return get("image");
    if (QStringList{"txt", "md", "log", "ini", "cfg", "conf", "nfo"}.contains(ext))
        return get("text");

    return get("file");
}
