#include "platform/headless.h"
#include "popout/popout.h"
#include "recorder.h"
#include "settings.h"
#include "shell/accessible.h"
#include "shell/mainwindow.h"
#include "style/appearance.h"
#include "style/style.h"
#include "views/apphistory.h"
#include "widgets/icons.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QStandardPaths>
#include <QTimer>
#include <QTranslator>
#include <cstdlib>
#ifdef __GLIBC__
#include <malloc.h>
#endif
#include <cstring>

using namespace fg;

namespace {

// Qt's Wayland plugin prints this about a mouse grab Wayland doesn't offer, and nothing
// can be done about it from here: this app never asks for a grab. Only this exact text
// is dropped, and every other message is passed on.
QtMessageHandler g_nextHandler = nullptr;

void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    if (message == QLatin1String("This plugin supports grabbing the mouse only for popup windows"))
        return;
    g_nextHandler(type, context, message);
}

// Hidden test aid: saves a picture of every view in every scheme, then quits.
void takeScreenshots(MainWindow& window, const QString& directory, const QStringList& schemeIds, const QList<int>& textSizes)
{
    QDir().mkpath(directory);
    struct Shot {
        QString scheme;
        int text;
        int view;
    };
    auto shots = std::make_shared<QList<Shot>>();
    for (const QString& scheme : schemeIds)
        for (const int text : textSizes)
            for (int view = 0; view < window.viewCount(); ++view)
                shots->append({scheme, text, view});

    auto next = std::make_shared<std::function<void()>>();
    *next = [&window, directory, shots, next] {
        if (shots->isEmpty()) {
            QApplication::quit();
            return;
        }
        const Shot shot = shots->takeFirst();
        Settings::instance().setValue("appearance/scheme", shot.scheme);
        Settings::instance().setValue("appearance/textPercent", shot.text);
        window.applyAppearance();
        window.openView(shot.view);
        // Two samples, so that rates have something to show.
        QTimer::singleShot(1500, &window, [&window, directory, shot, next] {
            window.grab().save(QStringLiteral("%1/%2-%3-%4.png").arg(directory, shot.scheme).arg(shot.text).arg(shot.view, 2, 10, QLatin1Char('0')));
            (*next)();
        });
    };
    QTimer::singleShot(0, &window, *next);
}

// Hidden test aid: saves a picture of the popout in every skin, then quits.
void takeSkinShots(SamplerHub& hub, const QString& directory)
{
    QDir().mkpath(directory);
    auto* popout = new Popout(hub);
    popout->show();
    auto remaining = std::make_shared<QList<Skin>>(popout->library().skins);
    auto next = std::make_shared<std::function<void()>>();
    *next = [popout, directory, remaining, next] {
        if (remaining->isEmpty()) {
            QApplication::quit();
            return;
        }
        const Skin skin = remaining->takeFirst();
        popout->applySkin(skin);
        QTimer::singleShot(2200, popout, [popout, directory, skin, next] {
            popout->grab().save(QStringLiteral("%1/skin-%2.png").arg(directory, skin.id));
            (*next)();
        });
    };
    QTimer::singleShot(0, popout, *next);
}

} // namespace

int main(int argc, char** argv)
{
#ifdef __GLIBC__
    ::mallopt(M_ARENA_MAX, 1); // a second thread gets no separate memory pool of its own
#endif

    // Recording needs no window, so it must also work with no display at all.
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], "--record") == 0) {
            int seconds = 0;
            int intervalMs = 1000;
            for (int j = 1; j + 1 < argc; ++j) {
                if (std::strcmp(argv[j], "--seconds") == 0)
                    seconds = std::atoi(argv[j + 1]);
                else if (std::strcmp(argv[j], "--interval-ms") == 0)
                    intervalMs = std::max(100, std::atoi(argv[j + 1]));
            }
            return recordHeadless(argv[i + 1], seconds, intervalMs);
        }

    g_nextHandler = qInstallMessageHandler(messageHandler);
    QApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Freberg.guru - Task manager"));
    QApplication::setOrganizationName(QStringLiteral("Freberg.guru"));
    // Claiming this ID makes Qt register it with the desktop portal, which refuses an ID
    // with no installed desktop file. So a build run from its own folder doesn't claim it.
    if (!QStandardPaths::locate(QStandardPaths::ApplicationsLocation, QStringLiteral("guru.freberg.TaskManager.desktop")).isEmpty())
        QApplication::setDesktopFileName(QStringLiteral("guru.freberg.TaskManager"));
    QApplication::setApplicationVersion(QStringLiteral(FG_VERSION));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption shotsOption("screenshots", "Save a picture of every view into this folder, then quit.", "folder");
    QCommandLineOption schemesOption("schemes", "Comma-separated schemes for --screenshots.", "ids", "standard");
    QCommandLineOption textOption("text-sizes", "Comma-separated text sizes for --screenshots.", "percent", "100");
    QCommandLineOption viewOption("view", "Open this view number at start.", "number");
    QCommandLineOption skinShotsOption("skin-screenshots", "Save a picture of the popout in every skin into this folder, then quit.", "folder");
    QCommandLineOption iconOption("export-icon", "Save the app icon at 256 pixels to this file, then quit.", "file");
    QCommandLineOption startTestOption("start-test", "Quit as soon as the first window has been drawn, to measure start time.");
    QCommandLineOption recordOption("record", "Record to this .fgtrace file with no window, then quit.", "file");
    QCommandLineOption secondsOption("seconds", "How long --record runs. Without it, until interrupted.", "seconds");
    QCommandLineOption intervalOption("interval-ms", "Time between samples for --record.", "ms");
    parser.addOptions({shotsOption, schemesOption, textOption, viewOption, skinShotsOption, iconOption, startTestOption, recordOption, secondsOption, intervalOption});
    parser.process(app);

    if (parser.isSet(iconOption))
        return Icons::app().pixmap(256, 256).save(parser.value(iconOption)) ? 0 : 1;

    QTranslator translator;
    const QString language = Settings::instance().language();
    const QLocale locale = language.isEmpty() ? QLocale::system() : QLocale(language);
    if (translator.load(locale, QStringLiteral("freberg-taskmanager"), QStringLiteral("_"), QStringLiteral(":/i18n")))
        QApplication::installTranslator(&translator);
    QLocale::setDefault(locale);

    installAccessibility();
    installAppearance();

    SamplerHub hub;
    MainWindow window(hub);
    startAppHistoryIfEnabled(hub);
    startFlightRecorderIfEnabled(hub);
    if (parser.isSet(viewOption))
        window.openView(parser.value(viewOption).toInt());
    window.show();

    if (parser.isSet(shotsOption)) {
        QList<int> sizes;
        for (const QString& size : parser.value(textOption).split(','))
            sizes << size.toInt();
        takeScreenshots(window, parser.value(shotsOption), parser.value(schemesOption).split(','), sizes);
    }
    if (parser.isSet(startTestOption))
        QTimer::singleShot(0, &app, &QApplication::quit);
    if (parser.isSet(skinShotsOption))
        takeSkinShots(hub, parser.value(skinShotsOption));
    return QApplication::exec();
}
