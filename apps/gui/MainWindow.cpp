#include "MainWindow.h"
#include "EditStore.h"
#include "ExternalFiles.h"
#include "FormatUtils.h"
#include "SecretUtf8.h"
#include "ShellRequest.h"
#include "Theme.h"
#include "dialogs/AboutDialog.h"
#include "dialogs/BatchExtractDialog.h"
#include "dialogs/CreateArchiveDialog.h"
#include "dialogs/ExtractDialog.h"
#include "dialogs/SettingsDialog.h"
#include "pages/ArchivePage.h"
#include "pages/HomePage.h"
#include "widgets/BreadcrumbBar.h"
#include "widgets/IconProvider.h"
#include "widgets/OperationPanel.h"
#include "widgets/ToastOverlay.h"
#include <QRegularExpression>
#include <QStyleHints>
#include <QtWidgets>
#include <atomic>

static QString text(const rust::String &s) {
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}
static std::string utf8(const QString &s) {
    auto b = s.toUtf8();
    return std::string(b.constData(), size_t(b.size()));
}
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    session_ = std::make_shared<ArchiveSession>();
    sessions_.append(session_);
    if (QCoreApplication::arguments().contains("--smoke-test") ||
        QCoreApplication::arguments().contains("--capture")) {
        transientStorage_ = std::make_unique<QTemporaryDir>();
        edits_ = std::make_unique<EditStore>(transientStorage_->path() + "/WorkingCopies");
        externalFiles_ = std::make_unique<ExternalFiles>(transientStorage_->path() + "/Views");
    } else {
        edits_ = std::make_unique<EditStore>();
        externalFiles_ = std::make_unique<ExternalFiles>();
    }
    setWindowTitle(tr("VYNX ARC"));
    setWindowIcon(QIcon(":/assets/icons/vynx-arc.png"));
    resize(1060, 720);
    setMinimumSize(620, 440);
    setAcceptDrops(true);
    const QString portable = QCoreApplication::applicationDirPath() + "/portable.flag";
    if (QFile::exists(portable))
        settings_ = std::make_unique<QSettings>(
            QCoreApplication::applicationDirPath() + "/data/settings.ini", QSettings::IniFormat);
    else
        settings_ = std::make_unique<QSettings>(QSettings::IniFormat, QSettings::UserScope, "VYNX",
                                                "VYNX ARC");
    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    auto addAction = [this](QMenu *menu, const QString &label, const QKeySequence &key,
                            auto callback) {
        auto *a = menu->addAction(label);
        a->setShortcut(key);
        connect(a, &QAction::triggered, this, callback);
        return a;
    };
    auto *open =
        addAction(fileMenu, tr("&Open archive…"), QKeySequence::Open, [this] { chooseOpen(); });
    auto *create =
        addAction(fileMenu, tr("&Create archive…"), QKeySequence::New, [this] { chooseCreate(); });
    idleActions_ = {};
    addAction(fileMenu, tr("Batch extraction..."), QKeySequence("Ctrl+Shift+E"),
              [this] { chooseBatchExtract(); });
    addAction(fileMenu, tr("Convert archives..."), QKeySequence("Ctrl+Shift+C"),
              [this] { chooseConversion(); });
    fileMenu->addSeparator();
    addAction(fileMenu, tr("Close archive"), QKeySequence("Ctrl+W"),
              [this] { closeSession(tabs_->currentIndex()); });
    addAction(fileMenu, tr("Exit"), QKeySequence::Quit, [this] { close(); });
    auto *operationsMenu = menuBar()->addMenu(tr("&Archive"));
    auto *extract = addAction(operationsMenu, tr("Extract…"), QKeySequence("Ctrl+E"),
                              [this] { chooseExtract(); });
    auto *smart =
        addAction(operationsMenu, tr("Smart Extract…"), {}, [this] { chooseExtract(true); });
    auto *here =
        addAction(operationsMenu, tr("Extract here"), {}, [this] { chooseExtract(false, true); });
    auto *named = addAction(operationsMenu, tr("Extract to named folder"), {},
                            [this] { chooseExtract(false, false, true); });
    auto *test = addAction(operationsMenu, tr("Test archive"), QKeySequence("Ctrl+T"),
                           [this] { testArchive(); });
    auto *hash =
        addAction(operationsMenu, tr("Calculate SHA-256 / CRC32"), {}, [this] { hashFile(); });
    archiveActions_ = {extract, smart, here, named, test, hash};
    hashSelectedAction_ =
        addAction(operationsMenu, tr("Hash selected files"), {}, [this] { hashContents(); });
    verifySelectedAction_ = addAction(operationsMenu, tr("Verify selected file hash…"), {},
                                      [this] { hashContents(true); });
    auto *verifyArchive = addAction(operationsMenu, tr("Verify archive file hash…"), {},
                                    [this] { hashContents(true, true); });
    previewAction_ = addAction(operationsMenu, tr("Preview file"), QKeySequence("F3"),
                               [this] { previewSelected(); });
    externalAction_ = addAction(operationsMenu, tr("Open externally..."),
                                QKeySequence("Ctrl+Return"), [this] { openSelectedExternally(); });
    archiveActions_.append({hashSelectedAction_, verifySelectedAction_, verifyArchive,
                            previewAction_, externalAction_});
    editAction_ = addAction(operationsMenu, tr("Edit working copy..."),
                            QKeySequence("Ctrl+Shift+Return"), [this] { editSelected(); });
    archiveActions_.append(editAction_);
    operationsMenu->addSeparator();
    auto *addFiles = addAction(operationsMenu, tr("Add files…"), QKeySequence("Ctrl+Shift+A"),
                               [this] { chooseModify(0); });
    auto *addFolder =
        addAction(operationsMenu, tr("Add folder…"), {}, [this] { chooseModify(0, {}, true); });
    auto *rename = addAction(operationsMenu, tr("Rename entry…"), QKeySequence("F2"),
                             [this] { chooseModify(2); });
    auto *remove = addAction(operationsMenu, tr("Delete entries…"), QKeySequence::Delete,
                             [this] { chooseModify(1); });
    archiveActions_.append({addFiles, addFolder, rename, remove});
    for (auto *a : archiveActions_)
        a->setEnabled(false);
    auto *viewMenu = menuBar()->addMenu(tr("&View"));
    addAction(viewMenu, tr("Settings…"), QKeySequence("Ctrl+,"), [this] { settings(); });
    auto *workingCopies = addAction(viewMenu, tr("Working copies..."), QKeySequence("Ctrl+Shift+W"),
                                    [this] { showWorkingCopies(); });
    addAction(viewMenu, tr("Temporary files..."), {}, [this] { showTemporaryFiles(); });
    auto *help = menuBar()->addMenu(tr("&Help"));
    addAction(help, tr("About VYNX ARC"), {}, [this] {
        AboutDialog dialog(this);
        dialog.exec();
    });

    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(12);
    setCentralWidget(central);
    tabs_ = new QTabBar;
    tabs_->setObjectName("archiveTabs");
    tabs_->setTabsClosable(true);
    tabs_->setExpanding(false);
    tabs_->addTab(tr("New archive"));
    tabs_->hide();
    layout->addWidget(tabs_);
    connect(tabs_, &QTabBar::currentChanged, this, &MainWindow::activateSession);
    connect(tabs_, &QTabBar::tabCloseRequested, this, &MainWindow::closeSession);
    for (const auto &key : {QString("Ctrl+Tab"), QString("Ctrl+Shift+Tab")}) {
        auto *shortcut = new QShortcut(QKeySequence(key), this);
        connect(shortcut, &QShortcut::activated, this, [this, key] {
            const int count = tabs_->count();
            if (count)
                tabs_->setCurrentIndex(
                    (tabs_->currentIndex() + (key.contains("Shift") ? count - 1 : 1)) % count);
        });
    }
    pages_ = new QStackedWidget;
    layout->addWidget(pages_, 1);
    auto *home = new HomePage(open, create);
    recent_ = home->recent;
    connect(home, &HomePage::openRequested, this, &MainWindow::openPath);
    connect(home, &HomePage::clearHistory, this, [this] {
        settings_->remove("recent");
        updateRecent();
    });
    connect(home, &HomePage::removeRecent, this, [this](const QString &path) {
        auto recent = settings_->value("recent").toStringList();
        recent.removeAll(path);
        settings_->setValue("recent", recent);
        updateRecent();
    });
    // The hidden home page must not impose its full content height on archive docks.
    auto *homeScroll = new QScrollArea;
    homeScroll->setWidgetResizable(true);
    homeScroll->setFrameShape(QFrame::NoFrame);
    homeScroll->setWidget(home);
    pages_->addWidget(homeScroll);
    auto *properties = addAction(operationsMenu, tr("Properties"), QKeySequence("Alt+Return"),
                                 [this] { showProperties(); });
    propertiesAction_ = properties;
    archiveActions_.append(properties);
    properties->setEnabled(false);
    archivePage_ = new ArchivePage(extract, addFiles, test, remove, rename, hashSelectedAction_,
                                   {smart, here, named, hash, hashSelectedAction_,
                                    verifySelectedAction_, verifyArchive, previewAction_, addFiles,
                                    addFolder, rename, remove, properties});
    pages_->addWidget(archivePage_);
    connect(archivePage_->back, &QToolButton::clicked, this, &MainWindow::back);
    connect(archivePage_->up, &QToolButton::clicked, this, &MainWindow::up);
    breadcrumb_ = archivePage_->breadcrumb;
    search_ = archivePage_->search;
    table_ = archivePage_->table;
    connect(breadcrumb_, &BreadcrumbBar::navigateRequested, this, &MainWindow::navigate);
    session_->filter->setSourceModel(session_->model);
    session_->filter->setFilterCaseSensitivity(Qt::CaseInsensitive);
    session_->filter->setFilterKeyColumn(0);
    table_->setModel(session_->filter);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->sortByColumn(0, Qt::AscendingOrder);
    table_->setColumnWidth(1, 100);
    table_->setColumnWidth(2, 100);
    table_->setColumnWidth(
        3,
        qMax(70, table_->fontMetrics().horizontalAdvance(
                     session_->model->headerData(3, Qt::Horizontal, Qt::DisplayRole).toString()) +
                     28));
    table_->setColumnWidth(4, 80);
    table_->setColumnWidth(5, 140);
    table_->setColumnHidden(6, true);
    table_->setColumnHidden(7, true);
    table_->setColumnWidth(7, 260);
    mutationActions_ = {addFiles, addFolder, rename, remove};
    renameAction_ = rename;
    deleteAction_ = remove;
    archiveSummary_ = new QLabel;
    archiveSummary_->setObjectName("muted");
    statusBar()->addPermanentWidget(archiveSummary_);
    toast_ = new ToastOverlay(central);
    connect(search_, &QLineEdit::textChanged, this, [this](const QString &s) {
        session_->model->setSearchMode(!s.isEmpty());
        table_->setColumnHidden(7, s.isEmpty());
        if (s.contains('*') || s.contains('?'))
            session_->filter->setFilterRegularExpression(
                QRegularExpression(QRegularExpression::wildcardToRegularExpression(
                                       s, QRegularExpression::UnanchoredWildcardConversion),
                                   QRegularExpression::CaseInsensitiveOption));
        else
            session_->filter->setFilterFixedString(s);
        updateStatus();
    });
    connect(table_, &QTableView::doubleClicked, this, [this](QModelIndex i) {
        const auto &r = session_->model->row(session_->filter->mapToSource(i).row());
        if (r.folder)
            navigate(r.fullPath);
        else
            previewSelected();
    });
    connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this] { updateStatus(); });
    auto *selectAll = new QShortcut(QKeySequence::SelectAll, table_);
    connect(selectAll, &QShortcut::activated, table_, &QTableView::selectAll);
    auto *searchShortcut = new QShortcut(QKeySequence::Find, this);
    connect(searchShortcut, &QShortcut::activated, this, [this] {
        search_->setFocus();
        search_->selectAll();
    });
    auto *escapeSearch = new QShortcut(QKeySequence(Qt::Key_Escape), search_);
    escapeSearch->setContext(Qt::WidgetShortcut);
    connect(escapeSearch, &QShortcut::activated, this, [this] {
        if (!search_->text().isEmpty())
            search_->clear();
        else
            table_->setFocus();
    });
    auto *upShortcut = new QShortcut(QKeySequence("Alt+Up"), this);
    connect(upShortcut, &QShortcut::activated, this, [this] { up(); });
    auto *backShortcut = new QShortcut(QKeySequence("Alt+Left"), this);
    connect(backShortcut, &QShortcut::activated, this, [this] { back(); });
    auto *backspace = new QShortcut(QKeySequence(Qt::Key_Backspace), table_);
    connect(backspace, &QShortcut::activated, this, [this] { up(); });
    auto *enter = new QShortcut(QKeySequence(Qt::Key_Return), table_);
    connect(enter, &QShortcut::activated, this, [this] {
        const auto rows = table_->selectionModel()->selectedRows();
        if (rows.size() == 1) {
            const auto &r = session_->model->row(session_->filter->mapToSource(rows.first()).row());
            if (r.folder)
                navigate(r.fullPath);
            else
                previewSelected();
        }
    });
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table_, &QWidget::customContextMenuRequested, this,
            [this, extract, addFiles, addFolder, rename, remove, properties](QPoint p) {
                auto index = table_->indexAt(p);
                if (!index.isValid())
                    return;
                if (!table_->selectionModel()->isSelected(index))
                    table_->selectRow(index.row());
                QMenu menu;
                const auto rows = table_->selectionModel()->selectedRows();
                if (rows.size() == 1 &&
                    session_->model->row(session_->filter->mapToSource(rows.first()).row()).folder)
                    connect(menu.addAction(tr("Open folder")), &QAction::triggered, this,
                            [this, index] {
                                navigate(
                                    session_->model->row(session_->filter->mapToSource(index).row())
                                        .fullPath);
                            });
                menu.addAction(extract);
                menu.addAction(hashSelectedAction_);
                menu.addAction(previewAction_);
                if (writableArchive()) {
                    menu.addSeparator();
                    for (auto *a : {addFiles, addFolder, rename, remove})
                        menu.addAction(a);
                }
                menu.addSeparator();
                menu.addAction(properties);
                menu.exec(table_->viewport()->mapToGlobal(p));
            });
    auto *columns = viewMenu->addMenu(tr("Columns"));
    for (int c = 1; c < 7; ++c) {
        auto *a = columns->addAction(
            session_->model->headerData(c, Qt::Horizontal, Qt::DisplayRole).toString());
        a->setCheckable(true);
        a->setChecked(!table_->isColumnHidden(c));
        connect(a, &QAction::toggled, this,
                [this, c](bool show) { archivePage_->setColumnPreference(c, show); });
    }
    connect(columns, &QMenu::aboutToShow, this, [this, columns] {
        int column = 1;
        for (auto *action : columns->actions()) {
            QSignalBlocker blocker(action);
            action->setChecked(!table_->isColumnHidden(column++));
        }
    });

    setupOperations(layout, viewMenu);
    setupJournal();
    auto *editStatus = new QTimer(this);
    editStatus->setInterval(2000);
    connect(editStatus, &QTimer::timeout, this, [this, workingCopies] {
        int changed = 0;
        QSet<QString> changedArchives;
        for (const auto &value : edits_->records()) {
            const auto record = value.toObject();
            if (record["state"] == "needs-review" || record["state"] == "preparing" ||
                EditStore::fingerprint(record["file"].toString()) !=
                    record["fingerprint"].toString()) {
                ++changed;
                changedArchives.insert(archiveIdentity(record["archive"].toString()));
            }
        }
        workingCopies->setText(changed ? tr("Working copies (%1 changed)...").arg(changed)
                                       : tr("Working copies..."));
        for (int i = 0; i < sessions_.size(); ++i) {
            const auto &path = sessions_[i]->path;
            if (!path.isEmpty())
                tabs_->setTabText(
                    i, QFileInfo(path).fileName() +
                           (changedArchives.contains(archiveIdentity(path)) ? " *" : ""));
        }
    });
    editStatus->start();
    QTimer::singleShot(1000, this, [this] {
        if (!QCoreApplication::arguments().contains("--capture") &&
            !QCoreApplication::arguments().contains("--smoke-test") && !edits_->records().isEmpty())
            toast_->notify(tr("Retained edits are available in View > Working copies."));
    });
    updateRecent();
    applyTheme(settings_->value("theme", "system").toString());
    QTimer::singleShot(500, this, &MainWindow::restoreWorkspace);
    connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (settings_->value("theme", "system") == "system")
            applyTheme("system");
    });
}
MainWindow::~MainWindow() {
    queue_.clear();
    for (const auto &reader : readers_) {
        if (reader->operation)
            vynx::cancel(**reader->operation);
        reader->watcher().waitForFinished();
    }
    readers_.clear();
    if (operations_.operation)
        vynx::cancel(**operations_.operation);
    operations_.watcher().waitForFinished();
    session_->password.reset();
}
void MainWindow::chooseOpen() {
    const auto paths =
        QFileDialog::getOpenFileNames(this, tr("Open archives"), {},
                                      tr("Archives (*.zip *.7z *.rar *.tar *.tar.gz *.tgz *.gz "
                                         "*.xz *.bz2 *.zst *.lzma);;All files (*)"));
    for (const auto &path : paths)
        openPath(path);
}
QString MainWindow::askPassword(bool *accepted) {
    return QInputDialog::getText(this, tr("Archive password"),
                                 tr("Password (kept only for this open archive)"),
                                 QLineEdit::Password, {}, accepted);
}
void MainWindow::openPath(const QString &path) { openWithPassword(path, {}); }
void MainWindow::openWithPassword(const QString &path, const QString &password) {
    openWithSecret(path, secret(password));
}
void MainWindow::openWithSecret(const QString &path, const std::shared_ptr<SecretUtf8> &pw,
                                bool reload) {
    if (operations_.busy) {
        handleShellWithSecret(1, {path}, pw);
        return;
    }
    const auto identity = archiveIdentity(path);
    for (int i = 0; i < sessions_.size(); ++i) {
        if (!sessions_[i]->path.isEmpty() && archiveIdentity(sessions_[i]->path) == identity) {
            activateSession(i);
            if (!reload && sessions_[i]->archive)
                return;
            break;
        }
    }
    if (!session_->path.isEmpty() && archiveIdentity(session_->path) != identity) {
        auto next = std::make_shared<ArchiveSession>();
        sessions_.append(next);
        const QSignalBlocker blocker(tabs_);
        tabs_->addTab(QFileInfo(path).fileName());
        bindSession(next);
        tabs_->setCurrentIndex(sessions_.size() - 1);
    }
    session_->path = path;
    tabs_->show();
    tabs_->setTabText(tabs_->currentIndex(), QFileInfo(path).fileName());
    beginOperation();
    activeSpec_ = QJsonObject{{"kind", "open"}, {"sources", QJsonArray{path}}};
    auto op = operations_.operation;
    auto result = std::make_shared<std::shared_ptr<rust::Box<vynx::Archive>>>();
    auto prepared = std::make_shared<std::shared_ptr<PreparedArchive>>();
    auto p = utf8(path);
    runJob(
        tr("Opening archive…"),
        [op, result, prepared, p, pw] {
            *result = std::make_shared<rust::Box<vynx::Archive>>(
                vynx::open_archive(p, pw->bytes(), **op));
            *prepared = ArchiveModel::prepare(vynx::list_entries(***result));
            return QString();
        },
        [this, result, prepared, path, pw] {
            session_->password = pw;
            session_->archive = *result;
            session_->path = path;
            session_->model->loadPrepared(*prepared);
            session_->history.clear();
            search_->clear();
            pages_->setCurrentIndex(1);
            setWindowTitle(tr("%1 — VYNX ARC").arg(QFileInfo(path).fileName()));
            breadcrumb_->setPath(QFileInfo(path).fileName(), {});
            archivePage_->setWritable(writableArchive());
            for (auto *a : archiveActions_)
                a->setEnabled(true);
            updateStatus();
            if (settings_->value("historyEnabled", true).toBool()) {
                QStringList recent = settings_->value("recent").toStringList();
                recent.removeAll(path);
                recent.prepend(path);
                while (recent.size() > 12)
                    recent.removeLast();
                settings_->setValue("recent", recent);
                updateRecent();
            }
        },
        [this, path, pw](QString error) {
            if (error.contains("password", Qt::CaseInsensitive) ||
                (!pw->bytes().empty() && error.startsWith("7Z"))) {
                bool accepted = false;
                auto next = secret(askPassword(&accepted));
                if (accepted)
                    QTimer::singleShot(0, this, [this, path, next] { openWithSecret(path, next); });
            } else
                QMessageBox::warning(this, tr("Could not open archive"), error);
        });
}
void MainWindow::chooseExtract(bool smart, bool here, bool named) {
    if (operations_.busy || session_->busy || !session_->archive)
        return;
    QString defaultDest = QFileInfo(session_->path).absolutePath();
    if (named)
        defaultDest += "/" + QFileInfo(session_->path).completeBaseName();
    const auto selectedRows = table_->selectionModel()->selectedRows();
    ExtractDialog dialog(defaultDest, !selectedRows.isEmpty(), smart, here,
                         session_->password ? session_->password->text() : QString(), this);
    auto *destination = dialog.destination;
    auto *scope = dialog.scope;
    auto *conflicts = dialog.conflicts;
    auto *smartBox = dialog.smartBox;
    auto *password = dialog.password;
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (destination->text().isEmpty())
        return;
    auto ids = std::make_shared<rust::Vec<quint64>>();
    if (scope->currentIndex() == 1) {
        for (auto i : selectedRows) {
            const auto &r = session_->model->row(session_->filter->mapToSource(i).row());
            for (auto id : r.memberIds)
                ids->push_back(id);
        }
    }
    auto pw = secret(password->text());
    password->clear();
    session_->password = pw;
    auto dest = utf8(destination->text());
    const quint8 policies[] = {3, 2, 1, 4, 5, 0};
    auto conflict = policies[conflicts->currentIndex()];
    smart = smartBox->isChecked();
    beginOperation();
    QJsonArray selectedNames;
    if (scope->currentIndex() == 1)
        for (const auto &entry : vynx::list_entries(**session_->archive)) {
            for (auto id : *ids)
                if (entry.id == id)
                    selectedNames.append(
                        QString::fromUtf8(entry.name.data(), qsizetype(entry.name.size())));
        }
    activeSpec_ = QJsonObject{{"kind", "extract"},
                              {"sources", QJsonArray{session_->path}},
                              {"destination", destination->text()},
                              {"conflict", int(conflict)},
                              {"smart", smart},
                              {"names", selectedNames},
                              {"resource", settings_->value("resourceMode", 1).toInt()},
                              {"needsPassword", !pw->bytes().empty()}};

    auto op = operations_.operation;
    auto archive = session_->archive;
    runJob(tr("Extracting…"), [archive, op, ids, dest, pw, conflict, smart] {
        vynx::extract_archive(**archive, dest, rust::Slice<const quint64>(ids->data(), ids->size()),
                              conflict, smart, pw->bytes(), **op);
        return QString();
    });
}
void MainWindow::testArchive() {
    if (operations_.busy || session_->busy || !session_->archive)
        return;
    bool ok = true;
    QString pw = askPassword(&ok);
    if (!ok)
        return;
    beginOperation();
    activeSpec_ = QJsonObject{{"kind", "test"}, {"sources", QJsonArray{session_->path}}};
    auto archive = session_->archive;
    auto op = operations_.operation;
    auto p = secret(pw);
    pw.fill(QChar(0));
    runJob(
        tr("Testing archive…"),
        [archive, op, p] {
            vynx::test_archive(**archive, p->bytes(), **op);
            return QString();
        },
        [this] { toast_->notify(tr("All archive streams decoded successfully.")); });
}
void MainWindow::hashFile() {
    if (operations_.busy || session_->busy || !session_->archive)
        return;
    beginOperation();
    auto op = operations_.operation;
    auto p = utf8(session_->path);
    auto result = std::make_shared<QString>();
    runJob(
        tr("Hashing archive file…"),
        [op, p, result] {
            *result = text(vynx::hash_archive_file(p, **op));
            return QString();
        },
        [this, result] {
            QDialog d(this);
            d.setWindowTitle(tr("Archive hashes"));
            auto *l = new QVBoxLayout(&d);
            auto *t = new QPlainTextEdit(*result);
            t->setReadOnly(true);
            l->addWidget(t);
            auto *b = new QPushButton(tr("Copy hashes"));
            l->addWidget(b);
            connect(b, &QPushButton::clicked, &d, [this, result] {
                QApplication::clipboard()->setText(*result);
                toast_->notify(tr("Hash copied"));
            });
            d.resize(660, 200);
            d.exec();
        });
}
void MainWindow::hashContents(bool verify, bool wholeArchive) {
    if (operations_.busy || session_->busy || !session_->archive)
        return;
    auto ids = std::make_shared<rust::Vec<quint64>>();
    if (!wholeArchive) {
        for (const auto &index : table_->selectionModel()->selectedRows()) {
            const auto &row = session_->model->row(session_->filter->mapToSource(index).row());
            if (row.folder)
                continue;
            for (auto id : row.memberIds)
                ids->push_back(id);
        }
        if (ids->empty() || (verify && ids->size() != 1)) {
            statusBar()->showMessage(tr("Select regular files; verification requires one file."),
                                     6000);
            return;
        }
    }
    std::string expected;
    if (verify) {
        bool accepted = false;
        auto value = QInputDialog::getText(this, tr("Verify hash"),
                                           tr("Expected SHA-256 (64 hex) or CRC32 (8 hex)"),
                                           QLineEdit::Normal, {}, &accepted);
        if (!accepted)
            return;
        expected = utf8(value.trimmed());
    }
    beginOperation();
    auto op = operations_.operation;
    auto archive = session_->archive;
    auto path = utf8(session_->path);
    auto pw = session_->password ? session_->password : secret({});
    auto result = std::make_shared<QString>();
    runJob(
        tr("Calculating hashes…"),
        [archive, ids, op, path, pw, expected, verify, wholeArchive, result] {
            if (verify) {
                if (wholeArchive)
                    *result = text(vynx::verify_file_hash(path, expected, **op));
                else
                    *result = text(
                        vynx::verify_entry_hash(**archive, (*ids)[0], expected, pw->bytes(), **op));
            } else {
                auto hashes = vynx::hash_entries(
                    **archive, rust::Slice<const quint64>(ids->data(), ids->size()), pw->bytes(),
                    **op);
                for (const auto &h : hashes)
                    *result += text(h.name) + "\nSHA-256: " + text(h.sha256) +
                               "\nCRC32: " + text(h.crc32) + "\n\n";
            }
            return QString();
        },
        [this, result, verify] {
            if (verify) {
                auto *report = new QDialog(this);
                report->setAttribute(Qt::WA_DeleteOnClose);
                report->setWindowTitle(tr("Verify hash"));
                auto *layout = new QVBoxLayout(report);
                auto *value = new QPlainTextEdit(*result);
                value->setReadOnly(true);
                layout->addWidget(value);
                auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
                layout->addWidget(buttons);
                connect(buttons, &QDialogButtonBox::rejected, report, &QDialog::reject);
                report->resize(700, 240);
                report->show();
                return;
            }
            QDialog dialog(this);
            dialog.setWindowTitle(tr("Entry hashes"));
            auto *layout = new QVBoxLayout(&dialog);
            auto *report = new QPlainTextEdit(*result);
            report->setReadOnly(true);
            layout->addWidget(report);
            auto *copy = new QPushButton(tr("Copy hashes"));
            layout->addWidget(copy);
            connect(copy, &QPushButton::clicked, &dialog, [this, result] {
                QApplication::clipboard()->setText(*result);
                toast_->notify(tr("Hash copied"));
            });
            dialog.resize(700, 400);
            dialog.exec();
        });
}
void MainWindow::chooseCreate(const QStringList &initial, int initialFormat) {
    CreateArchiveDialog d(initial, initialFormat, this);
    d.resource->setCurrentIndex(qBound(0, settings_->value("resourceMode", 1).toInt(), 2));
    d.preset->setCurrentIndex(qBound(0, settings_->value("compressionPreset", 2).toInt(), 3));
    auto *inputs = d.inputs;
    auto *format = d.format;
    auto *output = d.output;
    auto *password = d.password;
    auto *advanced = d.advanced;
    auto *split = d.split;
    auto *customSplit = d.customSplit;
    if (d.exec() != QDialog::Accepted)
        return;
    auto sources = std::make_shared<rust::Vec<rust::String>>();
    for (int i = 0; i < inputs->count(); ++i)
        sources->push_back(utf8(inputs->item(i)->text()));
    auto out = utf8(output->text());
    auto pw = secret(password->text());
    password->clear();
    const auto selectedFormat = uint8_t(format->currentIndex());
    const quint64 splitSizes[] = {0,          100ULL << 20, 500ULL << 20,
                                  1ULL << 30, 4ULL << 30,   quint64(customSplit->value()) << 20};
    const auto volumeSize =
        selectedFormat == 1 && advanced->isChecked() ? splitSizes[split->currentIndex()] : 0;
    const auto preset = uint8_t(d.preset->currentIndex());
    const auto resource = uint8_t(d.resource->currentIndex());
    settings_->setValue("resourceMode", resource);
    settings_->setValue("compressionPreset", preset);
    QJsonArray sourcePaths;
    for (int i = 0; i < inputs->count(); ++i)
        sourcePaths.append(inputs->item(i)->text());
    const QJsonObject createSpec{{"kind", "create"},
                                 {"sources", sourcePaths},
                                 {"destination", output->text()},
                                 {"format", int(selectedFormat)},
                                 {"volumeSize", double(volumeSize)},
                                 {"preset", int(preset)},
                                 {"resource", int(resource)},
                                 {"needsPassword", !pw->bytes().empty()}};
    auto createJob = [this, sources, out, pw, selectedFormat, volumeSize, preset, resource,
                      createSpec] {
        beginOperation();
        activeSpec_ = createSpec;
        auto op = operations_.operation;
        vynx::configure_operation(**op, preset, resource);
        runJob(tr("Creating archive…"), [sources, out, pw, op, selectedFormat, volumeSize] {
            if (volumeSize)
                vynx::create_split_archive(
                    out, rust::Slice<const rust::String>(sources->data(), sources->size()),
                    volumeSize, pw->bytes(), **op);
            else
                vynx::create_archive_as(
                    out, rust::Slice<const rust::String>(sources->data(), sources->size()),
                    selectedFormat, pw->bytes(), **op);
            return QString();
        });
    };
    if (runningItem_ && !operations_.busy)
        createJob();
    else
        enqueueRequest(tr("Create %1").arg(QFileInfo(output->text()).fileName()),
                       std::move(createJob), output->text(), createSpec);
}
void MainWindow::navigate(const QString &folder) {
    if (session_->busy || !session_->archive)
        return;
    if (folder == session_->model->folder()) {
        search_->clear();
        return;
    }
    session_->history.append(session_->model->folder());
    session_->model->navigate(folder);
    search_->clear();
    breadcrumb_->setPath(QFileInfo(session_->path).fileName(), folder);
    updateStatus();
    table_->setFocus();
}

bool MainWindow::writableArchive() const {
    if (!session_->archive)
        return false;
    return vynx::archive_writable(**session_->archive);
}
void MainWindow::chooseModify(int kind, const QStringList &sources, bool folders) {
    if (operations_.busy || session_->busy || !session_->archive)
        return;
    if (!writableArchive()) {
        statusBar()->showMessage(
            tr("This archive is read only. Modification supports single-file ZIP and 7Z."), 10000);
        return;
    }
    QStringList names = sources;
    if (kind == 0 && names.isEmpty()) {
        if (folders) {
            const auto folder = QFileDialog::getExistingDirectory(this, tr("Source folder"));
            if (!folder.isEmpty())
                names.append(folder);
        } else
            names = QFileDialog::getOpenFileNames(this, tr("Source files"));
    } else if (kind != 0) {
        for (const auto &index : table_->selectionModel()->selectedRows())
            names.append(session_->model->row(session_->filter->mapToSource(index).row()).fullPath);
    }
    if (names.isEmpty()) {
        statusBar()->showMessage(tr("Select files or folders first."), 6000);
        return;
    }
    QString newName;
    if (kind == 2) {
        if (names.size() != 1) {
            statusBar()->showMessage(tr("Select one entry to rename."), 6000);
            return;
        }
        bool accepted = false;
        newName = QInputDialog::getText(this, tr("Rename entry"), tr("New archive path"),
                                        QLineEdit::Normal, names.first(), &accepted);
        if (!accepted || newName == names.first())
            return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Modify archive"));
    auto *form = new QFormLayout(&dialog);
    const auto operation = kind == 0   ? tr("Add files and folders")
                           : kind == 1 ? tr("Delete entries")
                                       : tr("Rename entry");
    auto *summary = new QLabel(
        tr("%1: %2 item(s)\nDestination in archive: %3\nA verified replacement will be published "
           "only after completion.\nDuplicate paths are rejected. Contents are recompressed.")
            .arg(operation)
            .arg(names.size())
            .arg(session_->model->folder().isEmpty() ? "/" : session_->model->folder()));
    summary->setTextFormat(Qt::PlainText);
    summary->setWordWrap(true);
    form->addRow(summary);
    auto *password = new QLineEdit(session_->password ? session_->password->text() : QString());
    password->setEchoMode(QLineEdit::Password);
    form->addRow(tr("Password, if needed"), password);
    auto *preset = new QComboBox;
    preset->addItems({tr("Store / None"), tr("Fast"), tr("Balanced"), tr("Maximum")});
    preset->setCurrentIndex(qBound(0, settings_->value("compressionPreset", 2).toInt(), 3));
    auto *resource = new QComboBox;
    resource->addItems({tr("Eco"), tr("Balanced"), tr("Maximum")});
    resource->setCurrentIndex(qBound(0, settings_->value("resourceMode", 1).toInt(), 2));
    form->addRow(tr("Compression"), preset);
    form->addRow(tr("Resources"), resource);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(operation);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    auto values = std::make_shared<rust::Vec<rust::String>>();
    for (const auto &name : names)
        values->push_back(utf8(name));
    auto archive = session_->archive;
    const auto path = session_->path;
    const auto destination = utf8(session_->model->folder());
    const auto renamed = utf8(newName);
    const auto pw = secret(password->text());
    password->clear();
    beginOperation();
    activeSpec_ = QJsonObject{{"kind", "modify"},
                              {"sources", QJsonArray{path}},
                              {"action", kind},
                              {"names", QJsonArray::fromStringList(names)},
                              {"newName", newName},
                              {"folder", session_->model->folder()},
                              {"preset", 2},
                              {"resource", settings_->value("resourceMode", 1).toInt()},
                              {"needsPassword", !pw->bytes().empty()}};

    auto op = operations_.operation;
    vynx::configure_operation(**op, uint8_t(preset->currentIndex()),
                              uint8_t(resource->currentIndex()));
    activeSpec_["preset"] = preset->currentIndex();
    activeSpec_["resource"] = resource->currentIndex();

    runJob(
        operation,
        [archive, values, kind, destination, renamed, pw, op] {
            vynx::modify_archive(**archive, uint8_t(kind),
                                 rust::Slice<const rust::String>(values->data(), values->size()),
                                 renamed, destination, pw->bytes(), **op);
            return QString();
        },
        [this, path, pw] {
            QTimer::singleShot(0, this, [this, path, pw] { reloadArchive(path, pw); });
        },
        [this, path, pw](QString error) {
            QString message = error;
            if (error.startsWith("ARCHIVE_BUSY:"))
                message = tr("Another application is using this archive. Close it, reload the "
                             "archive, and run the operation again.");
            else if (error.startsWith("ACCESS_DENIED:"))
                message = tr("Access was denied. Check permissions and the read-only attribute, "
                             "then reload the archive before trying again.");
            QMessageBox report(QMessageBox::Warning, tr("Archive was not updated"), message,
                               QMessageBox::Close, this);
            report.setTextFormat(Qt::PlainText);
            if (message != error)
                report.setDetailedText(error);
            auto *reload = report.addButton(tr("Reload archive"), QMessageBox::AcceptRole);
            report.exec();
            if (report.clickedButton() == reload)
                QTimer::singleShot(0, this, [this, path, pw] { reloadArchive(path, pw); });
        });
}
void MainWindow::back() {
    if (session_->busy || session_->history.isEmpty())
        return;
    QString folder = session_->history.takeLast();
    session_->model->navigate(folder);
    breadcrumb_->setPath(QFileInfo(session_->path).fileName(), folder);
    search_->clear();
    updateStatus();
}
void MainWindow::handleShellRequest(quint32 action, const QStringList &paths) {
    handleShellWithSecret(action, paths, secret({}));
}
void MainWindow::handleShellWithSecret(quint32 action, const QStringList &paths,
                                       const std::shared_ptr<SecretUtf8> &pw) {
    if (paths.isEmpty() || paths.size() > 10000 || action < 1 || action > 8)
        return;
    if (operations_.busy) {
        const auto title = tr("Requested operation — %1").arg(QFileInfo(paths.first()).fileName());
        enqueueRequest(
            title, [this, action, paths, pw] { handleShellWithSecret(action, paths, pw); },
            paths.join('\n'),
            QJsonObject{{"kind", "shell"},
                        {"action", int(action)},
                        {"sources", QJsonArray::fromStringList(paths)},
                        {"needsPassword", !pw->bytes().empty()}});
        return;
    }
    if (action == 1) {
        openPath(paths.first());
        if (paths.size() > 1) {
            auto *dialog = new QDialog(this);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->setWindowTitle(tr("Selected archives"));
            auto *layout = new QVBoxLayout(dialog);
            auto *list = new QListView;
            list->setModel(new QStringListModel(paths, list));
            layout->addWidget(list);
            connect(list, &QListView::doubleClicked, this, [this](const QModelIndex &index) {
                if (!operations_.busy)
                    openPath(index.data().toString());
            });
            dialog->resize(680, 350);
            dialog->show();
        }
        return;
    }
    if (action == 6) {
        chooseCreate(paths);
        return;
    }
    beginOperation();
    auto op = operations_.operation;
    auto values = std::make_shared<rust::Vec<rust::String>>();
    for (const auto &path : paths)
        values->push_back(utf8(path));
    if (action >= 7) {
        const QFileInfo first(paths.first());
        const QString name = paths.size() == 1
                                 ? (first.isDir() ? first.fileName() : first.completeBaseName())
                                 : "Archive";
        const quint8 format = action == 7 ? 0 : 1;
        const auto output = outputForFormat(first.absolutePath() + "/" + name, format);
        runJob(
            tr("Creating archive…"),
            [values, op, target = utf8(output), format] {
                vynx::create_archive_as(
                    target, rust::Slice<const rust::String>(values->data(), values->size()), format,
                    "", **op);
                return QString();
            },
            [this, output] { QTimer::singleShot(0, this, [this, output] { openPath(output); }); });
        return;
    }
    runJob(
        tr("Processing selected archives…"),
        [paths, op, action, pw] {
            // A single worker processes this bounded selection sequentially.
            for (const auto &path : paths) {
                auto archive = vynx::open_archive(utf8(path), pw->bytes(), **op);
                if (action == 5) {
                    vynx::test_archive(*archive, pw->bytes(), **op);
                    continue;
                }
                const QFileInfo info(path);
                QString destination = info.absolutePath();
                if (action == 3)
                    destination += "/" + info.completeBaseName();
                vynx::extract_archive(*archive, utf8(destination), {}, 3, action == 4, pw->bytes(),
                                      **op);
            }
            return QString();
        },
        {},
        [this, paths, action](const QString &error) {
            if (error.contains("password", Qt::CaseInsensitive)) {
                bool accepted = false;
                const auto password = secret(askPassword(&accepted));
                if (accepted)
                    QTimer::singleShot(0, this, [this, paths, action, password] {
                        handleShellWithSecret(action, paths, password);
                    });
            } else
                QMessageBox::warning(this, tr("Operation stopped"), error);
        });
}
void MainWindow::up() {
    QString folder = session_->model->folder();
    if (folder.isEmpty())
        return;
    int slash = folder.lastIndexOf('/');
    navigate(slash < 0 ? QString() : folder.left(slash));
}
void MainWindow::updateStatus() {
    if (!session_->archive)
        return;
    const auto rows = table_->selectionModel()->selectedRows();
    archivePage_->back->setEnabled(!session_->busy && !session_->history.isEmpty());
    archivePage_->up->setEnabled(!session_->busy && !session_->model->folder().isEmpty());
    for (auto *action : mutationActions_)
        action->setEnabled(!operations_.busy && !session_->busy && writableArchive());
    renameAction_->setEnabled(!operations_.busy && !session_->busy && writableArchive() &&
                              rows.size() == 1);
    deleteAction_->setEnabled(!operations_.busy && !session_->busy && writableArchive() &&
                              !rows.isEmpty());
    bool regular = false;
    for (const auto &index : rows)
        regular |= !session_->model->row(session_->filter->mapToSource(index).row()).folder;
    hashSelectedAction_->setEnabled(!operations_.busy && regular);
    verifySelectedAction_->setEnabled(!operations_.busy && regular && rows.size() == 1);
    externalAction_->setEnabled(
        !operations_.busy && rows.size() == 1 &&
        !session_->model->row(session_->filter->mapToSource(rows.first()).row()).folder);
    editAction_->setEnabled(!operations_.busy && !session_->busy && writableArchive() && regular &&
                            rows.size() == 1);
    previewAction_->setEnabled(!session_->busy && regular && rows.size() == 1);
    propertiesAction_->setEnabled(!session_->busy && rows.size() == 1);
    auto summary = text(vynx::archive_format(**session_->archive));
    if (!writableArchive())
        summary += tr(" · read only");
    auto packed = session_->model->totalPacked();
    auto total = session_->model->totalSize();
    if (packed)
        summary += tr(" · %1 packed").arg(displaySize(packed));
    if (packed && total && packed <= total)
        summary += tr(" · %1% saved").arg(qRound(100.0 * (1 - double(packed) / double(total))));
    archiveSummary_->setText(summary);
    if (rows.isEmpty())
        statusBar()->showMessage(tr("%1 items · %2")
                                     .arg(session_->filter->rowCount())
                                     .arg(displaySize(session_->model->totalSize())));
    else {
        quint64 size = 0;
        for (auto i : rows)
            size += session_->model->row(session_->filter->mapToSource(i).row()).size;
        statusBar()->showMessage(tr("%1 selected · %2").arg(rows.size()).arg(displaySize(size)));
    }
}
void MainWindow::showProperties() {
    if (session_->busy || !session_->archive)
        return;
    const auto rows = table_->selectionModel()->selectedRows();
    if (rows.size() != 1)
        return;
    const auto &row = session_->model->row(session_->filter->mapToSource(rows.first()).row());
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Properties"));
    dialog.setMinimumWidth(360);
    auto *layout = new QFormLayout(&dialog);
    auto label = [&](const QString &title, const QString &value) {
        auto *text = new QLabel(value);
        text->setTextFormat(Qt::PlainText);
        text->setWordWrap(true);
        text->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addRow(title, text);
    };
    label(tr("Name"), row.name);
    label(tr("Archive path"), row.fullPath);
    if (!row.folder) {
        label(tr("Size"), displaySize(row.size));
        label(tr("Packed"), row.packed ? displaySize(row.packed) : tr("Unknown"));
        label(tr("Modified"),
              row.modifiedKnown
                  ? QLocale().toString(QDateTime::fromSecsSinceEpoch(row.modifiedTime),
                                       QLocale::LongFormat)
                  : tr("Unknown"));
        if (!row.crc.isEmpty())
            label("CRC32", row.crc);
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}
void MainWindow::updateRecent() {
    recent_->clear();
    for (auto p : settings_->value("recent").toStringList()) {
        auto *i = new QListWidgetItem(QFileInfo(p).fileName() + "\n" + p, recent_);
        i->setIcon(Icons::get("archive"));
        i->setData(Qt::UserRole, p);
        i->setToolTip(p);
    }
}
void MainWindow::settings() {
    SettingsDialog d(settings_->value("theme", "system").toString(),
                     settings_->value("language", "en").toString(),
                     settings_->value("historyEnabled", true).toBool(), this);
    d.resource->setCurrentIndex(qBound(0, settings_->value("resourceMode", 1).toInt(), 2));
    auto *theme = d.theme;
    auto *language = d.language;
    auto *history = d.history;
    const QStringList ids{"system", "light", "dark"}, languages{"en", "uk", "ru"};
    if (d.exec() == QDialog::Accepted) {
        settings_->setValue("language", languages[language->currentIndex()]);
        settings_->setValue("theme", ids[theme->currentIndex()]);
        settings_->setValue("resourceMode", d.resource->currentIndex());
        settings_->setValue("historyEnabled", history->isChecked());
        if (!journal_.setEnabled(history->isChecked()))
            QMessageBox::warning(
                this, tr("Queue history"),
                tr("Could not update queue history settings. Close other instances and retry."));
        if (!history->isChecked())
            settings_->remove("workspace");
        if (!history->isChecked())
            settings_->remove("recent");
        updateRecent();
        applyTheme(ids[theme->currentIndex()]);
    }
}
void MainWindow::applyTheme(const QString &theme) {
    applyUiTheme(theme);
    table_->viewport()->update();
}
void MainWindow::dragEnterEvent(QDragEnterEvent *e) {
    if (!operations_.busy && e->mimeData()->hasUrls())
        e->acceptProposedAction();
}
void MainWindow::dropEvent(QDropEvent *e) {
    if (operations_.busy)
        return;
    const auto urls = e->mimeData()->urls();
    if (session_->archive) {
        QStringList sources;
        for (const auto &url : urls) {
            if (!url.isLocalFile())
                return;
            sources.append(url.toLocalFile());
        }
        chooseModify(0, sources);
        e->acceptProposedAction();
        return;
    }
    if (urls.size() == 1 && urls.first().isLocalFile()) {
        session_->password.reset();
        openPath(urls.first().toLocalFile());
        e->acceptProposedAction();
    }
}
void MainWindow::closeEvent(QCloseEvent *e) {
    if (operations_.busy) {
        if (QMessageBox::question(this, tr("Operation in progress"),
                                  tr("Cancel the operation and close after it stops?")) !=
            QMessageBox::Yes) {
            e->ignore();
            return;
        }
        vynx::cancel(**operations_.operation);
        queue_.clear();
        closePending_ = true;
        e->ignore();
        return;
    }
    if (!edits_->records().isEmpty() && !QCoreApplication::arguments().contains("--smoke-test") &&
        !QCoreApplication::arguments().contains("--capture") &&
        QMessageBox::question(this, tr("Working copies"),
                              tr("Keep working copies on disk and close? They can be recovered "
                                 "from View > Working copies next time."),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::Yes) != QMessageBox::Yes) {
        e->ignore();
        return;
    }
    saveWorkspace();
    closePending_ = true;
    queue_.clear();
    e->accept();
}
bool MainWindow::smokeTest() {
    const auto smokeFailure = [](int line) {
        qWarning("GUI smoke failed at source line %d", line);
        return false;
    };
    try {
        QByteArray shellData;
        QDataStream shellStream(&shellData, QIODevice::WriteOnly);
        shellStream.setByteOrder(QDataStream::LittleEndian);
        shellStream << quint32(0x52415856) << quint32(7) << quint32(1000);
        QString shellPath = "C:";
        for (int i = 0; i < 6; ++i)
            shellPath += "/" + QString(60, 'x');
        shellPath += "/дані with spaces.txt";
        for (int i = 0; i < 1000; ++i) {
            shellStream << quint32(shellPath.size());
            for (auto c : shellPath)
                shellStream << c.unicode();
        }
        auto shellRequest = decodeShellRequest(shellData);
        if (shellRequest.action != 7 || shellRequest.paths.size() != 1000 ||
            shellRequest.paths.last() != shellPath)
            return smokeFailure(__LINE__);
        try {
            decodeShellRequest(shellData + 'x');
            return smokeFailure(__LINE__);
        } catch (const std::exception &) {
        }
        if (outputForFormat("archive.zip", 1) != "archive.7z" ||
            outputForFormat("archive.tar.gz", 0) != "archive.zip")
            return smokeFailure(__LINE__);
        applyTheme("light");
        if (qApp->palette().color(QPalette::Window).lightness() < 200)
            return smokeFailure(__LINE__);
        applyTheme("dark");
        if (qApp->palette().color(QPalette::Window).lightness() > 80)
            return smokeFailure(__LINE__);
        applyTheme("system");
        const bool systemDark = qApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark;
        if ((qApp->palette().color(QPalette::Window).lightness() < 80) != systemDark)
            return smokeFailure(__LINE__);
        QTemporaryDir temp;
        if (!temp.isValid())
            return smokeFailure(__LINE__);
        settings_ =
            std::make_unique<QSettings>(temp.path() + "/settings.ini", QSettings::IniFormat);
        settings_->setValue("historyEnabled", false);
        if (!journal_.open(temp.path() + "/smoke-jobs.json", true))
            return smokeFailure(__LINE__);

        const QString source = temp.path() + "/source";
        QDir().mkpath(source + "/nested");
        QFile input(source + "/nested/дані.txt");
        if (!input.open(QIODevice::WriteOnly))
            return smokeFailure(__LINE__);
        input.write("real GUI bridge roundtrip");
        input.close();
        auto op = vynx::new_operation();
        rust::Vec<rust::String> inputs;
        inputs.push_back(utf8(source));
        const QString archivePath = temp.path() + "/sample.zip";
        vynx::create_archive(utf8(archivePath),
                             rust::Slice<const rust::String>(inputs.data(), inputs.size()), "",
                             *op);
        QEventLoop loop;
        connect(&operations_.watcher(), &QFutureWatcher<QString>::finished, &loop,
                &QEventLoop::quit);
        QTimer::singleShot(10000, &loop, &QEventLoop::quit);
        QMimeData droppedArchive;
        droppedArchive.setUrls({QUrl::fromLocalFile(archivePath)});
        QDragEnterEvent drag(QPoint(20, 20), Qt::CopyAction, &droppedArchive, Qt::LeftButton,
                             Qt::NoModifier);
        dragEnterEvent(&drag);
        if (!drag.isAccepted())
            return smokeFailure(__LINE__);
        QDropEvent drop(QPointF(20, 20), Qt::CopyAction, &droppedArchive, Qt::LeftButton,
                        Qt::NoModifier);
        dropEvent(&drop);
        if (!drop.isAccepted())
            return smokeFailure(__LINE__);
        loop.exec();
        if (operations_.busy || !session_->archive || session_->model->rowCount() != 1 ||
            !session_->model->row(0).folder)
            return smokeFailure(__LINE__);
        navigate("source/nested");
        if (session_->model->rowCount() != 1 || session_->model->row(0).name != "дані.txt")
            return smokeFailure(__LINE__);
        search_->setText("ДАНІ");
        if (session_->filter->rowCount() != 1)
            return smokeFailure(__LINE__);
        table_->selectAll();
        if (table_->selectionModel()->selectedRows().size() != 1)
            return smokeFailure(__LINE__);
        vynx::test_archive(**session_->archive, "", *op);
        vynx::extract_archive(**session_->archive, utf8(temp.path() + "/out"), {}, 0, false, "",
                              *op);
        QFile restored(temp.path() + "/out/source/nested/дані.txt");
        if (!restored.open(QIODevice::ReadOnly) ||
            restored.readAll() != "real GUI bridge roundtrip")
            return smokeFailure(__LINE__);
        restored.close();
        bool previewSeen = false;
        QTimer previewCloser;
        connect(&previewCloser, &QTimer::timeout, this, [&] {
            for (auto *viewer : findChildren<QPlainTextEdit *>()) {
                if (viewer->isVisible() && viewer->isReadOnly()) {
                    previewSeen = viewer->toPlainText() == "real GUI bridge roundtrip";
                    viewer->window()->close();
                }
            }
        });
        previewCloser.start(20);
        previewSelected();
        loop.exec();
        previewCloser.stop();
        if (operations_.busy || !previewSeen)
            return smokeFailure(__LINE__);
        // Exercise the real asynchronous conflict dialog and bridge reply.
        beginOperation();
        auto conflictOp = operations_.operation;
        auto conflictArchive = session_->archive;
        bool conflictDialogSeen = false;
        QTimer answer;
        connect(&answer, &QTimer::timeout, this, [&] {
            for (auto *dialog : findChildren<QDialog *>()) {
                if (dialog->windowTitle() != tr("File already exists"))
                    continue;
                for (auto *box : dialog->findChildren<QCheckBox *>())
                    box->setChecked(true);
                for (auto *button : dialog->findChildren<QPushButton *>()) {
                    if (button->text() == tr("Skip")) {
                        conflictDialogSeen = true;
                        button->click();
                        return;
                    }
                }
            }
        });
        answer.start(50);
        runJob(tr("Extracting…"), [conflictArchive, conflictOp,
                                   destination = utf8(temp.path() + "/out")] {
            vynx::extract_archive(**conflictArchive, destination, {}, 3, false, "", **conflictOp);
            return QString();
        });
        loop.exec();
        answer.stop();
        if (operations_.busy || !conflictDialogSeen || vynx::conflict_request(**conflictOp).id)
            return smokeFailure(__LINE__);
        rust::Vec<rust::String> rename;
        rename.push_back("source/nested/дані.txt");
        vynx::modify_archive(**session_->archive, 2,
                             rust::Slice<const rust::String>(rename.data(), rename.size()),
                             "source/nested/renamed.txt", "", "", *op);
        openWithSecret(archivePath, secret({}), true);
        loop.exec();
        if (operations_.busy || session_->busy || !session_->archive)
            return smokeFailure(__LINE__);
        navigate("source/nested");
        if (session_->model->rowCount() != 1 || session_->model->row(0).name != "renamed.txt")
            return smokeFailure(__LINE__);
        rust::Vec<quint64> hashIds;
        hashIds.push_back(session_->model->row(0).memberIds.front());
        auto hashes =
            vynx::hash_entries(**session_->archive,
                               rust::Slice<const quint64>(hashIds.data(), hashIds.size()), "", *op);
        if (hashes.size() != 1 || hashes[0].sha256.size() != 64 || hashes[0].crc32.size() != 8)
            return smokeFailure(__LINE__);
        vynx::verify_entry_hash(**session_->archive, hashIds[0], hashes[0].sha256, "", *op);
        auto jobSecret = secret("private");
        std::weak_ptr<SecretUtf8> secretLifetime = jobSecret;
        const auto encryptedPath = utf8(temp.path() + "/encrypted.zip");
        vynx::create_archive(encryptedPath,
                             rust::Slice<const rust::String>(inputs.data(), inputs.size()),
                             jobSecret->bytes(), *op);
        beginOperation();
        auto passwordOp = operations_.operation;
        runJob(tr("Testing archive…"), [jobSecret, encryptedPath, passwordOp] {
            auto encrypted = vynx::open_archive(encryptedPath, jobSecret->bytes(), **passwordOp);
            vynx::test_archive(*encrypted, jobSecret->bytes(), **passwordOp);
            return QString();
        });
        jobSecret.reset();
        loop.exec();
        if (operations_.busy || !secretLifetime.expired())
            return smokeFailure(__LINE__);
        // Exercise real dispatch, visible state and pending cancellation while a job runs.
        beginOperation();
        runJob(tr("Queue control"), [] { return QString(); });
        handleShellRequest(5, {archivePath});
        auto *firstQueued = queueView_->item(queueView_->count() - 1);
        handleShellRequest(5, {temp.path() + "/must-not-run.zip"});
        auto *cancelledQueued = queueView_->item(queueView_->count() - 1);
        handleShellRequest(5, {archivePath});
        auto *lastQueued = queueView_->item(queueView_->count() - 1);
        if (queue_.size() != 3 || !firstQueued->text().startsWith(tr("Pending")))
            return smokeFailure(__LINE__);
        queueView_->setCurrentItem(cancelledQueued);
        cancelPending_->trigger();
        if (queue_.size() != 2 || !cancelledQueued->text().startsWith(tr("Cancelled")))
            return smokeFailure(__LINE__);
        QElapsedTimer queueDeadline;
        queueDeadline.start();
        do {
            QTimer::singleShot(10000, &loop, &QEventLoop::quit);
            loop.exec();
        } while ((operations_.busy || queue_.size()) && queueDeadline.elapsed() < 15000);
        if (operations_.busy || queue_.size())
            return smokeFailure(__LINE__);
        if (!firstQueued->text().startsWith(tr("Completed")) ||
            !lastQueued->text().startsWith(tr("Completed")))
            return smokeFailure(__LINE__);
        // Real batch dispatch: paused jobs do not start, one failure does not stop siblings.
        auto *pauseQueue = findChild<QAction *>("pauseQueue");
        if (!pauseQueue)
            return smokeFailure(__LINE__);
        pauseQueue->setChecked(true);
        const QStringList batchPaths{archivePath, temp.path() + "/missing.zip", archivePath};
        const auto batchRoot = temp.path() + "/batch";
        const auto batchOutputs = batchDestinations(batchPaths, batchRoot);
        enqueueBatch(batchPaths, batchRoot, 4, secret({}));
        QCoreApplication::processEvents();
        if (operations_.busy || queue_.size() != 3 || QDir(batchRoot).exists())
            return smokeFailure(__LINE__);
        pauseQueue->setChecked(false);
        queueDeadline.restart();
        do {
            QTimer::singleShot(10000, &loop, &QEventLoop::quit);
            loop.exec();
        } while ((operations_.busy || queue_.size()) && queueDeadline.elapsed() < 15000);
        if (operations_.busy || queue_.size())
            return smokeFailure(__LINE__);
        for (const auto index : {0, 2}) {
            QFile file(batchOutputs[index] + "/source/nested/renamed.txt");
            if (!file.open(QIODevice::ReadOnly) || file.readAll() != "real GUI bridge roundtrip")
                return smokeFailure(__LINE__);
        }
        bool batchFailureRecorded = false;
        for (int i = 0; i < queueView_->count(); ++i)
            batchFailureRecorded |= queueView_->item(i)->text().startsWith(tr("Stopped")) &&
                                    queueView_->item(i)->toolTip().contains("missing.zip");
        if (!batchFailureRecorded)
            return smokeFailure(__LINE__);
        // Three real documents retain independent state while the write lane is busy.
        auto firstSession = session_;
        const auto secondPath = temp.path() + "/second.zip";
        if (!QFile::copy(archivePath, secondPath))
            return smokeFailure(__LINE__);
        openPath(secondPath);
        loop.exec();
        auto secondSession = session_;
        openWithSecret(QString::fromUtf8(encryptedPath.data(), qsizetype(encryptedPath.size())),
                       secret("private"));
        loop.exec();
        auto encryptedSession = session_;
        const auto selectionModels = table_->findChildren<QItemSelectionModel *>().size();
        for (int i = 0; i < 60; ++i)
            activateSession(i % sessions_.size());
        activateSession(sessions_.indexOf(encryptedSession));
        if (table_->findChildren<QItemSelectionModel *>().size() > selectionModels)
            return smokeFailure(__LINE__);
        if (sessions_.size() != 3 || encryptedSession->password->bytes() != "private" ||
            secondSession == firstSession)
            return smokeFailure(__LINE__);
        activateSession(sessions_.indexOf(firstSession));
        navigate("source/nested");
        search_->setText("renamed.txt");
        table_->selectRow(0);
        activateSession(sessions_.indexOf(secondSession));
        search_->setText("source/nested");
        activateSession(sessions_.indexOf(firstSession));
        if (search_->text() != "renamed.txt" ||
            table_->selectionModel()->selectedRows().size() != 1)
            return smokeFailure(__LINE__);
        const int tabCount = tabs_->count();
        openPath(archivePath);
        if (tabs_->count() != tabCount)
            return smokeFailure(__LINE__);
        auto release = std::make_shared<std::atomic<bool>>(false);
        beginOperation();
        runJob(tr("Session ownership test"), [release] {
            QElapsedTimer deadline;
            deadline.start();
            while (!release->load() && deadline.elapsed() < 5000)
                QThread::msleep(1);
            return QString();
        });
        activateSession(sessions_.indexOf(secondSession));
        search_->setText("renamed.txt");
        table_->selectRow(0);
        if (!archivePage_->isEnabled() || !firstSession->busy) {
            release->store(true);
            return smokeFailure(__LINE__);
        }
        previewSeen = false;
        previewCloser.start(20);
        previewSelected();
        QElapsedTimer readDeadline;
        readDeadline.start();
        while (!readers_.isEmpty() && readDeadline.elapsed() < 4000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            QThread::msleep(1);
        }
        previewCloser.stop();
        release->store(true);
        if (!previewSeen || !readers_.isEmpty())
            return smokeFailure(__LINE__);
        if (operations_.busy)
            loop.exec();
        if (session_ != secondSession || firstSession->busy)
            return smokeFailure(__LINE__);
        activateSession(sessions_.indexOf(firstSession));
        if (search_->text() != "renamed.txt")
            return smokeFailure(__LINE__);
        closeSession(sessions_.indexOf(encryptedSession));
        closeSession(sessions_.indexOf(secondSession));
        // A callback may start another job; finishing the old job cannot clear its owner.
        beginOperation();
        runJob(
            tr("Session ownership test"), [] { return QString(); },
            [this] {
                beginOperation();
                runJob(tr("Session ownership test"), [] { return QString(); });
            });
        loop.exec();
        QElapsedTimer handoffDeadline;
        handoffDeadline.start();
        while (operations_.busy && handoffDeadline.elapsed() < 10000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            QThread::msleep(1);
        }
        if (operations_.busy || session_->busy)
            return smokeFailure(__LINE__);
        // The GUI edit workflow uses the same transactional bridge as normal saves.
        const auto editingId = session_->model->row(0).id;
        prepareEdit(editingId, "source/nested/renamed.txt", false);
        loop.exec();
        if (operations_.busy || edits_->records().size() != 1)
            return smokeFailure(__LINE__);
        const auto edit = edits_->records()[0].toObject();
        const auto editKey = edit["id"].toString();
        QFile working(edit["file"].toString());
#ifdef Q_OS_WIN
        // A separate editor process writes the copy; the GUI must reread its bytes.
        QString quotedPath = working.fileName();
        quotedPath.replace("'", "''");
        const auto script =
            QString("[IO.File]::WriteAllBytes('%1',[Convert]::FromBase64String('%2'))")
                .arg(quotedPath, QString::fromLatin1(QByteArray("GUI edited content").toBase64()));
        const auto encoded =
            QByteArray(reinterpret_cast<const char *>(script.utf16()), script.size() * 2)
                .toBase64();
        QProcess editor;
        editor.setCreateProcessArgumentsModifier(
            [](QProcess::CreateProcessArguments *args) { args->flags |= 0x08000000; });
        editor.start(qEnvironmentVariable("SystemRoot") +
                         "/System32/WindowsPowerShell/v1.0/powershell.exe",
                     {"-NoLogo", "-NoProfile", "-NonInteractive", "-EncodedCommand",
                      QString::fromLatin1(encoded)});
        if (!editor.waitForFinished(10000) || editor.exitCode() != 0)
            return smokeFailure(__LINE__);
#else
        if (!working.open(QIODevice::WriteOnly))
            return smokeFailure(__LINE__);
        working.write("GUI edited content");
        working.close();
#endif
        auto beforeReload = session_->archive;
        saveWorkingCopy(editKey, secret({}));
        loop.exec();
        QElapsedTimer reloadDeadline;
        reloadDeadline.start();
        while ((session_->archive == beforeReload || operations_.busy || queue_.size()) &&
               reloadDeadline.elapsed() < 10000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            QThread::msleep(1);
        }
        if (session_->archive == beforeReload || operations_.busy ||
            session_->model->searchMode() != !search_->text().isEmpty() ||
            edits_->record(editKey)["state"] != "saved")
            return smokeFailure(__LINE__);
        auto verifyEdited = vynx::open_archive(utf8(archivePath), "", *op);
        auto editedBytes = vynx::preview_bytes(*verifyEdited, editingId, "", *op);
        if (QByteArray(reinterpret_cast<const char *>(editedBytes.data()),
                       qsizetype(editedBytes.size())) != "GUI edited content")
            return smokeFailure(__LINE__);
        if (!working.open(QIODevice::WriteOnly))
            return smokeFailure(__LINE__);
        working.write("unsaved second edit");
        working.close();
        const auto savedAsPath = temp.path() + "/edited-as-new.7z";
        const QJsonObject saveAsSpec{{"kind", "convert"},
                                     {"sources", QJsonArray{archivePath}},
                                     {"destination", savedAsPath},
                                     {"format", 1},
                                     {"preset", 2},
                                     {"resource", 1},
                                     {"allowDecrypt", true},
                                     {"editId", editKey}};
        enqueueRequest(
            "Save as smoke",
            [this, saveAsSpec] { dispatchConversionSpec(saveAsSpec, secret({}), secret({})); }, {},
            saveAsSpec);
        loop.exec();
        if (!QFileInfo::exists(savedAsPath) ||
            text(vynx::hash_archive_file(utf8(archivePath), *op)) !=
                edits_->record(editKey)["archiveHash"].toString())
            return smokeFailure(__LINE__);
        auto savedAsArchive = vynx::open_archive(utf8(savedAsPath), "", *op);
        bool foundSavedEdit = false;
        for (const auto &entry : vynx::list_entries(*savedAsArchive))
            if (text(entry.name) == edit["entry"].toString()) {
                const auto bytes = vynx::preview_bytes(*savedAsArchive, entry.id, "", *op);
                foundSavedEdit = QByteArray(reinterpret_cast<const char *>(bytes.data()),
                                            qsizetype(bytes.size())) == "unsaved second edit";
            }
        if (!foundSavedEdit || !edits_->validPayload(edits_->record(editKey)))
            return smokeFailure(__LINE__);

        rust::Vec<rust::String> externalRename;
        externalRename.push_back("source/nested/renamed.txt");
        vynx::modify_archive(*verifyEdited, 2, {externalRename.data(), externalRename.size()},
                             "source/nested/external.txt", "", "", *op);
        const auto changedHash = text(vynx::hash_archive_file(utf8(archivePath), *op));
        QTimer editErrorCloser;
        connect(&editErrorCloser, &QTimer::timeout, this, [this] {
            for (auto *message : findChildren<QMessageBox *>())
                if (message->isVisible())
                    message->accept();
        });
        editErrorCloser.start(20);
        saveWorkingCopy(editKey, secret({}));
        loop.exec();
        editErrorCloser.stop();
        if (edits_->record(editKey)["state"] != "needs-review" ||
            text(vynx::hash_archive_file(utf8(archivePath), *op)) != changedHash)
            return smokeFailure(__LINE__);
        if (!working.open(QIODevice::ReadOnly) || working.readAll() != "unsaved second edit")
            return smokeFailure(__LINE__);
        working.close();
        if (!edits_->discard(editKey))
            return smokeFailure(__LINE__);
        // Action bars must stay inside a narrow window while long content scrolls.
        showWorkingCopies();
        resize(620, 440);
        show();
        QCoreApplication::processEvents();
        for (const auto &name : {QString("workingCopyControls"), QString("queueControls")}) {
            const auto *bar = findChild<QToolBar *>(name);
            if (!bar || !bar->isVisible() ||
                !rect().contains(QRect(bar->mapTo(this, QPoint()), bar->size())))
                return smokeFailure(__LINE__);
        }
        if (queueView_->count()) {
            queueView_->setCurrentItem(queueView_->item(0));
            queueFilter_->setCurrentIndex(1);
            if (queueView_->currentItem() && queueView_->currentItem()->isHidden())
                return smokeFailure(__LINE__);
            queueFilter_->setCurrentIndex(0);
        }
        hide();
        // Large model/view metadata is test input only, never a simulated product archive.
        rust::Vec<vynx::EntryInfo> many;
        for (quint64 i = 0; i < 100000; ++i) {
            vynx::EntryInfo e;
            e.id = i;
            e.name = utf8(QString("file_%1.txt").arg(i));
            e.size = i;
            many.push_back(std::move(e));
        }
        session_->model->load(std::move(many));
        search_->clear();
        if (session_->model->rowCount() != 100000)
            return smokeFailure(__LINE__);
        auto clone = vynx::clone_operation(*op);
        vynx::cancel(*clone);
        try {
            vynx::hash_archive_file(utf8(archivePath), *op);
            return smokeFailure(__LINE__);
        } catch (const std::exception &e) {
            if (!QString::fromUtf8(e.what()).contains("CANCELLED"))
                return smokeFailure(__LINE__);
        }
        return true;
    } catch (const std::exception &e) {
        qWarning("GUI smoke failure: %s", e.what());
        return smokeFailure(__LINE__);
    }
}
