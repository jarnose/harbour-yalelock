#ifdef QT_QML_DEBUG
#include <QtQuick>
#endif

#include <sailfishapp.h>
#include <QGuiApplication>
#include <QQuickView>
#include <QQmlContext>
#include <QScopedPointer>
#include <QLocale>
#include <QTranslator>

#include "controller/appcontroller.h"

int main(int argc, char *argv[])
{
    QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));

    // Source strings are English; load a matching translation (if any) for
    // the device's locale - see harbour-otpcove's main() for how
    // QTranslator::load()'s fallback works.
    QTranslator translator;
    const QString translationsDir = SailfishApp::pathTo(QStringLiteral("translations")).toLocalFile();
    if (translator.load(QLocale(), QStringLiteral("harbour-yalelock"), QStringLiteral("-"),
                         translationsDir)) {
        app->installTranslator(&translator);
    }

    // Sailjail whitelists (and persists) ~/.local/share/<OrganizationName>/
    // <ApplicationName> based on the .desktop file's [X-Sailjail] values -
    // QStandardPaths::AppDataLocation (used by AppController for its SQLite
    // database) has to be built from exactly those same values, or the app
    // ends up reading/writing a directory Sailjail never actually persists
    // across launches (bit harbour-otpcove for real - see its main.cpp).
    // Keep these two values in lockstep with harbour-yalelock.desktop's
    // [X-Sailjail] section.
    app->setOrganizationName(QStringLiteral("io.github.jarnose"));
    app->setApplicationName(QStringLiteral("yalelock"));

    QScopedPointer<QQuickView> view(SailfishApp::createView());

    AppController controller;
    view->rootContext()->setContextProperty(QStringLiteral("AppController"), &controller);

    view->setSource(SailfishApp::pathToMainQml());
    view->show();

    return app->exec();
}
