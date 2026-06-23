#include <QApplication>
#include <QIcon>
#include <QMessageBox>
#include <QSettings>
#include <QSystemTrayIcon>
#include <QTimer>
#include "mainwindow.h"
#include "translations/tsparser.h"

int main(int argc, char* argv[])
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // High-DPI is always enabled in Qt6 — no extra setup needed
#endif

    QApplication app(argc, argv);
    app.setApplicationName("TrackType");
    app.setApplicationVersion("0.9.0");
    app.setOrganizationName("TrackType");
    app.setOrganizationDomain("tracktype.app");

#ifdef Q_OS_LINUX
    // Windows and macOS get their app icon from the embedded .ico / bundle .icns;
    // Linux has no embedded equivalent, so set the window icon at runtime (used
    // in the task switcher / window list) and tell the desktop environment which
    // .desktop entry this window belongs to so GNOME/KDE show its icon in the
    // dock and overview (the basename must match the installed TrackType.desktop
    // and the window's WM_CLASS / app_id).
    app.setWindowIcon(QIcon(":/icons/app.svg"));
    app.setDesktopFileName("TrackType");
#endif

    // Install translator for the saved language before any UI is created.
    // The pointer is passed to MainWindow so it can remove it when the user
    // switches languages (e.g. back to English); MainWindow takes ownership.
    QTranslator* startupTranslator = nullptr;
    {
        QSettings s("TrackType", "TrackType");
        const QString lang = s.value("language", "en").toString();
        startupTranslator = loadBestTranslator(lang, &app);
        if (startupTranslator)
            app.installTranslator(startupTranslator);
    }

    // Don't quit when last window is hidden (keep tray alive)
    app.setQuitOnLastWindowClosed(false);

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        QMessageBox::warning(nullptr,
            QCoreApplication::translate("main", "TrackType"),
            QCoreApplication::translate("main",
                "No system tray detected. The application will still run,\n"
                "but you won't be able to hide it to the tray."));
    }

    MainWindow w(startupTranslator);

    // Honour "start minimized to tray": read the persisted setting before
    // showing the window so we never flash it on screen then hide it.
    {
        QSettings s("TrackType", "TrackType");
        if (!s.value("window/startMin", false).toBool())
            w.show();
    }

    // Once the event loop is running, offer to fix input-device permissions on
    // Linux/Wayland if needed so dwell-clicking works over every window.  No-op
    // on other platforms and when access is already available.
    QTimer::singleShot(0, &w, [&w]{ w.promptForInputAccessIfNeeded(); });

    return app.exec();
}
