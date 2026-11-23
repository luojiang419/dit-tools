#include "shared/FileRevealService.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QUrl>

#ifdef Q_OS_WIN
#include <shlobj.h>
#endif

namespace {
void setError(QString *errorMessage, const QString &message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
}

#ifdef Q_OS_WIN
bool revealFileWithWindowsShell(const QString &filePath)
{
    auto *absoluteItem = ILCreateFromPathW(
        reinterpret_cast<PCWSTR>(filePath.utf16()));
    if (!absoluteItem) {
        return false;
    }

    auto *parentFolder = ILCloneFull(absoluteItem);
    const auto *childItem = ILFindLastID(absoluteItem);
    if (!parentFolder || !childItem || !ILRemoveLastID(parentFolder)) {
        if (parentFolder) {
            ILFree(parentFolder);
        }
        ILFree(absoluteItem);
        return false;
    }

    const auto result = SHOpenFolderAndSelectItems(parentFolder, 1, &childItem, 0);
    ILFree(parentFolder);
    ILFree(absoluteItem);
    return SUCCEEDED(result);
}
#endif
}

bool FileRevealService::revealFile(const QString &filePath, QString *errorMessage)
{
    const QFileInfo info(filePath);
    if (!info.exists()) {
        setError(errorMessage, QStringLiteral("文件不存在：%1").arg(info.absoluteFilePath()));
        return false;
    }
    if (info.isDir()) {
        return openDirectory(info.absoluteFilePath(), errorMessage);
    }
    if (!info.isFile()) {
        setError(errorMessage, QStringLiteral("目标不是可定位的普通文件：%1").arg(info.absoluteFilePath()));
        return false;
    }

#ifdef Q_OS_WIN
    const auto nativePath = QDir::toNativeSeparators(info.absoluteFilePath());
    if (revealFileWithWindowsShell(nativePath)) {
        return true;
    }
    if (QProcess::startDetached(QStringLiteral("explorer.exe"),
                                {QStringLiteral("/select,%1").arg(nativePath)})) {
        return true;
    }
#endif

    if (QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()))) {
        setError(errorMessage, QStringLiteral("系统不支持选中文件，已打开所在目录。"));
        return true;
    }
    setError(errorMessage, QStringLiteral("无法打开文件所在目录：%1").arg(info.absolutePath()));
    return false;
}

bool FileRevealService::openDirectory(const QString &directoryPath, QString *errorMessage)
{
    const QFileInfo info(directoryPath);
    if (!info.exists() || !info.isDir()) {
        setError(errorMessage, QStringLiteral("目录不存在：%1").arg(info.absoluteFilePath()));
        return false;
    }
    if (QDesktopServices::openUrl(QUrl::fromLocalFile(info.absoluteFilePath()))) {
        return true;
    }
    setError(errorMessage, QStringLiteral("无法打开目录：%1").arg(info.absoluteFilePath()));
    return false;
}
