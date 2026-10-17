#include "../ArchiveModel.h"
#include "../EditStore.h"
#include "../ExternalFiles.h"
#include "../JobJournal.h"
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
#include <QJsonDocument>
#include <QSaveFile>
#include <QtTest>
#include <QtWidgets>
#include <cmath>
#include <memory>

class GuiPresentationTest : public QObject {
    Q_OBJECT
    QList<QUrl> openedUrls_;
  public slots:
    void captureUrl(const QUrl &url) { openedUrls_.append(url); }
  private slots:
    void performance100kReport() {
        const auto path = qEnvironmentVariable("VYNX_UI_BENCH_REPORT");
        if (path.isEmpty())
            QSKIP("Opt-in performance corpus; run benchmark tooling");
        ArchiveModel model;
        ArchiveFilter filter;
        filter.setSourceModel(&model);
        filter.setFilterCaseSensitivity(Qt::CaseInsensitive);
        rust::Vec<vynx::EntryInfo> entries;
        for (quint64 i = 0; i < 100000; ++i) {
            vynx::EntryInfo entry;
            entry.id = i;
            entry.name = QString("group%1/file_%2.txt")
                             .arg(i / 1000, 3, 10, QChar('0'))
                             .arg(i, 6, 10, QChar('0'))
                             .toStdString();
            entry.size = 1024;
            entries.push_back(std::move(entry));
        }
        QElapsedTimer timer;
        timer.start();
        model.load(std::move(entries));
        const auto prepareMs = double(timer.nsecsElapsed()) / 1e6;
        QTableView view;
        view.setModel(&filter);
        view.setSortingEnabled(true);
        view.sortByColumn(0, Qt::AscendingOrder);
        view.resize(1060, 720);
        view.show();
        QCoreApplication::processEvents();
        QList<double> samples;
        QJsonArray raw;
        for (int i = 0; i < 23; ++i) {
            timer.restart();
            model.setSearchMode(true);
            filter.setFilterFixedString(QString("file_%1").arg(i % 10, 2, 10, QChar('0')));
            const auto count = filter.rowCount();
            QVERIFY(count > 0);
            QCoreApplication::processEvents();
            const auto ms = double(timer.nsecsElapsed()) / 1e6;
            if (i >= 3) {
                samples.append(ms);
                raw.append(ms);
            }
            filter.setFilterFixedString("");
            model.setSearchMode(false);
            QCoreApplication::processEvents();
        }
        std::sort(samples.begin(), samples.end());
        QSaveFile output(path);
        QVERIFY(output.open(QIODevice::WriteOnly));
        const auto json =
            QJsonDocument(
                QJsonObject{{"scope", "synthetic 100000-member model, sorting and visible table; "
                                      "not codec throughput"},
                            {"prepare_ms", prepareMs},
                            {"samples_ms", raw},
                            {"median_ms", samples[samples.size() / 2]},
                            {"p95_ms", samples[int(std::ceil(samples.size() * .95)) - 1]}})
                .toJson();
        QCOMPARE(output.write(json), json.size());
        QVERIFY(output.commit());
    }

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
    void workingCopiesPersistAndNeverJoinDisposableCleanup() {
        QTemporaryDir root;
        const auto work = root.path() + "/edits";
        QString id;
        {
            EditStore store(work);
            QVERIFY(store.available());
            id = store.reserve("archive.zip", "nested/note.txt");
            QVERIFY(!id.isEmpty());
            QFile file(store.directory(id) + "/note.txt");
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("unsaved content");
            file.close();
            QVERIFY(store.update(id, QJsonObject{{"file", file.fileName()},
                                                 {"state", "ready"},
                                                 {"archiveHash", "baseline"},
                                                 {"baseHash", "entry baseline"}}));
            QVERIFY(store.validPayload(store.record(id)));
            EditStore other(work);
            QVERIFY(!other.available());
            ExternalFiles disposable(root.path() + "/views");
            disposable.cleanup();
            QVERIFY(QFile::exists(file.fileName()));
        }
        EditStore recovered(work);
        QCOMPARE(recovered.records().size(), 1);
        QVERIFY(recovered.validPayload(recovered.record(id)));
        QFile file(recovered.record(id)["file"].toString());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("unsaved content"));
        file.close();
        const auto outside = root.path() + "/outside.txt";
        QFile external(outside);
        QVERIFY(external.open(QIODevice::WriteOnly));
        external.write("keep");
        external.close();
        QVERIFY(recovered.update(id, QJsonObject{{"file", outside}}));
        QVERIFY(!recovered.validPayload(recovered.record(id)));
        QVERIFY(recovered.discard(id));
        QVERIFY(QFile::exists(outside));
        QCOMPARE(recovered.records().size(), 0);
    }
    void temporaryViewsHaveExplicitOwnedCleanup() {
        QTemporaryDir root;
        ExternalFiles files(root.path() + "/views");
        const auto path = files.create();
        QVERIFY(!path.isEmpty());
        QFile file(path + "/readme.txt");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("view");
        file.close();
        QCOMPARE(files.items().size(), 1);
        QVERIFY(!files.remove(root.path()));
        QVERIFY(files.remove(path));
        QCOMPARE(files.items().size(), 0);
    }
    void journalRecoveryIsExplicitAndNeverSerializesPasswords() {
        QTemporaryDir root;
        const auto path = root.path() + "/jobs.json";
        QString id;
        {
            JobJournal journal;
            QVERIFY(journal.open(path, true));
            id = journal.add("Create",
                             QJsonObject{{"kind", "create"},
                                         {"sources", QJsonArray{root.path() + "/source.txt"}},
                                         {"password", "TOP-SECRET"},
                                         {"credentials", QJsonObject{{"key", "TOP-SECRET"}}}});
            QVERIFY(!id.isEmpty());
            QVERIFY(journal.setState(id, "running"));
            QVERIFY(journal.setState(id, "decision"));
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QVERIFY(!file.readAll().contains("TOP-SECRET"));
            JobJournal second;
            QVERIFY(!second.open(path, true));
        }
        JobJournal recovered;
        QVERIFY(recovered.open(path, true));
        QCOMPARE(recovered.record(id)["state"].toString(), QString("interrupted"));
        QVERIFY(recovered.setEnabled(false));
        QVERIFY(!QFile::exists(path));
    }
    void journalRejectsDamageAndDetectsSourceChanges() {
        QTemporaryDir root;
        const auto path = root.path() + "/jobs.json";
        QFile source(root.path() + "/source.txt");
        QVERIFY(source.open(QIODevice::WriteOnly));
        source.write("one");
        source.close();
        {
            JobJournal journal;
            QVERIFY(journal.open(path, true));
            auto id =
                journal.add("Extract", QJsonObject{{"kind", "extract"},
                                                   {"sources", QJsonArray{source.fileName()}}});
            QVERIFY(JobJournal::sourcesUnchanged(journal.record(id)));
            QVERIFY(source.open(QIODevice::WriteOnly));
            source.write("changed source");
            source.close();
            QVERIFY(!JobJournal::sourcesUnchanged(journal.record(id)));
            QVERIFY(journal.setState(id, "failed", "Original failure"));
            QVERIFY(!journal.setState(id, "fake-success"));
        }
        QFile damaged(path);
        QVERIFY(damaged.open(QIODevice::WriteOnly));
        damaged.write("{broken");
        damaged.close();
        JobJournal rejected;
        QVERIFY(!rejected.open(path, true));
        QVERIFY(damaged.open(QIODevice::ReadOnly));
        QCOMPARE(damaged.readAll(), QByteArray("{broken"));
    }
    void journalSurvivesARealKilledProcess() {
        QTemporaryDir root;
        const auto path = root.path() + "/jobs.json";
        QProcess child;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("VYNX_JOURNAL_TEST_CHILD", path);
        child.setProcessEnvironment(environment);
        child.start(QCoreApplication::applicationFilePath(), {});
        QVERIFY(child.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(path + ".ready"), 10000);
        child.kill();
        QVERIFY(child.waitForFinished());
        JobJournal recovered;
        QVERIFY(recovered.open(path, true));
        QCOMPARE(recovered.records().size(), 1);
        QCOMPARE(recovered.records()[0].toObject()["state"].toString(), QString("interrupted"));
        EditStore edits(path + ".edits");
        QVERIFY(edits.available());
        QCOMPARE(edits.records().size(), 1);
        const auto record = edits.records()[0].toObject();
        QVERIFY(edits.validPayload(record));
        QFile payload(record["file"].toString());
        QVERIFY(payload.open(QIODevice::ReadOnly));
        QCOMPARE(payload.readAll(), QByteArray("unsaved after process kill"));
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
int main(int argc, char **argv) {
    QApplication application(argc, argv);
    const auto childPath = qEnvironmentVariable("VYNX_JOURNAL_TEST_CHILD");
    if (!childPath.isEmpty()) {
        JobJournal journal;
        if (!journal.open(childPath, true))
            return 2;
        const auto id = journal.add(
            "Interrupted write",
            QJsonObject{{"kind", "create"}, {"sources", QJsonArray{childPath + ".source"}}});
        if (!journal.setState(id, "running"))
            return 3;
        EditStore edits(childPath + ".edits");
        const auto key = edits.reserve("archive.zip", "note.txt");
        QFile payload(edits.directory(key) + "/note.txt");
        if (!payload.open(QIODevice::WriteOnly))
            return 5;
        payload.write("unsaved after process kill");
        payload.close();
        if (!edits.update(key, QJsonObject{{"file", payload.fileName()}, {"state", "ready"}}))
            return 6;
        QFile ready(childPath + ".ready");
        if (!ready.open(QIODevice::WriteOnly))
            return 4;
        ready.write("ready");
        ready.close();
        return application.exec();
    }
    GuiPresentationTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "GuiPresentationTest.moc"
