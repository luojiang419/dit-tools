#include "infrastructure/config/AppSettings.h"

#include <QFileInfo>
#include <QSettings>

#include <utility>

namespace {
constexpr auto kRecentProjectsKey = "recentProjects";
constexpr auto kKnownProjectsKey = "knownProjects";
constexpr auto kVisionBaseUrlKey = "materialCenter/visionBaseUrl";
constexpr auto kVisionApiKeyKey = "materialCenter/visionApiKey";
constexpr auto kVisionModelKey = "materialCenter/visionModel";
constexpr auto kCustomAnalysisDimensionsKey = "materialCenter/customAnalysisDimensions";
constexpr auto kSearchAssistantEnabledKey = "materialCenter/searchAssistantEnabled";
constexpr auto kSearchAssistantAutoUnloadMinutesKey = "materialCenter/searchAssistantAutoUnloadMinutes";
constexpr auto kQuickSearchEnabledKey = "quickSearch/enabled";
constexpr auto kQuickSearchShortcutKey = "quickSearch/shortcut";
constexpr auto kQuickSearchWindowXKey = "quickSearch/windowX";
constexpr auto kQuickSearchWindowYKey = "quickSearch/windowY";
constexpr auto kStartAtLoginKey = "quickSearch/startAtLogin";
constexpr auto kAnalysisModeKey = "materialCenter/analysisMode";
constexpr auto kFrameIntervalKey = "materialCenter/frameInterval";
constexpr auto kThumbnailFrameIndexKey = "materialCenter/thumbnailFrameIndex";
constexpr auto kContactSheetFrameCountKey = "materialCenter/contactSheetFrameCount";
constexpr auto kAnalysisTimeoutSecKey = "materialCenter/analysisTimeoutSec";
constexpr auto kThemeModeKey = "ui/themeMode";
constexpr auto kCloseButtonBehaviorKey = "ui/closeButtonBehavior";
constexpr auto kPendingUpdateVersionKey = "updates/pendingVersion";
constexpr auto kPendingUpdateInstallerPathKey = "updates/pendingInstallerPath";
constexpr auto kDownloadedUpdateVersionKey = "updates/downloadedVersion";
constexpr auto kScheduledUpdateVersionKey = "updates/scheduledVersion";
constexpr auto kAutoInstallUpdatesKey = "updates/autoInstall";
constexpr auto kUpdateDownloadModeKey = "updates/downloadMode";
constexpr auto kUpdateManualProxyUrlKey = "updates/manualProxyUrl";
constexpr auto kFeedbackSessionKey = "feedback/sessionJson";

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

}

AppSettings::AppSettings()
    : m_settings(new QSettings)
{
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
    return m_settings->value(QLatin1String(kVisionBaseUrlKey)).toString().trimmed();
}

void AppSettings::setVisionBaseUrl(const QString &value)
{
    m_settings->setValue(QLatin1String(kVisionBaseUrlKey), value.trimmed());
}

QString AppSettings::visionApiKey() const
{
    return m_settings->value(QLatin1String(kVisionApiKeyKey)).toString().trimmed();
}

void AppSettings::setVisionApiKey(const QString &value)
{
    m_settings->setValue(QLatin1String(kVisionApiKeyKey), value.trimmed());
}

QString AppSettings::visionModel() const
{
    return m_settings->value(QLatin1String(kVisionModelKey), QStringLiteral("gpt-4.1-mini")).toString().trimmed();
}

void AppSettings::setVisionModel(const QString &value)
{
    const auto normalized = value.trimmed();
    m_settings->setValue(QLatin1String(kVisionModelKey), normalized.isEmpty() ? QStringLiteral("gpt-4.1-mini") : normalized);
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
        m_settings->value(QLatin1String(kSearchAssistantAutoUnloadMinutesKey), 60).toInt());
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
    return m_settings->value(QLatin1String(kUpdateManualProxyUrlKey)).toString().trimmed();
}

void AppSettings::setUpdateManualProxyUrl(const QString &value)
{
    m_settings->setValue(QLatin1String(kUpdateManualProxyUrlKey), value.trimmed());
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
