#include "PreviewDialog.h"
#include <QImageReader>
#include <QStringDecoder>
#include <QtWidgets>

PreviewContent PreviewDialog::decode(QByteArray bytes) {
    PreviewContent result;
    result.bytes = std::move(bytes);
    const auto &data = result.bytes;
    if (data.size() <= 4 * 1024 * 1024) {
        auto encoding = QStringDecoder::Utf8;
        if (data.startsWith(QByteArray::fromHex("fffe")))
            encoding = QStringDecoder::Utf16LE;
        else if (data.startsWith(QByteArray::fromHex("feff")))
            encoding = QStringDecoder::Utf16BE;
        QStringDecoder decoder(encoding, QStringConverter::Flag::Stateless);
        QString text = decoder.decode(data);
        bool valid =
            !decoder.hasError() && (encoding == QStringDecoder::Utf8 || data.size() % 2 == 0);
        for (const auto c : text)
            if ((c.unicode() < 32 && c != '\n' && c != '\r' && c != '\t') || c.unicode() == 127)
                valid = false;
        if (valid)
            result.text = text;
    }
    QBuffer source;
    source.setData(data);
    source.open(QIODevice::ReadOnly);
    QImageReader reader(&source);
    const auto format = reader.format().toLower();
    // Only raster image decoders; no SVG, HTML or document rendering.
    if (QList<QByteArray>{"png", "jpeg", "jpg", "bmp", "gif", "webp"}.contains(format)) {
        const auto size = reader.size();
        if (!size.isValid() || size.width() > 16384 || size.height() > 16384 ||
            qint64(size.width()) * size.height() > 16 * 1024 * 1024) {
            result.imageError = tr("Image dimensions exceed the preview limit.");
        } else {
            reader.setAutoTransform(true);
            result.image = reader.read();
            if (result.image.isNull())
                result.imageError = tr("The image could not be decoded. Hex view is available.");
        }
    }
    return result;
}
PreviewDialog::PreviewDialog(const QString &name, const PreviewContent &content, bool allowExternal,
                             QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("File preview"));
    resize(860, 640);
    auto *layout = new QVBoxLayout(this);
    auto *path = new QLabel(name);
    path->setTextFormat(Qt::PlainText);
    path->setWordWrap(true);
    path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(path);
    auto *info = new QLabel(tr("%1 bytes. Read-only preview; content is never executed.")
                                .arg(QLocale().toString(content.bytes.size())));
    info->setWordWrap(true);
    layout->addWidget(info);
    auto *tabs = new QTabWidget;
    tabs->setObjectName("previewTabs");
    layout->addWidget(tabs, 1);
    if (content.text) {
        auto *page = new QWidget;
        auto *column = new QVBoxLayout(page);
        column->setContentsMargins(0, 4, 0, 0);
        auto *search = new QHBoxLayout;
        auto *query = new QLineEdit;
        query->setObjectName("previewSearch");
        query->setPlaceholderText(tr("Find in text"));
        query->setClearButtonEnabled(true);
        auto *previous = new QPushButton(tr("Previous"));
        auto *next = new QPushButton(tr("Next"));
        search->addWidget(query, 1);
        search->addWidget(previous);
        search->addWidget(next);
        column->addLayout(search);
        auto *text = new QPlainTextEdit;
        text->setObjectName("textPreview");
        text->setReadOnly(true);
        text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        text->setPlainText(*content.text);
        column->addWidget(text, 1);
        auto find = [text, query](bool backwards) {
            if (query->text().isEmpty())
                return;
            const auto flags = backwards ? QTextDocument::FindBackward : QTextDocument::FindFlags();
            if (!text->find(query->text(), flags)) {
                text->moveCursor(backwards ? QTextCursor::End : QTextCursor::Start);
                text->find(query->text(), flags);
            }
        };
        connect(next, &QPushButton::clicked, this, [find] { find(false); });
        connect(previous, &QPushButton::clicked, this, [find] { find(true); });
        connect(query, &QLineEdit::returnPressed, this, [find] { find(false); });
        auto *shortcut = new QShortcut(QKeySequence::Find, this);
        connect(shortcut, &QShortcut::activated, query, [query] {
            query->setFocus();
            query->selectAll();
        });
        tabs->addTab(page, tr("Text"));
    }
    if (!content.image.isNull()) {
        auto *page = new QWidget;
        auto *column = new QVBoxLayout(page);
        column->setContentsMargins(0, 4, 0, 0);
        auto *tools = new QHBoxLayout;
        auto *fit = new QPushButton(tr("Fit"));
        auto *actual = new QPushButton(tr("100%"));
        auto *zoomIn = new QPushButton(tr("Zoom in"));
        auto *zoomOut = new QPushButton(tr("Zoom out"));
        tools->addWidget(fit);
        tools->addWidget(actual);
        tools->addWidget(zoomIn);
        tools->addWidget(zoomOut);
        tools->addStretch();
        tools->addWidget(new QLabel(
            tr("%1 x %2 pixels").arg(content.image.width()).arg(content.image.height())));
        column->addLayout(tools);
        auto *view = new QGraphicsView;
        view->setObjectName("imagePreview");
        auto *scene = new QGraphicsScene(view);
        scene->addPixmap(QPixmap::fromImage(content.image));
        view->setScene(scene);
        view->setDragMode(QGraphicsView::ScrollHandDrag);
        view->setRenderHint(QPainter::SmoothPixmapTransform);
        column->addWidget(view, 1);
        connect(fit, &QPushButton::clicked, view,
                [view] { view->fitInView(view->sceneRect(), Qt::KeepAspectRatio); });
        connect(actual, &QPushButton::clicked, view, [view] { view->resetTransform(); });
        connect(zoomIn, &QPushButton::clicked, view, [view] {
            if (view->transform().m11() < 8)
                view->scale(1.25, 1.25);
        });
        connect(zoomOut, &QPushButton::clicked, view, [view] {
            if (view->transform().m11() > 0.05)
                view->scale(0.8, 0.8);
        });
        tabs->addTab(page, tr("Image"));
        QTimer::singleShot(0, view,
                           [view] { view->fitInView(view->sceneRect(), Qt::KeepAspectRatio); });
    }
    auto *hex = new QPlainTextEdit;
    hex->setObjectName("hexPreview");
    hex->setReadOnly(true);
    hex->setLineWrapMode(QPlainTextEdit::NoWrap);
    hex->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    QString dump;
    const auto data = content.bytes.first(qMin(qsizetype(65536), content.bytes.size()));
    for (qsizetype offset = 0; offset < data.size(); offset += 16) {
        const auto row = data.mid(offset, 16);
        QString ascii;
        for (auto c : row)
            ascii += uchar(c) >= 32 && uchar(c) < 127 ? QChar(uchar(c)) : QChar('.');
        dump += QString("%1  %2  %3\n")
                    .arg(offset, 8, 16, QChar('0'))
                    .arg(QString::fromLatin1(row.toHex(' ')).leftJustified(47), ascii);
    }
    hex->setPlainText(dump);
    tabs->addTab(hex, tr("Hex (first 64 KiB)"));
    if (!content.imageError.isEmpty()) {
        auto *warning = new QLabel(content.imageError);
        warning->setWordWrap(true);
        layout->addWidget(warning);
    }
    if (!content.text && content.image.isNull()) {
        auto *hint =
            new QLabel(tr("Binary or unsupported text encoding. Use Hex or extract the file."));
        hint->setWordWrap(true);
        layout->addWidget(hint);
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    if (allowExternal) {
        auto *external = buttons->addButton(tr("Open externally..."), QDialogButtonBox::ActionRole);
        connect(external, &QPushButton::clicked, this, &QDialog::accept);
    }
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}
