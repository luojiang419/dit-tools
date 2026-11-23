#include "application/SourceChangeMonitor.h"

#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>

#include <algorithm>
#include <array>
#include <cstddef>
#include <stop_token>
#include <thread>
#include <utility>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

struct SourceChangeMonitor::WatchRegistration {
    qint64 sourceRootId = 0;
    QString sourcePath;
    std::jthread thread;
};

namespace {
constexpr qsizetype MaximumChangedPathsPerBatch = 4096;

QString normalizedAbsolutePath(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool isPathInside(const QString &candidatePath, const QString &rootPath)
{
    auto candidate = QDir::fromNativeSeparators(normalizedAbsolutePath(candidatePath));
    auto root = QDir::fromNativeSeparators(normalizedAbsolutePath(rootPath));
#ifdef Q_OS_WIN
    candidate = candidate.toCaseFolded();
    root = root.toCaseFolded();
#endif
    return candidate == root || candidate.startsWith(root + QLatin1Char('/'));
}

#ifdef Q_OS_WIN
QString windowsErrorMessage(DWORD errorCode)
{
    wchar_t *buffer = nullptr;
    const auto length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER
                                           | FORMAT_MESSAGE_FROM_SYSTEM
                                           | FORMAT_MESSAGE_IGNORE_INSERTS,
                                       nullptr,
                                       errorCode,
                                       0,
                                       reinterpret_cast<wchar_t *>(&buffer),
                                       0,
                                       nullptr);
    const auto message = length > 0 && buffer
        ? QString::fromWCharArray(buffer, static_cast<qsizetype>(length)).trimmed()
        : QStringLiteral("Windows 错误 %1").arg(errorCode);
    if (buffer) {
        LocalFree(buffer);
    }
    return message;
}

QString extendedWindowsPath(const QString &sourcePath)
{
    auto path = QDir::toNativeSeparators(QFileInfo(sourcePath).absoluteFilePath());
    if (path.startsWith(QStringLiteral("\\\\?\\"))) {
        return path;
    }
    if (path.startsWith(QStringLiteral("\\\\"))) {
        return QStringLiteral("\\\\?\\UNC\\") + path.mid(2);
    }
    return QStringLiteral("\\\\?\\") + path;
}
#endif
}

SourceChangeMonitor::SourceChangeMonitor(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<SourceChangeBatch>();
    m_debounceTimer.setSingleShot(true);
    m_debounceTimer.setInterval(1500);
    connect(&m_debounceTimer,
            &QTimer::timeout,
            this,
            &SourceChangeMonitor::flushPendingChanges);
}

SourceChangeMonitor::~SourceChangeMonitor()
{
    stop();
}

void SourceChangeMonitor::setSourceRoots(const QVector<SourceRoot> &sourceRoots)
{
    stop();

    for (const auto &sourceRoot : sourceRoots) {
        const QFileInfo info(sourceRoot.path);
        if (sourceRoot.id <= 0 || !info.exists() || !info.isDir() || !info.isReadable()) {
            postUnavailable(sourceRoot.id,
                            sourceRoot.path,
                            QStringLiteral("素材源当前不可访问，已暂停变化监测。"));
            continue;
        }

        auto watch = std::make_unique<WatchRegistration>();
        watch->sourceRootId = sourceRoot.id;
        watch->sourcePath = info.absoluteFilePath();
        const auto sourceRootId = watch->sourceRootId;
        const auto sourcePath = watch->sourcePath;
        QPointer<SourceChangeMonitor> self(this);

        watch->thread = std::jthread([self, sourceRootId, sourcePath](std::stop_token stopToken) {
#ifdef Q_OS_WIN
            const auto nativePath = extendedWindowsPath(sourcePath).toStdWString();
            const auto directoryHandle = CreateFileW(nativePath.c_str(),
                                                     FILE_LIST_DIRECTORY,
                                                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                                     nullptr,
                                                     OPEN_EXISTING,
                                                     FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                                                     nullptr);
            if (directoryHandle == INVALID_HANDLE_VALUE) {
                const auto message = QStringLiteral("无法监测目录变化：%1")
                                         .arg(windowsErrorMessage(GetLastError()));
                if (self) {
                    QMetaObject::invokeMethod(self, [self, sourceRootId, sourcePath, message]() {
                        if (self) self->postUnavailable(sourceRootId, sourcePath, message);
                    }, Qt::QueuedConnection);
                }
                return;
            }

            const auto changeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            const auto stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!changeEvent || !stopEvent) {
                const auto message = QStringLiteral("创建目录监测事件失败：%1")
                                         .arg(windowsErrorMessage(GetLastError()));
                if (changeEvent) CloseHandle(changeEvent);
                if (stopEvent) CloseHandle(stopEvent);
                CloseHandle(directoryHandle);
                if (self) {
                    QMetaObject::invokeMethod(self, [self, sourceRootId, sourcePath, message]() {
                        if (self) self->postUnavailable(sourceRootId, sourcePath, message);
                    }, Qt::QueuedConnection);
                }
                return;
            }

            std::stop_callback stopCallback(stopToken, [stopEvent]() { SetEvent(stopEvent); });
            std::array<std::byte, 32 * 1024> buffer{};
            const DWORD notifyFilter = FILE_NOTIFY_CHANGE_FILE_NAME
                | FILE_NOTIFY_CHANGE_DIR_NAME
                | FILE_NOTIFY_CHANGE_SIZE
                | FILE_NOTIFY_CHANGE_LAST_WRITE
                | FILE_NOTIFY_CHANGE_CREATION;

            while (!stopToken.stop_requested()) {
                ResetEvent(changeEvent);
                OVERLAPPED overlapped{};
                overlapped.hEvent = changeEvent;
                const auto started = ReadDirectoryChangesW(directoryHandle,
                                                           buffer.data(),
                                                           static_cast<DWORD>(buffer.size()),
                                                           TRUE,
                                                           notifyFilter,
                                                           nullptr,
                                                           &overlapped,
                                                           nullptr);
                if (!started) {
                    const auto message = QStringLiteral("目录变化监测失败：%1")
                                             .arg(windowsErrorMessage(GetLastError()));
                    if (self) {
                        QMetaObject::invokeMethod(self, [self, sourceRootId, sourcePath, message]() {
                            if (self) self->postUnavailable(sourceRootId, sourcePath, message);
                        }, Qt::QueuedConnection);
                    }
                    break;
                }

                const HANDLE events[]{changeEvent, stopEvent};
                const auto waitResult = WaitForMultipleObjects(2, events, FALSE, INFINITE);
                if (waitResult == WAIT_OBJECT_0 + 1 || stopToken.stop_requested()) {
                    CancelIoEx(directoryHandle, &overlapped);
                    break;
                }
                if (waitResult != WAIT_OBJECT_0) {
                    CancelIoEx(directoryHandle, &overlapped);
                    const auto message = QStringLiteral("等待目录变化失败：%1")
                                             .arg(windowsErrorMessage(GetLastError()));
                    if (self) {
                        QMetaObject::invokeMethod(self, [self, sourceRootId, sourcePath, message]() {
                            if (self) self->postUnavailable(sourceRootId, sourcePath, message);
                        }, Qt::QueuedConnection);
                    }
                    break;
                }

                DWORD transferred = 0;
                bool overflowed = false;
                if (!GetOverlappedResult(directoryHandle, &overlapped, &transferred, FALSE)) {
                    const auto errorCode = GetLastError();
                    if (errorCode == ERROR_OPERATION_ABORTED && stopToken.stop_requested()) {
                        break;
                    }
                    if (errorCode != ERROR_NOTIFY_ENUM_DIR) {
                        const auto message = QStringLiteral("读取目录变化失败：%1")
                                                 .arg(windowsErrorMessage(errorCode));
                        if (self) {
                            QMetaObject::invokeMethod(self, [self, sourceRootId, sourcePath, message]() {
                                if (self) self->postUnavailable(sourceRootId, sourcePath, message);
                            }, Qt::QueuedConnection);
                        }
                        break;
                    }
                    overflowed = true;
                }

                QStringList changedPaths;
                if (!overflowed && transferred > 0) {
                    DWORD offset = 0;
                    while (offset < transferred) {
                        constexpr auto headerSize = offsetof(FILE_NOTIFY_INFORMATION, FileName);
                        if (transferred - offset < headerSize) {
                            overflowed = true;
                            changedPaths.clear();
                            break;
                        }
                        const auto *notification = reinterpret_cast<const FILE_NOTIFY_INFORMATION *>(
                            buffer.data() + offset);
                        if (notification->FileNameLength > transferred - offset - headerSize) {
                            overflowed = true;
                            changedPaths.clear();
                            break;
                        }
                        const auto nameLength = static_cast<qsizetype>(
                            notification->FileNameLength / sizeof(wchar_t));
                        const auto relativePath = QDir::fromNativeSeparators(
                            QString::fromWCharArray(notification->FileName, nameLength));
                        if (!relativePath.trimmed().isEmpty()) {
                            changedPaths.append(QDir(sourcePath).absoluteFilePath(relativePath));
                        }
                        if (notification->NextEntryOffset == 0) {
                            break;
                        }
                        if (notification->NextEntryOffset > transferred - offset) {
                            overflowed = true;
                            changedPaths.clear();
                            break;
                        }
                        offset += notification->NextEntryOffset;
                    }
                } else if (transferred == 0) {
                    overflowed = true;
                }

                if (self) {
                    QMetaObject::invokeMethod(self, [self,
                                                     sourceRootId,
                                                     sourcePath,
                                                     changedPaths,
                                                     overflowed]() {
                        if (self) {
                            self->postChanges(sourceRootId,
                                              sourcePath,
                                              changedPaths,
                                              overflowed);
                        }
                    }, Qt::QueuedConnection);
                }
            }

            CloseHandle(stopEvent);
            CloseHandle(changeEvent);
            CloseHandle(directoryHandle);
#else
            Q_UNUSED(stopToken)
            if (self) {
                QMetaObject::invokeMethod(self, [self, sourceRootId, sourcePath]() {
                    if (self) {
                        self->postUnavailable(sourceRootId,
                                              sourcePath,
                                              QStringLiteral("当前平台暂不支持递归目录变化监测。"));
                    }
                }, Qt::QueuedConnection);
            }
#endif
        });
        m_watches.push_back(std::move(watch));
    }
}

void SourceChangeMonitor::stop()
{
    for (auto &watch : m_watches) {
        if (watch && watch->thread.joinable()) {
            watch->thread.request_stop();
        }
    }
    m_watches.clear();
    m_debounceTimer.stop();
    m_pendingChanges.clear();
}

int SourceChangeMonitor::watchedSourceCount() const
{
    return static_cast<int>(m_watches.size());
}

#ifdef CINEVAULT_TESTING
void SourceChangeMonitor::recordChangesForTesting(qint64 sourceRootId,
                                                  const QString &sourcePath,
                                                  const QStringList &changedPaths,
                                                  bool overflowed)
{
    postChanges(sourceRootId, sourcePath, changedPaths, overflowed);
}
#endif

void SourceChangeMonitor::postChanges(qint64 sourceRootId,
                                      const QString &sourcePath,
                                      const QStringList &changedPaths,
                                      bool overflowed)
{
    if (sourceRootId <= 0 || sourcePath.trimmed().isEmpty()) {
        return;
    }

    auto &pending = m_pendingChanges[sourceRootId];
    pending.sourcePath = normalizedAbsolutePath(sourcePath);
    pending.overflowed = pending.overflowed || overflowed;
    if (!pending.overflowed) {
        for (const auto &changedPath : changedPaths) {
            const auto absolutePath = normalizedAbsolutePath(changedPath);
            const auto parentPath = QFileInfo(absolutePath).absolutePath();
            for (const auto &candidate : {absolutePath, parentPath}) {
                if (isPathInside(candidate, pending.sourcePath)) {
                    pending.changedPaths.insert(normalizedAbsolutePath(candidate));
                }
            }
            if (pending.changedPaths.size() > MaximumChangedPathsPerBatch) {
                pending.changedPaths.clear();
                pending.overflowed = true;
                break;
            }
        }
    }
    if (pending.overflowed) {
        pending.changedPaths.clear();
    }
    m_debounceTimer.start();
}

void SourceChangeMonitor::flushPendingChanges()
{
    auto pendingChanges = std::exchange(m_pendingChanges, QHash<qint64, PendingChanges>{});
    QList<qint64> sourceRootIds = pendingChanges.keys();
    std::sort(sourceRootIds.begin(), sourceRootIds.end());
    for (const auto sourceRootId : std::as_const(sourceRootIds)) {
        const auto pending = pendingChanges.value(sourceRootId);
        SourceChangeBatch batch;
        batch.sourceRootId = sourceRootId;
        batch.sourcePath = pending.sourcePath;
        batch.changedPaths = pending.changedPaths.values();
        std::sort(batch.changedPaths.begin(), batch.changedPaths.end());
        batch.overflowed = pending.overflowed;
        emit sourceChangesDetected(batch);
        emit sourceChanged(sourceRootId, pending.sourcePath);
    }
}

void SourceChangeMonitor::postUnavailable(qint64 sourceRootId,
                                          const QString &sourcePath,
                                          const QString &message)
{
    emit sourceUnavailable(sourceRootId, sourcePath, message);
}
