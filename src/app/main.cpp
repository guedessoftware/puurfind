#include "app/AppController.h"
#include "app/GlobalShortcut.h"

#include "core/Config.h"
#include "preview/PreviewImageProvider.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QApplication>
#include <QAction>
#include <QIcon>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QCursor>
#include <QScreen>
#include <QProcess>
#include <QTimer>
#include <QDebug>
#include <functional>
#include <memory>

class AppInstance : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.purrfind.Application1")
public slots:
    void Show() { emit showRequested(); }
signals:
    void showRequested();
};

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    application.setApplicationName("PurrFind");
    application.setOrganizationName("PurrFind");
    application.setApplicationVersion(PURRFIND_VERSION);
    application.setOrganizationDomain("guedessoftware.github.io");
    application.setDesktopFileName("io.github.guedessoftware.PurrFind");
    const bool startHidden = application.arguments().contains("--background");

    auto bus = QDBusConnection::sessionBus();
    if (!bus.registerService("org.purrfind.Application")) {
        if (!startHidden) {
            const QDBusMessage showMessage = QDBusMessage::createMethodCall(
                "org.purrfind.Application", "/org/purrfind/Application",
                "org.purrfind.Application1", "Show");
            bus.call(showMessage, QDBus::NoBlock);
        }
        return 0;
    }

    AppInstance instance;
    bus.registerObject("/org/purrfind/Application", &instance,
                       QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals);

    QSystemTrayIcon tray(QIcon::fromTheme(
        "io.github.guedessoftware.PurrFind",
        QIcon(":/resources/icons/io.github.guedessoftware.PurrFind.svg")));
    QMenu trayMenu;
    QAction openAction("Abrir PurrFind", &trayMenu);
    QAction shortcutStatusAction("Atalho global: registrando…", &trayMenu);
    QAction quitAction("Sair", &trayMenu);
    shortcutStatusAction.setEnabled(false);
    trayMenu.addAction(&openAction);
    trayMenu.addSeparator();
    trayMenu.addAction(&shortcutStatusAction);
    trayMenu.addSeparator();
    trayMenu.addAction(&quitAction);
    tray.setContextMenu(&trayMenu);
    tray.setToolTip(QStringLiteral("PurrFind %1 — ativo").arg(QStringLiteral(PURRFIND_VERSION)));
    tray.show();

#ifdef PURRFIND_FLATPAK
    // A Flatpak cannot install a host systemd --user unit. Keep the same
    // D-Bus indexer contract by supervising the indexer inside this sandbox.
    // It is deliberately compiled only for the Flatpak target, so DEB/RPM/
    // Arch retain their independent user service and D-Bus activation.
    QProcess indexerProcess;
    bool stopping = false;
    std::function<void()> startIndexer;
    startIndexer = [&] {
        if (stopping || indexerProcess.state() != QProcess::NotRunning) return;
        const QString executable = QCoreApplication::applicationDirPath() + "/purrfind-indexer";
        indexerProcess.setProgram(executable);
        indexerProcess.setArguments({});
        indexerProcess.start();
        if (!indexerProcess.waitForStarted(2000))
            qWarning() << "PurrFind Flatpak: unable to start indexer:" << indexerProcess.errorString();
    };
    QObject::connect(&indexerProcess, &QProcess::errorOccurred, &application,
        [](QProcess::ProcessError error) {
            qWarning() << "PurrFind Flatpak: indexer process error" << error;
        });
    QObject::connect(&indexerProcess,
        qOverload<int, QProcess::ExitStatus>(&QProcess::finished), &application,
        [&](int exitCode, QProcess::ExitStatus status) {
            if (stopping) return;
            qWarning() << "PurrFind Flatpak: indexer stopped" << exitCode << status
                       << "— restarting";
            QTimer::singleShot(1500, &application, startIndexer);
        });
    startIndexer();
#endif

    purrfind::GlobalShortcut shortcut;
    const QString configuredShortcut = purrfind::Config::load().globalShortcut;
    QString shortcutError;
    bool shortcutWarningShown = false;

    std::shared_ptr<purrfind::PreviewCache> previewCache;
    std::unique_ptr<purrfind::AppController> controller;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow *window = nullptr;
    QTimer uiUnloadTimer;
    uiUnloadTimer.setSingleShot(true);
    uiUnloadTimer.setInterval(60000);
    QObject::connect(&uiUnloadTimer, &QTimer::timeout, &application, [&] {
        if (!window || window->isVisible()) return;
        window = nullptr;
        engine.reset();
        controller.reset();
        previewCache.reset();
    });
    std::function<void()> show;
    auto ensureUi = [&]() {
        if (window) return true;
        previewCache = std::make_shared<purrfind::PreviewCache>();
        controller = std::make_unique<purrfind::AppController>(previewCache);
        if (!shortcutError.isEmpty()) controller->reportError(shortcutError);
        engine = std::make_unique<QQmlApplicationEngine>();
        engine->addImageProvider("purrfind-preview", new purrfind::PreviewImageProvider(previewCache));
        QObject::connect(engine.get(), &QQmlApplicationEngine::warnings, &application,
            [](const QList<QQmlError> &warnings) {
                for (const auto &warning : warnings) qWarning().noquote() << warning.toString();
            });
        engine->rootContext()->setContextProperty("purrfindController", controller.get());
        engine->load(QUrl("qrc:/qml/Main.qml"));
        if (engine->rootObjects().isEmpty()) {
            qCritical() << "Failed to create the PurrFind QML window";
            return false;
        }
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().constFirst());
        if (!window) return false;
        QObject::connect(controller.get(), &purrfind::AppController::showRequested,
                         &application, [&show] { show(); });
        QObject::connect(window, &QWindow::visibleChanged, &application, [&](bool visible) {
            if (visible) uiUnloadTimer.stop();
            else if (window) uiUnloadTimer.start();
        });
        return true;
    };
    show = [&] {
        if (!ensureUi()) { application.exit(1); return; }
        QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
        if (!screen) screen = QGuiApplication::primaryScreen();
        if (screen) {
            window->setScreen(screen);
            const QRect area = screen->availableGeometry();
            window->setPosition(area.x() + qMax(0, (area.width() - window->width()) / 2),
                                area.y() + qMax(0, qRound(area.height() * 0.16)));
        }
        window->show();
        window->raise();
        window->requestActivate();
    };
    QObject::connect(&openAction, &QAction::triggered, &application, [&show] { show(); });
    QObject::connect(&quitAction, &QAction::triggered, &application, [&] {
#ifdef PURRFIND_FLATPAK
        stopping = true;
        indexerProcess.terminate();
#endif
        tray.hide();
        application.quit();
    });
    QObject::connect(&tray, &QSystemTrayIcon::activated, &application,
        [&show](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
                show();
        });
    QObject::connect(&shortcut, &purrfind::GlobalShortcut::registrationChanged, &application,
        [&](bool available, const QString &message) {
            shortcutError = available ? QString() : message;
            shortcutStatusAction.setText(available
                ? QString("Atalho global: %1 ativo").arg(configuredShortcut)
                : "Atalho global: indisponível");
            shortcutStatusAction.setToolTip(message);
            tray.setToolTip(available
                ? QString("PurrFind — ativo · %1 disponível").arg(configuredShortcut)
                : QString("PurrFind — ativo · %1 indisponível").arg(configuredShortcut));
            // Wayland portals can become ready a few seconds after autostart.
            // GlobalShortcut retries in the background; avoid a notification
            // storm while keeping the first actionable warning visible.
            if (!available && !shortcutWarningShown) {
                tray.showMessage("Atalho global indisponível",
                    message + "\nAbra o PurrFind pelo ícone da bandeja.",
                    QSystemTrayIcon::Warning, 7000);
                shortcutWarningShown = true;
            }
            if (available) shortcutWarningShown = false;
            if (controller && !available) controller->reportError(message);
        });
    shortcut.registerShortcut(configuredShortcut);
    QObject::connect(&instance, &AppInstance::showRequested, &application, [&show] { show(); });
    QObject::connect(&shortcut, &purrfind::GlobalShortcut::activated, &application, [&show] { show(); });
    const int screenshotOption = application.arguments().indexOf("--screenshot");
    if (!startHidden || screenshotOption >= 0) show();
    if (screenshotOption >= 0 && screenshotOption + 1 < application.arguments().size()) {
        const QString screenshotPath = application.arguments().at(screenshotOption + 1);
        QTimer::singleShot(750, &application, [&window, screenshotPath, &application] {
            if (!window) { application.exit(3); return; }
            if (!window->grabWindow().save(screenshotPath)) application.exit(3);
            else application.quit();
        });
    }
    return application.exec();
}

#include "main.moc"
