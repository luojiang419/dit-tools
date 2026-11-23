#include "application/UpdateService.h"

#include "application/UpdaterSession.h"

#include "infrastructure/config/AppSettings.h"
#include "shared/Paths.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QNetworkProxyFactory>
#include <QNetworkProxyQuery>
#include <QProcess>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QVersionNumber>

#if defined(Q_OS_WIN)
#include <qt_windows.h>
#endif

#include <limits>

#ifndef CINEVAULT_UPDATE_SIGNER_SHA256
#define CINEVAULT_UPDATE_SIGNER_SHA256 ""
#endif

namespace {
constexpr auto kLatestReleaseUrl = "https://api.github.com/repos/luojiang419/dit-tools/releases/latest";
constexpr int kUpdateDownloadModeAuto = 0;
constexpr int kUpdateDownloadModeManual = 1;
constexpr int kUpdateDownloadModeDirect = 2;
constexpr int kUpdatePolicyAutomatic = 0;
constexpr int kUpdatePolicyManual = 1;
constexpr int kUpdatePolicyDisabled = 2;
constexpr qint64 kMinimumUpdateSpaceReserve = 512LL * 1024 * 1024;

QString normalizedPlatformKey(QString platformKey)
{
    platformKey = platformKey.trimmed().toLower();
    return platformKey.isEmpty() ? UpdateService::currentPlatformKey() : platformKey;
}

QString updatesRootForPlatform(const QString &platformKey)
{
    return QDir(Paths::updatesRoot()).filePath(normalizedPlatformKey(platformKey));
}

bool installerNameMatchesExpected(const QString &installerName, const QStringList &expectedNames)
{
    for (const auto &expectedName : expectedNames) {
        if (installerName.compare(expectedName, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

QString expectedInstallerNamesLabel(const QStringList &expectedNames)
{
    return expectedNames.join(QStringLiteral(" / "));
}

QStringList curlNetworkArguments(const QString &proxyUrl)
{
    if (proxyUrl.trimmed().isEmpty()) {
        return {QStringLiteral("--noproxy"), QStringLiteral("*")};
    }

    return {QStringLiteral("--proxy"), proxyUrl};
}

void appendUnique(QStringList *items, const QString &value)
{
    const auto normalized = value.trimmed();
    if (!normalized.isEmpty() && !items->contains(normalized, Qt::CaseInsensitive)) {
        items->append(normalized);
    }
}

QString proxyUrlForHostPort(const QString &scheme, const QString &host, int port)
{
    QUrl url;
    url.setScheme(scheme);
    url.setHost(host);
    url.setPort(port);
    return url.toString(QUrl::FullyEncoded);
}

QStringList localProxyHosts()
{
    QStringList hosts{QStringLiteral("127.0.0.1"), QStringLiteral("localhost")};
    const auto addresses = QNetworkInterface::allAddresses();
    for (const auto &address : addresses) {
        if (address.protocol() != QAbstractSocket::IPv4Protocol || address.isLoopback()) {
            continue;
        }
        appendUnique(&hosts, address.toString());
    }
    return hosts;
}

bool pathIsInside(const QString &path, const QString &root)
{
    const auto normalizedPath = QDir::fromNativeSeparators(QDir::cleanPath(path));
    auto normalizedRoot = QDir::fromNativeSeparators(QDir::cleanPath(root));
    if (!normalizedRoot.endsWith(QLatin1Char('/'))) {
        normalizedRoot.append(QLatin1Char('/'));
    }
#if defined(Q_OS_WIN)
    return normalizedPath.startsWith(normalizedRoot, Qt::CaseInsensitive);
#else
    return normalizedPath.startsWith(normalizedRoot, Qt::CaseSensitive);
#endif
}

bool isReparsePoint(const QString &path)
{
#if defined(Q_OS_WIN)
    const auto nativePath = QDir::toNativeSeparators(path).toStdWString();
    const auto attributes = GetFileAttributesW(nativePath.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES
        && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    Q_UNUSED(path)
    return false;
#endif
}
}

UpdateService::UpdateService(AppSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
}

UpdateService::~UpdateService()
{
    if (m_checkProcess) {
        m_checkProcess->kill();
        m_checkProcess->waitForFinished(2000);
    }
    if (m_downloadProcess) {
        m_downloadProcess->kill();
        m_downloadProcess->waitForFinished(2000);
    }
}

QString UpdateService::currentPlatformKey()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    return QStringLiteral("unknown");
#endif
}

QString UpdateService::normalizeVersionTag(const QString &versionTag)
{
    auto normalized = versionTag.trimmed();
    if (normalized.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) {
        normalized.remove(0, 1);
    }

    const auto parsed = QVersionNumber::fromString(normalized);
    if (parsed.isNull()) {
        return {};
    }

    return QStringLiteral("v") + parsed.toString();
}

int UpdateService::compareVersionTags(const QString &left, const QString &right)
{
    const auto normalizedLeft = normalizeVersionTag(left);
    const auto normalizedRight = normalizeVersionTag(right);

    if (normalizedLeft.isEmpty() && normalizedRight.isEmpty()) {
        return 0;
    }
    if (normalizedLeft.isEmpty()) {
        return -1;
    }
    if (normalizedRight.isEmpty()) {
        return 1;
    }

    return QVersionNumber::compare(QVersionNumber::fromString(normalizedLeft.mid(1)),
                                   QVersionNumber::fromString(normalizedRight.mid(1)));
}

QStringList UpdateService::expectedInstallerNames(const QString &versionTag, const QString &platformKey)
{
    const auto normalized = normalizeVersionTag(versionTag);
    if (normalized.isEmpty()) {
        return {};
    }

    const auto normalizedPlatform = normalizedPlatformKey(platformKey);
    if (normalizedPlatform == QStringLiteral("windows")) {
        return {QStringLiteral("CineVault-Setup-%1.exe").arg(normalized)};
    }
    if (normalizedPlatform == QStringLiteral("macos")) {
        return {
            QStringLiteral("CineVault-macOS-%1.dmg").arg(normalized),
            QStringLiteral("CineVault-macOS-%1.pkg").arg(normalized)
        };
    }

    return {};
}

QString UpdateService::expectedInstallerName(const QString &versionTag, const QString &platformKey)
{
    const auto expectedNames = expectedInstallerNames(versionTag, platformKey);
    return expectedNames.isEmpty() ? QString() : expectedNames.constFirst();
}

bool UpdateService::parseLatestRelease(const QByteArray &payload, UpdateReleaseInfo *info, QString *errorMessage, const QString &platformKey)
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("GitHub 发布信息解析失败。");
        }
        return false;
    }

    const auto root = document.object();
    const auto versionTag = normalizeVersionTag(root.value(QStringLiteral("tag_name")).toString());
    if (versionTag.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("GitHub 发布信息缺少有效版本号。");
        }
        return false;
    }

    const auto expectedNames = expectedInstallerNames(versionTag, platformKey);
    if (expectedNames.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("当前平台暂不支持自动匹配更新资产。");
        }
        return false;
    }

    const auto assets = root.value(QStringLiteral("assets")).toArray();
    for (const auto &expectedName : expectedNames) {
        QList<QJsonObject> matchingAssets;
        for (const auto &assetValue : assets) {
            const auto assetObject = assetValue.toObject();
            if (assetObject.value(QStringLiteral("name")).toString() != expectedName) {
                continue;
            }

            matchingAssets.append(assetObject);
        }

        if (matchingAssets.size() > 1) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("最新发布版本包含重复更新资产：%1").arg(expectedName);
            }
            return false;
        }
        if (matchingAssets.isEmpty()) {
            continue;
        }

        const auto assetObject = matchingAssets.constFirst();
        const auto downloadUrl = assetObject.value(QStringLiteral("browser_download_url")).toString().trimmed();
        const auto installerSize = assetObject.value(QStringLiteral("size")).toVariant().toLongLong();
        const auto installerSha256 = normalizeSha256(assetObject.value(QStringLiteral("digest")).toString());
        if (!isTrustedReleaseUrl(downloadUrl)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("更新资产下载地址不是受信任的 GitHub HTTPS 地址。");
            }
            return false;
        }
        if (installerSize <= 0) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("更新资产大小无效：%1").arg(expectedName);
            }
            return false;
        }
        if (installerSha256.isEmpty()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("更新资产缺少有效 SHA-256 摘要：%1").arg(expectedName);
            }
            return false;
        }

        if (info) {
            info->versionTag = versionTag;
            info->installerName = expectedName;
            info->installerUrl = downloadUrl;
            info->installerSize = installerSize;
            info->installerSha256 = installerSha256;
        }
        return true;
    }

    if (errorMessage) {
        *errorMessage = QStringLiteral("最新发布版本缺少当前平台更新包：%1")
                            .arg(expectedInstallerNamesLabel(expectedNames));
    }
    return false;
}

QString UpdateService::normalizeSha256(const QString &value)
{
    auto normalized = value.trimmed().toLower();
    if (normalized.startsWith(QStringLiteral("sha256:"))) {
        normalized.remove(0, 7);
    }
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]{64}$"));
    return pattern.match(normalized).hasMatch() ? normalized : QString();
}

QString UpdateService::fileSha256(const QString &path, QString *errorMessage)
{
    if (errorMessage) {
        errorMessage->clear();
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法读取更新安装包：%1").arg(path);
        }
        return {};
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("计算更新安装包 SHA-256 失败：%1").arg(path);
        }
        return {};
    }
    return QString::fromLatin1(hash.result().toHex());
}

bool UpdateService::isTrustedReleaseUrl(const QString &url)
{
    const QUrl parsed(url);
    if (!parsed.isValid() || parsed.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0) {
        return false;
    }
    const auto host = parsed.host().trimmed().toLower();
    return host == QStringLiteral("github.com")
        || host == QStringLiteral("api.github.com")
        || host.endsWith(QStringLiteral(".githubusercontent.com"));
}

bool UpdateService::updateCheckAllowed(int updatePolicy, bool manual)
{
    if (updatePolicy == kUpdatePolicyDisabled) {
        return false;
    }
    return manual || updatePolicy == kUpdatePolicyAutomatic;
}

bool UpdateService::directFallbackAllowed(int networkMode)
{
    return networkMode == kUpdateDownloadModeAuto;
}

bool UpdateService::validateInstallerFile(const QString &versionTag,
                                          const QString &installerPath,
                                          qint64 expectedSize,
                                          const QString &expectedSha256,
                                          QString *errorMessage)
{
    if (errorMessage) {
        errorMessage->clear();
    }
    const auto normalizedVersion = normalizeVersionTag(versionTag);
    const auto normalizedSha256 = normalizeSha256(expectedSha256);
    const QFileInfo installerInfo(installerPath);
    if (normalizedVersion.isEmpty() || normalizedSha256.isEmpty() || expectedSize <= 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("更新安装包校验信息不完整。");
        }
        return false;
    }
    if (!installerInfo.isFile() || installerInfo.isSymLink() || isReparsePoint(installerInfo.absoluteFilePath())) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("更新安装包不存在或不是普通文件：%1").arg(installerPath);
        }
        return false;
    }
    if (!installerNameMatchesExpected(installerInfo.fileName(), expectedInstallerNames(normalizedVersion))) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("更新安装包名称与版本不匹配：%1").arg(installerInfo.fileName());
        }
        return false;
    }

    const auto canonicalPath = installerInfo.canonicalFilePath();
    const QFileInfo rootInfo(updatesRootForPlatform(currentPlatformKey()));
    const auto canonicalRoot = rootInfo.canonicalFilePath().isEmpty()
        ? rootInfo.absoluteFilePath()
        : rootInfo.canonicalFilePath();
    if (canonicalPath.isEmpty() || !pathIsInside(canonicalPath, canonicalRoot)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("更新安装包不在应用专用缓存目录内。");
        }
        return false;
    }
    if (installerInfo.size() != expectedSize) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("更新安装包大小与发布信息不一致。");
        }
        return false;
    }

    QString hashError;
    const auto actualSha256 = fileSha256(canonicalPath, &hashError);
    if (actualSha256.isEmpty() || actualSha256 != normalizedSha256) {
        if (errorMessage) {
            *errorMessage = hashError.isEmpty()
                ? QStringLiteral("更新安装包 SHA-256 校验失败。"): hashError;
        }
        return false;
    }
    return true;
}

QString UpdateService::expectedUpdateSignerSha256()
{
    return normalizeSha256(QString::fromLatin1(CINEVAULT_UPDATE_SIGNER_SHA256));
}

bool UpdateService::verifyInstallerAuthenticode(const QString &installerPath,
                                                 QString *errorMessage)
{
    if (errorMessage) {
        errorMessage->clear();
    }
#if !defined(Q_OS_WIN)
    Q_UNUSED(installerPath)
    return true;
#else
    const auto expectedSigner = expectedUpdateSignerSha256();
    if (expectedSigner.isEmpty()) {
        return true;
    }

    QProcess process;
    process.setProgram(QStringLiteral("powershell.exe"));
    process.setArguments({
        QStringLiteral("-NoProfile"),
        QStringLiteral("-NonInteractive"),
        QStringLiteral("-ExecutionPolicy"),
        QStringLiteral("Bypass"),
        QStringLiteral("-Command"),
        QStringLiteral(
            "$ErrorActionPreference='Stop';"
            "$signature=Get-AuthenticodeSignature -LiteralPath $env:CINEVAULT_INSTALLER_PATH;"
            "if($signature.Status -ne 'Valid' -or $null -eq $signature.SignerCertificate){exit 2};"
            "$thumbprint=$signature.SignerCertificate.GetCertHashString('SHA256').Replace(' ','').ToLowerInvariant();"
            "if($thumbprint -ne $env:CINEVAULT_EXPECTED_SIGNER_SHA256){exit 3};"
            "Write-Output $signature.SignerCertificate.Subject")
    });
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("CINEVAULT_INSTALLER_PATH"), QFileInfo(installerPath).absoluteFilePath());
    environment.insert(QStringLiteral("CINEVAULT_EXPECTED_SIGNER_SHA256"), expectedSigner);
    process.setProcessEnvironment(environment);
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
        arguments->flags |= CREATE_NO_WINDOW;
    });
    process.start();
    if (!process.waitForStarted(3000) || !process.waitForFinished(20000)) {
        process.kill();
        process.waitForFinished(2000);
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法完成更新安装包发布者验证。");
        }
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (errorMessage) {
            *errorMessage = process.exitCode() == 3
                ? QStringLiteral("更新安装包签名者与内置可信发布者不一致。")
                : QStringLiteral("更新安装包没有有效的 Authenticode 签名。");
        }
        return false;
    }
    return true;
#endif
}

QString UpdateService::latestReleaseStatusMessage(int statusCode, const QString &networkErrorString)
{
    if (statusCode == 404) {
        return QStringLiteral("当前仓库还没有可用的发布版本。");
    }

    return QStringLiteral("检查更新失败：%1").arg(
        networkErrorString.trimmed().isEmpty() ? QStringLiteral("网络请求没有返回结果。") : networkErrorString);
}

QString UpdateService::normalizedProxyUrl(const QString &proxyUrl)
{
    auto candidate = proxyUrl.trimmed();
    if (candidate.isEmpty()) {
        return {};
    }
    if (!candidate.contains(QStringLiteral("://"))) {
        candidate.prepend(QStringLiteral("http://"));
    }

    QUrl url(candidate);
    const auto scheme = url.scheme().trimmed().toLower();
    if (!url.isValid()
        || url.host().trimmed().isEmpty()
        || url.port() <= 0
        || !url.userName().isEmpty()
        || !url.password().isEmpty()
        || (scheme != QStringLiteral("http")
            && scheme != QStringLiteral("https")
            && scheme != QStringLiteral("socks4")
            && scheme != QStringLiteral("socks4a")
            && scheme != QStringLiteral("socks5")
            && scheme != QStringLiteral("socks5h"))) {
        return {};
    }

    url.setScheme(scheme);
    return url.toString(QUrl::FullyEncoded);
}

QString UpdateService::proxyUrlForNetworkProxy(const QNetworkProxy &proxy)
{
    QString scheme;
    switch (proxy.type()) {
    case QNetworkProxy::HttpProxy:
    case QNetworkProxy::HttpCachingProxy:
    case QNetworkProxy::FtpCachingProxy:
        scheme = QStringLiteral("http");
        break;
    case QNetworkProxy::Socks5Proxy:
        scheme = QStringLiteral("socks5");
        break;
    default:
        return {};
    }

    if (proxy.hostName().trimmed().isEmpty() || proxy.port() <= 0) {
        return {};
    }

    QUrl url;
    url.setScheme(scheme);
    url.setHost(proxy.hostName().trimmed());
    url.setPort(proxy.port());
    if (!proxy.user().trimmed().isEmpty()) {
        url.setUserName(proxy.user());
    }
    if (!proxy.password().isEmpty()) {
        url.setPassword(proxy.password());
    }
    return normalizedProxyUrl(url.toString(QUrl::FullyEncoded));
}

QString UpdateService::preferredProxyUrl(const QList<QNetworkProxy> &proxies)
{
    for (const auto &proxy : proxies) {
        const auto proxyUrl = proxyUrlForNetworkProxy(proxy);
        if (!proxyUrl.isEmpty()) {
            return proxyUrl;
        }
    }

    return {};
}

QStringList UpdateService::proxyUrlsForEnvironment(const QProcessEnvironment &environment)
{
    QStringList proxyUrls;
    const QStringList keys{
        QStringLiteral("HTTPS_PROXY"),
        QStringLiteral("https_proxy"),
        QStringLiteral("HTTP_PROXY"),
        QStringLiteral("http_proxy"),
        QStringLiteral("ALL_PROXY"),
        QStringLiteral("all_proxy")
    };

    for (const auto &key : keys) {
        appendUnique(&proxyUrls, normalizedProxyUrl(environment.value(key)));
    }

    return proxyUrls;
}

QStringList UpdateService::localProxyCandidates(const QStringList &hosts)
{
    const auto resolvedHosts = hosts.isEmpty() ? localProxyHosts() : hosts;
    QStringList candidates;
    for (const auto &host : resolvedHosts) {
        const auto normalizedHost = host.trimmed();
        if (normalizedHost.isEmpty()) {
            continue;
        }

        for (const auto port : {7890, 7897, 7899, 8080, 10809, 20171}) {
            appendUnique(&candidates, proxyUrlForHostPort(QStringLiteral("http"), normalizedHost, port));
        }
        for (const auto port : {1080, 10808}) {
            appendUnique(&candidates, proxyUrlForHostPort(QStringLiteral("socks5"), normalizedHost, port));
        }
    }
    return candidates;
}

QString UpdateService::firstReachableProxyUrl(const QStringList &proxyUrls, int timeoutMs)
{
    QStringList normalizedUrls;
    for (const auto &proxyUrl : proxyUrls) {
        appendUnique(&normalizedUrls, normalizedProxyUrl(proxyUrl));
    }

    for (const auto &proxyUrl : normalizedUrls) {
        const QUrl url(proxyUrl);
        QTcpSocket socket;
        socket.connectToHost(url.host(), url.port());
        if (socket.waitForConnected(qMax(20, timeoutMs))) {
            socket.disconnectFromHost();
            return proxyUrl;
        }
    }

    return {};
}

QString UpdateService::currentVersionTag() const
{
    const auto normalized = normalizeVersionTag(QCoreApplication::applicationVersion());
    return normalized.isEmpty() ? QStringLiteral("v0.0.0") : normalized;
}

bool UpdateService::isBusy() const
{
    return m_busy;
}

bool UpdateService::hasPendingUpdate() const
{
    QString versionTag;
    QString installerPath;
    if (!readPendingUpdate(&versionTag, &installerPath)) {
        return false;
    }

    return compareVersionTags(versionTag, currentVersionTag()) > 0;
}

void UpdateService::beginStartupFlow()
{
    clearPendingUpdateIfCurrentOrMissing();
    cleanupUpdateCache();

    const auto updatePolicy = m_settings ? m_settings->updatePolicy() : kUpdatePolicyAutomatic;
    if (updatePolicy == kUpdatePolicyDisabled) {
        setStatusMessage(QStringLiteral("已禁止更新，不会在启动时联网检查。"));
        return;
    }

    QString versionTag;
    QString installerPath;
    if (readPendingUpdate(&versionTag, &installerPath)
        && compareVersionTags(versionTag, currentVersionTag()) > 0) {
        const auto scheduledVersion = m_settings
            ? normalizeVersionTag(m_settings->scheduledUpdateVersion())
            : QString();
        const auto shouldInstall = m_settings
            && compareVersionTags(scheduledVersion, versionTag) == 0;
        if (shouldInstall) {
            QString errorMessage;
            if (installPendingUpdateNow(&errorMessage)) {
                return;
            }
            setStatusMessage(errorMessage);
        }
        setStatusMessage(QStringLiteral("已检测到待安装更新：%1").arg(versionTag));
        emit updateReady(versionTag, installerPath, false);
        return;
    }

    if (updatePolicy == kUpdatePolicyManual) {
        setStatusMessage(QStringLiteral("更新策略为手动，仅在点击检查更新后联网。"));
        return;
    }

    checkForUpdates(false);
}

void UpdateService::checkForUpdates(bool manual)
{
    if (m_busy) {
        setStatusMessage(QStringLiteral("正在检查或下载更新，请稍候。"));
        return;
    }

    const auto updatePolicy = m_settings ? m_settings->updatePolicy() : kUpdatePolicyAutomatic;
    if (!updateCheckAllowed(updatePolicy, manual)) {
        setStatusMessage(updatePolicy == kUpdatePolicyDisabled
            ? QStringLiteral("更新已被禁止，请先在设置中切换到自动或手动更新。")
            : QStringLiteral("当前仅允许手动检查更新。"));
        return;
    }

    clearPendingUpdateIfCurrentOrMissing();

    QString versionTag;
    QString installerPath;
    if (readPendingUpdate(&versionTag, &installerPath)
        && compareVersionTags(versionTag, currentVersionTag()) > 0) {
        setStatusMessage(QStringLiteral("已找到待安装更新：%1").arg(versionTag));
        emit updateReady(versionTag, installerPath, manual);
        return;
    }

    m_manualCheck = manual;
    setBusy(true);
    QString proxyErrorMessage;
    const auto proxyUrl = configuredProxyUrl(&proxyErrorMessage);
    if (!proxyErrorMessage.isEmpty()) {
        setBusy(false);
        setStatusMessage(proxyErrorMessage);
        return;
    }

    const auto proxyLabel = proxyStatusLabel(proxyUrl);
    const auto directMode = m_settings && m_settings->updateDownloadMode() == kUpdateDownloadModeDirect;
    setStatusMessage(proxyUrl.isEmpty()
        ? (directMode
            ? (manual ? QStringLiteral("正在直连检查最新发布版本...")
                      : QStringLiteral("启动后正在直连检查最新发布版本..."))
            : (manual ? QStringLiteral("未检测到可用代理，正在直连检查最新发布版本...")
                      : QStringLiteral("未检测到可用代理，启动后正在直连检查最新发布版本...")))
        : (manual ? QStringLiteral("正在通过%1检查最新发布版本...").arg(proxyLabel)
                  : QStringLiteral("启动后正在通过%1检查最新发布版本...").arg(proxyLabel)));
    const auto networkMode = m_settings ? m_settings->updateDownloadMode() : kUpdateDownloadModeAuto;
    launchCheckProcess(proxyUrl,
                       !proxyUrl.isEmpty() && directFallbackAllowed(networkMode));
}

QString UpdateService::systemProxyUrl() const
{
    return preferredProxyUrl(
        QNetworkProxyFactory::systemProxyForQuery(QNetworkProxyQuery(QUrl(QString::fromLatin1(kLatestReleaseUrl)))));
}

QString UpdateService::autoDetectedProxyUrl() const
{
    QStringList candidates;
    appendUnique(&candidates, systemProxyUrl());
    for (const auto &proxyUrl : proxyUrlsForEnvironment(QProcessEnvironment::systemEnvironment())) {
        appendUnique(&candidates, proxyUrl);
    }
    for (const auto &proxyUrl : localProxyCandidates()) {
        appendUnique(&candidates, proxyUrl);
    }

    return firstReachableProxyUrl(candidates);
}

QString UpdateService::configuredProxyUrl(QString *errorMessage) const
{
    if (errorMessage) {
        errorMessage->clear();
    }

    const auto mode = m_settings ? m_settings->updateDownloadMode() : kUpdateDownloadModeAuto;
    if (mode == kUpdateDownloadModeDirect) {
        return {};
    }

    if (mode == kUpdateDownloadModeManual) {
        const auto proxyUrl = normalizedProxyUrl(m_settings ? m_settings->updateManualProxyUrl() : QString());
        if (proxyUrl.isEmpty() && errorMessage) {
            *errorMessage = QStringLiteral("手动代理地址无效，请填写类似 http://127.0.0.1:7890 的地址。");
        }
        return proxyUrl;
    }

    return autoDetectedProxyUrl();
}

QString UpdateService::proxyStatusLabel(const QString &proxyUrl) const
{
    if (proxyUrl.trimmed().isEmpty()) {
        return {};
    }

    return m_settings && m_settings->updateDownloadMode() == kUpdateDownloadModeManual
        ? QStringLiteral("手动代理")
        : QStringLiteral("自动检测代理");
}

void UpdateService::launchCheckProcess(const QString &proxyUrl, bool allowDirectFallback)
{
    if (m_checkProcess) {
        m_checkProcess->kill();
        m_checkProcess->waitForFinished(2000);
        m_checkProcess->deleteLater();
        m_checkProcess = nullptr;
    }

    m_checkProxyUrl = proxyUrl.trimmed();
    m_checkAllowDirectFallback = allowDirectFallback && !m_checkProxyUrl.isEmpty();

    QStringList arguments = curlNetworkArguments(m_checkProxyUrl);
    arguments << QStringLiteral("-L")
              << QStringLiteral("--silent")
              << QStringLiteral("--show-error")
              << QStringLiteral("--connect-timeout")
              << QStringLiteral("20")
              << QStringLiteral("--max-time")
              << QStringLiteral("60")
              << QStringLiteral("-H")
              << QStringLiteral("User-Agent: CineVault")
              << QStringLiteral("-H")
              << QStringLiteral("Accept: application/vnd.github+json")
              << QStringLiteral("--output")
              << QStringLiteral("-")
              << QStringLiteral("--write-out")
              << QStringLiteral("\n%{http_code}")
              << QString::fromLatin1(kLatestReleaseUrl);

    m_checkProcess = new QProcess(this);
    m_checkProcess->setProgram(QStringLiteral("curl.exe"));
    m_checkProcess->setArguments(arguments);
    connect(m_checkProcess,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            &UpdateService::finishCheckProcess);
    m_checkProcess->start();
    if (!m_checkProcess->waitForStarted(3000)) {
        m_checkProcess->deleteLater();
        m_checkProcess = nullptr;
        m_checkProxyUrl.clear();
        m_checkAllowDirectFallback = false;
        setBusy(false);
        setStatusMessage(QStringLiteral("无法启动版本检查进程。"));
    }
}

bool UpdateService::retryCheckWithoutProxy()
{
    if (!m_checkAllowDirectFallback || m_checkProxyUrl.isEmpty()) {
        return false;
    }

    setStatusMessage(QStringLiteral("代理检查失败，正在尝试直连 GitHub..."));
    launchCheckProcess(QString(), false);
    return true;
}

bool UpdateService::installPendingUpdateNow(QString *errorMessage)
{
    clearPendingUpdateIfCurrentOrMissing();

    QString versionTag;
    QString installerPath;
    qint64 installerSize = 0;
    QString installerSha256;
    if (!readPendingUpdate(&versionTag, &installerPath, &installerSize, &installerSha256)
        || compareVersionTags(versionTag, currentVersionTag()) <= 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("当前没有可安装的更新包。");
        }
        return false;
    }

#if !defined(Q_OS_WIN)
    if (errorMessage) {
        *errorMessage = QStringLiteral("当前平台暂未实现自动安装，请手动打开已下载的更新包：%1")
                            .arg(QDir::toNativeSeparators(installerPath));
    }
    return false;
#endif

    if (!verifyInstallerAuthenticode(installerPath, errorMessage)) {
        return false;
    }

    const auto appDir = QCoreApplication::applicationDirPath();
    const auto appPid = QCoreApplication::applicationPid();
    if (!UpdaterSessionRunner::launchDetached(versionTag,
                                              installerPath,
                                              installerSize,
                                              installerSha256,
                                              appDir,
                                              QCoreApplication::applicationFilePath(),
                                              appPid,
                                              errorMessage)) {
        return false;
    }

    setStatusMessage(QStringLiteral("更新进度窗口已打开，正在退出旧版本：%1").arg(versionTag));
    QTimer::singleShot(500, QCoreApplication::instance(), &QCoreApplication::quit);
    return true;
}

void UpdateService::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }

    m_busy = busy;
    emit busyChanged();
}

void UpdateService::setStatusMessage(const QString &message)
{
    if (m_statusMessage == message) {
        return;
    }

    m_statusMessage = message;
    emit statusMessageChanged(message);
}

void UpdateService::clearPendingUpdateIfCurrentOrMissing()
{
    if (!m_settings) {
        return;
    }

    const auto versionTag = normalizeVersionTag(m_settings->pendingUpdateVersion());
    if (versionTag.isEmpty()) {
        return;
    }

    QString installerPath;
    if (!readPendingUpdate(nullptr, &installerPath)
        || compareVersionTags(versionTag, currentVersionTag()) <= 0) {
        m_settings->clearPendingUpdate();
    }
}

void UpdateService::cleanupUpdateCache()
{
    const auto platformRoot = updatesRootForPlatform(currentPlatformKey());
    QDir root(platformRoot);
    if (!root.exists()) {
        return;
    }

    const auto currentVersion = currentVersionTag();
    const auto pendingPath = m_settings
        ? QFileInfo(m_settings->pendingUpdateInstallerPath()).canonicalFilePath()
        : QString();
    const auto now = QDateTime::currentDateTimeUtc();
    const auto files = root.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Time);
    int retainedPartialCount = 0;
    int retainedFutureInstallerCount = 0;
    for (const auto &file : files) {
        if (file.canonicalFilePath() == pendingPath) {
            continue;
        }
        if (file.fileName().endsWith(QStringLiteral(".part"), Qt::CaseInsensitive)) {
            ++retainedPartialCount;
            if (retainedPartialCount > 2
                || file.lastModified().toUTC().secsTo(now) > 14LL * 24 * 60 * 60) {
                QFile::remove(file.absoluteFilePath());
            }
            continue;
        }

        const QRegularExpression installerPattern(
            QStringLiteral("^CineVault-Setup-(v[0-9]+(?:\\.[0-9]+)+)\\.exe$"),
            QRegularExpression::CaseInsensitiveOption);
        const auto match = installerPattern.match(file.fileName());
        if (match.hasMatch()) {
            if (compareVersionTags(match.captured(1), currentVersion) <= 0) {
                QFile::remove(file.absoluteFilePath());
            } else {
                ++retainedFutureInstallerCount;
                if (retainedFutureInstallerCount > 2) {
                    QFile::remove(file.absoluteFilePath());
                }
            }
        }
    }

    const auto cleanupOldDirectories = [&now](const QString &directoryPath, int retentionDays) {
        QDir directory(directoryPath);
        const auto entries = directory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
        for (const auto &entry : entries) {
            if (entry.lastModified().toUTC().secsTo(now)
                > static_cast<qint64>(retentionDays) * 24 * 60 * 60) {
                QDir(entry.absoluteFilePath()).removeRecursively();
            }
        }
    };
    cleanupOldDirectories(root.filePath(QStringLiteral("staging")), 1);
    cleanupOldDirectories(root.filePath(QStringLiteral("sessions")), 30);
}

bool UpdateService::readPendingUpdate(QString *versionTag,
                                      QString *installerPath,
                                      qint64 *installerSize,
                                      QString *installerSha256) const
{
    if (!m_settings) {
        return false;
    }

    const auto normalizedVersionTag = normalizeVersionTag(m_settings->pendingUpdateVersion());
    const auto normalizedInstallerPath = m_settings->pendingUpdateInstallerPath().trimmed();
    const auto expectedSize = m_settings->pendingUpdateInstallerSize();
    const auto expectedSha256 = normalizeSha256(m_settings->pendingUpdateInstallerSha256());
    if (normalizedVersionTag.isEmpty()
        || normalizedInstallerPath.isEmpty()
        || !validateInstallerFile(normalizedVersionTag,
                                  normalizedInstallerPath,
                                  expectedSize,
                                  expectedSha256)) {
        return false;
    }

    if (versionTag) {
        *versionTag = normalizedVersionTag;
    }
    if (installerPath) {
        *installerPath = QFileInfo(normalizedInstallerPath).canonicalFilePath();
    }
    if (installerSize) {
        *installerSize = expectedSize;
    }
    if (installerSha256) {
        *installerSha256 = expectedSha256;
    }
    return true;
}

bool UpdateService::useExistingInstaller(const UpdateReleaseInfo &release, bool manual)
{
    QString versionTag;
    QString installerPath;
    if (readPendingUpdate(&versionTag, &installerPath)
        && compareVersionTags(versionTag, release.versionTag) == 0
        && QFileInfo(installerPath).size() == release.installerSize
        && fileSha256(installerPath) == release.installerSha256) {
        setStatusMessage(QStringLiteral("已找到已下载更新包：%1").arg(versionTag));
        emit updateReady(versionTag, installerPath, manual);
        return true;
    }

    const QStringList candidatePaths{
        QDir(updatesRootForPlatform(currentPlatformKey())).filePath(release.installerName)
    };

    QString existingInstallerPath;
    for (const auto &candidatePath : candidatePaths) {
        const QFileInfo candidateInfo(candidatePath);
        if (validateInstallerFile(release.versionTag,
                                  candidatePath,
                                  release.installerSize,
                                  release.installerSha256)) {
            existingInstallerPath = candidatePath;
            break;
        }
    }

    if (existingInstallerPath.isEmpty()) {
        return false;
    }

    if (m_settings) {
        m_settings->setDownloadedUpdateVersion(release.versionTag);
        m_settings->setPendingUpdateVersion(release.versionTag);
        m_settings->setPendingUpdateInstallerPath(existingInstallerPath);
        m_settings->setPendingUpdateInstallerSize(release.installerSize);
        m_settings->setPendingUpdateInstallerSha256(release.installerSha256);
        m_settings->setScheduledUpdateVersion(QString());
        m_settings->sync();
    }

    setStatusMessage(QStringLiteral("已复用已下载更新包：%1").arg(release.versionTag));
    emit updateReady(release.versionTag, existingInstallerPath, manual);
    return true;
}

void UpdateService::startInstallerDownload(const UpdateReleaseInfo &release, bool manual)
{
    const auto platformUpdatesRoot = updatesRootForPlatform(currentPlatformKey());
    if (!QDir().mkpath(platformUpdatesRoot)) {
        setBusy(false);
        setStatusMessage(QStringLiteral("无法创建更新缓存目录：%1").arg(platformUpdatesRoot));
        return;
    }

    const QStorageInfo storage(platformUpdatesRoot);
    const auto requiredSpace = release.installerSize
            > (std::numeric_limits<qint64>::max() - kMinimumUpdateSpaceReserve) / 3
        ? std::numeric_limits<qint64>::max()
        : release.installerSize * 3 + kMinimumUpdateSpaceReserve;
    if (storage.isValid() && storage.isReady() && storage.bytesAvailable() < requiredSpace) {
        setBusy(false);
        setStatusMessage(QStringLiteral("更新所需磁盘空间不足：至少需要 %1 GiB 可用空间。")
                             .arg(static_cast<double>(requiredSpace)
                                      / (1024.0 * 1024.0 * 1024.0),
                                  0,
                                  'f',
                                  1));
        return;
    }

    m_manualCheck = manual;
    m_downloadVersionTag = release.versionTag;
    m_downloadSourceUrl = release.installerUrl;
    m_downloadTargetPath = QDir(platformUpdatesRoot).filePath(release.installerName);
    m_downloadPartPath = m_downloadTargetPath + QStringLiteral(".part");
    m_downloadExpectedSize = release.installerSize;
    m_downloadExpectedSha256 = release.installerSha256;
    QString proxyErrorMessage;
    const auto proxyUrl = configuredProxyUrl(&proxyErrorMessage);
    if (!proxyErrorMessage.isEmpty()) {
        setBusy(false);
        setStatusMessage(proxyErrorMessage);
        return;
    }

    const auto proxyLabel = proxyStatusLabel(proxyUrl);
    const auto directMode = m_settings && m_settings->updateDownloadMode() == kUpdateDownloadModeDirect;
    setStatusMessage(proxyUrl.isEmpty()
        ? (directMode
            ? QStringLiteral("发现新版本 %1，正在直连下载更新包...").arg(release.versionTag)
            : QStringLiteral("发现新版本 %1，未检测到可用代理，正在直连下载更新包...").arg(release.versionTag))
        : QStringLiteral("发现新版本 %1，正在通过%2下载更新包...").arg(release.versionTag, proxyLabel));
    const auto networkMode = m_settings ? m_settings->updateDownloadMode() : kUpdateDownloadModeAuto;
    launchDownloadProcess(proxyUrl,
                          !proxyUrl.isEmpty() && directFallbackAllowed(networkMode));
}

void UpdateService::launchDownloadProcess(const QString &proxyUrl, bool allowDirectFallback)
{
    if (m_downloadProcess) {
        m_downloadProcess->kill();
        m_downloadProcess->waitForFinished(2000);
        m_downloadProcess->deleteLater();
        m_downloadProcess = nullptr;
    }

    m_downloadProxyUrl = proxyUrl.trimmed();
    m_downloadAllowDirectFallback = allowDirectFallback && !m_downloadProxyUrl.isEmpty();
    QStringList arguments = curlNetworkArguments(m_downloadProxyUrl);
    arguments << QStringLiteral("--fail")
              << QStringLiteral("--location")
              << QStringLiteral("--silent")
              << QStringLiteral("--show-error")
              << QStringLiteral("--retry")
              << QStringLiteral("4")
              << QStringLiteral("--retry-all-errors")
              << QStringLiteral("--retry-delay")
              << QStringLiteral("3")
              << QStringLiteral("--connect-timeout")
              << QStringLiteral("30")
              << QStringLiteral("--speed-time")
              << QStringLiteral("120")
              << QStringLiteral("--speed-limit")
              << QStringLiteral("1024")
              << QStringLiteral("--proto")
              << QStringLiteral("=https")
              << QStringLiteral("--proto-redir")
              << QStringLiteral("=https")
              << QStringLiteral("-H")
              << QStringLiteral("User-Agent: CineVault")
              << QStringLiteral("--continue-at")
              << QStringLiteral("-")
              << QStringLiteral("--output")
              << QDir::toNativeSeparators(m_downloadPartPath)
              << m_downloadSourceUrl;

    m_downloadProcess = new QProcess(this);
    m_downloadProcess->setProgram(QStringLiteral("curl.exe"));
    m_downloadProcess->setArguments(arguments);
    m_downloadProcess->setWorkingDirectory(QFileInfo(m_downloadTargetPath).absolutePath());
    connect(m_downloadProcess,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            &UpdateService::finishDownloadProcess);
    m_downloadProcess->start();
    if (!m_downloadProcess->waitForStarted(3000)) {
        m_downloadProcess->deleteLater();
        m_downloadProcess = nullptr;
        m_downloadProxyUrl.clear();
        m_downloadAllowDirectFallback = false;
        setBusy(false);
        setStatusMessage(QStringLiteral("无法启动更新包下载进程。"));
    }
}

bool UpdateService::retryDownloadWithoutProxy()
{
    if (!m_downloadAllowDirectFallback || m_downloadProxyUrl.isEmpty()) {
        return false;
    }

    setStatusMessage(QStringLiteral("代理下载失败，正在尝试直连 GitHub..."));
    launchDownloadProcess(QString(), false);
    return true;
}

void UpdateService::finishCheckProcess(int exitCode, QProcess::ExitStatus exitStatus)
{
    auto *checkProcess = m_checkProcess;
    m_checkProcess = nullptr;
    const auto resetCheckState = [this]() {
        m_checkProxyUrl.clear();
        m_checkAllowDirectFallback = false;
    };

    if (!checkProcess) {
        resetCheckState();
        setBusy(false);
        return;
    }

    const auto standardOutput = QString::fromLocal8Bit(checkProcess->readAllStandardOutput()).trimmed();
    const auto standardError = QString::fromLocal8Bit(checkProcess->readAllStandardError()).trimmed();
    checkProcess->deleteLater();

    if (exitStatus != QProcess::NormalExit) {
        if (retryCheckWithoutProxy()) {
            return;
        }
        resetCheckState();
        setBusy(false);
        setStatusMessage(latestReleaseStatusMessage(0, standardError.isEmpty() ? standardOutput : standardError));
        return;
    }

    if (exitCode != 0) {
        if (retryCheckWithoutProxy()) {
            return;
        }
        resetCheckState();
        setBusy(false);
        setStatusMessage(latestReleaseStatusMessage(0, standardError.isEmpty() ? standardOutput : standardError));
        return;
    }

    auto normalizedOutput = standardOutput;
    normalizedOutput.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    const auto separatorIndex = normalizedOutput.lastIndexOf(QLatin1Char('\n'));
    if (separatorIndex <= 0) {
        if (retryCheckWithoutProxy()) {
            return;
        }
        resetCheckState();
        setBusy(false);
        setStatusMessage(QStringLiteral("检查更新失败：版本检查结果无法解析。"));
        return;
    }

    const auto payload = normalizedOutput.left(separatorIndex).toUtf8();
    const auto statusCode = normalizedOutput.mid(separatorIndex + 1).trimmed().toInt();

    if (statusCode != 200) {
        if (statusCode != 404 && retryCheckWithoutProxy()) {
            return;
        }
        resetCheckState();
        setBusy(false);
        setStatusMessage(latestReleaseStatusMessage(statusCode, standardError));
        return;
    }

    UpdateReleaseInfo release;
    QString errorMessage;
    if (!parseLatestRelease(payload, &release, &errorMessage)) {
        resetCheckState();
        setBusy(false);
        setStatusMessage(errorMessage);
        return;
    }

    if (compareVersionTags(release.versionTag, currentVersionTag()) <= 0) {
        resetCheckState();
        setBusy(false);
        setStatusMessage(QStringLiteral("当前已是最新版本：%1").arg(currentVersionTag()));
        return;
    }

    if (useExistingInstaller(release, m_manualCheck)) {
        resetCheckState();
        setBusy(false);
        return;
    }

    resetCheckState();
    startInstallerDownload(release, m_manualCheck);
}

void UpdateService::finishDownloadProcess(int exitCode, QProcess::ExitStatus exitStatus)
{
    auto *downloadProcess = m_downloadProcess;
    m_downloadProcess = nullptr;
    const auto resetDownloadState = [this]() {
        m_downloadVersionTag.clear();
        m_downloadSourceUrl.clear();
        m_downloadTargetPath.clear();
        m_downloadPartPath.clear();
        m_downloadProxyUrl.clear();
        m_downloadAllowDirectFallback = false;
        m_downloadExpectedSize = 0;
        m_downloadExpectedSha256.clear();
    };

    if (!downloadProcess) {
        resetDownloadState();
        setBusy(false);
        return;
    }

    const auto standardError = QString::fromLocal8Bit(downloadProcess->readAllStandardError()).trimmed();
    const auto standardOutput = QString::fromLocal8Bit(downloadProcess->readAllStandardOutput()).trimmed();
    downloadProcess->deleteLater();

    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        if (retryDownloadWithoutProxy()) {
            return;
        }
        const auto errorOutput = standardError.isEmpty()
            ? (standardOutput.isEmpty() ? QStringLiteral("未知错误") : standardOutput)
            : standardError;
        resetDownloadState();
        setBusy(false);
        setStatusMessage(QStringLiteral("下载更新包失败：%1").arg(errorOutput));
        return;
    }

    QFileInfo partInfo(m_downloadPartPath);
    if (!partInfo.exists() || partInfo.size() <= 0) {
        QFile::remove(m_downloadPartPath);
        if (retryDownloadWithoutProxy()) {
            return;
        }
        resetDownloadState();
        setBusy(false);
        setStatusMessage(QStringLiteral("下载更新包失败：未生成完整安装包。"));
        return;
    }

    if (m_downloadExpectedSize > 0 && partInfo.size() != m_downloadExpectedSize) {
        QFile::remove(m_downloadPartPath);
        if (retryDownloadWithoutProxy()) {
            return;
        }
        resetDownloadState();
        setBusy(false);
        setStatusMessage(QStringLiteral("下载更新包失败：安装包大小与发布资产不一致。"));
        return;
    }

    QString hashError;
    const auto actualSha256 = fileSha256(m_downloadPartPath, &hashError);
    if (actualSha256.isEmpty() || actualSha256 != m_downloadExpectedSha256) {
        QFile::remove(m_downloadPartPath);
        if (retryDownloadWithoutProxy()) {
            return;
        }
        resetDownloadState();
        setBusy(false);
        setStatusMessage(hashError.isEmpty()
            ? QStringLiteral("下载更新包失败：SHA-256 校验不一致。")
            : hashError);
        return;
    }

    if (QFile::exists(m_downloadTargetPath) && !QFile::remove(m_downloadTargetPath)) {
        QFile::remove(m_downloadPartPath);
        const auto targetPath = m_downloadTargetPath;
        resetDownloadState();
        setBusy(false);
        setStatusMessage(QStringLiteral("无法覆盖旧更新包：%1").arg(targetPath));
        return;
    }

    if (!QFile::rename(m_downloadPartPath, m_downloadTargetPath)) {
        QFile::remove(m_downloadPartPath);
        const auto targetPath = m_downloadTargetPath;
        resetDownloadState();
        setBusy(false);
        setStatusMessage(QStringLiteral("无法保存更新包：%1").arg(targetPath));
        return;
    }

    const auto versionTag = m_downloadVersionTag;
    const auto targetPath = m_downloadTargetPath;
    const auto installerSize = m_downloadExpectedSize;
    const auto installerSha256 = m_downloadExpectedSha256;
    if (m_settings) {
        m_settings->setDownloadedUpdateVersion(versionTag);
        m_settings->setPendingUpdateVersion(versionTag);
        m_settings->setPendingUpdateInstallerPath(targetPath);
        m_settings->setPendingUpdateInstallerSize(installerSize);
        m_settings->setPendingUpdateInstallerSha256(installerSha256);
        m_settings->setScheduledUpdateVersion(QString());
        m_settings->sync();
    }

    resetDownloadState();
    setBusy(false);
    setStatusMessage(QStringLiteral("更新包已下载完成：%1").arg(versionTag));
    emit updateReady(versionTag, targetPath, m_manualCheck);
}
