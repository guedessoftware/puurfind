#include "indexer/IndexerService.h"

#include "core/Logging.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>
#include <QDebug>

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    application.setApplicationName("purrfind-indexer");
    application.setOrganizationName("PurrFind");
    application.setApplicationVersion(PURRFIND_VERSION);

    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qCritical() << "Cannot connect to the session D-Bus";
        return 1;
    }
    if (!bus.registerService("org.purrfind.Indexer")) {
        qCritical() << "purrfind-indexer is already running:" << bus.lastError().message();
        return 2;
    }

    purrfind::IndexerService service;
    QString error;
    if (!service.initialize(&error)) {
        qCritical().noquote() << "Indexer initialization failed:" << error;
        return 1;
    }
    if (!bus.registerObject("/org/purrfind/Indexer", &service,
            QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals)) {
        qCritical() << "Cannot export indexer D-Bus object:" << bus.lastError().message();
        return 1;
    }
    qCInfo(logIndexer) << "PurrFind indexer" << PURRFIND_VERSION << "started";
    return application.exec();
}
