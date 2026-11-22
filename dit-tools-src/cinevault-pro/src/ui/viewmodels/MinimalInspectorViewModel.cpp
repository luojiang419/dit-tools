#include "ui/viewmodels/MinimalInspectorViewModel.h"

#include "shared/Formatters.h"
#include "ui/viewmodels/MinimalLibraryWorkspaceViewModel.h"
#include "ui/viewmodels/MinimalSourceRailViewModel.h"

#include <QVariantMap>

namespace {
QVariantList makeRows(const QList<QPair<QString, QString>> &items)
{
    QVariantList rows;
    for (const auto &item : items) {
        QVariantMap row;
        row.insert(QStringLiteral("label"), item.first);
        row.insert(QStringLiteral("value"), item.second);
        rows.append(row);
    }
    return rows;
}
}

MinimalInspectorViewModel::MinimalInspectorViewModel(MinimalSourceRailViewModel *sourceRailViewModel,
                                                     MinimalLibraryWorkspaceViewModel *libraryWorkspaceViewModel,
                                                     QObject *parent)
    : QObject(parent)
    , m_sourceRailViewModel(sourceRailViewModel)
    , m_libraryWorkspaceViewModel(libraryWorkspaceViewModel)
{
    clear();
}

QString MinimalInspectorViewModel::title() const
{
    return m_title;
}

QString MinimalInspectorViewModel::subtitle() const
{
    return m_subtitle;
}

QVariantList MinimalInspectorViewModel::details() const
{
    return m_details;
}

void MinimalInspectorViewModel::showSource(qint64 sourceRootId)
{
    const auto source = m_sourceRailViewModel->sourceById(sourceRootId);
    if (source.id == 0) {
        clear();
        return;
    }

    m_title = source.name;
    m_subtitle = Formatters::statusLabel(source.status);
    m_details = makeRows({
        {QStringLiteral("源路径"), source.path},
        {QStringLiteral("文件数"), QString::number(source.totalFiles)},
        {QStringLiteral("文件夹数"), QString::number(source.totalFolders)},
        {QStringLiteral("总容量"), Formatters::formatBytes(source.totalSizeBytes)},
        {QStringLiteral("视频数"), QString::number(source.videoCount)},
        {QStringLiteral("警告数"), QString::number(source.warningCount)}
    });
    emit stateChanged();
}

void MinimalInspectorViewModel::showAsset(qint64 assetId)
{
    const auto asset = m_libraryWorkspaceViewModel->assetById(assetId);
    if (asset.id == 0) {
        clear();
        return;
    }

    m_title = asset.name;
    m_subtitle = Formatters::assetTypeLabel(asset.assetType);
    m_details = makeRows({
        {QStringLiteral("相对路径"), asset.relativePath},
        {QStringLiteral("绝对路径"), asset.absolutePath},
        {QStringLiteral("类型"), Formatters::assetTypeLabel(asset.assetType)},
        {QStringLiteral("文件大小"), Formatters::formatBytes(asset.sizeBytes)},
        {QStringLiteral("修改时间"), asset.modifiedAt},
        {QStringLiteral("可读状态"), asset.readable ? QStringLiteral("正常") : QStringLiteral("不可读")}
    });
    emit stateChanged();
}

void MinimalInspectorViewModel::clear()
{
    m_title = QStringLiteral("检查器");
    m_subtitle = QStringLiteral("当前为最小GUI测试模式");
    m_details = makeRows({
        {QStringLiteral("测试目标"), QStringLiteral("验证窗口壳体、切页与基础联动")},
        {QStringLiteral("当前限制"), QStringLiteral("项目、导入、扫描、报表与媒体处理尚未接回")}
    });
    emit stateChanged();
}
