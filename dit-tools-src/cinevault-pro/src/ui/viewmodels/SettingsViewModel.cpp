#include "ui/viewmodels/SettingsViewModel.h"

#include "application/UpdateService.h"
#include "application/SearchAssistantLifecycleController.h"
#include "domain/Enums.h"
#include "infrastructure/config/AppSettings.h"
#include "infrastructure/network/VisionApiClient.h"
#include "infrastructure/search/LocalSearchAssistantRuntime.h"
#include "shared/Formatters.h"
#include "shared/Paths.h"
#include "ui/window/QuickSearchController.h"

#include <QtConcurrent>

#include <QApplication>
#include <QDirIterator>
#include <QMessageBox>
#include <QMetaObject>
#include <QPushButton>

namespace {
qint64 directorySize(const QString &path)
{
    qint64 total = 0;
    QDirIterator it(path, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

QWidget *dialogParent()
{
    return QApplication::activeWindow();
}

QString autoUnloadTimeLabel(int minutes)
{
    if (minutes >= 60 && minutes % 60 == 0) {
        return QStringLiteral("%1小时").arg(minutes / 60);
    }
    return QStringLiteral("%1分钟").arg(minutes);
}

QStringList builtInAnalysisDimensions()
{
    return {
        QStringLiteral("色彩风格"),
        QStringLiteral("构图与镜头语言"),
        QStringLiteral("品牌/文字线索"),
        QStringLiteral("适用场景"),
        QStringLiteral("情绪氛围")
    };
}

bool containsDimension(const QStringList &dimensions, const QString &name)
{
    for (const auto &dimension : dimensions) {
        if (dimension.compare(name, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}
}

SettingsViewModel::SettingsViewModel(AppSettings *settings,
                                     VisionApiClient *visionApiClient,
                                     VideoAnalysisService *videoAnalysisService,
                                     UpdateService *updateService,
                                     QuickSearchController *quickSearchController,
                                     LocalSearchAssistantRuntime *localSearchAssistantRuntime,
                                     SearchAssistantLifecycleController *searchAssistantLifecycleController,
                                     QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_visionApiClient(visionApiClient)
    , m_videoAnalysisService(videoAnalysisService)
    , m_updateService(updateService)
    , m_quickSearchController(quickSearchController)
    , m_localSearchAssistantRuntime(localSearchAssistantRuntime)
    , m_searchAssistantLifecycleController(searchAssistantLifecycleController)
{
    if (m_quickSearchController) {
        connect(m_quickSearchController,
                &QuickSearchController::shortcutStatusChanged,
                this,
                &SettingsViewModel::settingsChanged);
    }
    if (m_localSearchAssistantRuntime) {
        connect(m_localSearchAssistantRuntime,
                &LocalSearchAssistantRuntime::statusChanged,
                this,
                &SettingsViewModel::settingsChanged);
    }
    if (m_searchAssistantLifecycleController) {
        connect(m_searchAssistantLifecycleController,
                &SearchAssistantLifecycleController::lifecycleChanged,
                this,
                &SettingsViewModel::settingsChanged);
    }
    if (m_updateService) {
        connect(m_updateService, &UpdateService::statusMessageChanged, this, &SettingsViewModel::setLastMessage);
        connect(m_updateService, &UpdateService::busyChanged, this, &SettingsViewModel::settingsChanged);
        connect(m_updateService, &UpdateService::updateReady, this, [this](const QString &versionTag, const QString &, bool) {
            if (m_settings && m_settings->autoInstallUpdates()) {
                QString errorMessage;
                if (!m_updateService->installPendingUpdateNow(&errorMessage)) {
                    setLastMessage(errorMessage);
                    QMessageBox::warning(dialogParent(), QStringLiteral("安装更新失败"), errorMessage);
                }
                return;
            }
            promptInstallUpdate(versionTag);
        });
    }

    refreshCacheInfo();
}

QString SettingsViewModel::visionBaseUrl() const
{
    return m_settings ? m_settings->visionBaseUrl() : QString();
}

void SettingsViewModel::setVisionBaseUrl(const QString &value)
{
    if (!m_settings || m_settings->visionBaseUrl() == value.trimmed()) {
        return;
    }
    m_settings->setVisionBaseUrl(value);
    emit settingsChanged();
}

QString SettingsViewModel::visionApiKey() const
{
    return m_settings ? m_settings->visionApiKey() : QString();
}

void SettingsViewModel::setVisionApiKey(const QString &value)
{
    if (!m_settings || m_settings->visionApiKey() == value.trimmed()) {
        return;
    }
    m_settings->setVisionApiKey(value);
    emit settingsChanged();
}

QString SettingsViewModel::visionModel() const
{
    return m_settings ? m_settings->visionModel() : QString();
}

void SettingsViewModel::setVisionModel(const QString &value)
{
    if (!m_settings || m_settings->visionModel() == value.trimmed()) {
        return;
    }
    m_settings->setVisionModel(value);
    emit settingsChanged();
}

QStringList SettingsViewModel::customAnalysisDimensions() const
{
    return m_settings ? m_settings->customAnalysisDimensions() : QStringList{};
}

bool SettingsViewModel::addCustomAnalysisDimension(const QString &name)
{
    if (!m_settings) {
        setLastMessage(QStringLiteral("设置服务尚未准备好。"));
        return false;
    }

    const auto normalized = name.trimmed();
    if (normalized.isEmpty()) {
        setLastMessage(QStringLiteral("请输入自定义解析维度。"));
        return false;
    }
    if (normalized.size() > 32) {
        setLastMessage(QStringLiteral("维度名称不能超过 32 个字符。"));
        return false;
    }
    if (containsDimension(builtInAnalysisDimensions(), normalized)) {
        setLastMessage(QStringLiteral("该维度已包含在常规解析任务中，无需重复添加。"));
        return false;
    }

    auto dimensions = m_settings->customAnalysisDimensions();
    if (containsDimension(dimensions, normalized)) {
        setLastMessage(QStringLiteral("该自定义维度已经存在。"));
        return false;
    }
    if (dimensions.size() >= 20) {
        setLastMessage(QStringLiteral("最多可添加 20 个自定义解析维度。"));
        return false;
    }

    dimensions.append(normalized);
    m_settings->setCustomAnalysisDimensions(dimensions);
    m_settings->sync();
    setLastMessage(QStringLiteral("已添加自定义解析维度：%1").arg(normalized));
    emit settingsChanged();
    return true;
}

bool SettingsViewModel::removeCustomAnalysisDimension(const QString &name)
{
    if (!m_settings) {
        return false;
    }

    const auto normalized = name.trimmed();
    auto dimensions = m_settings->customAnalysisDimensions();
    for (qsizetype index = 0; index < dimensions.size(); ++index) {
        if (dimensions.at(index).compare(normalized, Qt::CaseInsensitive) != 0) {
            continue;
        }
        const auto removed = dimensions.takeAt(index);
        m_settings->setCustomAnalysisDimensions(dimensions);
        m_settings->sync();
        setLastMessage(QStringLiteral("已移除自定义解析维度：%1").arg(removed));
        emit settingsChanged();
        return true;
    }
    return false;
}

bool SettingsViewModel::searchAssistantEnabled() const
{
    return m_settings && m_settings->searchAssistantEnabled();
}

void SettingsViewModel::setSearchAssistantEnabled(bool enabled)
{
    if (!m_settings || m_settings->searchAssistantEnabled() == enabled) return;
    m_settings->setSearchAssistantEnabled(enabled);
    m_settings->sync();
    if (m_searchAssistantLifecycleController) {
        m_searchAssistantLifecycleController->applySettings();
    }
    emit settingsChanged();
}

int SettingsViewModel::searchAssistantAutoUnloadMinutes() const
{
    return m_settings ? m_settings->searchAssistantAutoUnloadMinutes() : 30;
}

void SettingsViewModel::setSearchAssistantAutoUnloadMinutes(int minutes)
{
    if (!m_settings) {
        return;
    }
    const auto normalized = qBound(5, minutes, 24 * 60);
    if (m_settings->searchAssistantAutoUnloadMinutes() == normalized) {
        return;
    }
    m_settings->setSearchAssistantAutoUnloadMinutes(normalized);
    m_settings->sync();
    if (m_searchAssistantLifecycleController) {
        m_searchAssistantLifecycleController->applySettings();
    }
    emit settingsChanged();
}

bool SettingsViewModel::quickSearchEnabled() const
{
    return m_settings && m_settings->quickSearchEnabled();
}

QString SettingsViewModel::quickSearchShortcut() const
{
    return m_settings ? m_settings->quickSearchShortcut() : QStringLiteral("Alt+Space");
}

bool SettingsViewModel::startAtLogin() const
{
    return m_settings && m_settings->startAtLogin();
}

QString SettingsViewModel::quickSearchStatusText() const
{
    return m_quickSearchController
        ? m_quickSearchController->shortcutStatusText()
        : QStringLiteral("快捷搜索控制器未初始化");
}

int SettingsViewModel::analysisMode() const
{
    return m_settings ? static_cast<int>(m_settings->analysisMode()) : 0;
}

void SettingsViewModel::setAnalysisMode(int value)
{
    if (!m_settings || analysisMode() == value) {
        return;
    }
    if (value == static_cast<int>(AnalysisMode::EveryFrame)) {
        m_settings->setAnalysisMode(AnalysisMode::EveryFrame);
    } else if (value == static_cast<int>(AnalysisMode::CustomInterval)) {
        m_settings->setAnalysisMode(AnalysisMode::CustomInterval);
    } else {
        m_settings->setAnalysisMode(AnalysisMode::Every10Frames);
    }
    emit settingsChanged();
}

int SettingsViewModel::frameInterval() const
{
    return m_settings ? m_settings->frameInterval() : 10;
}

void SettingsViewModel::setFrameInterval(int value)
{
    if (!m_settings || frameInterval() == qMax(1, value)) {
        return;
    }
    m_settings->setFrameInterval(value);
    emit settingsChanged();
}

int SettingsViewModel::thumbnailFrameIndex() const
{
    return m_settings ? m_settings->thumbnailFrameIndex() : 3;
}

void SettingsViewModel::setThumbnailFrameIndex(int value)
{
    if (!m_settings || thumbnailFrameIndex() == qMax(1, value)) {
        return;
    }
    m_settings->setThumbnailFrameIndex(value);
    emit settingsChanged();
}

int SettingsViewModel::contactSheetFrameCount() const
{
    return m_settings ? m_settings->contactSheetFrameCount() : 24;
}

void SettingsViewModel::setContactSheetFrameCount(int value)
{
    if (!m_settings || contactSheetFrameCount() == qBound(1, value, 64)) {
        return;
    }
    m_settings->setContactSheetFrameCount(value);
    emit settingsChanged();
}

int SettingsViewModel::analysisTimeoutSec() const
{
    return m_settings ? m_settings->analysisTimeoutSec() : 60;
}

void SettingsViewModel::setAnalysisTimeoutSec(int value)
{
    if (!m_settings || analysisTimeoutSec() == qMax(5, value)) {
        return;
    }
    m_settings->setAnalysisTimeoutSec(value);
    emit settingsChanged();
}

int SettingsViewModel::themeMode() const
{
    return m_settings ? m_settings->themeMode() : 0;
}

void SettingsViewModel::setThemeMode(int value)
{
    if (!m_settings || themeMode() == value) {
        return;
    }
    m_settings->setThemeMode(value);
    m_settings->sync();
    emit settingsChanged();
}

int SettingsViewModel::closeButtonBehavior() const
{
    return m_settings ? m_settings->closeButtonBehavior() : 0;
}

void SettingsViewModel::setCloseButtonBehavior(int value)
{
    const auto normalized = qBound(0, value, 2);
    if (!m_settings || closeButtonBehavior() == normalized) {
        return;
    }
    m_settings->setCloseButtonBehavior(normalized);
    m_settings->sync();
    emit settingsChanged();
}

QString SettingsViewModel::localSearchAssistantStatusText() const
{
    const auto unloadMinutes = searchAssistantAutoUnloadMinutes();
    const auto unloadLabel = autoUnloadTimeLabel(unloadMinutes);
    if (!searchAssistantEnabled()) {
        return QStringLiteral("内置文本模型已关闭，不会在软件启动时加载");
    }
    if (!m_localSearchAssistantRuntime) {
        return QStringLiteral("内置文本模型运行时未初始化");
    }
    if (!m_localSearchAssistantRuntime->assetsAvailable()) {
        return QStringLiteral("内置文本模型资产缺失，搜索会自动退回本地规则");
    }
    if (m_searchAssistantLifecycleController
        && m_searchAssistantLifecycleController->isIdleUnloaded()) {
        return QStringLiteral("软件无操作 %1，模型已自动卸载；恢复操作后会自动重新加载")
            .arg(unloadLabel);
    }
    if (m_localSearchAssistantRuntime->isReady()) {
        return QStringLiteral("Qwen3 0.6B 本地模型已就绪（GPU：%1）；软件无操作 %2 后自动卸载")
            .arg(m_localSearchAssistantRuntime->gpuDeviceName(), unloadLabel);
    }
    if (m_localSearchAssistantRuntime->isStarting()) {
        return QStringLiteral("Qwen3 0.6B 本地模型正在启动…");
    }
    if (!m_localSearchAssistantRuntime->lastError().isEmpty()) {
        return QStringLiteral("内置文本模型上次启动失败：%1")
            .arg(m_localSearchAssistantRuntime->lastError());
    }
    return QStringLiteral("Qwen3 0.6B 已内置，将在打开搜索或输入查询时智能加载；无搜索操作 %1 后自动卸载")
        .arg(unloadLabel);
}

bool SettingsViewModel::updateBusy() const
{
    return m_updateService && m_updateService->isBusy();
}

bool SettingsViewModel::autoInstallUpdates() const
{
    return m_settings && m_settings->autoInstallUpdates();
}

void SettingsViewModel::setAutoInstallUpdates(bool enabled)
{
    if (!m_settings || m_settings->autoInstallUpdates() == enabled) {
        return;
    }
    m_settings->setAutoInstallUpdates(enabled);
    m_settings->sync();
    emit settingsChanged();
}

QString SettingsViewModel::currentVersionLabel() const
{
    return QStringLiteral("当前版本：%1").arg(
        m_updateService ? m_updateService->currentVersionTag() : QStringLiteral("v0.0.0"));
}

int SettingsViewModel::updateDownloadMode() const
{
    return m_settings ? m_settings->updateDownloadMode() : 0;
}

void SettingsViewModel::setUpdateDownloadMode(int value)
{
    if (!m_settings || updateDownloadMode() == qBound(0, value, 2)) {
        return;
    }
    m_settings->setUpdateDownloadMode(value);
    m_settings->sync();
    emit settingsChanged();
}

QString SettingsViewModel::updateManualProxyUrl() const
{
    return m_settings ? m_settings->updateManualProxyUrl() : QString();
}

void SettingsViewModel::setUpdateManualProxyUrl(const QString &value)
{
    if (!m_settings || updateManualProxyUrl() == value.trimmed()) {
        return;
    }
    m_settings->setUpdateManualProxyUrl(value);
    m_settings->sync();
    emit settingsChanged();
}

QString SettingsViewModel::dataRootPath() const
{
    return Paths::resolvedDataRoot();
}

QString SettingsViewModel::frameCacheSizeLabel() const
{
    return m_frameCacheSizeLabel;
}

QString SettingsViewModel::lastMessage() const
{
    return m_lastMessage;
}

void SettingsViewModel::beginStartupUpdateFlow()
{
    if (!m_updateService) {
        return;
    }

    m_updateService->beginStartupFlow();
}

void SettingsViewModel::checkForUpdates()
{
    if (!m_updateService) {
        return;
    }

    m_updateService->checkForUpdates(true);
}

void SettingsViewModel::saveUpdateDownloadSettings(int updateDownloadMode, const QString &updateManualProxyUrl)
{
    if (!m_settings) {
        return;
    }

    m_settings->setUpdateDownloadMode(updateDownloadMode);
    m_settings->setUpdateManualProxyUrl(updateManualProxyUrl);
    m_settings->sync();
    emit settingsChanged();
}

void SettingsViewModel::refresh()
{
    refreshCacheInfo();
    emit settingsChanged();
}

void SettingsViewModel::refreshCacheInfo()
{
    m_frameCacheSizeLabel = Formatters::formatBytes(directorySize(Paths::frameCacheRoot()));
    emit settingsChanged();
}

void SettingsViewModel::testConnection()
{
    if (!m_visionApiClient || !m_settings) {
        return;
    }

    testConnectionWith(m_settings->visionBaseUrl(),
                       m_settings->visionApiKey(),
                       m_settings->visionModel(),
                       m_settings->analysisTimeoutSec());
}

void SettingsViewModel::testConnectionWith(const QString &visionBaseUrl,
                                           const QString &visionApiKey,
                                           const QString &visionModel,
                                           int analysisTimeoutSec)
{
    if (!m_visionApiClient) {
        return;
    }

    const auto baseUrl = visionBaseUrl.trimmed();
    const auto apiKey = visionApiKey.trimmed();
    const auto model = visionModel.trimmed();
    const auto timeoutSec = qMax(5, analysisTimeoutSec);
    setLastMessage(QStringLiteral("正在测试视觉接口连通状态..."));

    auto future = QtConcurrent::run([this, baseUrl, apiKey, model, timeoutSec]() {
        QString errorMessage;
        const auto ok = m_visionApiClient->testConnection(baseUrl, apiKey, model, timeoutSec, &errorMessage);
        QMetaObject::invokeMethod(this, [this, ok, errorMessage]() {
            setLastMessage(ok ? QStringLiteral("视觉接口连通测试成功。") : errorMessage);
        }, Qt::QueuedConnection);
    });
    Q_UNUSED(future);
}

QString SettingsViewModel::shortcutFromKeyEvent(int key, int modifiers) const
{
    return QuickSearchController::shortcutFromKeyEvent(key, modifiers);
}

void SettingsViewModel::saveAndApply(const QString &visionBaseUrl,
                                     const QString &visionApiKey,
                                     const QString &visionModel,
                                     bool searchAssistantEnabled,
                                     int searchAssistantAutoUnloadMinutes,
                                     bool quickSearchEnabled,
                                     const QString &quickSearchShortcut,
                                     bool startAtLogin,
                                     int closeButtonBehavior,
                                     int analysisMode,
                                     int frameInterval,
                                     int thumbnailFrameIndex,
                                     int contactSheetFrameCount,
                                     int analysisTimeoutSec,
                                     int updateDownloadMode,
                                     const QString &updateManualProxyUrl)
{
    if (!m_settings) {
        return;
    }

    const auto normalizedShortcut = QuickSearchController::normalizedShortcut(quickSearchShortcut);
    QString shortcutError;
    if (m_quickSearchController
        && !m_quickSearchController->applyShortcutConfiguration(quickSearchEnabled,
                                                                 normalizedShortcut,
                                                                 &shortcutError)) {
        setLastMessage(shortcutError);
        emit settingsChanged();
        return;
    }

    QString startAtLoginError;
    const auto startAtLoginChanged = m_settings->startAtLogin() != startAtLogin;
    if (startAtLoginChanged
        && m_quickSearchController
        && !m_quickSearchController->setStartAtLogin(startAtLogin, &startAtLoginError)) {
        setLastMessage(startAtLoginError);
        emit settingsChanged();
        return;
    }

    m_settings->setVisionBaseUrl(visionBaseUrl);
    m_settings->setVisionApiKey(visionApiKey);
    m_settings->setVisionModel(visionModel);
    m_settings->setSearchAssistantEnabled(searchAssistantEnabled);
    m_settings->setSearchAssistantAutoUnloadMinutes(searchAssistantAutoUnloadMinutes);
    m_settings->setQuickSearchEnabled(quickSearchEnabled);
    m_settings->setQuickSearchShortcut(normalizedShortcut);
    m_settings->setStartAtLogin(startAtLogin);
    m_settings->setCloseButtonBehavior(closeButtonBehavior);
    AnalysisMode resolvedMode = AnalysisMode::Every10Frames;
    if (analysisMode == static_cast<int>(AnalysisMode::EveryFrame)) {
        resolvedMode = AnalysisMode::EveryFrame;
    } else if (analysisMode == static_cast<int>(AnalysisMode::CustomInterval)) {
        resolvedMode = AnalysisMode::CustomInterval;
    }
    m_settings->setAnalysisMode(resolvedMode);
    m_settings->setFrameInterval(resolvedMode == AnalysisMode::Every10Frames ? 10 : frameInterval);
    m_settings->setThumbnailFrameIndex(thumbnailFrameIndex);
    m_settings->setContactSheetFrameCount(contactSheetFrameCount);
    m_settings->setAnalysisTimeoutSec(analysisTimeoutSec);
    m_settings->setUpdateDownloadMode(updateDownloadMode);
    m_settings->setUpdateManualProxyUrl(updateManualProxyUrl);
    m_settings->sync();
    if (m_searchAssistantLifecycleController) {
        m_searchAssistantLifecycleController->applySettings();
    }

    setLastMessage(QStringLiteral("设置已保存并应用，快捷搜索、模型自动卸载、素材解析与搜索将使用新参数。"));
    emit settingsChanged();
    emit searchSettingsChanged();
}

void SettingsViewModel::setLastMessage(const QString &message)
{
    if (m_lastMessage == message) {
        return;
    }
    m_lastMessage = message;
    emit settingsChanged();
}

void SettingsViewModel::promptInstallUpdate(const QString &versionTag)
{
    if (!m_updateService) {
        return;
    }

    QMessageBox box(dialogParent());
    box.setIcon(QMessageBox::Information);
    box.setWindowTitle(QStringLiteral("发现新版本"));
    box.setText(QStringLiteral("更新包 %1 已下载完成。").arg(versionTag));
    box.setInformativeText(QStringLiteral("立即更新会打开独立进度窗口、关闭当前程序、静默安装并自动启动新版本。也可以安排在下次启动时自动更新。"));
    QAbstractButton *installNowButton = box.addButton(QStringLiteral("立即更新"), QMessageBox::AcceptRole);
    QAbstractButton *updateLaterButton = box.addButton(QStringLiteral("下次启动更新"), QMessageBox::RejectRole);
    box.exec();

    if (box.clickedButton() == installNowButton) {
        if (m_settings) {
            m_settings->setScheduledUpdateVersion(QString());
            m_settings->sync();
        }
        QString errorMessage;
        if (!m_updateService->installPendingUpdateNow(&errorMessage)) {
            setLastMessage(errorMessage);
            QMessageBox::warning(dialogParent(), QStringLiteral("安装更新失败"), errorMessage);
        }
        return;
    }

    if (box.clickedButton() == updateLaterButton || box.clickedButton() == nullptr) {
        if (m_settings) {
            m_settings->setScheduledUpdateVersion(versionTag);
            m_settings->sync();
        }
        setLastMessage(QStringLiteral("已安排下次启动更新：%1").arg(versionTag));
    }
}
