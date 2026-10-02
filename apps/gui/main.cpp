#include "MainWindow.h"
#include <QApplication>
#include <QFile>
#include <QLocale>
#include <QSettings>
#include <QTimer>
#include <QTranslator>
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName("VYNX ARC");
    app.setOrganizationName("VYNX");
    app.setApplicationVersion("0.1.0");
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
    QTranslator translator;
    if (language != "en" && translator.load(":/i18n/vynx_" + language + ".qm"))
        app.installTranslator(&translator);
    QTranslator qtTranslator;
    if (language != "en" &&
        qtTranslator.load("qtbase_" + language,
                          QCoreApplication::applicationDirPath() + "/translations"))
        app.installTranslator(&qtTranslator);
    MainWindow window;
    if (app.arguments().contains("--smoke-test"))
        return window.smokeTest() ? 0 : 1;
    window.show();
    const int captureIndex = app.arguments().indexOf("--capture");
    if (captureIndex >= 0 && captureIndex + 1 < app.arguments().size()) {
        const auto output = app.arguments()[captureIndex + 1];
        QTimer::singleShot(1800, &window, [&window, output, &app] {
            app.exit(window.grab().save(output) ? 0 : 1);
        });
    }
    if (app.arguments().size() > 1 && !app.arguments()[1].startsWith("--")) {
        QString path = app.arguments()[1];
        QTimer::singleShot(0, &window, [&window, path] { window.openPath(path); });
    }
    return app.exec();
}
