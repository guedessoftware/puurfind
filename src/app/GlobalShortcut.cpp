#include "app/GlobalShortcut.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QCoreApplication>
#include <QProcess>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QSocketNotifier>
#include <QTimer>

#ifdef PURRFIND_WITH_X11_SHORTCUT
#include <X11/Xlib.h>
#include <X11/keysym.h>
#endif

namespace {

#ifdef PURRFIND_WITH_X11_SHORTCUT
bool x11GrabFailed = false;

int shortcutX11ErrorHandler(Display *, XErrorEvent *event)
{
    if (event && event->error_code == BadAccess) x11GrabFailed = true;
    return 0;
}

unsigned int modifierMaskForKey(Display *display, KeySym symbol)
{
    const KeyCode keyCode = XKeysymToKeycode(display, symbol);
    if (!keyCode) return 0;
    unsigned int result = 0;
    XModifierKeymap *map = XGetModifierMapping(display);
    if (!map) return 0;
    for (int modifier = 0; modifier < 8; ++modifier) {
        for (int slot = 0; slot < map->max_keypermod; ++slot) {
            if (map->modifiermap[modifier * map->max_keypermod + slot] == keyCode)
                result |= (1u << modifier);
        }
    }
    XFreeModifiermap(map);
    return result;
}
#endif

} // namespace

namespace purrfind {

QDBusArgument &operator<<(QDBusArgument &argument, const PortalShortcut &shortcut)
{
    argument.beginStructure();
    argument << shortcut.id << shortcut.options;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, PortalShortcut &shortcut)
{
    argument.beginStructure();
    argument >> shortcut.id >> shortcut.options;
    argument.endStructure();
    return argument;
}

GlobalShortcut::GlobalShortcut(QObject *parent) : QObject(parent)
{
    qDBusRegisterMetaType<PortalShortcut>();
    qDBusRegisterMetaType<PortalShortcutList>();
    QDBusConnection::sessionBus().connect("org.freedesktop.portal.Desktop",
        "/org/freedesktop/portal/desktop", "org.freedesktop.portal.GlobalShortcuts",
        "Activated", this, SLOT(portalActivated(QDBusObjectPath,QString,qulonglong,QVariantMap)));
}

GlobalShortcut::~GlobalShortcut()
{
    releaseX11Shortcut();
}

bool GlobalShortcut::registerX11Shortcut(const QString &shortcut)
{
#ifdef PURRFIND_WITH_X11_SHORTCUT
    // Use the native X11 grab only for an X11 session.  XWayland may expose a
    // DISPLAY while the compositor still owns Super-key events, so Wayland
    // sessions are routed through their compositor integration below.
    if (qEnvironmentVariable("DISPLAY").isEmpty())
        return false;

    const QStringList parts = shortcut.split('+', Qt::SkipEmptyParts);
    if (parts.isEmpty()) return false;
    unsigned int modifiers = 0;
    for (int i = 0; i + 1 < parts.size(); ++i) {
        const QString modifier = parts.at(i).trimmed();
        if (modifier.compare("Super", Qt::CaseInsensitive) == 0
            || modifier.compare("Meta", Qt::CaseInsensitive) == 0)
            modifiers |= Mod4Mask;
        else if (modifier.compare("Ctrl", Qt::CaseInsensitive) == 0
                 || modifier.compare("Control", Qt::CaseInsensitive) == 0)
            modifiers |= ControlMask;
        else if (modifier.compare("Alt", Qt::CaseInsensitive) == 0)
            modifiers |= Mod1Mask;
        else if (modifier.compare("Shift", Qt::CaseInsensitive) == 0)
            modifiers |= ShiftMask;
        else
            return false;
    }

    const QByteArray keyName = parts.constLast().trimmed().toLatin1();
    KeySym symbol = XStringToKeysym(keyName.constData());
    if (symbol == NoSymbol && keyName.size() == 1)
        symbol = XStringToKeysym(keyName.toUpper().constData());
    if (symbol == NoSymbol) return false;

    Display *display = XOpenDisplay(nullptr);
    if (!display) return false;
    const KeyCode keyCode = XKeysymToKeycode(display, symbol);
    if (!keyCode) { XCloseDisplay(display); return false; }

    const unsigned int ignored = LockMask
        | modifierMaskForKey(display, XK_Num_Lock)
        | modifierMaskForKey(display, XK_Scroll_Lock);
    x11GrabFailed = false;
    auto previousHandler = XSetErrorHandler(shortcutX11ErrorHandler);
    const Window root = DefaultRootWindow(display);
    for (unsigned int subset = ignored;; subset = (subset - 1) & ignored) {
        XGrabKey(display, keyCode, modifiers | subset, root, False,
                 GrabModeAsync, GrabModeAsync);
        if (subset == 0) break;
    }
    XSync(display, False);
    XSetErrorHandler(previousHandler);
    if (x11GrabFailed) {
        for (unsigned int subset = ignored;; subset = (subset - 1) & ignored) {
            XUngrabKey(display, keyCode, modifiers | subset, root);
            if (subset == 0) break;
        }
        XCloseDisplay(display);
        error_ = "The shortcut is already in use by another application";
        return false;
    }

    xDisplay_ = display;
    xKeyCode_ = keyCode;
    xModifiers_ = modifiers;
    xIgnoredModifiers_ = ignored;
    xNotifier_ = new QSocketNotifier(ConnectionNumber(display), QSocketNotifier::Read, this);
    connect(xNotifier_, &QSocketNotifier::activated, this,
            [this](QSocketDescriptor, QSocketNotifier::Type) { processX11Events(); });
    QTimer::singleShot(0, this, [this] { emit registrationChanged(true, {}); });
    return true;
#else
    Q_UNUSED(shortcut);
    return false;
#endif
}

namespace {

bool isWaylandSession()
{
    return qEnvironmentVariable("XDG_SESSION_TYPE").compare("wayland", Qt::CaseInsensitive) == 0
        || !qEnvironmentVariable("WAYLAND_DISPLAY").isEmpty();
}

bool isGnomeSession()
{
    const auto desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP").toLower();
    return desktop.contains("gnome") || desktop.contains("ubuntu");
}

QString gnomeTrigger(const QString &shortcut)
{
    const auto parts = shortcut.split('+', Qt::SkipEmptyParts);
    if (parts.isEmpty()) return {};
    QString trigger;
    for (int i = 0; i + 1 < parts.size(); ++i) {
        const auto modifier = parts.at(i).trimmed();
        if (modifier.compare("Super", Qt::CaseInsensitive) == 0
            || modifier.compare("Meta", Qt::CaseInsensitive) == 0
            || modifier.compare("Logo", Qt::CaseInsensitive) == 0)
            trigger += "<Super>";
        else if (modifier.compare("Ctrl", Qt::CaseInsensitive) == 0
                 || modifier.compare("Control", Qt::CaseInsensitive) == 0)
            trigger += "<Control>";
        else if (modifier.compare("Alt", Qt::CaseInsensitive) == 0)
            trigger += "<Alt>";
        else if (modifier.compare("Shift", Qt::CaseInsensitive) == 0)
            trigger += "<Shift>";
        else
            return {};
    }
    trigger += parts.constLast().trimmed().toLower();
    return trigger;
}

bool runGsettings(const QStringList &arguments, QByteArray *standardOutput = nullptr)
{
    QProcess process;
    process.start("gsettings", arguments);
    if (!process.waitForStarted(1500) || !process.waitForFinished(3000)
        || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return false;
    if (standardOutput) *standardOutput = process.readAllStandardOutput();
    return true;
}

} // namespace

bool GlobalShortcut::registerGnomeShortcut(const QString &shortcut)
{
    const QString trigger = gnomeTrigger(shortcut);
    if (trigger.isEmpty()) return false;

    constexpr auto schema = "org.gnome.settings-daemon.plugins.media-keys";
    constexpr auto key = "custom-keybindings";
    constexpr auto path = "/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/purrfind/";
    QByteArray configured;
    if (!runGsettings({"get", schema, key}, &configured)) return false;

    QStringList paths;
    const QRegularExpression quoted(R"('([^']+)')");
    auto match = quoted.globalMatch(QString::fromUtf8(configured));
    while (match.hasNext()) paths.append(match.next().captured(1));
    if (!paths.contains(QString::fromLatin1(path))) paths.append(QString::fromLatin1(path));
    QStringList quotedPaths;
    for (const auto &entry : paths) quotedPaths.append("'" + entry + "'");
    if (!runGsettings({"set", schema, key, "[" + quotedPaths.join(", ") + "]"})) return false;

    const QString keySchema = QString::fromLatin1(schema) + ":" + QString::fromLatin1(path);
    const QString command = QCoreApplication::applicationFilePath();
    if (!runGsettings({"set", keySchema, "name", "'PurrFind'"})
        || !runGsettings({"set", keySchema, "command", "'" + command + "'"})
        || !runGsettings({"set", keySchema, "binding", "'" + trigger + "'"}))
        return false;
    QTimer::singleShot(0, this, [this] { emit registrationChanged(true, {}); });
    return true;
}

void GlobalShortcut::releaseX11Shortcut()
{
#ifdef PURRFIND_WITH_X11_SHORTCUT
    if (!xDisplay_) return;
    auto *display = static_cast<Display *>(xDisplay_);
    if (xNotifier_) xNotifier_->setEnabled(false);
    const Window root = DefaultRootWindow(display);
    for (unsigned int subset = xIgnoredModifiers_;; subset = (subset - 1) & xIgnoredModifiers_) {
        XUngrabKey(display, xKeyCode_, xModifiers_ | subset, root);
        if (subset == 0) break;
    }
    XSync(display, False);
    delete xNotifier_;
    xNotifier_ = nullptr;
    XCloseDisplay(display);
    xDisplay_ = nullptr;
#endif
}

void GlobalShortcut::processX11Events()
{
#ifdef PURRFIND_WITH_X11_SHORTCUT
    auto *display = static_cast<Display *>(xDisplay_);
    if (!display) return;
    while (XPending(display)) {
        XEvent event;
        XNextEvent(display, &event);
        if (event.type != KeyPress) continue;
        const unsigned int effective = event.xkey.state & ~xIgnoredModifiers_;
        if (event.xkey.keycode == xKeyCode_ && effective == xModifiers_) emit activated();
    }
#endif
}

QString GlobalShortcut::portalTrigger(const QString &shortcut) const
{
    const auto parts = shortcut.split('+', Qt::SkipEmptyParts);
    QString trigger;
    for (int i = 0; i + 1 < parts.size(); ++i) {
        QString modifier = parts.at(i).trimmed();
        if (modifier.compare("Super", Qt::CaseInsensitive) == 0
            || modifier.compare("Meta", Qt::CaseInsensitive) == 0
            || modifier.compare("Logo", Qt::CaseInsensitive) == 0)
            modifier = "LOGO";
        else if (modifier.compare("Ctrl", Qt::CaseInsensitive) == 0
                 || modifier.compare("Control", Qt::CaseInsensitive) == 0)
            modifier = "CTRL";
        else if (modifier.compare("Alt", Qt::CaseInsensitive) == 0)
            modifier = "ALT";
        else if (modifier.compare("Shift", Qt::CaseInsensitive) == 0)
            modifier = "SHIFT";
        else
            return {};
        trigger += modifier + '+';
    }
    if (!parts.isEmpty()) trigger += parts.last().trimmed().toLower();
    return trigger;
}

void GlobalShortcut::connectRequest(const QDBusObjectPath &request, const char *slot)
{
    QDBusConnection::sessionBus().connect("org.freedesktop.portal.Desktop", request.path(),
        "org.freedesktop.portal.Request", "Response", this, slot);
}

QDBusObjectPath GlobalShortcut::requestPath(const QString &token) const
{
    QString sender = QDBusConnection::sessionBus().baseService();
    if (sender.startsWith(':')) sender.remove(0, 1);
    sender.replace('.', '_');
    return QDBusObjectPath(QString("/org/freedesktop/portal/desktop/request/%1/%2")
                               .arg(sender, token));
}

void GlobalShortcut::registerShortcut(const QString &shortcut)
{
    requestedShortcut_ = shortcut;
    error_.clear();
    releaseX11Shortcut();
    // XWayland can report a successful X11 grab while the compositor keeps
    // Super-key events. Prefer the compositor-aware path on Wayland.
    if (!isWaylandSession() && registerX11Shortcut(shortcut)) return;
    // GNOME 46 (Ubuntu 24.04) does not expose the GlobalShortcuts portal.
    // Its user custom-keybindings API is the supported native fallback.
    if (isWaylandSession() && isGnomeSession() && registerGnomeShortcut(shortcut)) return;
    const QString token = QString("purrfind_%1").arg(QRandomGenerator::global()->generate());
    QVariantMap options{{"handle_token", token}, {"session_handle_token", token + "_session"}};
    const QDBusObjectPath expectedRequest = requestPath(token);
    // The portal may emit Response before the synchronous CreateSession call
    // returns. Subscribe to the token-derived path first so that fast desktop
    // implementations cannot race us.
    connectRequest(expectedRequest, SLOT(sessionResponse(uint,QVariantMap)));
    QDBusInterface portal("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                          "org.freedesktop.portal.GlobalShortcuts", QDBusConnection::sessionBus());
    auto *watcher = new QDBusPendingCallWatcher(portal.asyncCall("CreateSession", options), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
        [this, watcher, expectedRequest](QDBusPendingCallWatcher *) {
            QDBusPendingReply<QDBusObjectPath> reply = *watcher;
            watcher->deleteLater();
            if (reply.isError()) {
                error_ = reply.error().message().isEmpty()
                    ? "Global shortcut portal is unavailable" : reply.error().message();
                emit registrationChanged(false, error_);
                return;
            }
            const QDBusObjectPath actualRequest = reply.value();
            if (actualRequest.path() != expectedRequest.path())
                connectRequest(actualRequest, SLOT(sessionResponse(uint,QVariantMap)));
        });
}

void GlobalShortcut::sessionResponse(uint response, const QVariantMap &results)
{
    if (response != 0) {
        error_ = "Desktop denied global shortcut session";
        emit registrationChanged(false, error_);
        return;
    }
    // GlobalShortcuts specifies session_handle as a string for historical
    // compatibility, even though its value is an object path.  Some portal
    // implementations therefore expose a QString here while others may expose
    // QDBusObjectPath.  Accept both representations before calling
    // BindShortcuts, whose argument is an actual object path.
    const QVariant sessionValue = results.value("session_handle");
    const QString sessionPath = sessionValue.metaType() == QMetaType::fromType<QDBusObjectPath>()
        ? sessionValue.value<QDBusObjectPath>().path()
        : sessionValue.toString();
    if (sessionPath.isEmpty() || !sessionPath.startsWith('/')) {
        error_ = "Desktop returned an invalid global shortcut session";
        emit registrationChanged(false, error_);
        return;
    }
    session_ = QDBusObjectPath(sessionPath);
    PortalShortcutList shortcuts;
    shortcuts.append({"show", {{"description", "Show PurrFind"},
                                {"preferred_trigger", portalTrigger(requestedShortcut_)}}});
    const QString token = QString("purrfind_bind_%1").arg(QRandomGenerator::global()->generate());
    QVariantMap options{{"handle_token", token}};
    const QDBusObjectPath expectedRequest = requestPath(token);
    connectRequest(expectedRequest, SLOT(bindingResponse(uint,QVariantMap)));
    QDBusInterface portal("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                          "org.freedesktop.portal.GlobalShortcuts", QDBusConnection::sessionBus());
    auto *watcher = new QDBusPendingCallWatcher(
        portal.asyncCall("BindShortcuts", QVariant::fromValue(session_),
                         QVariant::fromValue(shortcuts), QString(), options), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
        [this, watcher, expectedRequest](QDBusPendingCallWatcher *) {
            QDBusPendingReply<QDBusObjectPath> reply = *watcher;
            watcher->deleteLater();
            if (reply.isError()) {
                error_ = reply.error().message();
                emit registrationChanged(false, error_);
                return;
            }
            const QDBusObjectPath actualRequest = reply.value();
            if (actualRequest.path() != expectedRequest.path())
                connectRequest(actualRequest, SLOT(bindingResponse(uint,QVariantMap)));
        });
}

void GlobalShortcut::bindingResponse(uint response, const QVariantMap &)
{
    if (response == 0) emit registrationChanged(true, {});
    else {
        error_ = "Desktop denied the requested shortcut";
        emit registrationChanged(false, error_);
    }
}

void GlobalShortcut::portalActivated(const QDBusObjectPath &session, const QString &id,
                                     qulonglong, const QVariantMap &)
{
    if (session.path() == session_.path() && id == "show") emit activated();
}

} // namespace purrfind
