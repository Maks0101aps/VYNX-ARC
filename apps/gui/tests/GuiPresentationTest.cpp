#include "../ArchiveModel.h"
#include "../ExternalFiles.h"
#include "../OperationQueue.h"
#include "../Theme.h"
#include "../dialogs/BatchExtractDialog.h"
#include "../dialogs/CreateArchiveDialog.h"
#include "../dialogs/PreviewDialog.h"
#include "../pages/ArchivePage.h"
#include "../widgets/BreadcrumbBar.h"
#include "../widgets/IconProvider.h"
#include "../widgets/OperationPanel.h"
#include <QDesktopServices>
#include <QImageReader>
#include <QtTest>
#include <QtWidgets>
#include <memory>

class GuiPresentationTest : public QObject {
    Q_OBJECT
    QList<QUrl> openedUrls_;
  public slots:
    void captureUrl(const QUrl &url) { openedUrls_.append(url); }
  private slots:
    void viewerDecodesTextImagesAndHexWithoutExecutingContent() {
        QImageReader::setAllocationLimit(64);
        auto content = PreviewDialog::decode("<script>hello</script>\nhello");
        QVERIFY(content.text);
        PreviewDialog dialog("readme.html", content, false);
        QCOMPARE(dialog.findChild<QPlainTextEdit *>("textPreview")->toPlainText(),
                 QString("<script>hello</script>\nhello"));
        auto *query = dialog.findChild<QLineEdit *>("previewSearch");
        query->setText("hello");
        QMetaObject::invokeMethod(query, "returnPressed");
        QCOMPARE(dialog.findChild<QPlainTextEdit *>("textPreview")->textCursor().selectedText(),
                 QString("hello"));
        const auto utf16 = PreviewDialog::decode(QByteArray::fromHex("fffe480069000a00"));
        QVERIFY(utf16.text);
        QCOMPARE(*utf16.text, QString("Hi\n"));
        QVERIFY(!PreviewDialog::decode(QByteArray::fromHex("c3")).text);
        QVERIFY(!PreviewDialog::decode(QByteArray::fromHex("fffe4800ff")).text);
        const auto binary = PreviewDialog::decode(QByteArray::fromHex("0001ff"));
        QVERIFY(!binary.text);
        PreviewDialog hex("binary.bin", binary, false);
        QVERIFY(hex.findChild<QPlainTextEdit *>("hexPreview")->toPlainText().contains("00 01 ff"));
        QImage image(48, 32, QImage::Format_RGB32);
        image.fill(Qt::blue);
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        QVERIFY(image.save(&buffer, "PNG"));
        auto decoded = PreviewDialog::decode(png);
        QCOMPARE(decoded.image.size(), QSize(48, 32));
        PreviewDialog picture("image.png", decoded, true);
        QVERIFY(picture.findChild<QGraphicsView *>("imagePreview"));
        auto oversized = png;
        // Valid PNG IHDR with an impossible size: reader must never allocate its pixels.
        oversized[16] = 1;
        const auto rejected = PreviewDialog::decode(oversized);
        QVERIFY(rejected.image.isNull());
    }
    void externalViewsUseOwnedFilesAndSkipLiveCleanup() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto cacheRoot = root.path() + "/cache";
        QString path;
        QDesktopServices::setUrlHandler("file", this, "captureUrl");
        {
            ExternalFiles files(cacheRoot);
            path = files.create();
            QVERIFY(!path.isEmpty());
            QFile file(path + "/readme.txt");
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("hello");
            file.close();
            QVERIFY(files.open(file.fileName()));
            QCOMPARE(openedUrls_.last().toLocalFile(), file.fileName());
            QVERIFY(!files.open(root.path() + "/outside.txt"));
            QFile marker(path + "/.vynx-view");
            QVERIFY(marker.open(QIODevice::ReadWrite));
            QVERIFY(marker.setFileTime(QDateTime::currentDateTimeUtc().addDays(-8),
                                       QFileDevice::FileModificationTime));
            marker.close();
            ExternalFiles another(cacheRoot);
            QVERIFY(QDir(path).exists());
        }
        QDesktopServices::unsetUrlHandler("file");
        ExternalFiles cleaned(cacheRoot);
        QVERIFY(!QDir(path).exists());
        QVERIFY(ExternalFiles::allowed("photo.PNG"));
        QVERIFY(!ExternalFiles::allowed("photo.png.exe"));
        QVERIFY(!ExternalFiles::allowed("link.url"));
        QVERIFY(!ExternalFiles::allowed("script.html"));
        QVERIFY(!ExternalFiles::allowed("document.docm"));
    }
    void batchNamesAndQueueReorderingAreDeterministic() {
        const auto paths = batchDestinations(
            {"C:/one/Project.tar.xz", "C:/two/project.zip", "C:/three/CON.zip"}, "C:/Output");
        QCOMPARE(QFileInfo(paths[0]).fileName(), QString("Project"));
        QCOMPARE(QFileInfo(paths[1]).fileName(), QString("project (2)"));
        QCOMPARE(QFileInfo(paths[2]).fileName(), QString("_CON"));
        OperationQueue queue;
        auto first = queue.enqueue("first", [] {});
        auto second = queue.enqueue("second", [] {});
        auto third = queue.enqueue("third", [] {});
        QVERIFY(queue.move(third, -1));
        QVERIFY(queue.move(third, -1));
        QVERIFY(!queue.move(third, -1));
        QCOMPARE(queue.take()->id, third);
        QCOMPARE(queue.take()->id, first);
        QCOMPARE(queue.take()->id, second);
    }
    void globalSearchPreservesPathsAndFolder() {
        ArchiveModel model;
        ArchiveFilter filter;
        filter.setSourceModel(&model);
        rust::Vec<vynx::EntryInfo> entries;
        for (int i = 0; i < 4; ++i) {
            vynx::EntryInfo entry;
            entry.id = i;
            entry.name = i == 0   ? "one/readme.txt"
                         : i == 1 ? "two/readme.txt"
                         : i == 2 ? "two/image.png"
                                  : "empty/";
            entry.directory = i == 3;
            entries.push_back(std::move(entry));
        }
        model.load(std::move(entries));
        model.navigate("one");
        model.setSearchMode(true);
        filter.setFilterFixedString("readme");
        QCOMPARE(filter.rowCount(), 2);
        QSet<QString> paths;
        for (int i = 0; i < filter.rowCount(); ++i) {
            const auto &row = model.row(filter.mapToSource(filter.index(i, 0)).row());
            QCOMPARE(row.name, QString("readme.txt"));
            QCOMPARE(row.memberIds.size(), 1);
            paths.insert(filter.index(i, 7).data().toString());
        }
        QCOMPARE(paths, QSet<QString>({"one/readme.txt", "two/readme.txt"}));
        filter.setFilterFixedString("two/");
        QCOMPARE(filter.rowCount(), 2);
        filter.setFilterRegularExpression(
            QRegularExpression("readme\\.txt$", QRegularExpression::CaseInsensitiveOption));
        QCOMPARE(filter.rowCount(), 2);
        filter.setFilterFixedString("");
        model.setSearchMode(false);
        QCOMPARE(model.folder(), QString("one"));
        QCOMPARE(model.rowCount(), 1);
    }
    void queuedRequestsAreOrderedAndCancellable() {
        OperationQueue queue;
        QStringList started;
        const auto first = queue.enqueue("first", [&] { started << "first"; });
        const auto second = queue.enqueue("second", [&] { started << "second"; });
        queue.enqueue("third", [&] { started << "third"; });
        QVERIFY(first != second);
        QVERIFY(queue.cancel(second));
        auto job = queue.take();
        QVERIFY(job);
        job->start();
        job = queue.take();
        QVERIFY(job);
        job->start();
        QVERIFY(!queue.take());
        QCOMPARE(started, QStringList({"first", "third"}));
        auto owner = std::make_shared<int>(1);
        std::weak_ptr<int> retained = owner;
        const auto pending = queue.enqueue("captured", [owner] {});
        owner.reset();
        QVERIFY(!retained.expired());
        QVERIFY(queue.cancel(pending));
        QVERIFY(retained.expired());
        for (int i = 0; i < 32; ++i)
            QVERIFY(queue.enqueue("bounded", [] {}));
        QVERIFY(!queue.enqueue("overflow", [] {}));
        queue.clear();
        QCOMPARE(queue.size(), size_t(0));
    }
    void initTestCase() {
        const auto fontId = QFontDatabase::addApplicationFont(qEnvironmentVariable("SystemRoot") +
                                                              "/Fonts/segoeui.ttf");
        QVERIFY(fontId >= 0);
        qApp->setFont(QFont(QFontDatabase::applicationFontFamilies(fontId).first(), 10));
        QVERIFY(QFontMetrics(qApp->font()).horizontalAdvance("Breadcrumb") > 0);
    }
    void numericAndTimestampSorting() {
        ArchiveModel model;
        ArchiveFilter filter;
        filter.setSourceModel(&model);
        rust::Vec<vynx::EntryInfo> entries;
        for (int i = 0; i < 3; ++i) {
            vynx::EntryInfo entry;
            entry.id = i;
            entry.name = i == 0 ? "folder/child" : i == 1 ? "large" : "small";
            entry.size = i == 1 ? 1024 : 10;
            entry.packed = i == 1 ? 20 : 8;
            entry.modified_unix = i == 1 ? 1700000000 : 1800000000;
            entry.modified_known = true;
            entries.push_back(std::move(entry));
        }
        model.load(std::move(entries));
        filter.sort(0, Qt::AscendingOrder);
        QVERIFY(model.row(filter.mapToSource(filter.index(0, 0)).row()).folder);
        filter.sort(0, Qt::DescendingOrder);
        QVERIFY(model.row(filter.mapToSource(filter.index(0, 0)).row()).folder);
        for (int column : {1, 2}) {
            filter.sort(column, Qt::AscendingOrder);
            QCOMPARE(filter.index(1, 0).data().toString(), QString("small"));
            filter.sort(column, Qt::DescendingOrder);
            QCOMPARE(filter.index(1, 0).data().toString(), QString("large"));
        }
        filter.sort(3, Qt::AscendingOrder);
        QCOMPARE(filter.index(1, 0).data().toString(), QString("small"));
        filter.sort(5, Qt::AscendingOrder);
        QCOMPARE(filter.index(1, 0).data().toString(), QString("large"));
        QCOMPARE(model.totalSize(), quint64(1044));
        QCOMPARE(model.totalPacked(), quint64(36));
        QCOMPARE(model.row(0).size, quint64(10));
    }
    void unknownAndDosEpochDates() {
        ArchiveModel model;
        rust::Vec<vynx::EntryInfo> entries;
        vynx::EntryInfo missing;
        missing.name = "unknown";
        entries.push_back(std::move(missing));
        vynx::EntryInfo epoch;
        epoch.name = "epoch";
        epoch.modified = "1980-01-01 00:00:00";
        epoch.modified_unix = 315532800;
        epoch.modified_known = true;
        entries.push_back(std::move(epoch));
        vynx::EntryInfo valid;
        valid.name = "known";
        valid.modified_unix = 1700000000;
        valid.modified_known = true;
        entries.push_back(std::move(valid));
        vynx::EntryInfo unixEpoch;
        unixEpoch.name = "unix-epoch";
        unixEpoch.modified_known = true;
        entries.push_back(std::move(unixEpoch));
        model.load(std::move(entries));
        QCOMPARE(model.index(0, 5).data().toString(), QString::fromUtf8("—"));
        QCOMPARE(model.index(1, 5).data().toString(), QString::fromUtf8("—"));
        QVERIFY(model.index(2, 5).data().toString() != QString::fromUtf8("—"));
        QCOMPARE(model.index(2, 5).data(Qt::UserRole).toLongLong(), qint64(1700000000));
        QVERIFY(model.index(3, 5).data().toString() != QString::fromUtf8("—"));
    }
    void breadcrumbNavigationAndCollapse() {
        BreadcrumbBar bar;
        bar.resize(600, 36);
        bar.setPath("Project.zip", "src/components");
        bar.show();
        QSignalSpy spy(&bar, &BreadcrumbBar::navigateRequested);
        auto buttons = bar.findChildren<QToolButton *>();
        auto *src = *std::find_if(buttons.begin(), buttons.end(), [](QToolButton *button) {
            return button->accessibleName() == "src";
        });
        QTest::mouseClick(src, Qt::LeftButton);
        QCOMPARE(spy.last()[0].toString(), QString("src"));
        src->setFocus();
        QTest::keyClick(src, Qt::Key_Space);
        QCOMPARE(spy.count(), 2);
        bar.resize(180, 36);
        bar.setPath("Long archive filename.zip",
                    "a-very-long-folder/another-long-folder/active-tail");
        QCoreApplication::processEvents();
        QVERIFY(bar.toolTip().contains("active-tail"));
        bool collapsed = false, tail = false;
        for (auto *button : bar.findChildren<QToolButton *>()) {
            if (!button->isVisible())
                continue;
            collapsed |= button->menu() != nullptr;
            tail |= button->accessibleName() == "active-tail";
            QVERIFY(!button->toolTip().isEmpty());
        }
        QVERIFY(collapsed);
        QVERIFY(tail);
    }
    void commandAvailabilityAndSharedActions() {
        QAction extract("Extract"), add("Add files"), test("Test archive"), remove("Delete"),
            rename("Rename"), hash("Hash");
        ArchivePage page(&extract, &add, &test, &remove, &rename, &hash, {&add, &remove, &rename});
        page.resize(620, 440);
        page.show();
        page.setWritable(false);
        QToolButton *addButton = nullptr, *testButton = nullptr;
        for (auto *button : page.findChildren<QToolButton *>()) {
            if (button->defaultAction() == &add)
                addButton = button;
            if (button->defaultAction() == &test)
                testButton = button;
        }
        QVERIFY(addButton);
        QVERIFY(testButton);
        QVERIFY(!addButton->isVisible());
        page.setWritable(true);
        QVERIFY(addButton->isVisible());
        test.setEnabled(false);
        QVERIFY(!testButton->isEnabled());
        QCOMPARE(testButton->text(), QString("Test"));
        QVERIFY(!page.back->accessibleName().isEmpty());
        QVERIFY(!page.up->toolTip().isEmpty());
    }
    void authoritativeCreateFormat() {
        CreateArchiveDialog dialog({"C:/Test/source.txt"}, 0);
        dialog.show();
        QVERIFY(dialog.output->text().endsWith(".zip"));
        dialog.format->setCurrentIndex(1);
        QVERIFY(dialog.output->text().endsWith(".7z"));
        QVERIFY(dialog.advanced->isVisible());
        dialog.password->setText("private");
        dialog.format->setCurrentIndex(2);
        QVERIFY(dialog.output->text().endsWith(".tar"));
        QVERIFY(dialog.password->text().isEmpty());
        QVERIFY(!dialog.password->isEnabled());
        QVERIFY(!dialog.advanced->isVisible());
    }
    void etaRequiresProgressAndResets() {
        OperationPanel panel;
        panel.begin("Extracting");
        panel.updateProgress(0, 1000, "file", 0);
        panel.updateProgress(50, 1000, "file", 500);
        QVERIFY(!panel.rate->text().contains("remaining"));
        panel.updateProgress(500, 1000, "file", 5000);
        QVERIFY(panel.rate->text().contains("remaining"));
        panel.updateProgress(1, 1000, "new phase", 5100);
        QVERIFY(!panel.rate->text().contains("remaining"));
        panel.updateProgress(20, 0, "unknown total", 9000);
        QVERIFY(!panel.rate->text().contains("remaining"));
    }
    void effectivePresetAndCancellationAreVisible() {
        CreateArchiveDialog dialog({}, 0);
        dialog.preset->setCurrentIndex(0);
        QVERIFY(dialog.findChild<QLabel *>("effectiveCompression")->text().contains("Store"));
        dialog.format->setCurrentIndex(5);
        QVERIFY(dialog.preset->currentIndex() != 0);
        dialog.resource->setCurrentIndex(0);
        QVERIFY(dialog.findChild<QLabel *>("effectiveCompression")->text().contains("2 MiB"));
        OperationPanel panel;
        panel.begin("Working");
        panel.updateProgress(100, 0, "file", 5000, "Verifying", {}, true);
        QCOMPARE(panel.progress->maximum(), 0);
        QVERIFY(!panel.cancel->isEnabled());
    }
    void vectorIconsFollowTheme() {
        auto icon = Icons::get("folder");
        applyUiTheme("light");
        auto light = icon.pixmap(24, 24).toImage();
        applyUiTheme("dark");
        auto dark = icon.pixmap(24, 24).toImage();
        QVERIFY(!light.isNull());
        QVERIFY(!dark.isNull());
        QVERIFY(light != dark);
    }
};
QTEST_MAIN(GuiPresentationTest)
#include "GuiPresentationTest.moc"
