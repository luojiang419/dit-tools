#include "infrastructure/metadata/ExifToolAdapter.h"

#include "shared/FolderPathMetadata.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcess>
#include <QRegularExpression>
#include <QHash>
#include <QSet>
#include <QStandardPaths>

namespace {
struct ProcessResult {
    bool started = false;
    bool finished = false;
    int exitCode = -1;
    QByteArray standardOutput;
    QByteArray standardError;
    QString errorMessage;
};

QString existingExecutable(const QStringList &candidates)
{
    for (const auto &candidate : candidates) {
        if (candidate.trimmed().isEmpty()) {
            continue;
        }
        const QFileInfo info(candidate);
        if (info.exists() && info.isFile()) {
            return info.absoluteFilePath();
        }
    }
    return {};
}

ProcessResult runProcess(const QString &program,
                         const QStringList &arguments,
                         int timeoutMs)
{
    ProcessResult result;
    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();
    result.started = process.waitForStarted(5000);
    if (!result.started) {
        result.errorMessage = QStringLiteral("无法启动 ExifTool：%1").arg(process.errorString());
        return result;
    }
    result.finished = process.waitForFinished(timeoutMs);
    if (!result.finished) {
        process.kill();
        process.waitForFinished(3000);
        result.errorMessage = QStringLiteral("ExifTool 执行超时");
    }
    result.exitCode = process.exitCode();
    result.standardOutput = process.readAllStandardOutput();
    result.standardError = process.readAllStandardError();
    if (result.errorMessage.isEmpty()
        && (process.exitStatus() != QProcess::NormalExit || result.exitCode != 0)) {
        result.errorMessage = QString::fromUtf8(result.standardError).trimmed();
        if (result.errorMessage.isEmpty()) {
            result.errorMessage = QStringLiteral("ExifTool 退出码：%1").arg(result.exitCode);
        }
    }
    return result;
}

bool tagMatches(const QString &jsonKey, const QString &tagName)
{
    return jsonKey.compare(tagName, Qt::CaseInsensitive) == 0
        || jsonKey.endsWith(QStringLiteral(":") + tagName, Qt::CaseInsensitive);
}

QJsonValue tagValue(const QJsonObject &object, const QStringList &tagNames)
{
    for (const auto &tagName : tagNames) {
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            if (tagMatches(it.key(), tagName) && !it.value().isNull()) {
                return it.value();
            }
        }
    }
    return {};
}

QString jsonText(const QJsonValue &value)
{
    if (value.isString()) {
        return value.toString().trimmed();
    }
    if (value.isDouble()) {
        return QString::number(value.toDouble(), 'g', 15);
    }
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (value.isArray()) {
        QStringList values;
        for (const auto &item : value.toArray()) {
            const auto text = jsonText(item);
            if (!text.isEmpty()) values.append(text);
        }
        return values.join(QStringLiteral(", "));
    }
    if (value.isObject()) {
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    }
    return {};
}

double jsonDouble(const QJsonValue &value)
{
    if (value.isDouble()) {
        return value.toDouble();
    }
    bool ok = false;
    const auto number = value.toString().trimmed().toDouble(&ok);
    return ok ? number : 0;
}

int jsonInt(const QJsonValue &value)
{
    return static_cast<int>(jsonDouble(value));
}

QString normalizedTimestamp(QString value)
{
    value = value.trimmed();
    if (value.size() >= 19
        && value.at(4) == QLatin1Char(':')
        && value.at(7) == QLatin1Char(':')) {
        value[4] = QLatin1Char('-');
        value[7] = QLatin1Char('-');
        value[10] = QLatin1Char('T');
    }
    return value;
}

QString serialHash(const QStringList &serialValues)
{
    if (serialValues.isEmpty()) {
        return {};
    }
    const auto payload = QStringLiteral("CineVault|embedded-serial|%1")
                             .arg(serialValues.join(QLatin1Char('|')))
                             .toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex().left(24));
}

QString redactSerialNumbers(QJsonObject *object)
{
    QStringList serialValues;
    for (auto it = object->begin(); it != object->end(); ++it) {
        auto normalizedKey = it.key().toCaseFolded();
        normalizedKey.remove(QRegularExpression(QStringLiteral("[^a-z0-9#]")));
        const bool serialTag = normalizedKey.contains(QStringLiteral("serialnumber"))
            || normalizedKey.endsWith(QStringLiteral("serial#"))
            || normalizedKey.endsWith(QStringLiteral("cameraserial"))
            || normalizedKey.endsWith(QStringLiteral("deviceserial"));
        if (!serialTag) {
            continue;
        }
        const auto value = jsonText(it.value());
        if (!value.isEmpty()) {
            serialValues.append(value);
        }
    }
    serialValues.removeDuplicates();
    const auto hash = serialHash(serialValues);
    if (hash.isEmpty()) {
        return {};
    }
    for (auto it = object->begin(); it != object->end(); ++it) {
        auto normalizedKey = it.key().toCaseFolded();
        normalizedKey.remove(QRegularExpression(QStringLiteral("[^a-z0-9#]")));
        if (normalizedKey.contains(QStringLiteral("serialnumber"))
            || normalizedKey.endsWith(QStringLiteral("serial#"))
            || normalizedKey.endsWith(QStringLiteral("cameraserial"))
            || normalizedKey.endsWith(QStringLiteral("deviceserial"))) {
            it.value() = QStringLiteral("[已脱敏:%1]").arg(hash);
        }
    }
    return hash;
}

QString searchableMetadataText(const QJsonObject &object)
{
    QStringList parts;
    qsizetype totalLength = 0;
    constexpr qsizetype kMaxSearchTextLength = 128 * 1024;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (tagMatches(it.key(), QStringLiteral("SourceFile"))
            || tagMatches(it.key(), QStringLiteral("Directory"))) {
            continue;
        }
        const auto value = jsonText(it.value());
        if (value.isEmpty()) {
            continue;
        }
        const auto part = QStringLiteral("%1 %2").arg(it.key(), value);
        if (totalLength + part.size() > kMaxSearchTextLength) {
            break;
        }
        parts.append(part);
        totalLength += part.size() + 1;
    }
    return parts.join(QLatin1Char(' '));
}

EmbeddedMetadataResult resultFromObject(const AssetFile &asset,
                                        QJsonObject object,
                                        const QString &fallbackVersion)
{
    EmbeddedMetadataResult result;
    result.assetId = asset.id;
    result.fingerprintSize = asset.sizeBytes;
    result.fingerprintModified = asset.modifiedAt;
    result.toolVersion = jsonText(tagValue(object, {QStringLiteral("ExifToolVersion")}));
    if (result.toolVersion.isEmpty()) result.toolVersion = fallbackVersion;

    const auto exifError = jsonText(tagValue(object, {QStringLiteral("Error")}));
    result.status = exifError.isEmpty() ? ProbeStatus::Success : ProbeStatus::Failed;
    result.errorMessage = exifError;
    result.captureTime = normalizedTimestamp(jsonText(tagValue(object, {
        QStringLiteral("DateTimeOriginal"), QStringLiteral("MediaCreateDate"),
        QStringLiteral("TrackCreateDate"), QStringLiteral("CreateDate")})));
    result.createTime = normalizedTimestamp(jsonText(tagValue(object, {
        QStringLiteral("CreateDate"), QStringLiteral("FileCreateDate")})));
    result.cameraMake = jsonText(tagValue(object, {QStringLiteral("Make"), QStringLiteral("CameraManufacturerName")}));
    result.cameraModel = jsonText(tagValue(object, {QStringLiteral("Model"), QStringLiteral("CameraModelName")}));
    result.lensModel = jsonText(tagValue(object, {QStringLiteral("LensModel"), QStringLiteral("LensID")}));
    result.cameraSerialHash = redactSerialNumbers(&object);

    const auto latitude = tagValue(object, {QStringLiteral("GPSLatitude")});
    const auto longitude = tagValue(object, {QStringLiteral("GPSLongitude")});
    const auto altitude = tagValue(object, {QStringLiteral("GPSAltitude")});
    if (!latitude.isUndefined() && !latitude.isNull()) result.gpsLatitude = jsonDouble(latitude);
    if (!longitude.isUndefined() && !longitude.isNull()) result.gpsLongitude = jsonDouble(longitude);
    if (!altitude.isUndefined() && !altitude.isNull()) result.gpsAltitude = jsonDouble(altitude);

    result.orientation = jsonInt(tagValue(object, {QStringLiteral("Orientation"), QStringLiteral("Rotation")}));
    result.width = jsonInt(tagValue(object, {QStringLiteral("ImageWidth"), QStringLiteral("ExifImageWidth"), QStringLiteral("SourceImageWidth")}));
    result.height = jsonInt(tagValue(object, {QStringLiteral("ImageHeight"), QStringLiteral("ExifImageHeight"), QStringLiteral("SourceImageHeight")}));
    result.durationMs = static_cast<qint64>(jsonDouble(tagValue(object, {QStringLiteral("Duration")})) * 1000.0);
    result.frameRate = jsonDouble(tagValue(object, {QStringLiteral("VideoFrameRate"), QStringLiteral("VideoFrameRateNum")}));
    result.videoCodec = jsonText(tagValue(object, {QStringLiteral("VideoCodec"), QStringLiteral("CompressorID"), QStringLiteral("CodecID")}));
    result.colorSpace = jsonText(tagValue(object, {QStringLiteral("ColorSpace"), QStringLiteral("ColorRepresentation"), QStringLiteral("ColorPrimaries")}));
    result.sampleRate = jsonInt(tagValue(object, {QStringLiteral("AudioSampleRate"), QStringLiteral("SampleRate")}));
    result.channels = jsonInt(tagValue(object, {QStringLiteral("AudioChannels"), QStringLiteral("NumChannels")}));
    result.bitRate = static_cast<qint64>(jsonDouble(tagValue(object, {QStringLiteral("AvgBitrate"), QStringLiteral("AudioBitrate"), QStringLiteral("VideoBitrate")})));
    result.timecode = jsonText(tagValue(object, {QStringLiteral("TimeCode"), QStringLiteral("StartTimecode"), QStringLiteral("Timecode")}));
    result.title = jsonText(tagValue(object, {QStringLiteral("Title"), QStringLiteral("Headline")}));
    result.description = jsonText(tagValue(object, {QStringLiteral("Description"), QStringLiteral("ImageDescription"), QStringLiteral("Comment")}));
    result.artist = jsonText(tagValue(object, {QStringLiteral("Artist"), QStringLiteral("Creator"), QStringLiteral("Author")}));
    result.album = jsonText(tagValue(object, {QStringLiteral("Album")}));
    result.genre = jsonText(tagValue(object, {QStringLiteral("Genre")}));
    result.keywords = jsonText(tagValue(object, {QStringLiteral("Keywords"), QStringLiteral("Subject"), QStringLiteral("HierarchicalSubject")}));
    result.searchText = searchableMetadataText(object);
    result.rawJson = QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
    return result;
}
}

ExifToolAdapter::ExifToolAdapter()
{
    const auto appDir = QCoreApplication::applicationDirPath();
    const auto environmentPath = QString::fromLocal8Bit(qgetenv("CINEVAULT_EXIFTOOL_PATH")).trimmed();
    m_executablePath = existingExecutable({
        environmentPath,
        QDir(appDir).filePath(QStringLiteral("exiftool/exiftool.exe")),
        QDir(appDir).filePath(QStringLiteral("exiftool.exe")),
        QStandardPaths::findExecutable(QStringLiteral("exiftool.exe")),
        QStandardPaths::findExecutable(QStringLiteral("exiftool"))
    });
    if (m_executablePath.isEmpty()) {
        m_unavailableReason = QStringLiteral("未找到 ExifTool 运行时");
        return;
    }
    const auto versionResult = runProcess(m_executablePath, {QStringLiteral("-ver")}, 15000);
    if (!versionResult.started || !versionResult.finished || versionResult.exitCode != 0) {
        m_unavailableReason = versionResult.errorMessage.isEmpty()
            ? QStringLiteral("ExifTool 版本检测失败")
            : versionResult.errorMessage;
        m_executablePath.clear();
        return;
    }
    m_version = QString::fromUtf8(versionResult.standardOutput).trimmed();
}

bool ExifToolAdapter::isAvailable() const
{
    return !m_executablePath.isEmpty();
}

QString ExifToolAdapter::unavailableReason() const
{
    return m_unavailableReason;
}

QString ExifToolAdapter::executablePath() const
{
    return m_executablePath;
}

QString ExifToolAdapter::version() const
{
    return m_version;
}

QVector<EmbeddedMetadataResult> ExifToolAdapter::extract(const QVector<AssetFile> &assets) const
{
    return extract(assets, 120000);
}

QVector<EmbeddedMetadataResult> ExifToolAdapter::extract(const QVector<AssetFile> &assets,
                                                         int timeoutMs) const
{
    QVector<EmbeddedMetadataResult> results;
    results.reserve(assets.size());
    if (assets.isEmpty()) {
        return results;
    }
    if (!isAvailable()) {
        for (const auto &asset : assets) {
            EmbeddedMetadataResult result;
            result.assetId = asset.id;
            result.status = ProbeStatus::Unavailable;
            result.fingerprintSize = asset.sizeBytes;
            result.fingerprintModified = asset.modifiedAt;
            result.errorMessage = m_unavailableReason;
            results.append(result);
        }
        return results;
    }

    QStringList arguments{
        QStringLiteral("-json"),
        QStringLiteral("-G1"),
        QStringLiteral("-n"),
        QStringLiteral("-struct"),
        QStringLiteral("-charset"), QStringLiteral("filename=UTF8"),
        QStringLiteral("-api"), QStringLiteral("QuickTimeUTC=1"),
        QStringLiteral("-api"), QStringLiteral("LargeFileSupport=1")
    };
    QHash<QString, AssetFile> assetsByPath;
    for (const auto &asset : assets) {
        arguments.append(asset.absolutePath);
        assetsByPath.insert(FolderPathMetadata::normalizedPathKey(asset.absolutePath), asset);
    }

    const auto process = runProcess(m_executablePath, arguments, qMax(1000, timeoutMs));
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(process.standardOutput, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        const auto message = !process.errorMessage.isEmpty()
            ? process.errorMessage
            : QStringLiteral("ExifTool JSON 解析失败：%1").arg(parseError.errorString());
        for (const auto &asset : assets) {
            EmbeddedMetadataResult result;
            result.assetId = asset.id;
            result.status = ProbeStatus::Failed;
            result.fingerprintSize = asset.sizeBytes;
            result.fingerprintModified = asset.modifiedAt;
            result.toolVersion = m_version;
            result.errorMessage = message;
            results.append(result);
        }
        return results;
    }

    QSet<qint64> returnedAssetIds;
    for (const auto &value : document.array()) {
        const auto object = value.toObject();
        const auto sourcePath = jsonText(tagValue(object, {QStringLiteral("SourceFile")}));
        const auto asset = assetsByPath.value(FolderPathMetadata::normalizedPathKey(sourcePath));
        if (asset.id <= 0) {
            continue;
        }
        results.append(resultFromObject(asset, object, m_version));
        returnedAssetIds.insert(asset.id);
    }
    for (const auto &asset : assets) {
        if (returnedAssetIds.contains(asset.id)) {
            continue;
        }
        EmbeddedMetadataResult result;
        result.assetId = asset.id;
        result.status = ProbeStatus::Failed;
        result.fingerprintSize = asset.sizeBytes;
        result.fingerprintModified = asset.modifiedAt;
        result.toolVersion = m_version;
        result.errorMessage = process.errorMessage.isEmpty()
            ? QStringLiteral("ExifTool 未返回该文件的元数据")
            : process.errorMessage;
        results.append(result);
    }
    return results;
}
