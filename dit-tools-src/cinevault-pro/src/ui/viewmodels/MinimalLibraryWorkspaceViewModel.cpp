#include "ui/viewmodels/MinimalLibraryWorkspaceViewModel.h"

#include "shared/FileRevealService.h"

#include "ui/models/AssetListModel.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QUrl>
#include <algorithm>

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

MinimalLibraryWorkspaceViewModel::MinimalLibraryWorkspaceViewModel(QObject *parent)
    : QObject(parent)
    , m_model(new AssetListModel(this))
{
    seedAssets();
    refresh();
}

AssetListModel *MinimalLibraryWorkspaceViewModel::model() const
{
    return m_model;
}

int MinimalLibraryWorkspaceViewModel::viewMode() const
{
    return m_viewMode;
}

bool MinimalLibraryWorkspaceViewModel::favoritesOnly() const
{
    return m_favoritesOnly;
}

bool MinimalLibraryWorkspaceViewModel::modifiedTimeAscending() const
{
    return m_modifiedTimeAscending;
}

QString MinimalLibraryWorkspaceViewModel::sortOrderText() const
{
    return m_modifiedTimeAscending ? QStringLiteral("修改时间正序") : QStringLiteral("修改时间倒序");
}

QString MinimalLibraryWorkspaceViewModel::statusText() const
{
    if (m_favoritesOnly) {
        return QStringLiteral("当前结果 %1 条 · 仅显示收藏 · %2").arg(m_totalCount).arg(sortOrderText());
    }
    return QStringLiteral("当前结果 %1 条 · %2").arg(m_totalCount).arg(sortOrderText());
}

qint64 MinimalLibraryWorkspaceViewModel::totalAssetCount() const
{
    return m_totalCount;
}

qint64 MinimalLibraryWorkspaceViewModel::readyAssetCount() const
{
    return m_readyCount;
}

qint64 MinimalLibraryWorkspaceViewModel::pendingAssetCount() const
{
    return m_pendingCount;
}

qint64 MinimalLibraryWorkspaceViewModel::issueAssetCount() const
{
    return m_issueCount;
}

qint64 MinimalLibraryWorkspaceViewModel::selectedAssetId() const
{
    return m_selectedAssetId;
}

int MinimalLibraryWorkspaceViewModel::selectedAssetIndex() const
{
    for (int index = 0; index < m_assets.size(); ++index) {
        if (m_assets.at(index).id == m_selectedAssetId) {
            return index;
        }
    }
    return -1;
}

QString MinimalLibraryWorkspaceViewModel::selectedPreviewTitle() const
{
    const auto asset = assetById(m_selectedAssetId);
    return asset.id > 0 ? asset.name : QString();
}

QUrl MinimalLibraryWorkspaceViewModel::selectedPreviewUrl() const
{
    const auto asset = assetById(m_selectedAssetId);
    if (asset.id <= 0 || asset.assetType != AssetType::Video || asset.absolutePath.isEmpty()) {
        return {};
    }
    return QUrl::fromLocalFile(asset.absolutePath);
}

QUrl MinimalLibraryWorkspaceViewModel::selectedPreviewThumbnailUrl() const
{
    const auto asset = assetById(m_selectedAssetId);
    if (asset.id <= 0 || asset.assetType != AssetType::Video || asset.thumbnailPath.isEmpty()) {
        return {};
    }
    return QUrl::fromLocalFile(asset.thumbnailPath);
}

QUrl MinimalLibraryWorkspaceViewModel::selectedAssetUrl() const
{
    const auto asset = assetById(m_selectedAssetId);
    if (asset.id <= 0 || asset.absolutePath.isEmpty()) {
        return {};
    }
    return QUrl::fromLocalFile(asset.absolutePath);
}

bool MinimalLibraryWorkspaceViewModel::selectedPreviewIsVideo() const
{
    const auto asset = assetById(m_selectedAssetId);
    return asset.id > 0 && asset.assetType == AssetType::Video;
}

bool MinimalLibraryWorkspaceViewModel::selectedPreviewIsImage() const
{
    const auto asset = assetById(m_selectedAssetId);
    return asset.id > 0 && asset.assetType == AssetType::Image;
}

bool MinimalLibraryWorkspaceViewModel::selectedPreviewIsDocument() const
{
    const auto asset = assetById(m_selectedAssetId);
    return asset.id > 0 && asset.assetType == AssetType::Document;
}

AssetFile MinimalLibraryWorkspaceViewModel::assetById(qint64 assetId) const
{
    for (const auto &asset : m_allAssets) {
        if (asset.id == assetId) {
            return asset;
        }
    }
    return {};
}

void MinimalLibraryWorkspaceViewModel::setViewMode(int viewMode)
{
    if (m_viewMode == viewMode) {
        return;
    }
    m_viewMode = viewMode;
    emit viewModeChanged();
}

void MinimalLibraryWorkspaceViewModel::setFavoritesOnly(bool favoritesOnly)
{
    if (m_favoritesOnly == favoritesOnly) {
        return;
    }
    m_favoritesOnly = favoritesOnly;
    emit filtersChanged();
    refresh();
}

void MinimalLibraryWorkspaceViewModel::reload()
{
    refresh();
}

void MinimalLibraryWorkspaceViewModel::setSearchText(const QString &searchText)
{
    if (m_searchText == searchText) {
        return;
    }
    m_searchText = searchText;
    refresh();
}

void MinimalLibraryWorkspaceViewModel::setSourceFilter(qint64 sourceRootId)
{
    m_sourceFilter = sourceRootId > 0 ? std::optional<qint64>(sourceRootId) : std::nullopt;
    refresh();
}

void MinimalLibraryWorkspaceViewModel::setAssetTypeFilter(int assetType)
{
    m_assetTypeFilter = assetType >= 0 ? std::optional<AssetType>(static_cast<AssetType>(assetType)) : std::nullopt;
    refresh();
}

void MinimalLibraryWorkspaceViewModel::toggleModifiedTimeOrder()
{
    m_modifiedTimeAscending = !m_modifiedTimeAscending;
    emit filtersChanged();
    refresh();
}

void MinimalLibraryWorkspaceViewModel::selectAsset(qint64 assetId)
{
    if (m_selectedAssetId == assetId) {
        return;
    }
    m_selectedAssetId = assetId;
    emit selectionChanged();
    emit assetSelected(assetId);
}

void MinimalLibraryWorkspaceViewModel::selectAssetAt(int index)
{
    if (index < 0 || index >= m_assets.size()) {
        return;
    }
    selectAsset(m_assets.at(index).id);
}

void MinimalLibraryWorkspaceViewModel::moveAssetSelection(int delta)
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

bool MinimalLibraryWorkspaceViewModel::toggleAssetFavorite(qint64 assetId)
{
    for (auto &asset : m_allAssets) {
        if (asset.id == assetId) {
            asset.favorite = !asset.favorite;
            refresh();
            if (m_selectedAssetId == assetId) {
                emit assetSelected(assetId);
            }
            return true;
        }
    }
    return false;
}

bool MinimalLibraryWorkspaceViewModel::removeAsset(qint64 assetId)
{
    for (qsizetype i = 0; i < m_allAssets.size(); ++i) {
        if (m_allAssets.at(i).id == assetId) {
            m_allAssets.removeAt(i);
            if (m_selectedAssetId == assetId) {
                m_selectedAssetId = 0;
                emit selectionChanged();
                emit assetSelected(0);
            }
            refresh();
            return true;
        }
    }
    return false;
}

bool MinimalLibraryWorkspaceViewModel::openAssetFolder(qint64 assetId)
{
    const auto asset = assetById(assetId);
    if (asset.id <= 0 || asset.absolutePath.trimmed().isEmpty()) {
        return false;
    }

    return FileRevealService::revealFile(asset.absolutePath);
}

bool MinimalLibraryWorkspaceViewModel::copyAssetPath(qint64 assetId)
{
    const auto asset = assetById(assetId);
    auto *clipboard = QGuiApplication::clipboard();
    if (asset.id <= 0 || asset.absolutePath.trimmed().isEmpty() || !clipboard) {
        return false;
    }

    clipboard->setText(QDir::toNativeSeparators(QFileInfo(asset.absolutePath).absoluteFilePath()));
    return true;
}

void MinimalLibraryWorkspaceViewModel::seedAssets()
{
    m_allAssets = {
        AssetFile{101, 1, QStringLiteral("A001_C001.mov"), QStringLiteral("mov"), QStringLiteral("G:/demo/A001_CARD/A001_C001.mov"), QStringLiteral("A001_C001.mov"), QStringLiteral("G:/demo/A001_CARD"), AssetType::Video, 1850LL * 1024 * 1024, QStringLiteral("2026-05-30T09:10:00"), true},
        AssetFile{102, 1, QStringLiteral("A001_C002.mov"), QStringLiteral("mov"), QStringLiteral("G:/demo/A001_CARD/A001_C002.mov"), QStringLiteral("A001_C002.mov"), QStringLiteral("G:/demo/A001_CARD"), AssetType::Video, 1934LL * 1024 * 1024, QStringLiteral("2026-05-30T09:12:00"), true},
        AssetFile{201, 2, QStringLiteral("B001_C014.mov"), QStringLiteral("mov"), QStringLiteral("G:/demo/B_CAM_REEL/B001_C014.mov"), QStringLiteral("DAY01/B001_C014.mov"), QStringLiteral("G:/demo/B_CAM_REEL/DAY01"), AssetType::Video, 2230LL * 1024 * 1024, QStringLiteral("2026-05-30T08:48:00"), true},
        AssetFile{301, 3, QStringLiteral("SND_001.wav"), QStringLiteral("wav"), QStringLiteral("G:/demo/SOUND_DAY01/SND_001.wav"), QStringLiteral("SND_001.wav"), QStringLiteral("G:/demo/SOUND_DAY01"), AssetType::Audio, 184LL * 1024 * 1024, QStringLiteral("2026-05-30T07:35:00"), true},
        AssetFile{302, 3, QStringLiteral("SND_002.wav"), QStringLiteral("wav"), QStringLiteral("G:/demo/SOUND_DAY01/SND_002.wav"), QStringLiteral("SND_002.wav"), QStringLiteral("G:/demo/SOUND_DAY01"), AssetType::Audio, 176LL * 1024 * 1024, QStringLiteral("2026-05-30T07:40:00"), true}
    };
}

void MinimalLibraryWorkspaceViewModel::refresh()
{
    const auto previousSelectedIndex = selectedAssetIndex();
    QVector<AssetFile> filtered;
    for (const auto &asset : m_allAssets) {
        if (m_sourceFilter.has_value() && asset.sourceRootId != m_sourceFilter.value()) {
            continue;
        }
        if (m_assetTypeFilter.has_value() && asset.assetType != m_assetTypeFilter.value()) {
            continue;
        }
        if (m_favoritesOnly && !asset.favorite) {
            continue;
        }
        if (!m_searchText.trimmed().isEmpty()) {
            const auto keyword = m_searchText.trimmed();
            if (!asset.name.contains(keyword, Qt::CaseInsensitive)
                && !asset.relativePath.contains(keyword, Qt::CaseInsensitive)) {
                continue;
            }
        }
        filtered.append(asset);
    }

    std::sort(filtered.begin(), filtered.end(), [this](const AssetFile &left, const AssetFile &right) {
        if (left.modifiedAt == right.modifiedAt) {
            return m_modifiedTimeAscending ? left.id < right.id : left.id > right.id;
        }
        return m_modifiedTimeAscending ? left.modifiedAt < right.modifiedAt : left.modifiedAt > right.modifiedAt;
    });

    m_assets = filtered;
    m_model->setItems(m_assets);
    m_totalCount = m_assets.size();
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

    bool selectionExists = false;
    for (const auto &asset : m_assets) {
        if (asset.id == m_selectedAssetId) {
            selectionExists = true;
            break;
        }
    }
    if (!selectionExists && m_selectedAssetId != 0) {
        m_selectedAssetId = 0;
        emit selectionChanged();
        emit assetSelected(0);
    } else if (m_selectedAssetId != 0 && previousSelectedIndex != selectedAssetIndex()) {
        emit selectionChanged();
    }

    emit statusChanged();
}
