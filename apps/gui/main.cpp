#include "MainWindow.h"
#include "ShellRequest.h"
#include <QApplication>
#include <QFile>
#include <QLocale>
#include <QMessageBox>
#include <QPointer>
#include <QSettings>
#include <QTimer>
#include <QTranslator>
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName("VYNX ARC");
    app.setOrganizationName("VYNX");
    app.setApplicationVersion(VYNX_VERSION);
    app.setStyle("Fusion");
    const QString portable = QCoreApplication::applicationDirPath() + "/portable.flag";
    std::unique_ptr<QSettings> settings;
    if (QFile::exists(portable))
        settings = std::make_unique<QSettings>(
            QCoreApplication::applicationDirPath() + "/data/settings.ini", QSettings::IniFormat);
    else
        settings = std::make_unique<QSettings>(QSettings::IniFormat, QSettings::UserScope, "VYNX",
                                               "VYNX ARC");
    const QString language = settings->value("language", "en").toString();
    QLocale::setDefault(QLocale(language == "uk" ? "uk_UA" : language == "ru" ? "ru_RU" : "en_US"));
    QTranslator translator;
    if (language != "en" && translator.load(":/i18n/vynx_" + language + ".qm"))
        app.installTranslator(&translator);
    QTranslator qtTranslator;
    if (language != "en" &&
        qtTranslator.load("qtbase_" + language,
                          QCoreApplication::applicationDirPath() + "/translations"))
        app.installTranslator(&qtTranslator);
    MainWindow window;
    const int sizeIndex = app.arguments().indexOf("--capture-size");
    if (sizeIndex >= 0 && sizeIndex + 1 < app.arguments().size()) {
        const auto dimensions = app.arguments()[sizeIndex + 1].split('x');
        if (dimensions.size() != 2)
            return 2;
        bool widthValid = false, heightValid = false;
        int width = dimensions[0].toInt(&widthValid), height = dimensions[1].toInt(&heightValid);
        if (!widthValid || !heightValid || width < 620 || height < 440 || width > 3840 ||
            height > 2160)
            return 2;
        window.resize(width, height);
    }
    if (app.arguments().contains("--smoke-test"))
        return window.smokeTest() ? 0 : 1;
    window.show();
    const int shellIndex = app.arguments().indexOf("--shell-request");
    if (shellIndex >= 0) {
        if (shellIndex + 1 >= app.arguments().size())
            return 2;
        try {
            auto request = consumeShellRequest(app.arguments()[shellIndex + 1]);
            QTimer::singleShot(0, &window, [&window, request] {
                window.handleShellRequest(request.action, request.paths);
            });
        } catch (const std::exception &error) {
            QMessageBox::warning(&window, "VYNX ARC", QString::fromUtf8(error.what()));
            return 2;
        }
    }
    const int captureIndex = app.arguments().indexOf("--capture");
    if (captureIndex >= 0 && captureIndex + 1 < app.arguments().size()) {
        const auto output = app.arguments()[captureIndex + 1];
        auto target = std::make_shared<QPointer<QWidget>>(&window);
        const int modeIndex = app.arguments().indexOf("--capture-ui");
        if (modeIndex >= 0 && modeIndex + 1 < app.arguments().size()) {
            const auto mode = app.arguments()[modeIndex + 1];
            QTimer::singleShot(1000, &window,
                               [&window, target, mode] { *target = window.prepareCapture(mode); });
        }
        QTimer::singleShot(1800, &window, [target, output, &app] {
            app.exit(*target && (*target)->grab().save(output) ? 0 : 1);
        });
    }
    if (app.arguments().size() > 1 && !app.arguments()[1].startsWith("--")) {
        QString path = app.arguments()[1];
        QTimer::singleShot(0, &window, [&window, path] { window.openPath(path); });
    }
    return app.exec();
}
