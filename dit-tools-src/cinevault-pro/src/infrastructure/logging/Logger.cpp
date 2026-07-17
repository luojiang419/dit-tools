#include "infrastructure/logging/Logger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>

namespace {
constexpr qint64 kMaxLogFileBytes = 5 * 1024 * 1024;
constexpr int kBackupFileCount = 3;
constexpr int kMaxMessageCharacters = 256 * 1024;

struct LoggerState {
    QMutex mutex;
    QString path;
    QFile file;
};

LoggerState &loggerState()
{
    static LoggerState state;
    return state;
}

QString backupPath(const QString &path, int index)
{
    return QStringLiteral("%1.%2").arg(path).arg(index);
}

bool rotateLocked(LoggerState &state)
{
    state.file.close();
    for (int index = kBackupFileCount; index >= 1; --index) {
        const auto destination = backupPath(state.path, index);
        const auto source = index == 1
            ? state.path
            : backupPath(state.path, index - 1);
        if (QFileInfo::exists(destination) && !QFile::remove(destination)) {
            break;
        }
        if (QFileInfo::exists(source) && !QFile::rename(source, destination)) {
            break;
        }
    }
    state.file.setFileName(state.path);
    return state.file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}

bool openLocked(LoggerState &state, const QString &path)
{
    state.file.close();
    state.path = path;
    state.file.setFileName(path);
    if (QFileInfo(path).size() >= kMaxLogFileBytes) {
        return rotateLocked(state);
    }
    return state.file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}
}

bool Logger::initialize(const QString &logFilePath, QString *errorMessage)
{
    const auto normalizedPath = QFileInfo(logFilePath).absoluteFilePath();
    QFileInfo info(normalizedPath);
    QDir dir;
    if (!dir.mkpath(info.absolutePath())) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法创建日志目录：%1").arg(info.absolutePath());
        }
        return false;
    }

    auto &state = loggerState();
    QMutexLocker locker(&state.mutex);
    if (!openLocked(state, normalizedPath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法打开日志文件：%1").arg(normalizedPath);
        }
        return false;
    }
    const auto line = QStringLiteral("\n[%1] [INFO] 日志系统已初始化\n")
                          .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs))
                          .toUtf8();
    state.file.write(line);
    state.file.flush();
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

void Logger::info(const QString &message)
{
    write(QStringLiteral("INFO"), message);
}

void Logger::warn(const QString &message)
{
    write(QStringLiteral("WARN"), message);
}

void Logger::error(const QString &message)
{
    write(QStringLiteral("ERROR"), message);
}

QString Logger::currentLogFile()
{
    auto &state = loggerState();
    QMutexLocker locker(&state.mutex);
    return state.path;
}

void Logger::shutdown()
{
    auto &state = loggerState();
    QMutexLocker locker(&state.mutex);
    state.file.flush();
    state.file.close();
    state.path.clear();
}

void Logger::write(const QString &level, const QString &message)
{
    auto &state = loggerState();
    QMutexLocker locker(&state.mutex);
    if (state.path.isEmpty() || !state.file.isOpen()) {
        return;
    }

    auto boundedMessage = message;
    if (boundedMessage.size() > kMaxMessageCharacters) {
        boundedMessage = boundedMessage.left(kMaxMessageCharacters)
            + QStringLiteral("…[日志消息已截断]");
    }
    const auto line = QStringLiteral("[%1] [%2] %3\n")
                          .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                               level,
                               boundedMessage)
                          .toUtf8();
    if (state.file.size() + line.size() > kMaxLogFileBytes
        && !rotateLocked(state)) {
        return;
    }
    state.file.write(line);
    if (level == QStringLiteral("WARN") || level == QStringLiteral("ERROR")) {
        state.file.flush();
    }
}
