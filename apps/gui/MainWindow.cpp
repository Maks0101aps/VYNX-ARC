#include "MainWindow.h"
#include "FormatUtils.h"
#include "SecretUtf8.h"
#include "ShellRequest.h"
#include "Theme.h"
#include "dialogs/AboutDialog.h"
#include "dialogs/ConflictDialog.h"
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
#include <QtConcurrent>
#include <QtWidgets>

static QString text(const rust::String &s) {
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}
static std::string utf8(const QString &s) {
    auto b = s.toUtf8();
    return std::string(b.constData(), size_t(b.size()));
}
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
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
    idleActions_ = {open, create};
    fileMenu->addSeparator();
    addAction(fileMenu, tr("Close archive"), QKeySequence("Ctrl+W"), [this] {
        if (busy_)
            return;
        archive_.reset();
        password_.reset();
        archivePath_.clear();
        pages_->setCurrentIndex(0);
        setWindowTitle(tr("VYNX ARC"));
        for (auto *a : archiveActions_)
            a->setEnabled(false);
        statusBar()->clearMessage();
        archiveSummary_->clear();
    });
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
    archiveActions_.append(
        addAction(operationsMenu, tr("Hash selected files"), {}, [this] { hashContents(); }));
    archiveActions_.append(addAction(operationsMenu, tr("Verify selected file hash…"), {},
                                     [this] { hashContents(true); }));
    archiveActions_.append(addAction(operationsMenu, tr("Verify archive file hash…"), {},
                                     [this] { hashContents(true, true); }));
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
    pages_->addWidget(home);
    auto *properties = addAction(operationsMenu, tr("Properties"), QKeySequence("Alt+Return"),
                                 [this] { showProperties(); });
    archiveActions_.append(properties);
    properties->setEnabled(false);
    archivePage_ =
        new ArchivePage(extract, addFiles, test, remove, rename, archiveActions_[6],
                        {smart, here, named, hash, archiveActions_[6], archiveActions_[7],
                         archiveActions_[8], addFiles, addFolder, rename, remove, properties});
    pages_->addWidget(archivePage_);
    connect(archivePage_->back, &QToolButton::clicked, this, &MainWindow::back);
    connect(archivePage_->up, &QToolButton::clicked, this, &MainWindow::up);
    breadcrumb_ = archivePage_->breadcrumb;
    search_ = archivePage_->search;
    table_ = archivePage_->table;
    connect(breadcrumb_, &BreadcrumbBar::navigateRequested, this, &MainWindow::navigate);
    model_ = new ArchiveModel(this);
    filter_ = new ArchiveFilter(this);
    filter_->setSourceModel(model_);
    filter_->setFilterCaseSensitivity(Qt::CaseInsensitive);
    filter_->setFilterKeyColumn(0);
    table_->setModel(filter_);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->sortByColumn(0, Qt::AscendingOrder);
    table_->setColumnWidth(1, 100);
    table_->setColumnWidth(2, 100);
    table_->setColumnWidth(3, 70);
    table_->setColumnWidth(4, 80);
    table_->setColumnWidth(5, 140);
    table_->setColumnHidden(6, true);
    mutationActions_ = {addFiles, addFolder, rename, remove};
    renameAction_ = rename;
    deleteAction_ = remove;
    archiveSummary_ = new QLabel;
    archiveSummary_->setObjectName("muted");
    statusBar()->addPermanentWidget(archiveSummary_);
    toast_ = new ToastOverlay(central);
    connect(search_, &QLineEdit::textChanged, this, [this](const QString &s) {
        if (s.contains('*') || s.contains('?'))
            filter_->setFilterRegularExpression(
                QRegularExpression(QRegularExpression::wildcardToRegularExpression(
                                       s, QRegularExpression::UnanchoredWildcardConversion),
                                   QRegularExpression::CaseInsensitiveOption));
        else
            filter_->setFilterFixedString(s);
    });
    connect(table_, &QTableView::doubleClicked, this, [this](QModelIndex i) {
        const auto &r = model_->row(filter_->mapToSource(i).row());
        if (r.folder)
            navigate(r.fullPath);
        else
            showProperties();
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
            const auto &r = model_->row(filter_->mapToSource(rows.first()).row());
            if (r.folder)
                navigate(r.fullPath);
            else
                showProperties();
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
                    model_->row(filter_->mapToSource(rows.first()).row()).folder)
                    connect(menu.addAction(tr("Open folder")), &QAction::triggered, this,
                            [this, index] {
                                navigate(model_->row(filter_->mapToSource(index).row()).fullPath);
                            });
                menu.addAction(extract);
                menu.addAction(archiveActions_[6]);
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
        auto *a =
            columns->addAction(model_->headerData(c, Qt::Horizontal, Qt::DisplayRole).toString());
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

    operationView_ = new OperationPanel;
    operationPanel_ = operationView_;
    current_ = operationView_->current;
    rate_ = operationView_->rate;
    progress_ = operationView_->progress;
    cancel_ = operationView_->cancel;
    layout->addWidget(operationPanel_);
    connect(cancel_, &QPushButton::clicked, this, [this] {
        if (operation_) {
            vynx::cancel(**operation_);
            cancel_->setEnabled(false);
            current_->setText(tr("Cancelling…"));
        }
    });
    timer_ = new QTimer(this);
    timer_->setInterval(100);
    connect(timer_, &QTimer::timeout, this, [this] {
        if (!operation_)
            return;
        auto p = vynx::progress(**operation_);
        operationView_->updateProgress(p.done, p.total, text(p.current), elapsed_.elapsed());
        showConflict();
    });
    connect(&watcher_, &QFutureWatcher<QString>::finished, this, [this] {
        timer_->stop();
        busy_ = false;
        operationPanel_->hide();
        pages_->setEnabled(true);
        for (auto *a : idleActions_)
            a->setEnabled(true);
        for (auto *a : archiveActions_)
            a->setEnabled(bool(archive_));
        if (archive_)
            updateStatus();
        QString error = watcher_.result();
        auto success = std::move(success_);
        auto failure = std::move(failure_);
        operation_.reset();
        if (closePending_) {
            QTimer::singleShot(0, this, [this] { close(); });
            return;
        }
        if (error.isEmpty()) {
            if (success)
                success();
            toast_->notify(tr("Operation completed"));
            updateStatus();
        } else if (failure) {
            failure(error);
        } else
            QMessageBox::warning(this, tr("Operation stopped"), error);
    });
    updateRecent();
    applyTheme(settings_->value("theme", "system").toString());
    connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (settings_->value("theme", "system") == "system")
            applyTheme("system");
    });
}
MainWindow::~MainWindow() {
    if (operation_)
        vynx::cancel(**operation_);
    watcher_.waitForFinished();
    password_.reset();
}
void MainWindow::beginOperation() {
    shownConflict_ = 0;
    operation_ = std::make_shared<rust::Box<vynx::Operation>>(vynx::new_operation());
}
void MainWindow::runJob(const QString &title, std::function<QString()> worker,
                        std::function<void()> success, std::function<void(QString)> failure) {
    if (busy_)
        return;
    busy_ = true;
    success_ = std::move(success);
    failure_ = std::move(failure);
    pages_->setEnabled(false);
    for (auto *a : idleActions_)
        a->setEnabled(false);
    for (auto *a : archiveActions_)
        a->setEnabled(false);
    operationView_->begin(title);
    cancel_->setEnabled(true);
    progress_->setRange(0, 0);
    elapsed_.start();
    timer_->start();
    watcher_.setFuture(QtConcurrent::run([worker = std::move(worker)]() mutable {
        QString result;
        try {
            result = worker();
        } catch (const std::exception &e) {
            result = QString::fromUtf8(e.what());
        } catch (...) {
            result = QStringLiteral("Unexpected worker failure");
        }
        worker = {}; // Release job captures even if the QFuture outlives completion.
        return result;
    }));
}
void MainWindow::chooseOpen() {
    if (busy_)
        return;
    QString p = QFileDialog::getOpenFileName(
        this, tr("Open archive"), {},
        tr("Archives (*.zip *.7z *.rar *.tar *.tar.gz *.tgz);;All files (*)"));
    if (!p.isEmpty())
        openPath(p);
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
void MainWindow::openWithSecret(const QString &path, const std::shared_ptr<SecretUtf8> &pw) {
    if (busy_)
        return;
    beginOperation();
    auto op = operation_;
    auto result = std::make_shared<std::shared_ptr<rust::Box<vynx::Archive>>>();
    auto entries = std::make_shared<rust::Vec<vynx::EntryInfo>>();
    auto p = utf8(path);
    runJob(
        tr("Opening archive…"),
        [op, result, entries, p, pw] {
            *result = std::make_shared<rust::Box<vynx::Archive>>(
                vynx::open_archive(p, pw->bytes(), **op));
            *entries = vynx::list_entries(***result);
            return QString();
        },
        [this, result, entries, path, pw] {
            password_ = pw;
            archive_ = *result;
            archivePath_ = path;
            model_->load(std::move(*entries));
            history_.clear();
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
    if (busy_ || !archive_)
        return;
    QString defaultDest = QFileInfo(archivePath_).absolutePath();
    if (named)
        defaultDest += "/" + QFileInfo(archivePath_).completeBaseName();
    const auto selectedRows = table_->selectionModel()->selectedRows();
    ExtractDialog dialog(defaultDest, !selectedRows.isEmpty(), smart, here,
                         password_ ? password_->text() : QString(), this);
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
            const auto &r = model_->row(filter_->mapToSource(i).row());
            for (auto id : r.memberIds)
                ids->push_back(id);
        }
    }
    auto pw = secret(password->text());
    password->clear();
    password_ = pw;
    auto dest = utf8(destination->text());
    const quint8 policies[] = {3, 2, 1, 4, 5, 0};
    auto conflict = policies[conflicts->currentIndex()];
    smart = smartBox->isChecked();
    beginOperation();
    auto op = operation_;
    auto archive = archive_;
    runJob(tr("Extracting…"), [archive, op, ids, dest, pw, conflict, smart] {
        vynx::extract_archive(**archive, dest, rust::Slice<const quint64>(ids->data(), ids->size()),
                              conflict, smart, pw->bytes(), **op);
        return QString();
    });
}
void MainWindow::showConflict() {
    if (!operation_)
        return;
    auto request = vynx::conflict_request(**operation_);
    if (!request.id || request.id == shownConflict_)
        return;
    shownConflict_ = request.id;
    auto op = operation_;
    auto *dialog = new ConflictDialog(text(request.existing_path), request.existing_size,
                                      request.existing_modified, text(request.incoming_name),
                                      request.incoming_size, request.incoming_modified, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::ApplicationModal);
    connect(dialog, &ConflictDialog::decision, dialog,
            [op, id = request.id](quint8 choice, bool all) {
                vynx::reply_conflict(**op, id, choice, all);
            });
    connect(dialog, &QDialog::finished, dialog,
            [op, id = request.id] { vynx::reply_conflict(**op, id, 0, false); });
    connect(&watcher_, &QFutureWatcher<QString>::finished, dialog, &QDialog::reject);
    auto *cancelTimer = new QTimer(dialog);
    connect(cancelTimer, &QTimer::timeout, dialog, [dialog, op] {
        if (vynx::cancelled(**op))
            dialog->reject();
    });
    cancelTimer->start(100);
    dialog->open();
}
void MainWindow::testArchive() {
    if (busy_ || !archive_)
        return;
    bool ok = true;
    QString pw = askPassword(&ok);
    if (!ok)
        return;
    beginOperation();
    auto archive = archive_;
    auto op = operation_;
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
    if (busy_ || !archive_)
        return;
    beginOperation();
    auto op = operation_;
    auto p = utf8(archivePath_);
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
    if (busy_ || !archive_)
        return;
    auto ids = std::make_shared<rust::Vec<quint64>>();
    if (!wholeArchive) {
        for (const auto &index : table_->selectionModel()->selectedRows()) {
            const auto &row = model_->row(filter_->mapToSource(index).row());
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
    auto op = operation_;
    auto archive = archive_;
    auto path = utf8(archivePath_);
    auto pw = password_ ? password_ : secret({});
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
    if (busy_)
        return;
    CreateArchiveDialog d(initial, initialFormat, this);
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
    beginOperation();
    auto op = operation_;
    runJob(tr("Creating archive…"), [sources, out, pw, op, selectedFormat, volumeSize] {
        if (volumeSize)
            vynx::create_split_archive(
                out, rust::Slice<const rust::String>(sources->data(), sources->size()), volumeSize,
                pw->bytes(), **op);
        else
            vynx::create_archive_as(
                out, rust::Slice<const rust::String>(sources->data(), sources->size()),
                selectedFormat, pw->bytes(), **op);
        return QString();
    });
}
void MainWindow::navigate(const QString &folder) {
    if (busy_ || !archive_)
        return;
    if (folder == model_->folder())
        return;
    history_.append(model_->folder());
    model_->navigate(folder);
    search_->clear();
    breadcrumb_->setPath(QFileInfo(archivePath_).fileName(), folder);
    updateStatus();
    table_->setFocus();
}
bool MainWindow::writableArchive() const {
    if (!archive_)
        return false;
    return vynx::archive_writable(**archive_);
}
void MainWindow::chooseModify(int kind, const QStringList &sources, bool folders) {
    if (busy_ || !archive_)
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
            names.append(model_->row(filter_->mapToSource(index).row()).fullPath);
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
            .arg(model_->folder().isEmpty() ? "/" : model_->folder()));
    summary->setTextFormat(Qt::PlainText);
    summary->setWordWrap(true);
    form->addRow(summary);
    auto *password = new QLineEdit(password_ ? password_->text() : QString());
    password->setEchoMode(QLineEdit::Password);
    form->addRow(tr("Password, if needed"), password);
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
    auto archive = archive_;
    const auto path = archivePath_;
    const auto destination = utf8(model_->folder());
    const auto renamed = utf8(newName);
    const auto pw = secret(password->text());
    password->clear();
    beginOperation();
    auto op = operation_;
    runJob(
        operation,
        [archive, values, kind, destination, renamed, pw, op] {
            vynx::modify_archive(**archive, uint8_t(kind),
                                 rust::Slice<const rust::String>(values->data(), values->size()),
                                 renamed, destination, pw->bytes(), **op);
            return QString();
        },
        [this, path, pw] {
            QTimer::singleShot(0, this, [this, path, pw] { openWithSecret(path, pw); });
        });
}
void MainWindow::back() {
    if (busy_ || history_.isEmpty())
        return;
    QString folder = history_.takeLast();
    model_->navigate(folder);
    breadcrumb_->setPath(QFileInfo(archivePath_).fileName(), folder);
    search_->clear();
    updateStatus();
}
void MainWindow::handleShellRequest(quint32 action, const QStringList &paths) {
    handleShellWithSecret(action, paths, secret({}));
}
void MainWindow::handleShellWithSecret(quint32 action, const QStringList &paths,
                                       const std::shared_ptr<SecretUtf8> &pw) {
    if (busy_ || paths.isEmpty() || paths.size() > 10000 || action < 1 || action > 8)
        return;
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
                if (!busy_)
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
    auto op = operation_;
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
    QString folder = model_->folder();
    if (folder.isEmpty())
        return;
    int slash = folder.lastIndexOf('/');
    navigate(slash < 0 ? QString() : folder.left(slash));
}
void MainWindow::updateStatus() {
    if (!archive_)
        return;
    const auto rows = table_->selectionModel()->selectedRows();
    archivePage_->back->setEnabled(!busy_ && !history_.isEmpty());
    archivePage_->up->setEnabled(!busy_ && !model_->folder().isEmpty());
    for (auto *action : mutationActions_)
        action->setEnabled(!busy_ && writableArchive());
    renameAction_->setEnabled(!busy_ && writableArchive() && rows.size() == 1);
    deleteAction_->setEnabled(!busy_ && writableArchive() && !rows.isEmpty());
    bool regular = false;
    for (const auto &index : rows)
        regular |= !model_->row(filter_->mapToSource(index).row()).folder;
    archiveActions_[6]->setEnabled(!busy_ && regular);
    archiveActions_[7]->setEnabled(!busy_ && regular && rows.size() == 1);
    archiveActions_.last()->setEnabled(!busy_ && rows.size() == 1);
    auto summary = text(vynx::archive_format(**archive_));
    if (!writableArchive())
        summary += tr(" · read only");
    auto packed = model_->totalPacked();
    auto total = model_->totalSize();
    if (packed)
        summary += tr(" · %1 packed").arg(displaySize(packed));
    if (packed && total && packed <= total)
        summary += tr(" · %1% saved").arg(qRound(100.0 * (1 - double(packed) / double(total))));
    archiveSummary_->setText(summary);
    if (rows.isEmpty())
        statusBar()->showMessage(
            tr("%1 items · %2").arg(filter_->rowCount()).arg(displaySize(model_->totalSize())));
    else {
        quint64 size = 0;
        for (auto i : rows)
            size += model_->row(filter_->mapToSource(i).row()).size;
        statusBar()->showMessage(tr("%1 selected · %2").arg(rows.size()).arg(displaySize(size)));
    }
}
void MainWindow::showProperties() {
    if (busy_ || !archive_)
        return;
    const auto rows = table_->selectionModel()->selectedRows();
    if (rows.size() != 1)
        return;
    const auto &row = model_->row(filter_->mapToSource(rows.first()).row());
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
    auto *theme = d.theme;
    auto *language = d.language;
    auto *history = d.history;
    const QStringList ids{"system", "light", "dark"}, languages{"en", "uk", "ru"};
    if (d.exec() == QDialog::Accepted) {
        settings_->setValue("language", languages[language->currentIndex()]);
        settings_->setValue("theme", ids[theme->currentIndex()]);
        settings_->setValue("historyEnabled", history->isChecked());
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
    if (!busy_ && e->mimeData()->hasUrls())
        e->acceptProposedAction();
}
void MainWindow::dropEvent(QDropEvent *e) {
    if (busy_)
        return;
    const auto urls = e->mimeData()->urls();
    if (archive_) {
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
        password_.reset();
        openPath(urls.first().toLocalFile());
        e->acceptProposedAction();
    }
}
void MainWindow::closeEvent(QCloseEvent *e) {
    if (busy_) {
        if (QMessageBox::question(this, tr("Operation in progress"),
                                  tr("Cancel the operation and close after it stops?")) !=
            QMessageBox::Yes) {
            e->ignore();
            return;
        }
        vynx::cancel(**operation_);
        closePending_ = true;
        e->ignore();
        return;
    }
    e->accept();
}
bool MainWindow::smokeTest() {
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
            return false;
        try {
            decodeShellRequest(shellData + 'x');
            return false;
        } catch (const std::exception &) {
        }
        if (outputForFormat("archive.zip", 1) != "archive.7z" ||
            outputForFormat("archive.tar.gz", 0) != "archive.zip")
            return false;
        applyTheme("light");
        if (qApp->palette().color(QPalette::Window).lightness() < 200)
            return false;
        applyTheme("dark");
        if (qApp->palette().color(QPalette::Window).lightness() > 80)
            return false;
        QTemporaryDir temp;
        if (!temp.isValid())
            return false;
        settings_ =
            std::make_unique<QSettings>(temp.path() + "/settings.ini", QSettings::IniFormat);
        settings_->setValue("historyEnabled", false);
        const QString source = temp.path() + "/source";
        QDir().mkpath(source + "/nested");
        QFile input(source + "/nested/дані.txt");
        if (!input.open(QIODevice::WriteOnly))
            return false;
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
        connect(&watcher_, &QFutureWatcher<QString>::finished, &loop, &QEventLoop::quit);
        QTimer::singleShot(10000, &loop, &QEventLoop::quit);
        openPath(archivePath);
        loop.exec();
        if (busy_ || !archive_ || model_->rowCount() != 1 || !model_->row(0).folder)
            return false;
        navigate("source/nested");
        if (model_->rowCount() != 1 || model_->row(0).name != "дані.txt")
            return false;
        search_->setText("ДАНІ");
        if (filter_->rowCount() != 1)
            return false;
        table_->selectAll();
        if (table_->selectionModel()->selectedRows().size() != 1)
            return false;
        vynx::test_archive(**archive_, "", *op);
        vynx::extract_archive(**archive_, utf8(temp.path() + "/out"), {}, 0, false, "", *op);
        QFile restored(temp.path() + "/out/source/nested/дані.txt");
        if (!restored.open(QIODevice::ReadOnly) ||
            restored.readAll() != "real GUI bridge roundtrip")
            return false;
        restored.close();
        // Exercise the real asynchronous conflict dialog and bridge reply.
        beginOperation();
        auto conflictOp = operation_;
        auto conflictArchive = archive_;
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
        if (busy_ || !conflictDialogSeen || vynx::conflict_request(**conflictOp).id)
            return false;
        rust::Vec<rust::String> rename;
        rename.push_back("source/nested/дані.txt");
        vynx::modify_archive(**archive_, 2,
                             rust::Slice<const rust::String>(rename.data(), rename.size()),
                             "source/nested/renamed.txt", "", "", *op);
        openPath(archivePath);
        loop.exec();
        if (busy_ || !archive_)
            return false;
        navigate("source/nested");
        if (model_->rowCount() != 1 || model_->row(0).name != "renamed.txt")
            return false;
        rust::Vec<quint64> hashIds;
        hashIds.push_back(model_->row(0).memberIds.front());
        auto hashes = vynx::hash_entries(
            **archive_, rust::Slice<const quint64>(hashIds.data(), hashIds.size()), "", *op);
        if (hashes.size() != 1 || hashes[0].sha256.size() != 64 || hashes[0].crc32.size() != 8)
            return false;
        vynx::verify_entry_hash(**archive_, hashIds[0], hashes[0].sha256, "", *op);
        auto jobSecret = secret("private");
        std::weak_ptr<SecretUtf8> secretLifetime = jobSecret;
        const auto encryptedPath = utf8(temp.path() + "/encrypted.zip");
        vynx::create_archive(encryptedPath,
                             rust::Slice<const rust::String>(inputs.data(), inputs.size()),
                             jobSecret->bytes(), *op);
        beginOperation();
        auto passwordOp = operation_;
        runJob(tr("Testing archive…"), [jobSecret, encryptedPath, passwordOp] {
            auto encrypted = vynx::open_archive(encryptedPath, jobSecret->bytes(), **passwordOp);
            vynx::test_archive(*encrypted, jobSecret->bytes(), **passwordOp);
            return QString();
        });
        jobSecret.reset();
        loop.exec();
        if (busy_ || !secretLifetime.expired())
            return false;
        // Large model/view metadata is test input only, never a simulated product archive.
        rust::Vec<vynx::EntryInfo> many;
        for (quint64 i = 0; i < 100000; ++i) {
            vynx::EntryInfo e;
            e.id = i;
            e.name = utf8(QString("file_%1.txt").arg(i));
            e.size = i;
            many.push_back(std::move(e));
        }
        model_->load(std::move(many));
        search_->clear();
        if (model_->rowCount() != 100000)
            return false;
        auto clone = vynx::clone_operation(*op);
        vynx::cancel(*clone);
        try {
            vynx::hash_archive_file(utf8(archivePath), *op);
            return false;
        } catch (const std::exception &e) {
            if (!QString::fromUtf8(e.what()).contains("CANCELLED"))
                return false;
        }
        return true;
    } catch (const std::exception &e) {
        qWarning("GUI smoke failure: %s", e.what());
        return false;
    }
}
