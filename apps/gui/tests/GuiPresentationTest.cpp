#include "../ArchiveModel.h"
#include "../Theme.h"
#include "../dialogs/CreateArchiveDialog.h"
#include "../pages/ArchivePage.h"
#include "../widgets/BreadcrumbBar.h"
#include "../widgets/IconProvider.h"
#include "../widgets/OperationPanel.h"
#include <QtTest>
#include <QtWidgets>

class GuiPresentationTest : public QObject {
    Q_OBJECT
  private slots:
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
