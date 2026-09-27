#include "mainwindow.h"
#include "sessionstore.h"

#include <QApplication>
#include <QDir>
#include <QLockFile>
#include <QMessageBox>
#include <QStandardPaths>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("PowerMeter"));
    QApplication::setApplicationName(QStringLiteral("PowerMeter"));
    QApplication::setApplicationVersion(QStringLiteral("1.0"));

    // Only one instance may log at a time, otherwise energy is counted twice.
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    QLockFile lock(dataDir + QStringLiteral("/powermeter.lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(100)) {
        QMessageBox::information(nullptr, QStringLiteral("Power Meter"),
                                 QStringLiteral("Power Meter is already running (check the system tray)."));
        return 0;
    }

    SessionStore store;
    QString error;
    if (!store.open(&error)) {
        QMessageBox::critical(nullptr, QStringLiteral("Power Meter"),
                              QStringLiteral("Could not open the log database:\n%1").arg(error));
        return 1;
    }

    MainWindow w(&store);
    if (!w.startHidden())
        w.show();
    return app.exec();
}
