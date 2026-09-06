#include "indexer/InotifyWatcher.h"

#include "core/FileSystem.h"
#include "core/Logging.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSocketNotifier>

#include <sys/inotify.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <filesystem>

namespace purrfind {

InotifyWatcher::InotifyWatcher(QObject *parent) : QObject(parent)
{
    scanTimer_.setSingleShot(true);
    scanTimer_.setInterval(0);
    connect(&scanTimer_, &QTimer::timeout, this, &InotifyWatcher::scanWatchBatch);
}
InotifyWatcher::~InotifyWatcher() { stop(); }

bool InotifyWatcher::start(const QStringList &roots, const QStringList &exclusions,
                           QString *error, bool excludeHidden)
{
    stop();
    roots_.reserve(roots.size());
    for (const auto &root : roots) roots_.append(FileSystem::normalizePath(root));
    exclusions_ = exclusions;
    excludeHidden_ = excludeHidden;
    watchLimitReached_ = false;
    QFile limitFile("/proc/sys/fs/inotify/max_user_watches");
    if (limitFile.open(QIODevice::ReadOnly)) systemWatchLimit_ = limitFile.readAll().trimmed().toLongLong();
    descriptor_ = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (descriptor_ < 0) {
        if (error) *error = QString::fromLocal8Bit(std::strerror(errno));
        return false;
    }
    notifier_ = new QSocketNotifier(descriptor_, QSocketNotifier::Read, this);
    connect(notifier_, &QSocketNotifier::activated, this, &InotifyWatcher::readEvents);
    for (const auto &root : roots) {
        addDirectory(root); // The configured root must be watched before start() returns.
        enqueueTree(root);
    }
    return true;
}

void InotifyWatcher::stop()
{
    if (notifier_) {
        delete notifier_;
        notifier_ = nullptr;
    }
    if (descriptor_ >= 0) ::close(descriptor_);
    descriptor_ = -1;
    scanTimer_.stop();
    watches_.clear();
    watchedPaths_.clear();
    queuedPaths_.clear();
    pendingDirectories_.clear();
    movedFrom_.clear();
    roots_.clear();
    excludeHidden_ = false;
}

bool InotifyWatcher::isIgnored(const QString &path) const
{
    const QString normalized = FileSystem::normalizePath(path);
    if (FileSystem::isExcluded(normalized, exclusions_)) return true;
    if (!excludeHidden_) return false;
    QString bestRoot;
    for (const auto &root : roots_) {
        if (FileSystem::isWithin(normalized, root) && root.size() > bestRoot.size())
            bestRoot = root;
    }
    return !bestRoot.isEmpty() && FileSystem::isHiddenWithin(normalized, bestRoot);
}

void InotifyWatcher::addDirectory(const QString &path)
{
    const QString normalized = FileSystem::normalizePath(path);
    if (descriptor_ < 0 || watchedPaths_.contains(normalized) || isIgnored(normalized)) return;
    const QByteArray encoded = QFile::encodeName(normalized);
    const uint32_t mask = IN_CREATE | IN_CLOSE_WRITE | IN_ATTRIB | IN_DELETE
        | IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF;
    const int watch = ::inotify_add_watch(descriptor_, encoded.constData(), mask);
    if (watch >= 0) {
        watches_.insert(watch, normalized);
        watchedPaths_.insert(normalized);
    }
    else if (errno == ENOSPC) {
        if (!watchLimitReached_)
            emit warning(QString("inotify watch limit reached (%1); index may be incomplete; increase the user limit or reduce indexed roots")
                         .arg(systemWatchLimit_));
        watchLimitReached_ = true;
    } else if (errno != EACCES && errno != ENOENT)
        emit warning(QString("Cannot watch %1: %2").arg(path, QString::fromLocal8Bit(std::strerror(errno))));
}

void InotifyWatcher::enqueueTree(const QString &root)
{
    const QString normalized = FileSystem::normalizePath(root);
    const QFileInfo info(normalized);
    if (!info.isDir() || info.isSymLink() || queuedPaths_.contains(normalized)
        || isIgnored(normalized)) return;
    queuedPaths_.insert(normalized);
    pendingDirectories_.enqueue(normalized);
    if (!scanTimer_.isActive()) scanTimer_.start();
}

void InotifyWatcher::scanWatchBatch()
{
    constexpr int directoriesPerTurn = 32;
    int processed = 0;
    while (descriptor_ >= 0 && !pendingDirectories_.isEmpty() && processed++ < directoriesPerTurn) {
        const QString directory = pendingDirectories_.dequeue();
        queuedPaths_.remove(directory);
        const QFileInfo info(directory);
        if (!info.isDir() || info.isSymLink() || isIgnored(directory)) continue;
        addDirectory(directory);
        const QFileInfoList children = QDir(directory).entryInfoList(
            QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
            QDir::NoSort);
        for (const auto &child : children)
            if (!child.isSymLink()) enqueueTree(child.absoluteFilePath());
    }
    if (descriptor_ >= 0 && !pendingDirectories_.isEmpty()) scanTimer_.start();
}

void InotifyWatcher::readEvents()
{
    alignas(inotify_event) char buffer[64 * 1024];
    while (true) {
        const ssize_t length = ::read(descriptor_, buffer, sizeof(buffer));
        if (length < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        if (length <= 0) break;
        for (const char *cursor = buffer; cursor < buffer + length;) {
            const auto *event = reinterpret_cast<const inotify_event *>(cursor);
            cursor += sizeof(inotify_event) + event->len;
            if (event->mask & IN_Q_OVERFLOW) {
                emit warning("inotify queue overflow; scheduling reconciliation");
                emit eventReceived({EventKind::Reconcile, {}, false, {}});
                continue;
            }
            const QString directory = watches_.value(event->wd);
            if (directory.isEmpty()) continue;
            const QString name = event->len ? QFile::decodeName(event->name) : QString();
            const QString path = name.isEmpty() ? directory : directory + '/' + name;
            const bool isDirectory = event->mask & IN_ISDIR;
            qCDebug(logTrace).noquote() << "inotify" << QString::number(event->mask, 16) << path;
            if (isIgnored(path)) continue;
            if (event->mask & IN_MOVED_FROM) {
                FsEvent removed{EventKind::Remove, path, isDirectory, {}};
                movedFrom_.insert(event->cookie, removed);
                emit eventReceived(removed);
            } else if (event->mask & (IN_DELETE | IN_DELETE_SELF | IN_MOVE_SELF)) {
                emit eventReceived({EventKind::Remove, path, isDirectory, {}});
            }
            if (event->mask & IN_MOVED_TO) {
                if (isDirectory) enqueueTree(path);
                const auto previous = movedFrom_.take(event->cookie);
                if (!previous.path.isEmpty())
                    emit eventReceived({EventKind::Rename, path, isDirectory, previous.path});
                else
                    emit eventReceived({EventKind::Upsert, path, isDirectory, {}});
            } else if (event->mask & (IN_CREATE | IN_CLOSE_WRITE | IN_ATTRIB)) {
                if (isDirectory && (event->mask & IN_CREATE)) enqueueTree(path);
                emit eventReceived({EventKind::Upsert, path, isDirectory, {}});
            }
            if (event->mask & IN_IGNORED) {
                watchedPaths_.remove(watches_.value(event->wd));
                watches_.remove(event->wd);
            }
        }
        if (movedFrom_.size() > 4096) movedFrom_.clear();
    }
}

} // namespace purrfind
