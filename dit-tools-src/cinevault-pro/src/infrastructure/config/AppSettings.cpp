#include "infrastructure/config/AppSettings.h"

#include "infrastructure/security/WindowsCredentialStore.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSet>
#include <QUrl>
#include <QUuid>

#include <algorithm>
#include <utility>

namespace {
constexpr auto kRecentProjectsKey = "recentProjects";
constexpr auto kKnownProjectsKey = "knownProjects";
constexpr auto kVisionBaseUrlKey = "materialCenter/visionBaseUrl";
constexpr auto kVisionApiKeyKey = "materialCenter/visionApiKey";
constexpr auto kVisionApiCredentialTarget = "CineVaultPro/material-center/vision-api-key";
constexpr auto kVisionModelKey = "materialCenter/visionModel";
constexpr auto kVisionApiConfigsKey = "materialCenter/visionApiConfigs";
constexpr auto kActiveVisionApiConfigIdKey = "materialCenter/activeVisionApiConfigId";
constexpr auto kVisionApiConfigCredentialPrefix = "CineVaultPro/material-center/vision-api-config/";
constexpr auto kCustomAnalysisDimensionsKey = "materialCenter/customAnalysisDimensions";
constexpr auto kSearchAssistantEnabledKey = "materialCenter/searchAssistantEnabled";
constexpr auto kSearchAssistantAutoUnloadMinutesKey = "materialCenter/searchAssistantAutoUnloadMinutes";
constexpr auto kQuickSearchEnabledKey = "quickSearch/enabled";
constexpr auto kQuickSearchShortcutKey = "quickSearch/shortcut";
constexpr auto kReportEnabledSectionsKey = "report/enabledSections";
constexpr auto kQuickSearchWindowXKey = "quickSearch/windowX";
constexpr auto kQuickSearchWindowYKey = "quickSearch/windowY";
constexpr auto kStartAtLoginKey = "quickSearch/startAtLogin";
constexpr auto kAnalysisModeKey = "materialCenter/analysisMode";
constexpr auto kFrameIntervalKey = "materialCenter/frameInterval";
constexpr auto kVideoFrameExtractionStrategyKey = "materialCenter/videoFrameExtractionStrategy";
constexpr auto kVideoFrameIntervalSecondsKey = "materialCenter/videoFrameIntervalSeconds";
constexpr auto kVideoSceneThresholdKey = "materialCenter/videoSceneThreshold";
constexpr auto kVideoMinimumSharpnessKey = "materialCenter/videoMinimumSharpness";
constexpr auto kThumbnailFrameIndexKey = "materialCenter/thumbnailFrameIndex";
constexpr auto kContactSheetFrameCountKey = "materialCenter/contactSheetFrameCount";
constexpr auto kAnalysisTimeoutSecKey = "materialCenter/analysisTimeoutSec";
constexpr auto kDocumentAutoAnalysisEnabledKey = "materialCenter/documentAutoAnalysisEnabled";
constexpr auto kPhotoshopAutoAnalysisEnabledKey = "materialCenter/photoshopAutoAnalysisEnabled";
constexpr auto kThemeModeKey = "ui/themeMode";
constexpr auto kCloseButtonBehaviorKey = "ui/closeButtonBehavior";
constexpr auto kPendingUpdateVersionKey = "updates/pendingVersion";
constexpr auto kPendingUpdateInstallerPathKey = "updates/pendingInstallerPath";
constexpr auto kPendingUpdateInstallerSizeKey = "updates/pendingInstallerSize";
constexpr auto kPendingUpdateInstallerSha256Key = "updates/pendingInstallerSha256";
constexpr auto kDownloadedUpdateVersionKey = "updates/downloadedVersion";
constexpr auto kScheduledUpdateVersionKey = "updates/scheduledVersion";
constexpr auto kAutoInstallUpdatesKey = "updates/autoInstall";
constexpr auto kUpdatePolicyKey = "updates/policy";
constexpr auto kUpdateDownloadModeKey = "updates/downloadMode";
constexpr auto kUpdateManualProxyUrlKey = "updates/manualProxyUrl";
constexpr auto kFeedbackSessionKey = "feedback/sessionJson";
constexpr double kDefaultVideoMinimumSharpness = 0.01;
constexpr double kLegacyVideoMinimumSharpness = 0.08;

int normalizedThemeMode(int value)
{
    return value >= 0 && value <= 2 ? value : 0;
}

int normalizedUpdateDownloadMode(int value)
{
    return value >= 0 && value <= 2 ? value : 0;
}

int normalizedCloseButtonBehavior(int value)
{
    return value >= 0 && value <= 2 ? value : 0;
}

int normalizedSearchAssistantAutoUnloadMinutes(int value)
{
    return qBound(5, value, 24 * 60);
}

QString normalizedProjectPath(const QString &projectPath)
{
    const auto trimmed = projectPath.trimmed();
    return trimmed.isEmpty() ? QString() : QFileInfo(trimmed).absoluteFilePath();
}

int normalizedUpdatePolicy(int value)
{
    return value >= 0 && value <= 2 ? value : 0;
}

QStringList normalizedCustomAnalysisDimensions(const QStringList &values)
{
    constexpr qsizetype kMaximumDimensionCount = 20;
    constexpr qsizetype kMaximumDimensionLength = 32;
    QStringList normalized;
    for (const auto &value : values) {
        const auto name = value.trimmed();
        if (name.isEmpty() || name.size() > kMaximumDimensionLength) {
            continue;
        }

        bool duplicate = false;
        for (const auto &existing : std::as_const(normalized)) {
            if (existing.compare(name, Qt::CaseInsensitive) == 0) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            normalized.append(name);
        }
        if (normalized.size() >= kMaximumDimensionCount) {
            break;
        }
    }
    return normalized;
}

VideoFrameExtractionStrategy normalizedVideoFrameExtractionStrategy(int value)
{
    if (value == static_cast<int>(VideoFrameExtractionStrategy::PerFrame)) {
        return VideoFrameExtractionStrategy::PerFrame;
    }
    if (value == static_cast<int>(VideoFrameExtractionStrategy::IntervalOnly)) {
        return VideoFrameExtractionStrategy::IntervalOnly;
    }
    if (value == static_cast<int>(VideoFrameExtractionStrategy::HighFidelity)) {
        return VideoFrameExtractionStrategy::HighFidelity;
    }
    return VideoFrameExtractionStrategy::SceneAndInterval;
}

double boundedVideoFrameIntervalSeconds(double value)
{
    return qBound(0.1, value, 240.0);
}

double boundedVideoSceneThreshold(double value)
{
    return qBound(0.05, value, 0.95);
}

double boundedVideoMinimumSharpness(double value)
{
    return qBound(0.0, value, 1.0);
}

QStringList normalizedReportEnabledSections(const QStringList &sections)
{
    static const QStringList allowed = {
        QStringLiteral("cover"), QStringLiteral("summary"), QStringLiteral("sourceOverview"),
        QStringLiteral("formatDistribution"), QStringLiteral("thumbnailIndex"), QStringLiteral("videoMetadata"),
        QStringLiteral("audioMetadata"), QStringLiteral("folderTree")
    };
    QStringList normalized;
    for (const auto &section : allowed) {
        if (sections.contains(section)) {
            normalized.append(section);
        }
    }
    return normalized;
}

QStringList replacedProjectList(const QStringList &projects, const QString &oldProjectPath, const QString &newProjectPath)
{
    QStringList replaced;
    for (const auto &project : projects) {
        const auto normalized = normalizedProjectPath(project);
        const auto next = normalized == oldProjectPath ? newProjectPath : normalized;
        if (!next.isEmpty() && !replaced.contains(next)) {
            replaced.append(next);
        }
    }
    return replaced;
}

QString visionApiConfigCredentialTarget(const QString &id)
{
    return QString::fromLatin1(kVisionApiConfigCredentialPrefix) + id;
}

QString normalizedVisionApiConfigId(const QString &value)
{
    const auto normalized = value.trimmed();
    return normalized.isEmpty()
        ? QUuid::createUuid().toString(QUuid::WithoutBraces)
        : normalized;
}

QList<VisionApiConfig> normalizedVisionApiConfigs(const QList<VisionApiConfig> &configs)
{
    constexpr qsizetype kMaximumConfigCount = 20;
    QList<VisionApiConfig> normalized;
    QSet<QString> seenIds;
    for (auto config : configs) {
        config.id = normalizedVisionApiConfigId(config.id);
        if (seenIds.contains(config.id)) {
            config.id = normalizedVisionApiConfigId(QString());
        }
        seenIds.insert(config.id);
        config.name = config.name.trimmed();
        if (config.name.isEmpty()) {
            config.name = QStringLiteral("未命名配置");
        }
        config.baseUrl = config.baseUrl.trimmed();
        config.apiKey = config.apiKey.trimmed();
        config.model = config.model.trimmed();
        if (config.model.isEmpty()) {
            config.model = QStringLiteral("gpt-4.1-mini");
        }
        normalized.append(config);
        if (normalized.size() >= kMaximumConfigCount) {
            break;
        }
    }
    return normalized;
}

QList<VisionApiConfig> storedVisionApiConfigs(QSettings *settings)
{
    const auto payload = settings->value(QLatin1String(kVisionApiConfigsKey)).toByteArray();
    const auto document = QJsonDocument::fromJson(payload);
    if (!document.isArray()) {
        return {};
    }

    QList<VisionApiConfig> configs;
    for (const auto &value : document.array()) {
        if (!value.isObject()) {
            continue;
        }
        const auto object = value.toObject();
        configs.append({object.value(QStringLiteral("id")).toString(),
                        object.value(QStringLiteral("name")).toString(),
                        object.value(QStringLiteral("baseUrl")).toString(),
                        {},
                        object.value(QStringLiteral("model")).toString()});
    }
    return normalizedVisionApiConfigs(configs);
}

VisionApiConfig legacyVisionApiConfig(QSettings *settings)
{
    VisionApiConfig config;
    config.id = QStringLiteral("default");
    config.name = QStringLiteral("默认配置");
    config.baseUrl = settings->value(QLatin1String(kVisionBaseUrlKey)).toString().trimmed();
    config.model = settings->value(QLatin1String(kVisionModelKey), QStringLiteral("gpt-4.1-mini"))
                       .toString().trimmed();
    const auto credentialTarget = QString::fromLatin1(kVisionApiCredentialTarget);
    config.apiKey = WindowsCredentialStore::read(credentialTarget).trimmed();
    if (config.apiKey.isEmpty()) {
        config.apiKey = settings->value(QLatin1String(kVisionApiKeyKey)).toString().trimmed();
    }
    return normalizedVisionApiConfigs({config}).first();
}

}

AppSettings::AppSettings()
    : m_settings(new QSettings)
{
    // v0.1.202 stored this value as the default, but it is too high for the
    // normalized adjacent-pixel gradient used by frame-quality inspection.
    if (m_settings->contains(QLatin1String(kVideoMinimumSharpnessKey))
        && qFuzzyCompare(m_settings->value(QLatin1String(kVideoMinimumSharpnessKey)).toDouble() + 1.0,
                         kLegacyVideoMinimumSharpness + 1.0)) {
        m_settings->setValue(QLatin1String(kVideoMinimumSharpnessKey), kDefaultVideoMinimumSharpness);
        m_settings->sync();
    }
}

AppSettings::~AppSettings()
{
    delete m_settings;
}

QStringList AppSettings::recentProjects() const
{
    return m_settings->value(QLatin1String(kRecentProjectsKey)).toStringList();
}

void AppSettings::addRecentProject(const QString &projectPath)
{
    const auto normalizedPath = normalizedProjectPath(projectPath);
    if (normalizedPath.isEmpty()) {
        return;
    }

    auto projects = recentProjects();
    projects = replacedProjectList(projects, normalizedPath, normalizedPath);
    projects.removeAll(normalizedPath);
    projects.prepend(normalizedPath);
    while (projects.size() > 10) {
        projects.removeLast();
    }
    m_settings->setValue(QLatin1String(kRecentProjectsKey), projects);
}

QStringList AppSettings::knownProjects() const
{
    return m_settings->value(QLatin1String(kKnownProjectsKey)).toStringList();
}

void AppSettings::addKnownProject(const QString &projectPath)
{
    const auto normalizedPath = normalizedProjectPath(projectPath);
    if (normalizedPath.isEmpty()) {
        return;
    }

    auto projects = knownProjects();
    projects = replacedProjectList(projects, normalizedPath, normalizedPath);
    projects.removeAll(normalizedPath);
    projects.prepend(normalizedPath);
    m_settings->setValue(QLatin1String(kKnownProjectsKey), projects);
}

void AppSettings::removeKnownProject(const QString &projectPath)
{
    const auto normalizedPath = normalizedProjectPath(projectPath);
    auto projects = replacedProjectList(knownProjects(), normalizedPath, normalizedPath);
    projects.removeAll(normalizedPath);
    m_settings->setValue(QLatin1String(kKnownProjectsKey), projects);

    auto recent = replacedProjectList(recentProjects(), normalizedPath, normalizedPath);
    recent.removeAll(normalizedPath);
    m_settings->setValue(QLatin1String(kRecentProjectsKey), recent);
}

void AppSettings::replaceProjectPath(const QString &oldProjectPath, const QString &newProjectPath)
{
    const auto oldNormalized = normalizedProjectPath(oldProjectPath);
    const auto newNormalized = normalizedProjectPath(newProjectPath);
    if (oldNormalized.isEmpty() || newNormalized.isEmpty() || oldNormalized == newNormalized) {
        return;
    }

    m_settings->setValue(QLatin1String(kKnownProjectsKey),
                         replacedProjectList(knownProjects(), oldNormalized, newNormalized));
    m_settings->setValue(QLatin1String(kRecentProjectsKey),
                         replacedProjectList(recentProjects(), oldNormalized, newNormalized));
}

QString AppSettings::visionBaseUrl() const
{
    const auto configs = visionApiConfigs();
    const auto activeId = activeVisionApiConfigId();
    for (const auto &config : configs) {
        if (config.id == activeId) {
            return config.baseUrl;
        }
    }
    return configs.isEmpty() ? QString() : configs.first().baseUrl;
}

void AppSettings::setVisionBaseUrl(const QString &value)
{
    auto configs = visionApiConfigs();
    if (configs.isEmpty()) {
        return;
    }
    const auto activeId = activeVisionApiConfigId();
    for (auto &config : configs) {
        if (config.id == activeId) {
            config.baseUrl = value;
            break;
        }
    }
    setVisionApiConfigs(configs, activeId);
}

QString AppSettings::visionApiKey() const
{
    const auto configs = visionApiConfigs();
    const auto activeId = activeVisionApiConfigId();
    for (const auto &config : configs) {
        if (config.id == activeId) {
            return config.apiKey;
        }
    }
    return configs.isEmpty() ? QString() : configs.first().apiKey;
}

void AppSettings::setVisionApiKey(const QString &value)
{
    auto configs = visionApiConfigs();
    if (configs.isEmpty()) {
        return;
    }
    const auto activeId = activeVisionApiConfigId();
    for (auto &config : configs) {
        if (config.id == activeId) {
            config.apiKey = value;
            break;
        }
    }
    setVisionApiConfigs(configs, activeId);
}

QString AppSettings::visionModel() const
{
    const auto configs = visionApiConfigs();
    const auto activeId = activeVisionApiConfigId();
    for (const auto &config : configs) {
        if (config.id == activeId) {
            return config.model;
        }
    }
    return configs.isEmpty() ? QStringLiteral("gpt-4.1-mini") : configs.first().model;
}

void AppSettings::setVisionModel(const QString &value)
{
    auto configs = visionApiConfigs();
    if (configs.isEmpty()) {
        return;
    }
    const auto activeId = activeVisionApiConfigId();
    for (auto &config : configs) {
        if (config.id == activeId) {
            config.model = value;
            break;
        }
    }
    setVisionApiConfigs(configs, activeId);
}

QList<VisionApiConfig> AppSettings::visionApiConfigs() const
{
    auto configs = storedVisionApiConfigs(m_settings);
    if (configs.isEmpty()) {
        return {legacyVisionApiConfig(m_settings)};
    }

    for (auto &config : configs) {
        config.apiKey = WindowsCredentialStore::read(visionApiConfigCredentialTarget(config.id)).trimmed();
        if (config.apiKey.isEmpty() && config.id == QStringLiteral("default")) {
            config.apiKey = legacyVisionApiConfig(m_settings).apiKey;
        }
    }
    return configs;
}

QString AppSettings::activeVisionApiConfigId() const
{
    const auto configs = visionApiConfigs();
    if (configs.isEmpty()) {
        return {};
    }
    const auto activeId = m_settings->value(QLatin1String(kActiveVisionApiConfigIdKey)).toString().trimmed();
    for (const auto &config : configs) {
        if (config.id == activeId) {
            return activeId;
        }
    }
    return configs.first().id;
}

void AppSettings::setVisionApiConfigs(const QList<VisionApiConfig> &configs,
                                      const QString &activeConfigId)
{
    auto normalized = normalizedVisionApiConfigs(configs);
    if (normalized.isEmpty()) {
        normalized.append(legacyVisionApiConfig(m_settings));
    }

    QString resolvedActiveId = activeConfigId.trimmed();
    const auto activeExists = std::any_of(normalized.cbegin(), normalized.cend(), [&resolvedActiveId](const auto &config) {
        return config.id == resolvedActiveId;
    });
    if (!activeExists) {
        resolvedActiveId = normalized.first().id;
    }

    QSet<QString> previousIds;
    for (const auto &config : storedVisionApiConfigs(m_settings)) {
        previousIds.insert(config.id);
    }
    QJsonArray serialized;
    for (const auto &config : normalized) {
        serialized.append(QJsonObject{{QStringLiteral("id"), config.id},
                                      {QStringLiteral("name"), config.name},
                                      {QStringLiteral("baseUrl"), config.baseUrl},
                                      {QStringLiteral("model"), config.model}});
        if (config.apiKey.isEmpty()) {
            WindowsCredentialStore::remove(visionApiConfigCredentialTarget(config.id));
        } else {
            WindowsCredentialStore::write(visionApiConfigCredentialTarget(config.id), config.apiKey);
        }
        previousIds.remove(config.id);
    }
    for (const auto &id : previousIds) {
        WindowsCredentialStore::remove(visionApiConfigCredentialTarget(id));
    }

    m_settings->setValue(QLatin1String(kVisionApiConfigsKey),
                         QJsonDocument(serialized).toJson(QJsonDocument::Compact));
    m_settings->setValue(QLatin1String(kActiveVisionApiConfigIdKey), resolvedActiveId);
    m_settings->remove(QLatin1String(kVisionBaseUrlKey));
    m_settings->remove(QLatin1String(kVisionApiKeyKey));
    m_settings->remove(QLatin1String(kVisionModelKey));
    WindowsCredentialStore::remove(QString::fromLatin1(kVisionApiCredentialTarget));
}

QStringList AppSettings::customAnalysisDimensions() const
{
    return normalizedCustomAnalysisDimensions(
        m_settings->value(QLatin1String(kCustomAnalysisDimensionsKey)).toStringList());
}

void AppSettings::setCustomAnalysisDimensions(const QStringList &values)
{
    const auto normalized = normalizedCustomAnalysisDimensions(values);
    if (normalized.isEmpty()) {
        m_settings->remove(QLatin1String(kCustomAnalysisDimensionsKey));
    } else {
        m_settings->setValue(QLatin1String(kCustomAnalysisDimensionsKey), normalized);
    }
}

bool AppSettings::searchAssistantEnabled() const
{
    return m_settings->value(QLatin1String(kSearchAssistantEnabledKey), true).toBool();
}

void AppSettings::setSearchAssistantEnabled(bool enabled)
{
    m_settings->setValue(QLatin1String(kSearchAssistantEnabledKey), enabled);
}

int AppSettings::searchAssistantAutoUnloadMinutes() const
{
    return normalizedSearchAssistantAutoUnloadMinutes(
        m_settings->value(QLatin1String(kSearchAssistantAutoUnloadMinutesKey), 30).toInt());
}

void AppSettings::setSearchAssistantAutoUnloadMinutes(int minutes)
{
    m_settings->setValue(QLatin1String(kSearchAssistantAutoUnloadMinutesKey),
                         normalizedSearchAssistantAutoUnloadMinutes(minutes));
}

bool AppSettings::quickSearchEnabled() const
{
    return m_settings->value(QLatin1String(kQuickSearchEnabledKey), true).toBool();
}

void AppSettings::setQuickSearchEnabled(bool enabled)
{
    m_settings->setValue(QLatin1String(kQuickSearchEnabledKey), enabled);
}

QString AppSettings::quickSearchShortcut() const
{
    const auto shortcut = m_settings->value(QLatin1String(kQuickSearchShortcutKey),
                                            QStringLiteral("Alt+Space"))
                              .toString()
                              .trimmed();
    return shortcut.isEmpty() ? QStringLiteral("Alt+Space") : shortcut;
}

void AppSettings::setQuickSearchShortcut(const QString &shortcut)
{
    const auto normalized = shortcut.trimmed();
    m_settings->setValue(QLatin1String(kQuickSearchShortcutKey),
                         normalized.isEmpty() ? QStringLiteral("Alt+Space") : normalized);
}

QStringList AppSettings::reportEnabledSections() const
{
    if (!m_settings->contains(QLatin1String(kReportEnabledSectionsKey))) {
        return normalizedReportEnabledSections({
            QStringLiteral("cover"), QStringLiteral("summary"), QStringLiteral("sourceOverview"),
            QStringLiteral("formatDistribution"), QStringLiteral("thumbnailIndex"), QStringLiteral("videoMetadata"),
            QStringLiteral("audioMetadata"), QStringLiteral("folderTree")
        });
    }
    return normalizedReportEnabledSections(
        m_settings->value(QLatin1String(kReportEnabledSectionsKey)).toStringList());
}

void AppSettings::setReportEnabledSections(const QStringList &sections)
{
    m_settings->setValue(QLatin1String(kReportEnabledSectionsKey), normalizedReportEnabledSections(sections));
    m_settings->sync();
}

bool AppSettings::hasQuickSearchWindowPosition() const
{
    return m_settings->contains(QLatin1String(kQuickSearchWindowXKey))
        && m_settings->contains(QLatin1String(kQuickSearchWindowYKey));
}

QPoint AppSettings::quickSearchWindowPosition() const
{
    return QPoint(m_settings->value(QLatin1String(kQuickSearchWindowXKey)).toInt(),
                  m_settings->value(QLatin1String(kQuickSearchWindowYKey)).toInt());
}

void AppSettings::setQuickSearchWindowPosition(const QPoint &position)
{
    m_settings->setValue(QLatin1String(kQuickSearchWindowXKey), position.x());
    m_settings->setValue(QLatin1String(kQuickSearchWindowYKey), position.y());
}

bool AppSettings::startAtLogin() const
{
    return m_settings->value(QLatin1String(kStartAtLoginKey), false).toBool();
}

void AppSettings::setStartAtLogin(bool enabled)
{
    m_settings->setValue(QLatin1String(kStartAtLoginKey), enabled);
}

AnalysisMode AppSettings::analysisMode() const
{
    const auto value = m_settings->value(QLatin1String(kAnalysisModeKey), static_cast<int>(AnalysisMode::Every10Frames)).toInt();
    if (value == static_cast<int>(AnalysisMode::EveryFrame)) {
        return AnalysisMode::EveryFrame;
    }
    if (value == static_cast<int>(AnalysisMode::CustomInterval)) {
        return AnalysisMode::CustomInterval;
    }

    const auto storedInterval = qMax(1, m_settings->value(QLatin1String(kFrameIntervalKey), 10).toInt());
    return storedInterval == 10 ? AnalysisMode::Every10Frames : AnalysisMode::CustomInterval;
}

void AppSettings::setAnalysisMode(AnalysisMode mode)
{
    m_settings->setValue(QLatin1String(kAnalysisModeKey), static_cast<int>(mode));
    if (mode == AnalysisMode::Every10Frames) {
        m_settings->setValue(QLatin1String(kFrameIntervalKey), 10);
    }
}

int AppSettings::frameInterval() const
{
    return qMax(1, m_settings->value(QLatin1String(kFrameIntervalKey), 10).toInt());
}

void AppSettings::setFrameInterval(int value)
{
    m_settings->setValue(QLatin1String(kFrameIntervalKey), qMax(1, value));
}

int AppSettings::thumbnailFrameIndex() const
{
    return qMax(1, m_settings->value(QLatin1String(kThumbnailFrameIndexKey), 3).toInt());
}

void AppSettings::setThumbnailFrameIndex(int value)
{
    m_settings->setValue(QLatin1String(kThumbnailFrameIndexKey), qMax(1, value));
}

int AppSettings::contactSheetFrameCount() const
{
    return qBound(1, m_settings->value(QLatin1String(kContactSheetFrameCountKey), 24).toInt(), 64);
}

void AppSettings::setContactSheetFrameCount(int value)
{
    m_settings->setValue(QLatin1String(kContactSheetFrameCountKey), qBound(1, value, 64));
}

int AppSettings::analysisTimeoutSec() const
{
    return qMax(5, m_settings->value(QLatin1String(kAnalysisTimeoutSecKey), 60).toInt());
}

void AppSettings::setAnalysisTimeoutSec(int value)
{
    m_settings->setValue(QLatin1String(kAnalysisTimeoutSecKey), qMax(5, value));
}

VideoFrameExtractionStrategy AppSettings::videoFrameExtractionStrategy() const
{
    return normalizedVideoFrameExtractionStrategy(
        m_settings->value(QLatin1String(kVideoFrameExtractionStrategyKey),
                          static_cast<int>(VideoFrameExtractionStrategy::SceneAndInterval)).toInt());
}

void AppSettings::setVideoFrameExtractionStrategy(VideoFrameExtractionStrategy value)
{
    m_settings->setValue(QLatin1String(kVideoFrameExtractionStrategyKey), static_cast<int>(value));
}

double AppSettings::videoFrameIntervalSeconds() const
{
    return boundedVideoFrameIntervalSeconds(
        m_settings->value(QLatin1String(kVideoFrameIntervalSecondsKey), 1.0).toDouble());
}

void AppSettings::setVideoFrameIntervalSeconds(double value)
{
    m_settings->setValue(QLatin1String(kVideoFrameIntervalSecondsKey), boundedVideoFrameIntervalSeconds(value));
}

double AppSettings::videoSceneThreshold() const
{
    return boundedVideoSceneThreshold(
        m_settings->value(QLatin1String(kVideoSceneThresholdKey), 0.3).toDouble());
}

void AppSettings::setVideoSceneThreshold(double value)
{
    m_settings->setValue(QLatin1String(kVideoSceneThresholdKey), boundedVideoSceneThreshold(value));
}

double AppSettings::videoMinimumSharpness() const
{
    return boundedVideoMinimumSharpness(
        m_settings->value(QLatin1String(kVideoMinimumSharpnessKey), kDefaultVideoMinimumSharpness).toDouble());
}

void AppSettings::setVideoMinimumSharpness(double value)
{
    m_settings->setValue(QLatin1String(kVideoMinimumSharpnessKey), boundedVideoMinimumSharpness(value));
}

bool AppSettings::documentAutoAnalysisEnabled() const
{
    return m_settings->value(QLatin1String(kDocumentAutoAnalysisEnabledKey), false).toBool();
}

void AppSettings::setDocumentAutoAnalysisEnabled(bool enabled)
{
    m_settings->setValue(QLatin1String(kDocumentAutoAnalysisEnabledKey), enabled);
}

bool AppSettings::photoshopAutoAnalysisEnabled() const
{
    return m_settings->value(QLatin1String(kPhotoshopAutoAnalysisEnabledKey), false).toBool();
}

void AppSettings::setPhotoshopAutoAnalysisEnabled(bool enabled)
{
    m_settings->setValue(QLatin1String(kPhotoshopAutoAnalysisEnabledKey), enabled);
}

int AppSettings::themeMode() const
{
    return normalizedThemeMode(m_settings->value(QLatin1String(kThemeModeKey), 0).toInt());
}

void AppSettings::setThemeMode(int value)
{
    m_settings->setValue(QLatin1String(kThemeModeKey), normalizedThemeMode(value));
}

int AppSettings::closeButtonBehavior() const
{
    return normalizedCloseButtonBehavior(
        m_settings->value(QLatin1String(kCloseButtonBehaviorKey), 0).toInt());
}

void AppSettings::setCloseButtonBehavior(int value)
{
    m_settings->setValue(QLatin1String(kCloseButtonBehaviorKey),
                         normalizedCloseButtonBehavior(value));
}

QString AppSettings::pendingUpdateVersion() const
{
    return m_settings->value(QLatin1String(kPendingUpdateVersionKey)).toString().trimmed();
}

void AppSettings::setPendingUpdateVersion(const QString &value)
{
    m_settings->setValue(QLatin1String(kPendingUpdateVersionKey), value.trimmed());
}

QString AppSettings::pendingUpdateInstallerPath() const
{
    return m_settings->value(QLatin1String(kPendingUpdateInstallerPathKey)).toString().trimmed();
}

void AppSettings::setPendingUpdateInstallerPath(const QString &value)
{
    m_settings->setValue(QLatin1String(kPendingUpdateInstallerPathKey), value.trimmed());
}

qint64 AppSettings::pendingUpdateInstallerSize() const
{
    return m_settings->value(QLatin1String(kPendingUpdateInstallerSizeKey), 0).toLongLong();
}

void AppSettings::setPendingUpdateInstallerSize(qint64 value)
{
    m_settings->setValue(QLatin1String(kPendingUpdateInstallerSizeKey), qMax<qint64>(0, value));
}

QString AppSettings::pendingUpdateInstallerSha256() const
{
    return m_settings->value(QLatin1String(kPendingUpdateInstallerSha256Key)).toString().trimmed();
}

void AppSettings::setPendingUpdateInstallerSha256(const QString &value)
{
    m_settings->setValue(QLatin1String(kPendingUpdateInstallerSha256Key), value.trimmed().toLower());
}

QString AppSettings::downloadedUpdateVersion() const
{
    return m_settings->value(QLatin1String(kDownloadedUpdateVersionKey)).toString().trimmed();
}

void AppSettings::setDownloadedUpdateVersion(const QString &value)
{
    m_settings->setValue(QLatin1String(kDownloadedUpdateVersionKey), value.trimmed());
}

QString AppSettings::scheduledUpdateVersion() const
{
    return m_settings->value(QLatin1String(kScheduledUpdateVersionKey)).toString().trimmed();
}

void AppSettings::setScheduledUpdateVersion(const QString &value)
{
    const auto normalized = value.trimmed();
    if (normalized.isEmpty()) {
        m_settings->remove(QLatin1String(kScheduledUpdateVersionKey));
    } else {
        m_settings->setValue(QLatin1String(kScheduledUpdateVersionKey), normalized);
    }
}

void AppSettings::clearPendingUpdate()
{
    m_settings->remove(QLatin1String(kPendingUpdateVersionKey));
    m_settings->remove(QLatin1String(kPendingUpdateInstallerPathKey));
    m_settings->remove(QLatin1String(kPendingUpdateInstallerSizeKey));
    m_settings->remove(QLatin1String(kPendingUpdateInstallerSha256Key));
    m_settings->remove(QLatin1String(kDownloadedUpdateVersionKey));
    m_settings->remove(QLatin1String(kScheduledUpdateVersionKey));
    m_settings->sync();
}

bool AppSettings::autoInstallUpdates() const
{
    return m_settings->value(QLatin1String(kAutoInstallUpdatesKey), false).toBool();
}

void AppSettings::setAutoInstallUpdates(bool enabled)
{
    m_settings->setValue(QLatin1String(kAutoInstallUpdatesKey), enabled);
}

int AppSettings::updatePolicy() const
{
    return normalizedUpdatePolicy(m_settings->value(QLatin1String(kUpdatePolicyKey), 0).toInt());
}

void AppSettings::setUpdatePolicy(int value)
{
    m_settings->setValue(QLatin1String(kUpdatePolicyKey), normalizedUpdatePolicy(value));
    // The former boolean meant unattended installation. It is intentionally
    // disabled after migration; automatic policy still prompts after download.
    m_settings->setValue(QLatin1String(kAutoInstallUpdatesKey), false);
}

int AppSettings::updateDownloadMode() const
{
    return normalizedUpdateDownloadMode(m_settings->value(QLatin1String(kUpdateDownloadModeKey), 0).toInt());
}

void AppSettings::setUpdateDownloadMode(int value)
{
    m_settings->setValue(QLatin1String(kUpdateDownloadModeKey), normalizedUpdateDownloadMode(value));
}

QString AppSettings::updateManualProxyUrl() const
{
    return m_settings->value(QLatin1String(kUpdateManualProxyUrlKey),
                             QStringLiteral("http://127.0.0.1:7890"))
        .toString()
        .trimmed();
}

void AppSettings::setUpdateManualProxyUrl(const QString &value)
{
    auto normalized = value.trimmed();
    auto parsedValue = normalized;
    if (!parsedValue.isEmpty() && !parsedValue.contains(QStringLiteral("://"))) {
        parsedValue.prepend(QStringLiteral("http://"));
    }
    const QUrl url(parsedValue);
    if (!url.userName().isEmpty() || !url.password().isEmpty()) {
        m_settings->remove(QLatin1String(kUpdateManualProxyUrlKey));
        return;
    }
    m_settings->setValue(QLatin1String(kUpdateManualProxyUrlKey), normalized);
}

QString AppSettings::feedbackSessionJson() const
{
    return m_settings->value(QLatin1String(kFeedbackSessionKey)).toString();
}

void AppSettings::setFeedbackSessionJson(const QString &json)
{
    if (json.trimmed().isEmpty()) {
        m_settings->remove(QLatin1String(kFeedbackSessionKey));
    } else {
        m_settings->setValue(QLatin1String(kFeedbackSessionKey), json);
    }
    m_settings->sync();
}

void AppSettings::sync()
{
    m_settings->sync();
}
