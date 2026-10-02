#include "MainWindow.h"
#include "SecretUtf8.h"
#include "ShellRequest.h"
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
static QString outputForFormat(QString path, int format) {
    const QStringList extensions{"zip", "7z", "tar", "tar.gz"};
    for (const auto &ext : QStringList{"tar.gz", "zip", "7z", "tar", "tgz"}) {
        if (path.endsWith("." + ext, Qt::CaseInsensitive)) {
            path.chop(ext.size() + 1);
            break;
        }
    }
    return path + "." + extensions.at(format);
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
        QMessageBox::about(this, tr("About VYNX ARC"),
                           tr("VYNX ARC 0.1.0\nWindows x64 · Qt 6.12 · Rust core\nDevelopment "
                              "build\n\nOffline archive operations. No telemetry.\nZIP, 7Z, TAR "
                              "and TAR.GZ.\n\nMIT application license. Third-party components "
                              "retain their licenses.\nSource: "
                              "github.com/Maks0101aps/VYNX-ARC\nSee bundled "
                              "THIRD_PARTY_NOTICES.md."));
    });

    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(24, 18, 24, 18);
    layout->setSpacing(16);
    setCentralWidget(central);
    auto *header = new QHBoxLayout;
    auto *logo = new QLabel;
    logo->setPixmap(QIcon(":/assets/icons/vynx-arc.png").pixmap(36, 36));
    auto *brand = new QLabel(tr("VYNX ARC"));
    brand->setObjectName("brand");
    header->addWidget(logo);
    header->addWidget(brand);
    header->addStretch();
    auto *openButton = new QPushButton(tr("Open archive"));
    auto *createButton = new QPushButton(tr("Create archive"));
    connect(openButton, &QPushButton::clicked, this, [this] { chooseOpen(); });
    connect(createButton, &QPushButton::clicked, this, [this] { chooseCreate(); });
    header->addWidget(openButton);
    header->addWidget(createButton);
    connect(open, &QAction::changed, openButton,
            [open, openButton] { openButton->setEnabled(open->isEnabled()); });
    connect(create, &QAction::changed, createButton,
            [create, createButton] { createButton->setEnabled(create->isEnabled()); });
    layout->addLayout(header);
    pages_ = new QStackedWidget;
    layout->addWidget(pages_, 1);
    auto *home = new QWidget;
    auto *homeLayout = new QVBoxLayout(home);
    homeLayout->setContentsMargins(32, 40, 32, 20);
    auto *title = new QLabel(tr("Everything in its place."));
    title->setObjectName("hero");
    homeLayout->addWidget(title);
    auto *subtitle = new QLabel(tr("Open, explore and extract your archives.\nDrop an archive "
                                   "anywhere in this window."));
    subtitle->setObjectName("muted");
    homeLayout->addWidget(subtitle);
    homeLayout->addSpacing(32);
    auto *recentHeader = new QHBoxLayout;
    recentHeader->addWidget(new QLabel(tr("Recent archives")));
    recentHeader->addStretch();
    auto *clear = new QPushButton(tr("Clear history"));
    recentHeader->addWidget(clear);
    homeLayout->addLayout(recentHeader);
    connect(clear, &QPushButton::clicked, this, [this] {
        settings_->remove("recent");
        updateRecent();
    });
    recent_ = new QListWidget;
    recent_->setAccessibleName(tr("Recent archives"));
    recent_->setObjectName("recent");
    homeLayout->addWidget(recent_, 1);
    connect(recent_, &QListWidget::itemActivated, this,
            [this](QListWidgetItem *i) { openPath(i->data(Qt::UserRole).toString()); });
    pages_->addWidget(home);
    auto *browser = new QWidget;
    auto *browserLayout = new QVBoxLayout(browser);
    browserLayout->setContentsMargins(0, 0, 0, 0);
    browserLayout->setSpacing(12);
    auto *nav = new QHBoxLayout;
    auto *backButton = new QPushButton(tr("Back"));
    auto *upButton = new QPushButton(tr("Up"));
    connect(backButton, &QPushButton::clicked, this, [this] { back(); });
    connect(upButton, &QPushButton::clicked, this, [this] { up(); });
    nav->addWidget(backButton);
    nav->addWidget(upButton);
    breadcrumb_ = new QLabel;
    breadcrumb_->setTextFormat(Qt::PlainText);
    breadcrumb_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    nav->addWidget(breadcrumb_, 1);
    search_ = new QLineEdit;
    search_->setPlaceholderText(tr("Filter filenames…"));
    search_->setAccessibleName(tr("Filter filenames"));
    search_->setMaximumWidth(260);
    nav->addWidget(search_);
    browserLayout->addLayout(nav);
    auto *commands = new QHBoxLayout;
    auto *extractButton = new QPushButton(tr("Extract"));
    extractButton->setObjectName("primary");
    auto *testButton = new QPushButton(tr("Test archive"));
    auto *more = new QPushButton(tr("More"));
    auto *moreMenu = new QMenu(more);
    for (auto *a : {smart, here, named, hash, addFiles, addFolder, rename, remove})
        moreMenu->addAction(a);
    more->setMenu(moreMenu);
    connect(extractButton, &QPushButton::clicked, this, [this] { chooseExtract(); });
    connect(testButton, &QPushButton::clicked, this, [this] { testArchive(); });
    commands->addWidget(extractButton);
    commands->addWidget(testButton);
    commands->addWidget(more);
    commands->addStretch();
    browserLayout->addLayout(commands);
    model_ = new ArchiveModel(this);
    filter_ = new ArchiveFilter(this);
    filter_->setSourceModel(model_);
    filter_->setFilterCaseSensitivity(Qt::CaseInsensitive);
    filter_->setFilterKeyColumn(0);
    table_ = new QTableView;
    table_->setModel(filter_);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->setSortingEnabled(true);
    table_->sortByColumn(0, Qt::AscendingOrder);
    table_->setAlternatingRowColors(false);
    table_->setShowGrid(false);
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setDefaultSectionSize(34);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->setColumnWidth(1, 105);
    table_->setColumnWidth(2, 105);
    table_->setColumnHidden(3, true);
    table_->setColumnHidden(6, true);
    table_->setAccessibleName(tr("Archive contents"));
    browserLayout->addWidget(table_, 1);
    pages_->addWidget(browser);
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
            chooseExtract();
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
                chooseExtract();
        }
    });
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table_, &QWidget::customContextMenuRequested, this,
            [this, extract, smart, test, hash](QPoint p) {
                QMenu m;
                for (auto *a : {extract, smart, test, hash})
                    m.addAction(a);
                m.exec(table_->viewport()->mapToGlobal(p));
            });
    auto *columns = viewMenu->addMenu(tr("Columns"));
    for (int c = 1; c < 7; ++c) {
        auto *a =
            columns->addAction(model_->headerData(c, Qt::Horizontal, Qt::DisplayRole).toString());
        a->setCheckable(true);
        a->setChecked(!table_->isColumnHidden(c));
        connect(a, &QAction::toggled, this,
                [this, c](bool show) { table_->setColumnHidden(c, !show); });
    }

    operationPanel_ = new QWidget;
    operationPanel_->setObjectName("operation");
    auto *opLayout = new QVBoxLayout(operationPanel_);
    opLayout->setContentsMargins(16, 12, 16, 12);
    current_ = new QLabel;
    current_->setWordWrap(true);
    current_->setTextFormat(Qt::PlainText);
    opLayout->addWidget(current_);
    auto *progressLine = new QHBoxLayout;
    progress_ = new QProgressBar;
    progress_->setAccessibleName(tr("Operation progress"));
    cancel_ = new QPushButton(tr("Cancel"));
    progressLine->addWidget(progress_, 1);
    progressLine->addWidget(cancel_);
    opLayout->addLayout(progressLine);
    rate_ = new QLabel;
    rate_->setObjectName("muted");
    opLayout->addWidget(rate_);
    layout->addWidget(operationPanel_);
    operationPanel_->hide();
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
        current_->setText(text(p.current));
        if (p.total) {
            progress_->setRange(0, 1000);
            progress_->setValue(int(qMin(1000.0, double(p.done) / double(p.total) * 1000.0)));
        } else
            progress_->setRange(0, 0);
        double seconds = qMax(0.001, elapsed_.elapsed() / 1000.0);
        rate_->setText(tr("%1 / %2 · %3/s · %4 s")
                           .arg(displaySize(p.done), p.total ? displaySize(p.total) : tr("unknown"),
                                displaySize(quint64(p.done / seconds)),
                                QString::number(seconds, 'f', 1)));
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
            statusBar()->showMessage(tr("Operation completed"), 8000);
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
    operationPanel_->show();
    current_->setText(title);
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
            setWindowTitle(tr("VYNX ARC — %1").arg(QFileInfo(path).fileName()));
            breadcrumb_->setText(QFileInfo(path).fileName() + " / ");
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
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Extract archive"));
    auto *form = new QFormLayout(&dialog);
    QString defaultDest = QFileInfo(archivePath_).absolutePath();
    if (named)
        defaultDest += "/" + QFileInfo(archivePath_).completeBaseName();
    auto *destination = new QLineEdit(defaultDest);
    auto *browse = new QPushButton(tr("Browse…"));
    auto *destLayout = new QHBoxLayout;
    destLayout->addWidget(destination);
    destLayout->addWidget(browse);
    form->addRow(tr("Destination"), destLayout);
    connect(browse, &QPushButton::clicked, &dialog, [&] {
        QString p = QFileDialog::getExistingDirectory(&dialog, tr("Extraction destination"),
                                                      destination->text());
        if (!p.isEmpty())
            destination->setText(p);
    });
    auto *scope = new QComboBox;
    scope->addItem(tr("All files"));
    auto selectedRows = table_->selectionModel()->selectedRows();
    if (!selectedRows.isEmpty()) {
        scope->addItem(tr("Selected files and folders"));
        scope->setCurrentIndex(1);
    }
    form->addRow(tr("Files"), scope);
    auto *conflicts = new QComboBox;
    conflicts->addItems({tr("Ask for each conflict"), tr("Replace existing files"),
                         tr("Skip existing files"), tr("Keep both / rename incoming"),
                         tr("Replace if newer"), tr("Stop on conflict")});
    form->addRow(tr("Existing files"), conflicts);
    auto *smartBox = new QCheckBox(tr("Smart Extract: avoid redundant nesting"));
    smartBox->setChecked(smart);
    form->addRow(smartBox);
    auto *security = new QLabel(tr("Unsafe paths, links and archive bombs are blocked."));
    security->setWordWrap(true);
    form->addRow(security);
    auto *password = new QLineEdit(password_ ? password_->text() : QString());
    password->setEchoMode(QLineEdit::Password);
    form->addRow(tr("Password, if needed"), password);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Extract"));
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (here) {
        smartBox->setChecked(false);
    }
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
    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("File already exists"));
    dialog->setWindowModality(Qt::ApplicationModal);
    dialog->setMinimumWidth(540);
    auto *layout = new QVBoxLayout(dialog);
    auto date = [this](quint64 seconds) {
        return seconds ? QDateTime::fromSecsSinceEpoch(qint64(seconds)).toString(Qt::ISODate)
                       : tr("Unknown");
    };
    auto *comparison = new QLabel(tr("Existing: %1\nSize: %2 bytes\nModified: %3\n\nIncoming: "
                                     "%4\nSize: %5 bytes\nModified: %6")
                                      .arg(text(request.existing_path))
                                      .arg(request.existing_size)
                                      .arg(date(request.existing_modified))
                                      .arg(text(request.incoming_name))
                                      .arg(request.incoming_size)
                                      .arg(date(request.incoming_modified)));
    comparison->setTextFormat(Qt::PlainText);
    comparison->setTextInteractionFlags(Qt::TextSelectableByMouse);
    comparison->setWordWrap(true);
    layout->addWidget(comparison);
    auto *all = new QCheckBox(tr("Apply this choice to all conflicts"));
    layout->addWidget(all);
    auto *buttons = new QDialogButtonBox;
    layout->addWidget(buttons);
    auto add = [&](const QString &label, quint8 choice, QDialogButtonBox::ButtonRole role) {
        auto *button = buttons->addButton(label, role);
        connect(button, &QPushButton::clicked, dialog, [dialog, op, id = request.id, all, choice] {
            vynx::reply_conflict(**op, id, choice, all->isChecked());
            dialog->accept();
        });
    };
    add(tr("Replace"), 2, QDialogButtonBox::DestructiveRole);
    add(tr("Skip"), 1, QDialogButtonBox::ActionRole);
    add(tr("Keep both"), 4, QDialogButtonBox::ActionRole);
    add(tr("Cancel"), 0, QDialogButtonBox::RejectRole);
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
        [this] {
            QMessageBox::information(this, tr("Archive test"),
                                     tr("All archive streams decoded successfully.\nChecksums were "
                                        "verified where provided by the backend."));
        });
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
            connect(b, &QPushButton::clicked, &d,
                    [result] { QApplication::clipboard()->setText(*result); });
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
                QMessageBox::information(this, tr("Verify hash"),
                                         tr("The computed digest matches the supplied value.") +
                                             "\n\n" + *result);
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
            connect(copy, &QPushButton::clicked, &dialog,
                    [result] { QApplication::clipboard()->setText(*result); });
            dialog.resize(700, 400);
            dialog.exec();
        });
}
void MainWindow::chooseCreate(const QStringList &initial, int initialFormat) {
    if (busy_)
        return;
    QDialog d(this);
    d.setWindowTitle(tr("Create archive"));
    auto *form = new QFormLayout(&d);
    auto *inputs = new QListWidget;
    inputs->addItems(initial);
    inputs->setMinimumWidth(460);
    form->addRow(tr("Sources"), inputs);
    auto *sourceButtons = new QHBoxLayout;
    auto *files = new QPushButton(tr("Add files…"));
    auto *folder = new QPushButton(tr("Add folder…"));
    auto *remove = new QPushButton(tr("Remove"));
    sourceButtons->addWidget(files);
    sourceButtons->addWidget(folder);
    sourceButtons->addWidget(remove);
    form->addRow(sourceButtons);
    connect(files, &QPushButton::clicked, &d,
            [&] { inputs->addItems(QFileDialog::getOpenFileNames(&d, tr("Source files"))); });
    connect(folder, &QPushButton::clicked, &d, [&] {
        auto p = QFileDialog::getExistingDirectory(&d, tr("Source folder"));
        if (!p.isEmpty())
            inputs->addItem(p);
    });
    connect(remove, &QPushButton::clicked, &d,
            [&] { delete inputs->takeItem(inputs->currentRow()); });
    auto *format = new QComboBox;
    format->addItems({"ZIP", "7Z", "TAR", "TAR.GZ"});
    format->setCurrentIndex(initialFormat);
    form->addRow(tr("Format"), format);
    auto *output = new QLineEdit;
    if (!initial.isEmpty())
        output->setText(outputForFormat(
            QFileInfo(initial.first()).absolutePath() + "/" +
                (initial.size() == 1 ? QFileInfo(initial.first()).completeBaseName() : "Archive"),
            initialFormat));
    auto *save = new QPushButton(tr("Browse…"));
    auto *outLine = new QHBoxLayout;
    outLine->addWidget(output);
    outLine->addWidget(save);
    form->addRow(tr("Output archive"), outLine);
    connect(save, &QPushButton::clicked, &d, [&] {
        const QStringList extensions{"zip", "7z", "tar", "tar.gz"};
        auto p = QFileDialog::getSaveFileName(&d, tr("Create archive"),
                                              "Archive." + extensions[format->currentIndex()],
                                              tr("All files (*)"));
        if (!p.isEmpty())
            output->setText(p);
    });
    auto *password = new QLineEdit;
    password->setEchoMode(QLineEdit::Password);
    form->addRow(tr("Password (optional)"), password);
    auto *advanced = new QGroupBox(tr("Advanced: split 7Z volumes"));
    advanced->setCheckable(true);
    advanced->setChecked(false);
    advanced->setVisible(initialFormat == 1);
    auto *splitForm = new QFormLayout(advanced);
    auto *split = new QComboBox;
    split->addItems({tr("No split"), "100 MiB", "500 MiB", "1 GiB", "4 GiB", tr("Custom")});
    auto *customSplit = new QSpinBox;
    customSplit->setRange(1, 1048576);
    customSplit->setValue(100);
    customSplit->setSuffix(" MiB");
    customSplit->setVisible(false);
    splitForm->addRow(tr("Volume size"), split);
    splitForm->addRow(customSplit);
    connect(split, &QComboBox::currentIndexChanged, &d,
            [customSplit](int index) { customSplit->setVisible(index == 5); });
    form->addRow(advanced);
    auto *encryptionLabel = new QLabel(tr("ZIP uses AES-256. 7Z encrypts data and filenames."));
    encryptionLabel->setWordWrap(true);
    form->addRow(encryptionLabel);
    connect(format, &QComboBox::currentIndexChanged, &d, [&](int i) {
        password->setEnabled(i < 2);
        if (i >= 2)
            password->clear();
        encryptionLabel->setVisible(i < 2);
        advanced->setVisible(i == 1);
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Create"));
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &d, [&] {
        if (inputs->count() == 0 || output->text().isEmpty()) {
            QMessageBox::information(&d, tr("Create archive"),
                                     tr("Choose source files and an output archive."));
            return;
        }
        output->setText(outputForFormat(output->text(), format->currentIndex()));
        d.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
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
    history_.append(model_->folder());
    model_->navigate(folder);
    search_->clear();
    breadcrumb_->setText(QFileInfo(archivePath_).fileName() + " / " + folder);
    updateStatus();
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
    breadcrumb_->setText(QFileInfo(archivePath_).fileName() + " / " + folder);
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
    const auto rows = table_->selectionModel()->selectedRows();
    if (rows.isEmpty())
        statusBar()->showMessage(
            tr("%1 entries · %2 · %3")
                .arg(model_->count())
                .arg(displaySize(model_->totalSize()),
                     archive_ ? text(vynx::archive_format(**archive_)) : QString()));
    else {
        quint64 size = 0;
        for (auto i : rows)
            size += model_->row(filter_->mapToSource(i).row()).size;
        statusBar()->showMessage(tr("%1 selected · %2").arg(rows.size()).arg(displaySize(size)));
    }
}
void MainWindow::updateRecent() {
    recent_->clear();
    for (auto p : settings_->value("recent").toStringList()) {
        auto *i = new QListWidgetItem(QFileInfo(p).fileName() + "\n" + p, recent_);
        i->setData(Qt::UserRole, p);
        i->setToolTip(p);
    }
}
void MainWindow::settings() {
    QDialog d(this);
    d.setWindowTitle(tr("Settings"));
    auto *l = new QFormLayout(&d);
    auto *theme = new QComboBox;
    theme->addItems({tr("Follow system"), tr("Light"), tr("Dark")});
    QStringList ids{"system", "light", "dark"};
    theme->setCurrentIndex(qMax(0, ids.indexOf(settings_->value("theme", "system").toString())));
    l->addRow(tr("Appearance"), theme);
    auto *language = new QComboBox;
    language->addItems({"English", "Українська", "Русский"});
    const QStringList languages{"en", "uk", "ru"};
    language->setCurrentIndex(
        qMax(0, languages.indexOf(settings_->value("language", "en").toString())));
    l->addRow(tr("Language (restart required)"), language);
    auto *history = new QCheckBox(tr("Remember recent archives locally"));
    history->setChecked(settings_->value("historyEnabled", true).toBool());
    l->addRow(history);
    auto *notice = new QLabel(tr("Path protection and link blocking are always enabled.\nNo "
                                 "network requests or background services.\nExplorer "
                                 "integration is not installed by this development build."));
    notice->setWordWrap(true);
    l->addRow(notice);
    auto *b = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    l->addRow(b);
    connect(b, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(b, &QDialogButtonBox::rejected, &d, &QDialog::reject);
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
    bool dark = theme == "dark" ||
                (theme == "system" && qApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark);
    QPalette p = qApp->style()->standardPalette();
    if (!dark) {
        p.setColor(QPalette::Window, QColor("#f5f6f9"));
        p.setColor(QPalette::WindowText, QColor("#202632"));
        p.setColor(QPalette::Base, Qt::white);
        p.setColor(QPalette::AlternateBase, QColor("#f0f3f8"));
        p.setColor(QPalette::Text, QColor("#202632"));
        p.setColor(QPalette::Button, QColor("#eef1f6"));
        p.setColor(QPalette::ButtonText, QColor("#202632"));
        p.setColor(QPalette::Disabled, QPalette::Text, QColor("#737c8b"));
        p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#737c8b"));
    } else {
        p.setColor(QPalette::Window, QColor("#17191e"));
        p.setColor(QPalette::WindowText, QColor("#f1f3f7"));
        p.setColor(QPalette::Base, QColor("#1e2128"));
        p.setColor(QPalette::AlternateBase, QColor("#242832"));
        p.setColor(QPalette::Text, QColor("#f1f3f7"));
        p.setColor(QPalette::Button, QColor("#272b34"));
        p.setColor(QPalette::ButtonText, QColor("#f1f3f7"));
        p.setColor(QPalette::Disabled, QPalette::Text, QColor("#858b97"));
        p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#858b97"));
    }
    p.setColor(QPalette::Highlight, QColor("#2563eb"));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::PlaceholderText, QColor(dark ? "#aab1bf" : "#626a79"));
    p.setColor(QPalette::ToolTipBase, QColor(dark ? "#272b34" : "#ffffff"));
    p.setColor(QPalette::ToolTipText, QColor(dark ? "#f1f3f7" : "#202632"));
    qApp->setPalette(p);
    qApp->setStyleSheet("QWidget { font-family: 'Segoe UI'; font-size: 10pt; } QLabel#brand { "
                        "font-size: 17pt; font-weight: 600; } QLabel#hero { font-size: 30pt; "
                        "font-weight: 600; } QLabel#muted { color: " +
                        QString(dark ? "#aab1bf" : "#626a79") +
                        "; } QPushButton { padding: 8px 16px; border-radius: 8px; border: 1px "
                        "solid " +
                        QString(dark ? "#393f4b" : "#cfd3db") +
                        "; } QPushButton:hover { border-color: #2563eb; } QPushButton:focus, "
                        "QLineEdit:focus { border: 2px solid #2563eb; } QPushButton#primary { "
                        "background: #2563eb; color: white; border-color: #2563eb; } QLineEdit { "
                        "padding: 8px; border: 1px solid " +
                        QString(dark ? "#393f4b" : "#cfd3db") +
                        "; border-radius: 8px; } QHeaderView::section { padding: 8px; border: 0; "
                        "} QTableView { border: 1px solid " +
                        QString(dark ? "#303541" : "#d8dce4") +
                        "; border-radius: 8px; } QProgressBar { min-height: 18px; border-radius: "
                        "5px; text-align: center; } QProgressBar::chunk { background: #2563eb; "
                        "border-radius: 4px; } QListWidget#recent { border: 0; } "
                        "QListWidget#recent::item { padding: 12px; } ");
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
