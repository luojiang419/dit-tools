#include "ui/viewmodels/LibraryWorkspaceViewModel.h"

#include "shared/FileRevealService.h"

#include "application/LibraryQueryService.h"
#include "ui/models/AssetListModel.h"

#include <QtConcurrent>

#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMetaObject>
#include <QSet>
#include <QUrl>

#include <utility>

namespace {
constexpr qsizetype AssetPageSize = 200;

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
    m_catalogReloadTimer.setSingleShot(true);
    m_catalogReloadTimer.setInterval(500);
    connect(&m_catalogReloadTimer, &QTimer::timeout, this, [this]() {
        beginResetQuery();
    });
    m_searchReloadTimer.setSingleShot(true);
    m_searchReloadTimer.setInterval(250);
    connect(&m_searchReloadTimer, &QTimer::timeout, this, [this]() {
        beginResetQuery();
    });
    m_countQueryTimer.setSingleShot(true);
    m_countQueryTimer.setInterval(300);
    connect(&m_countQueryTimer, &QTimer::timeout, this, [this]() {
        startCountQuery(m_pendingCountGeneration);
    });
}

LibraryWorkspaceViewModel::~LibraryWorkspaceViewModel()
{
    waitForIdle();
}

void LibraryWorkspaceViewModel::waitForIdle()
{
    m_futures.waitForFinished();
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
    QString result;
    if (m_loading && m_assets.isEmpty()) {
        result = QStringLiteral("正在后台加载首屏素材…");
    } else if (m_countPending) {
        result = QStringLiteral("已加载 %1 条 · 正在统计总数").arg(m_assets.size());
    } else {
        result = QStringLiteral("已加载 %1 / 共 %2 条").arg(m_assets.size()).arg(m_totalCount);
    }
    if (m_favoritesOnly) {
        result += QStringLiteral(" · 仅显示收藏");
    }
    result += QStringLiteral(" · %1").arg(sortOrderText());
    if (!m_queryError.isEmpty()) {
        result += QStringLiteral(" · 加载失败：%1").arg(m_queryError);
    }
    return result;
}

void LibraryWorkspaceViewModel::setFavoritesOnly(bool favoritesOnly)
{
    if (m_favoritesOnly == favoritesOnly) {
        return;
    }
    m_favoritesOnly = favoritesOnly;
    emit filtersChanged();
    beginResetQuery();
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

bool LibraryWorkspaceViewModel::loading() const
{
    return m_loading;
}

bool LibraryWorkspaceViewModel::hasMore() const
{
    return m_hasMore;
}

int LibraryWorkspaceViewModel::loadedAssetCount() const
{
    return m_assets.size();
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
    m_catalogReloadTimer.stop();
    m_searchReloadTimer.stop();
    m_countQueryTimer.stop();
    const bool hadSelection = m_selectedAssetId != 0;
    m_searchText.clear();
    m_sourceFilter.reset();
    m_assetTypeFilter.reset();
    if (m_favoritesOnly) {
        m_favoritesOnly = false;
        emit filtersChanged();
    }
    m_selectedAssetId = 0;
    beginResetQuery();
    if (hadSelection) {
        emit selectionChanged();
    }
}

void LibraryWorkspaceViewModel::reload()
{
    m_catalogReloadTimer.stop();
    m_searchReloadTimer.stop();
    beginResetQuery();
}

void LibraryWorkspaceViewModel::scheduleReload()
{
    if (!m_catalogReloadTimer.isActive()) {
        m_catalogReloadTimer.start();
    }
}

void LibraryWorkspaceViewModel::loadMore()
{
    if (m_loading || !m_hasMore || m_assets.isEmpty()) {
        return;
    }
    startPageQuery(m_queryGeneration, false);
}

void LibraryWorkspaceViewModel::setSearchText(const QString &searchText)
{
    if (m_searchText == searchText) {
        return;
    }
    m_searchText = searchText;
    m_searchReloadTimer.start();
}

void LibraryWorkspaceViewModel::setSourceFilter(qint64 sourceRootId)
{
    m_sourceFilter = sourceRootId > 0 ? std::optional<qint64>(sourceRootId) : std::nullopt;
    if (m_selectedAssetId != 0) {
        m_selectedAssetId = 0;
        emit selectionChanged();
    }
    beginResetQuery();
}

void LibraryWorkspaceViewModel::setAssetTypeFilter(int assetType)
{
    if (assetType < 0) {
        m_assetTypeFilter.reset();
    } else {
        m_assetTypeFilter = static_cast<AssetType>(assetType);
    }
    beginResetQuery();
}

void LibraryWorkspaceViewModel::toggleModifiedTimeOrder()
{
    m_modifiedTimeAscending = !m_modifiedTimeAscending;
    emit filtersChanged();
    beginResetQuery();
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
    if (targetIndex >= m_assets.size() - 12) {
        loadMore();
    }
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
    beginResetQuery();
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

LibraryAssetPageRequest LibraryWorkspaceViewModel::queryRequest(bool includeCursor) const
{
    LibraryAssetPageRequest request;
    request.keyword = m_searchText;
    request.sourceRootId = m_sourceFilter;
    request.assetType = m_assetTypeFilter;
    request.favoritesOnly = m_favoritesOnly;
    request.modifiedTimeAscending = m_modifiedTimeAscending;
    request.limit = AssetPageSize;
    if (includeCursor && !m_assets.isEmpty()) {
        const auto &last = m_assets.constLast();
        request.hasCursor = true;
        request.cursorModifiedAt = last.modifiedAt;
        request.cursorAssetId = last.id;
    }
    return request;
}

void LibraryWorkspaceViewModel::beginResetQuery()
{
    ++m_queryGeneration;
    const auto generation = m_queryGeneration;
    m_queryError.clear();
    m_assets.clear();
    m_model->clear();
    m_totalCount = 0;
    m_countPending = true;
    m_pendingCountGeneration = generation;
    setHasMore(false);
    setLoading(false);
    updateDerivedState();

    if (!m_libraryQueryService
        || m_libraryQueryService->projectDatabasePath().trimmed().isEmpty()) {
        m_countPending = false;
        emit statusChanged();
        return;
    }

    startPageQuery(generation, true);
    m_countQueryTimer.start();
}

void LibraryWorkspaceViewModel::startPageQuery(quint64 generation, bool firstPage)
{
    if (!m_libraryQueryService || generation != m_queryGeneration || m_loading) {
        return;
    }
    const auto projectDatabasePath = m_libraryQueryService->projectDatabasePath();
    if (projectDatabasePath.trimmed().isEmpty()) {
        return;
    }

    const auto request = queryRequest(!firstPage);
    auto *queryService = m_libraryQueryService;
    setLoading(true);
    auto future = QtConcurrent::run([this,
                                     queryService,
                                     projectDatabasePath,
                                     request,
                                     generation,
                                     firstPage]() {
        const auto result = queryService->fetchAssetPageForPath(projectDatabasePath, request);
        QMetaObject::invokeMethod(this,
                                  [this, generation, firstPage, result]() {
            applyPageResult(generation, firstPage, result);
        },
                                  Qt::QueuedConnection);
    });
    m_futures.addFuture(future);
}

void LibraryWorkspaceViewModel::startCountQuery(quint64 generation)
{
    if (!m_libraryQueryService || generation != m_queryGeneration) {
        return;
    }
    const auto projectDatabasePath = m_libraryQueryService->projectDatabasePath();
    if (projectDatabasePath.trimmed().isEmpty()) {
        return;
    }

    auto request = queryRequest(false);
    request.hasCursor = false;
    auto *queryService = m_libraryQueryService;
    auto future = QtConcurrent::run([this,
                                     queryService,
                                     projectDatabasePath,
                                     request,
                                     generation]() {
        const auto result = queryService->assetCountForPath(projectDatabasePath, request);
        QMetaObject::invokeMethod(this,
                                  [this, generation, result]() {
            applyCountResult(generation, result);
        },
                                  Qt::QueuedConnection);
    });
    m_futures.addFuture(future);
}

void LibraryWorkspaceViewModel::applyPageResult(
    quint64 generation,
    bool firstPage,
    const LibraryAssetPageResult &result)
{
    if (generation != m_queryGeneration) {
        return;
    }
    setLoading(false);
    m_queryError = result.errorMessage;
    if (!result.errorMessage.isEmpty()) {
        setHasMore(false);
        emit statusChanged();
        return;
    }

    if (firstPage) {
        m_assets = result.items;
        m_model->setItems(m_assets);
    } else {
        QSet<qint64> loadedIds;
        loadedIds.reserve(m_assets.size());
        for (const auto &asset : std::as_const(m_assets)) {
            loadedIds.insert(asset.id);
        }
        QVector<AssetFile> newItems;
        newItems.reserve(result.items.size());
        for (const auto &asset : result.items) {
            if (!loadedIds.contains(asset.id)) {
                loadedIds.insert(asset.id);
                newItems.append(asset);
            }
        }
        m_assets.append(newItems);
        m_model->appendItems(newItems);
    }
    setHasMore(result.hasMore);
    updateDerivedState();
}

void LibraryWorkspaceViewModel::applyCountResult(
    quint64 generation,
    const LibraryAssetCountResult &result)
{
    if (generation != m_queryGeneration) {
        return;
    }
    m_countPending = false;
    if (result.errorMessage.isEmpty()) {
        m_totalCount = qMax<qint64>(result.count, m_assets.size());
    } else if (m_queryError.isEmpty()) {
        m_queryError = result.errorMessage;
    }
    emit statusChanged();
}

void LibraryWorkspaceViewModel::updateDerivedState()
{
    const auto previousSelectedIndex = selectedAssetIndex();
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
    emit paginationChanged();
}

void LibraryWorkspaceViewModel::setLoading(bool loading)
{
    if (m_loading == loading) {
        return;
    }
    m_loading = loading;
    emit paginationChanged();
    emit statusChanged();
}

void LibraryWorkspaceViewModel::setHasMore(bool hasMore)
{
    if (m_hasMore == hasMore) {
        return;
    }
    m_hasMore = hasMore;
    emit paginationChanged();
}
