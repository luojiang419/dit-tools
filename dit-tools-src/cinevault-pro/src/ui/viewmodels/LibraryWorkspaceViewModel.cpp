#include "ui/viewmodels/LibraryWorkspaceViewModel.h"

#include "shared/FileRevealService.h"

#include "application/LibraryQueryService.h"
#include "ui/models/AssetListModel.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMetaObject>
#include <QUrl>

namespace {
bool isMediaAsset(AssetType type)
{
    return type == AssetType::Video || type == AssetType::Audio;
}

bool isIssueAsset(const AssetFile &asset)
{
    return !asset.readable
        || asset.probeStatus == ProbeStatus::Failed
        || asset.probeStatus == ProbeStatus::Unsupported;
}

bool isReadyAsset(const AssetFile &asset)
{
    if (!asset.readable) {
        return false;
    }
    return !isMediaAsset(asset.assetType) || asset.probeStatus == ProbeStatus::Success;
}
}

LibraryWorkspaceViewModel::LibraryWorkspaceViewModel(LibraryQueryService *libraryQueryService, QObject *parent)
    : QObject(parent)
    , m_libraryQueryService(libraryQueryService)
    , m_model(new AssetListModel(this))
{
}

AssetListModel *LibraryWorkspaceViewModel::model() const
{
    return m_model;
}

int LibraryWorkspaceViewModel::viewMode() const
{
    return m_viewMode;
}

bool LibraryWorkspaceViewModel::favoritesOnly() const
{
    return m_favoritesOnly;
}

bool LibraryWorkspaceViewModel::modifiedTimeAscending() const
{
    return m_modifiedTimeAscending;
}

QString LibraryWorkspaceViewModel::sortOrderText() const
{
    return m_modifiedTimeAscending ? QStringLiteral("修改时间正序") : QStringLiteral("修改时间倒序");
}

QString LibraryWorkspaceViewModel::statusText() const
{
    if (m_favoritesOnly) {
        return QStringLiteral("当前结果 %1 条 · 仅显示收藏 · %2").arg(m_totalCount).arg(sortOrderText());
    }
    return QStringLiteral("当前结果 %1 条 · %2").arg(m_totalCount).arg(sortOrderText());
}

void LibraryWorkspaceViewModel::setFavoritesOnly(bool favoritesOnly)
{
    if (m_favoritesOnly == favoritesOnly) {
        return;
    }
    m_favoritesOnly = favoritesOnly;
    emit filtersChanged();
    refresh();
}

qint64 LibraryWorkspaceViewModel::totalAssetCount() const
{
    return m_totalCount;
}

qint64 LibraryWorkspaceViewModel::readyAssetCount() const
{
    return m_readyCount;
}

qint64 LibraryWorkspaceViewModel::pendingAssetCount() const
{
    return m_pendingCount;
}

qint64 LibraryWorkspaceViewModel::issueAssetCount() const
{
    return m_issueCount;
}

qint64 LibraryWorkspaceViewModel::selectedAssetId() const
{
    return m_selectedAssetId;
}

int LibraryWorkspaceViewModel::selectedAssetIndex() const
{
    for (int index = 0; index < m_assets.size(); ++index) {
        if (m_assets.at(index).id == m_selectedAssetId) {
            return index;
        }
    }
    return -1;
}

QString LibraryWorkspaceViewModel::selectedPreviewTitle() const
{
    const auto asset = selectedAsset();
    return asset.id > 0 ? asset.name : QString();
}

QUrl LibraryWorkspaceViewModel::selectedPreviewUrl() const
{
    const auto asset = selectedAsset();
    if (asset.id <= 0 || asset.assetType != AssetType::Video || asset.absolutePath.isEmpty()) {
        return {};
    }
    return QUrl::fromLocalFile(asset.absolutePath);
}

QUrl LibraryWorkspaceViewModel::selectedPreviewThumbnailUrl() const
{
    const auto asset = selectedAsset();
    if (asset.id <= 0 || asset.assetType != AssetType::Video || asset.thumbnailPath.isEmpty()) {
        return {};
    }
    return QUrl::fromLocalFile(asset.thumbnailPath);
}

QUrl LibraryWorkspaceViewModel::selectedAssetUrl() const
{
    const auto asset = selectedAsset();
    if (asset.id <= 0 || asset.absolutePath.isEmpty()) {
        return {};
    }
    return QUrl::fromLocalFile(asset.absolutePath);
}

bool LibraryWorkspaceViewModel::selectedPreviewIsVideo() const
{
    const auto asset = selectedAsset();
    return asset.id > 0 && asset.assetType == AssetType::Video;
}

bool LibraryWorkspaceViewModel::selectedPreviewIsImage() const
{
    const auto asset = selectedAsset();
    return asset.id > 0 && asset.assetType == AssetType::Image;
}

bool LibraryWorkspaceViewModel::selectedPreviewIsDocument() const
{
    const auto asset = selectedAsset();
    return asset.id > 0 && asset.assetType == AssetType::Document;
}

void LibraryWorkspaceViewModel::setViewMode(int viewMode)
{
    if (m_viewMode == viewMode) {
        return;
    }
    m_viewMode = viewMode;
    emit viewModeChanged();
}

void LibraryWorkspaceViewModel::resetForProject()
{
    const bool hadSelection = m_selectedAssetId != 0;
    m_searchText.clear();
    m_sourceFilter.reset();
    m_assetTypeFilter.reset();
    if (m_favoritesOnly) {
        m_favoritesOnly = false;
        emit filtersChanged();
    }
    m_selectedAssetId = 0;
    refresh();
    if (hadSelection) {
        emit selectionChanged();
    }
}

void LibraryWorkspaceViewModel::reload()
{
    refresh();
}

void LibraryWorkspaceViewModel::setSearchText(const QString &searchText)
{
    if (m_searchText == searchText) {
        return;
    }
    m_searchText = searchText;
    refresh();
}

void LibraryWorkspaceViewModel::setSourceFilter(qint64 sourceRootId)
{
    m_sourceFilter = sourceRootId > 0 ? std::optional<qint64>(sourceRootId) : std::nullopt;
    if (m_selectedAssetId != 0) {
        m_selectedAssetId = 0;
        emit selectionChanged();
    }
    refresh();
}

void LibraryWorkspaceViewModel::setAssetTypeFilter(int assetType)
{
    if (assetType < 0) {
        m_assetTypeFilter.reset();
    } else {
        m_assetTypeFilter = static_cast<AssetType>(assetType);
    }
    refresh();
}

void LibraryWorkspaceViewModel::toggleModifiedTimeOrder()
{
    m_modifiedTimeAscending = !m_modifiedTimeAscending;
    emit filtersChanged();
    refresh();
}

void LibraryWorkspaceViewModel::selectAsset(qint64 assetId)
{
    if (m_selectedAssetId == assetId) {
        return;
    }
    m_selectedAssetId = assetId;
    emit selectionChanged();
    emit assetSelected(assetId);
}

void LibraryWorkspaceViewModel::selectAssetAt(int index)
{
    if (index < 0 || index >= m_assets.size()) {
        return;
    }
    selectAsset(m_assets.at(index).id);
}

void LibraryWorkspaceViewModel::moveAssetSelection(int delta)
{
    if (m_assets.isEmpty()) {
        return;
    }
    const auto currentIndex = selectedAssetIndex();
    const auto targetIndex = currentIndex < 0
        ? 0
        : qBound(0, currentIndex + delta, m_assets.size() - 1);
    selectAssetAt(targetIndex);
}

bool LibraryWorkspaceViewModel::toggleAssetFavorite(qint64 assetId)
{
    const auto asset = assetById(assetId);
    if (asset.id <= 0 || !m_libraryQueryService) {
        return false;
    }
    if (!m_libraryQueryService->setAssetFavorite(assetId, !asset.favorite)) {
        return false;
    }
    refresh();
    if (m_selectedAssetId == assetId) {
        emit assetSelected(assetId);
    }
    return true;
}

bool LibraryWorkspaceViewModel::removeAsset(qint64 assetId)
{
    if (assetId <= 0 || !m_libraryQueryService) {
        return false;
    }
    if (!m_libraryQueryService->removeAsset(assetId)) {
        return false;
    }
    if (m_selectedAssetId == assetId) {
        m_selectedAssetId = 0;
        emit selectionChanged();
        emit assetSelected(0);
    }
    QMetaObject::invokeMethod(this, &LibraryWorkspaceViewModel::reload, Qt::QueuedConnection);
    return true;
}

bool LibraryWorkspaceViewModel::openAssetFolder(qint64 assetId)
{
    const auto asset = assetById(assetId);
    if (asset.id <= 0 || asset.absolutePath.trimmed().isEmpty()) {
        return false;
    }

    return FileRevealService::revealFile(asset.absolutePath);
}

bool LibraryWorkspaceViewModel::copyAssetPath(qint64 assetId)
{
    const auto asset = assetById(assetId);
    auto *clipboard = QGuiApplication::clipboard();
    if (asset.id <= 0 || asset.absolutePath.trimmed().isEmpty() || !clipboard) {
        return false;
    }

    clipboard->setText(QDir::toNativeSeparators(QFileInfo(asset.absolutePath).absoluteFilePath()));
    return true;
}

AssetFile LibraryWorkspaceViewModel::assetById(qint64 assetId) const
{
    for (const auto &asset : m_assets) {
        if (asset.id == assetId) {
            return asset;
        }
    }
    return {};
}

AssetFile LibraryWorkspaceViewModel::selectedAsset() const
{
    return assetById(m_selectedAssetId);
}

void LibraryWorkspaceViewModel::refresh()
{
    const auto previousSelectedIndex = selectedAssetIndex();
    m_assets = m_libraryQueryService->fetchAssets(m_searchText,
                                                  m_sourceFilter,
                                                  m_assetTypeFilter,
                                                  m_favoritesOnly,
                                                  m_modifiedTimeAscending);
    m_model->setItems(m_assets);
    m_totalCount = m_libraryQueryService->assetCount(m_searchText, m_sourceFilter, m_assetTypeFilter, m_favoritesOnly);
    m_readyCount = 0;
    m_pendingCount = 0;
    m_issueCount = 0;
    for (const auto &asset : m_assets) {
        if (isIssueAsset(asset)) {
            ++m_issueCount;
        } else if (isReadyAsset(asset)) {
            ++m_readyCount;
        } else {
            ++m_pendingCount;
        }
    }
    bool foundSelection = false;
    for (const auto &item : m_assets) {
        if (item.id == m_selectedAssetId) {
            foundSelection = true;
            break;
        }
    }
    if (!foundSelection && m_selectedAssetId != 0) {
        m_selectedAssetId = 0;
        emit selectionChanged();
        emit assetSelected(0);
    } else if (m_selectedAssetId != 0 && previousSelectedIndex != selectedAssetIndex()) {
        emit selectionChanged();
    }
    emit statusChanged();
}
