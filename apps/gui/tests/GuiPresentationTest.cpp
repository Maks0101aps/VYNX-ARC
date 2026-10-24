#include "../NestedStore.h"
#include "../TempStore.h"
#include "../ArchiveSession.h"
#include "../dialogs/InterArchiveCopyDialog.h"
#include "../dialogs/ExtractionRecoveryDialog.h"
#include "../ArchiveModel.h"
#include "../EditStore.h"
#include "../ExternalFiles.h"
#include "../JobJournal.h"
#include "../OperationQueue.h"
#include "../Theme.h"
#include "../dialogs/BatchExtractDialog.h"
#include "../dialogs/ConversionDialog.h"
#include "../dialogs/CreateArchiveDialog.h"
#include "../dialogs/PreviewDialog.h"
#include "../dialogs/TemporaryStorageDialog.h"
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
    void workingCopyQuotaRejectsNewCopiesAndRetainsExistingEdits() {
        QTemporaryDir root;
        EditStore store(root.path() + "/copies", 8192);
        const auto id = store.reserve("source.zip", "note.txt", 1024);
        QVERIFY(!id.isEmpty());
        QFile file(store.directory(id) + "/note.txt");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(4096, 'x'));
        file.close();
        QVERIFY(store.update(id, QJsonObject{{"file", file.fileName()}, {"state", "ready"}}));
        QVERIFY(store.usedBytes() >= 4096);
        QVERIFY(store.reserve("another.zip", "another.txt", 4096).isEmpty());
        QVERIFY(store.validPayload(store.record(id)));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray(4096, 'x'));
    }
    void conversionPlanReadsRealArchivesAndPreventsDestinationCollisions() {
        QTemporaryDir root;
        const auto input = root.path() + "/note.txt";
        QFile file(input);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("conversion dialog content");
        file.close();
        const auto archive = root.path() + "/source.zip";
        auto op = vynx::new_operation();
        rust::Vec<rust::String> inputs;
        inputs.push_back(input.toUtf8().toStdString());
        vynx::create_archive(archive.toUtf8().toStdString(), {inputs.data(), inputs.size()}, "",
                             *op);
        ConversionDialog dialog({archive});
        dialog.encrypt->setChecked(false);
        dialog.allowDecrypt->setChecked(true);
        dialog.show();
        auto *start = dialog.findChild<QPushButton *>("queueConversion");
        QVERIFY(start);
        QTRY_VERIFY_WITH_TIMEOUT(start->isEnabled(), 15000);
        const auto plans = dialog.plan();
        QCOMPARE(plans.size(), 1);
        QVERIFY(plans[0].output.endsWith(".zip"));
        QFile occupied(root.path() + "/occupied.zip");
        QVERIFY(occupied.open(QIODevice::WriteOnly));
        occupied.write("preserved");
        occupied.close();
        dialog.outputName->setText("occupied");
        QVERIFY(!start->isEnabled());
        dialog.conflict->setCurrentIndex(1);
        QVERIFY(start->isEnabled());
        QVERIFY(dialog.plan()[0].output != occupied.fileName());
        dialog.format->setCurrentIndex(1);
        QVERIFY(dialog.plan()[0].output.endsWith(".7z"));
        dialog.sourcePassword->setText("changed-setting");
        QVERIFY(!start->isEnabled());
        dialog.findChild<QPushButton *>("readConversionPlan")->click();
        QTRY_VERIFY_WITH_TIMEOUT(start->isEnabled(), 15000);
        QVERIFY(occupied.open(QIODevice::ReadOnly));
        QCOMPARE(occupied.readAll(), QByteArray("preserved"));
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
        QCOMPARE(recovered.record(id)["state"].toString(), QString("unknown"));
        QVERIFY(recovered.setEnabled(false));
        QVERIFY(!QFile::exists(path));
    }
    void largeJournalSelectionsCannotSilentlyBecomePartialRetries() {
        QTemporaryDir root;
        const auto path = root.path() + "/jobs.json";
        QString id;
        {
            JobJournal journal;
            QVERIFY(journal.open(path, true));
            QJsonArray names;
            for (int i = 0; i < 100000; ++i)
                names.append(QString("file-%1").arg(i));
            id = journal.add("Large selection", QJsonObject{{"kind", "extract"}, {"names", names}});
            QVERIFY(!id.isEmpty());
            QVERIFY(!journal.record(id)["spec"].toObject()["incompleteSpec"].toBool());
        }
        JobJournal recovered;
        QVERIFY(recovered.open(path, true));
        const auto spec = recovered.record(id)["spec"].toObject();
        QVERIFY(!spec["incompleteSpec"].toBool());
        QCOMPARE(spec["names"].toArray().size(), 100000);
    }
    void journalRejectsTamperedPayloadAndMigratesLegacyWithoutResuming() {
        QTemporaryDir root;
        const auto path = root.path() + "/jobs.json";
        QString id;
        {
            JobJournal journal;
            QVERIFY(journal.open(path, true));
            QJsonArray names;
            for (int i = 0; i < 20000; ++i)
                names.append(QString("file-%1").arg(i));
            id = journal.add(
                "Pending",
                QJsonObject{{"kind", "extract"}, {"names", names}, {"password", "NEVER-ON-DISK"}});
            QVERIFY(!id.isEmpty());
        }
        {
            JobJournal recovered;
            QVERIFY(recovered.open(path, true));
            QCOMPARE(recovered.record(id)["state"].toString(), QString("interrupted"));
            const auto files = QDir(path + ".data").entryList({"*.json"}, QDir::Files);
            QCOMPARE(files.size(), 1);
            QFile payload(path + ".data/" + files.first());
            QVERIFY(payload.open(QIODevice::ReadOnly));
            auto bytes = payload.readAll();
            QVERIFY(!bytes.contains("NEVER-ON-DISK"));
            payload.close();
            bytes[bytes.size() - 2] = 'X';
            QVERIFY(payload.open(QIODevice::WriteOnly));
            payload.write(bytes);
            payload.close();
        }
        QFile original(path);
        QVERIFY(original.open(QIODevice::ReadOnly));
        const auto before = original.readAll();
        original.close();
        {
            JobJournal rejected;
            QVERIFY(!rejected.open(path, true));
        }
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), before);
        original.close();
        const auto legacy = root.path() + "/legacy.json";
        QFile file(legacy);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(
            R"({"schema":1,"jobs":[{"id":"legacy","state":"pending","spec":{"kind":"extract","names":["one"],"incompleteSpec":true},"sources":[]}]})");
        file.close();
        {
            JobJournal journal;
            QVERIFY(journal.open(legacy, true));
            QCOMPARE(journal.record("legacy")["state"].toString(), QString("interrupted"));
            QVERIFY(journal.record("legacy")["spec"].toObject()["incompleteSpec"].toBool());
            QVERIFY(journal.setState("legacy", "failed"));
        }
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(file.readAll()).object()["schema"].toInt(), 2);
    }
    void publicationIntentRetainsExactIdentitiesAndRejectsInvalidDigests() {
        QTemporaryDir root;
        const auto path = root.path() + "/jobs.json";
        QString id;
        const QJsonArray targets{QJsonObject{{"path", root.path() + "/result.zip"},
                                             {"sha256", QString(64, 'a')},
                                             {"before", QString(64, 'b')}}};
        {
            JobJournal journal;
            QVERIFY(journal.open(path, true));
            id =
                journal.add("Write", QJsonObject{{"kind", "create"},
                                                 {"sources", QJsonArray{root.path() + "/source"}}});
            QVERIFY(journal.setState(id, "running"));
            QVERIFY(!journal.recordIntent(
                id, QJsonArray{QJsonObject{{"path", root.path() + "/result.zip"},
                                           {"sha256", "SHA-256: bad"}}}));
            QVERIFY(!journal.recordIntent(id, targets, "invalid receipt"));
            QVERIFY(journal.recordIntent(
                id, targets, "SHA-256: " + QString(64, 'c') + "\nCRC32: " + QString(8, 'd')));
        }
        JobJournal recovered;
        QVERIFY(recovered.open(path, true));
        QCOMPARE(recovered.record(id)["state"].toString(), QString("unknown"));
        QCOMPARE(recovered.record(id)["publication"].toArray(), targets);
        QCOMPARE(recovered.record(id)["spec"].toObject()["inputHash"].toString(),
                 "SHA-256: " + QString(64, 'c') + "\nCRC32: " + QString(8, 'd'));
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
            QVERIFY(journal.setState(id, "unknown"));
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
        QCOMPARE(recovered.records()[0].toObject()["state"].toString(), QString("unknown"));
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
        QVERIFY(dialog.findChild<QScrollArea *>("createOptionsScroll"));
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
    void nativeRarCreationControlsAndJournalParameters() {
        CreateArchiveDialog dialog({"C:/Test/source.txt"}, 12);
        dialog.show();
        QVERIFY(dialog.output->text().endsWith(".rar"));
        QVERIFY(dialog.findChild<QGroupBox *>("rarOptions")->isVisible());
        QVERIFY(dialog.password->isEnabled());
        QVERIFY(dialog.advanced->isVisible());
        dialog.preset->setCurrentIndex(0);
        QCOMPARE(dialog.preset->currentIndex(), 0);
        QVERIFY(dialog.findChild<QLabel *>("effectiveCompression")->text().contains("RAR5 Store"));
        dialog.rarDictionary->setCurrentIndex(6);
        dialog.resource->setCurrentIndex(0);
        QCOMPARE(dialog.rarDictionary->currentIndex(), 0);
        ArchiveJobSpec spec;
        spec.kind = JobKind::Create;
        spec.sources = {"C:/Test/source.txt"};
        spec.destination = "C:/Test/out.rar";
        spec.format = ArchiveFormat::Rar;
        spec.rarSolid = true;
        spec.rarHeaders = true;
        spec.rarDictionary = 1ULL << 20;
        spec.rarRecovery = 3;
        const auto restored = ArchiveJobSpec::fromJson(JobJournal::sanitizeSpec(spec.toJson()));
        QVERIFY(restored.rarSolid && restored.rarHeaders);
        QCOMPARE(restored.rarDictionary, 1ULL << 20);
        QCOMPARE(restored.rarRecovery, 3);
        QVERIFY(restored.validationError().isEmpty());
        const auto capture = qEnvironmentVariable("VYNX_RAR_DIALOG_CAPTURE");
        if (!capture.isEmpty()) QVERIFY(dialog.grab().save(capture));
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

    void nestedStoreLimitsAndCleanup() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        NestedStore store(dir.path());
        QVERIFY(store.available());

        // Normal reserve
        const auto id1 = store.reserve("parent.zip", "nested1.zip", 1, 1024);
        QVERIFY(!id1.isEmpty());
        QCOMPARE(store.records().size(), 1);

        // Depth limit: depth > 4 must be rejected
        const auto idTooDeep = store.reserve("parent.zip", "deep.zip", 5, 1024);
        QVERIFY(idTooDeep.isEmpty());

        // Size limit: size > 256 MiB must be rejected
        const auto idTooLarge = store.reserve("parent.zip", "huge.zip", 1, 300ULL << 20);
        QVERIFY(idTooLarge.isEmpty());

        // Create second entry
        const auto id2 = store.reserve("parent.zip", "nested2.7z", 2, 2048);
        QVERIFY(!id2.isEmpty());
        QCOMPARE(store.records().size(), 2);

        // Cleanup: preserve active id1, remove inactive id2
        const int cleaned = store.cleanup({id1});
        QCOMPARE(cleaned, 1);
        QCOMPARE(store.records().size(), 1);
        QCOMPARE(store.records().first().toObject()["id"].toString(), id1);
    }

    void tempStoreAccountingAndSafeCleanup() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ExternalFiles ext(dir.path() + "/views");
        NestedStore nest(dir.path() + "/nested");
        EditStore edits(dir.path() + "/edits");
        TempStore store(&ext, &nest, &edits);

        // 1 preview view
        const auto viewDir = ext.create();
        QVERIFY(!viewDir.isEmpty());
        QFile vf(viewDir + "/file.txt");
        QVERIFY(vf.open(QIODevice::WriteOnly));
        vf.write("hello preview");
        vf.close();

        // 1 nested archive
        const auto nestId = nest.reserve("p.zip", "inner.zip", 1, 100);
        QVERIFY(!nestId.isEmpty());

        // 1 working copy
        const auto editId = edits.reserve("p.zip", "edit.txt", 100);
        QVERIFY(!editId.isEmpty());
        QFile ef(edits.directory(editId) + "/edit.txt");
        QVERIFY(ef.open(QIODevice::WriteOnly));
        ef.write("working copy edit");
        ef.close();
        edits.update(editId, QJsonObject{{"file", ef.fileName()}, {"state", "ready"}});

        // Check accounting
        QCOMPARE(store.previewUsage().count, 1);
        QCOMPARE(store.nestedUsage().count, 1);
        QCOMPARE(store.editUsage().count, 1);

        // Check preview cleanup list
        // Active nestedId is provided as active, so it should not be in cleanup list
        auto toClean = store.previewCleanupList({nestId});
        // Only viewDir is disposable and eligible for safe cleanup
        for (const auto &item : toClean) {
            QVERIFY(item.category != "WorkingCopy");
        }

        // Cleanup inactive nested
        auto toCleanInactive = store.previewCleanupList({}); // no active nested
        bool hasNested = false;
        bool hasWorking = false;
        for (const auto &item : toCleanInactive) {
            if (item.category == "Nested") hasNested = true;
            if (item.category == "WorkingCopy") hasWorking = true;
        }
        QVERIFY(hasNested);
        QVERIFY(!hasWorking); // Working copies are NEVER in safe cleanup list!
    }

    void interArchiveCopyConflictResolution() {
        QVector<CopyCandidateItem> items;
        CopyCandidateItem c1;
        c1.name = "new_file.txt";
        c1.size = 100;
        c1.hasConflict = false;
        c1.targetPath = "new_file.txt";
        items.append(c1);

        CopyCandidateItem c2;
        c2.name = "conflict.txt";
        c2.size = 200;
        c2.hasConflict = true;
        c2.targetPath = "conflict.txt";
        items.append(c2);

        InterArchiveCopyDialog dialog("source.zip", "target.zip", "", items, 10ULL << 20);

        // Default combo is Rename (policy 2)
        auto resolved = dialog.resolvedItems();
        QCOMPARE(resolved.size(), 2);

        // Find policy combo and set to Skip (policy 3)
        auto *combo = dialog.findChild<QComboBox *>("copyPolicyCombo");
        QVERIFY(combo);
        combo->setCurrentIndex(2); // Skip
        QCOMPARE(dialog.policy(), CopyConflictPolicy::Skip);

        auto resolvedSkip = dialog.resolvedItems();
        QCOMPARE(resolvedSkip.size(), 1);
        QCOMPARE(resolvedSkip.first().name, QString("new_file.txt"));

        QVERIFY(!combo->accessibleName().isEmpty());
        auto *table = dialog.findChild<QTableWidget *>("copyCandidateTable");
        QVERIFY(table);
        QVERIFY(!table->accessibleName().isEmpty());
        QVERIFY(!table->accessibleDescription().isEmpty());
        QCOMPARE(table->editTriggers(), QAbstractItemView::NoEditTriggers);
    }

    void dialogKeyboardAndAccessibility() {
        TemporaryStorageDialog storage(nullptr, {});
        QVERIFY(!storage.findChild<QComboBox *>("tempCategoryFilter")->accessibleName().isEmpty());
        auto *table = storage.findChild<QTableWidget *>("tempStorageTable");
        QVERIFY(table);
        QVERIFY(!table->accessibleDescription().isEmpty());
        QCOMPARE(table->editTriggers(), QAbstractItemView::NoEditTriggers);
        QStringList shortcuts;
        for (auto *button : storage.findChildren<QPushButton *>())
            shortcuts << button->shortcut().toString();
        QVERIFY(shortcuts.contains("Alt+C"));
        QVERIFY(shortcuts.contains("Alt+O"));
        QVERIFY(shortcuts.contains("F5"));
    }

    void extractionRecoveryReconciliation() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        // Dummy source archive file
        QFile sourceFile(dir.path() + "/archive.zip");
        QVERIFY(sourceFile.open(QIODevice::WriteOnly));
        sourceFile.write("dummy archive bytes");
        sourceFile.close();
        const QString sourceFp = QString("%1:%2").arg(sourceFile.size()).arg(sourceFile.fileTime(QFileDevice::FileModificationTime).toMSecsSinceEpoch());

        const QString destRoot = dir.path() + "/extracted";
        QDir().mkpath(destRoot);

        // 1. Untouched completed file: size 10 bytes
        QFile f1(destRoot + "/f1.txt");
        QVERIFY(f1.open(QIODevice::WriteOnly));
        f1.write("0123456789");
        f1.close();
        const qint64 f1Mtime = QFileInfo(f1).lastModified().toSecsSinceEpoch();

        // 2. User-modified file: size 22 bytes, file modified after extraction time
        QFile f2(destRoot + "/f2.txt");
        QVERIFY(f2.open(QIODevice::WriteOnly));
        f2.write("user modified content!");
        f2.close();
        const qint64 f2Mtime = QFileInfo(f2).lastModified().toSecsSinceEpoch();

        // 3. Corrupt file: size and time match, but CRC in plan differs!
        QFile f4(destRoot + "/f4.txt");
        QVERIFY(f4.open(QIODevice::WriteOnly));
        f4.write("corrupted!"); // 10 bytes
        f4.close();
        const qint64 f4Mtime = QFileInfo(f4).lastModified().toSecsSinceEpoch();

        // 4. f3.txt is missing

        QJsonArray planFiles;
        planFiles.append(QJsonObject{{"name", "f1.txt"}, {"size", 10.0}, {"modified", double(f1Mtime)}});
        planFiles.append(QJsonObject{{"name", "f2.txt"}, {"size", 22.0}, {"modified", double(f2Mtime - 100)}});
        planFiles.append(QJsonObject{{"name", "f3.txt"}, {"size", 50.0}, {"modified", 1000.0}});
        planFiles.append(QJsonObject{{"name", "f4.txt"}, {"size", 10.0}, {"modified", double(f4Mtime)}, {"crc", "12345678"}});

        QJsonObject plan{{"files", planFiles}};

        ExtractionRecoveryDialog dialog(sourceFile.fileName(), destRoot, sourceFp, plan);
        QVERIFY(dialog.canResume());
        auto *planTable = dialog.findChild<QTableWidget *>("recoveryPlanTable");
        QVERIFY(planTable);
        QCOMPARE(planTable->editTriggers(), QAbstractItemView::NoEditTriggers);
        QVERIFY(!planTable->accessibleDescription().isEmpty());

        const auto toExtract = dialog.filesToExtract();
        // Every existing file is preserved, including equal-size/mtime CRC mismatches.
        QCOMPARE(toExtract.size(), 1);
        QVERIFY(QFile::exists(f4.fileName()));
        QFile backup(destRoot + "/f4.txt.personal.tmp");
        QVERIFY(backup.open(QIODevice::WriteOnly)); backup.write("personal backup"); backup.close();
        dialog.filesToExtract();
        QVERIFY(QFile::exists(backup.fileName()));
        QVERIFY(QFile::exists(f4.fileName()));
        QStringList extractNames;
        for (const auto &it : toExtract)
            extractNames.append(it.name);
        QVERIFY(extractNames.contains("f3.txt"));
        QVERIFY(!extractNames.contains("f4.txt"));
        QVERIFY(!extractNames.contains("f2.txt"));
    }

    void previewCacheLRUAndInvalidation() {
        PreviewCache cache;
        QCOMPARE(cache.count(), 0);
        QCOMPARE(cache.totalBytes(), 0);

        PreviewContent c1;
        c1.bytes = "hello world";
        cache.put(1, c1);
        QCOMPARE(cache.count(), 1);

        auto ret = cache.get(1);
        QVERIFY(ret.has_value());
        QCOMPARE(ret->bytes, QByteArray("hello world"));

        // Image size accounting
        PreviewContent c2;
        QImage testImg(100, 100, QImage::Format_ARGB32);
        c2.image = testImg;
        quint64 expectedImgBytes = quint64(testImg.sizeInBytes());
        cache.put(2, c2);
        QCOMPARE(cache.count(), 2);
        QVERIFY(cache.totalBytes() >= expectedImgBytes);

        // Oversized single item (> 32 MiB) rejected
        PreviewContent oversized;
        oversized.bytes = QByteArray(33 * 1024 * 1024, 'X');
        cache.put(3, oversized);
        QCOMPARE(cache.count(), 2); // Not admitted!
        QVERIFY(!cache.get(3).has_value());

        // Invalidation
        cache.clear();
        QCOMPARE(cache.count(), 0);
        QCOMPARE(cache.totalBytes(), 0);
        QVERIFY(!cache.get(1).has_value());
    }

    void archiveDragIncludesSourceIdentity() {
        ArchiveModel model;
        auto prepared = std::make_shared<PreparedArchive>();
        ArchiveRow row; row.name = "note.txt"; row.fullPath = "note.txt"; row.id = 7;
        prepared->root.append(row);
        model.loadPrepared(prepared);
        model.setArchiveSource("C:/source.zip", "session-7");
        std::unique_ptr<QMimeData> mime(model.mimeData({model.index(0, 0)}));
        const auto obj = QJsonDocument::fromJson(mime->data("application/x-vynx-archive-copy")).object();
        QCOMPARE(obj["sourceArchive"].toString(), QString("C:/source.zip"));
        QCOMPARE(obj["sourceSessionId"].toString(), QString("session-7"));
        QCOMPARE(obj["items"].toArray().size(), 1);
    }

    void journalMigrationFrom06() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString historyPath = dir.path() + "/history.json";

        // Create 0.6 schema 2 journal file
        QJsonObject job1{
            {"id", "job-06-001"},
            {"state", "completed"},
            {"spec", QJsonObject{{"kind", "create"}, {"destination", "out.zip"}, {"sources", QJsonArray{"file1.txt"}}}}
        };
        QJsonObject job2{
            {"id", "job-06-002"},
            {"state", "interrupted"},
            {"spec", QJsonObject{{"kind", "extract"}, {"destination", "extracted/"}, {"sources", QJsonArray{"archive.zip"}}}}
        };
        QJsonObject oldDoc{
            {"schema", 2},
            {"jobs", QJsonArray{job1, job2}}
        };

        QFile f(historyPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(oldDoc).toJson());
        f.close();

        // Load into 0.7 JobJournal
        JobJournal journal;
        QVERIFY(journal.open(historyPath, true));
        const auto records = journal.records();
        QCOMPARE(records.size(), 2);

        QCOMPARE(records[0]["id"].toString(), QString("job-06-001"));
        QCOMPARE(records[0]["state"].toString(), QString("completed"));
        QCOMPARE(records[1]["id"].toString(), QString("job-06-002"));
    }

    void jobSpecTyping() {
        QVERIFY(ArchiveJobSpec::stringToKind("shell") == JobKind::Shell);
        QVERIFY(ArchiveJobSpec::stringToKind("read") == JobKind::Read);
        QVERIFY(ArchiveJobSpec::stringToKind("reconcile") == JobKind::Reconcile);
        QVERIFY(ArchiveJobSpec::stringToKind("copy") == JobKind::CopyEntries);
        QVERIFY(ArchiveJobSpec::stringToKind("not-a-job") == JobKind::Unknown);
        for (auto kind : {JobKind::Open, JobKind::Extract, JobKind::Create, JobKind::Modify,
                          JobKind::Convert, JobKind::EditSave, JobKind::Test, JobKind::Hash,
                          JobKind::Preview, JobKind::CopyEntries, JobKind::Shell, JobKind::Read,
                          JobKind::Reconcile, JobKind::Unknown})
            QVERIFY(ArchiveJobSpec::stringToKind(ArchiveJobSpec::kindToString(kind)) == kind);

        QVERIFY(ArchiveJobSpec::stringToState("verifying") == JobState::Verifying);
        QVERIFY(ArchiveJobSpec::stringToState("bogus") == JobState::Unknown);
        for (auto state : {JobState::Pending, JobState::Running, JobState::Decision,
                           JobState::Verifying, JobState::Completed, JobState::Failed,
                           JobState::Cancelled, JobState::Interrupted, JobState::Unknown})
            QVERIFY(ArchiveJobSpec::stringToState(ArchiveJobSpec::stateToString(state)) == state);

        QCOMPARE(int(ArchiveJobSpec::conflictFromInt(0)), int(ConflictPolicy::Refuse));
        QCOMPARE(int(ArchiveJobSpec::conflictFromInt(1)), int(ConflictPolicy::Skip));
        QCOMPARE(int(ArchiveJobSpec::conflictFromInt(2)), int(ConflictPolicy::Replace));
        QCOMPARE(int(ArchiveJobSpec::conflictFromInt(3)), int(ConflictPolicy::Ask));
        QCOMPARE(int(ArchiveJobSpec::conflictFromInt(4)), int(ConflictPolicy::Rename));
        QCOMPARE(int(ArchiveJobSpec::conflictFromInt(5)), int(ConflictPolicy::Newer));
        QCOMPARE(int(ArchiveJobSpec::conflictFromInt(9)), int(ConflictPolicy::Refuse));
        QCOMPARE(int(ArchiveJobSpec::formatFromInt(11)), int(ArchiveFormat::TarZst));
        QCOMPARE(int(ArchiveJobSpec::formatFromInt(12)), int(ArchiveFormat::Rar));
        QCOMPARE(int(ArchiveJobSpec::formatFromInt(13)), int(ArchiveFormat::Zip));

        ArchiveJobSpec spec;
        spec.kind = JobKind::Modify;
        spec.action = ModifyAction::RenameEntry;
        spec.format = ArchiveFormat::SevenZ;
        spec.preset = CompressionPreset::Maximum;
        spec.resource = ResourceMode::Eco;
        spec.conflict = ConflictPolicy::Newer;
        spec.sources = {"a.txt"};
        spec.names = {"old.txt"};
        const auto restored = ArchiveJobSpec::fromJson(spec.toJson());
        QVERIFY(restored.kind == JobKind::Modify);
        QVERIFY(restored.action == ModifyAction::RenameEntry);
        QVERIFY(restored.format == ArchiveFormat::SevenZ);
        QVERIFY(restored.preset == CompressionPreset::Maximum);
        QVERIFY(restored.resource == ResourceMode::Eco);
        QVERIFY(restored.conflict == ConflictPolicy::Newer);
        QCOMPARE(restored.sources, QStringList{"a.txt"});
        QCOMPARE(restored.names, QStringList{"old.txt"});

        ArchiveJobSpec shell;
        shell.kind = JobKind::Shell;
        shell.shellAction = 7;
        shell.sources = {"C:/file.txt"};
        QCOMPARE(ArchiveJobSpec::fromJson(shell.toJson()).shellAction, quint32(7));

        ArchiveJobSpec invalidExtract;
        invalidExtract.kind = JobKind::Extract;
        QVERIFY(!invalidExtract.validationError().isEmpty());
        invalidExtract.sources = {"archive.zip"};
        invalidExtract.destination = "output";
        QVERIFY(invalidExtract.validationError().isEmpty());

        ArchiveJobSpec invalidRename;
        invalidRename.kind = JobKind::Modify;
        invalidRename.sources = {"archive.zip"};
        invalidRename.names = {"old.txt"};
        invalidRename.action = ModifyAction::RenameEntry;
        QVERIFY(!invalidRename.validationError().isEmpty());
        invalidRename.newName = "new.txt";
        QVERIFY(invalidRename.validationError().isEmpty());
    }

    void tempStoreCategoryMapping() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        NestedStore nested(dir.path() + "/nested");
        TempStore store(nullptr, &nested, nullptr);

        const QString id = nested.reserve("parent.zip", "inner.7z", 1, 1024);
        QVERIFY(!id.isEmpty());
        QVERIFY(!nested.record(id).isEmpty());

        // Clean with category 'Nested archive' (UI label)
        TempStore::TempItem item;
        item.category = "Nested";
        item.id = id;
        item.canClean = true;

        int cleaned = store.cleanItems({item});
        QCOMPARE(cleaned, 1);
        QVERIFY(nested.record(id).isEmpty());
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
