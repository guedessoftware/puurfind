#pragma once

#include <QDBusObjectPath>
#include <QDBusArgument>
#include <QObject>
#include <QTimer>
#include <QVariantMap>

class QSocketNotifier;

namespace purrfind {

struct PortalShortcut {
    QString id;
    QVariantMap options;
};
using PortalShortcutList = QList<PortalShortcut>;

QDBusArgument &operator<<(QDBusArgument &argument, const PortalShortcut &shortcut);
const QDBusArgument &operator>>(const QDBusArgument &argument, PortalShortcut &shortcut);

class GlobalShortcut : public QObject {
    Q_OBJECT
public:
    explicit GlobalShortcut(QObject *parent = nullptr);
    ~GlobalShortcut() override;
    void registerShortcut(const QString &shortcut);
    QString error() const { return error_; }

signals:
    void activated();
    void registrationChanged(bool available, const QString &message);

private slots:
    void sessionResponse(uint response, const QVariantMap &results);
    void bindingResponse(uint response, const QVariantMap &results);
    void portalActivated(const QDBusObjectPath &session, const QString &id,
                         qulonglong timestamp, const QVariantMap &options);

private:
    bool registerX11Shortcut(const QString &shortcut);
    bool registerGnomeShortcut(const QString &shortcut);
    void attemptRegistration();
    void scheduleRetry(const QString &message);
    void releaseX11Shortcut();
    void processX11Events();
    void connectRequest(const QDBusObjectPath &request, const char *slot);
    QDBusObjectPath requestPath(const QString &token) const;
    QString portalTrigger(const QString &shortcut) const;
    QDBusObjectPath session_;
    QString requestedShortcut_;
    QString error_;
    QTimer retryTimer_;
    int retryDelayMs_{2000};
    bool registered_{false};
    quint64 registrationGeneration_{0};
    void *xDisplay_{nullptr};
    QSocketNotifier *xNotifier_{nullptr};
    unsigned int xKeyCode_{0};
    unsigned int xModifiers_{0};
    unsigned int xIgnoredModifiers_{0};
};

} // namespace purrfind

Q_DECLARE_METATYPE(purrfind::PortalShortcut)
Q_DECLARE_METATYPE(purrfind::PortalShortcutList)
