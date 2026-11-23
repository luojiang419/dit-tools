#include "application/StorageVolumeService.h"

#include <QDir>
#include <QLocale>
#include <QStorageInfo>
#include <QVariantMap>

#include <algorithm>
#include <utility>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {
QString driveTypeLabel(const QString &rootPath)
{
#ifdef Q_OS_WIN
    const auto nativeRoot = QDir::toNativeSeparators(rootPath);
    switch (GetDriveTypeW(reinterpret_cast<LPCWSTR>(nativeRoot.utf16()))) {
    case DRIVE_REMOVABLE: return QStringLiteral("可移动磁盘");
    case DRIVE_FIXED: return QStringLiteral("本地磁盘");
    case DRIVE_REMOTE: return QStringLiteral("网络磁盘");
    case DRIVE_CDROM: return QStringLiteral("光盘");
    case DRIVE_RAMDISK: return QStringLiteral("内存磁盘");
    default: return QStringLiteral("磁盘卷");
    }
#else
    Q_UNUSED(rootPath)
    return QStringLiteral("磁盘卷");
#endif
}

QString capacityText(qint64 available, qint64 total)
{
    const QLocale locale;
    if (total <= 0) {
        return QStringLiteral("容量未知");
    }
    return QStringLiteral("可用 %1 / 共 %2")
        .arg(locale.formattedDataSize(available), locale.formattedDataSize(total));
}
}

StorageVolumeService::StorageVolumeService(QObject *parent)
    : QObject(parent)
{
    refresh();
}

QVariantList StorageVolumeService::volumes() const
{
    return m_volumes;
}

void StorageVolumeService::refresh()
{
    QVariantList nextVolumes;
    auto mountedVolumes = QStorageInfo::mountedVolumes();
    std::sort(mountedVolumes.begin(), mountedVolumes.end(), [](const QStorageInfo &left,
                                                               const QStorageInfo &right) {
        return left.rootPath().compare(right.rootPath(), Qt::CaseInsensitive) < 0;
    });

    for (const auto &storage : std::as_const(mountedVolumes)) {
        if (!storage.isValid() || !storage.isReady() || storage.rootPath().trimmed().isEmpty()) {
            continue;
        }

        const auto rootPath = QDir::toNativeSeparators(QDir::cleanPath(storage.rootPath()));
        auto displayName = storage.displayName().trimmed();
        if (displayName.isEmpty()) {
            displayName = storage.name().trimmed();
        }
        if (displayName.isEmpty()) {
            displayName = rootPath;
        }
        const auto label = displayName.compare(rootPath, Qt::CaseInsensitive) == 0
            ? rootPath
            : QStringLiteral("%1 (%2)").arg(displayName, rootPath);

        nextVolumes.append(QVariantMap{
            {QStringLiteral("rootPath"), rootPath},
            {QStringLiteral("label"), label},
            {QStringLiteral("displayName"), displayName},
            {QStringLiteral("driveType"), driveTypeLabel(rootPath)},
            {QStringLiteral("fileSystem"), QString::fromLatin1(storage.fileSystemType())},
            {QStringLiteral("bytesTotal"), storage.bytesTotal()},
            {QStringLiteral("bytesAvailable"), storage.bytesAvailable()},
            {QStringLiteral("capacityText"), capacityText(storage.bytesAvailable(), storage.bytesTotal())},
            {QStringLiteral("readOnly"), storage.isReadOnly()}
        });
    }

    if (m_volumes == nextVolumes) {
        return;
    }
    m_volumes = std::move(nextVolumes);
    emit volumesChanged();
}
