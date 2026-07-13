#include "application/LibraryQueryService.h"

#include "core/search/SearchEngine.h"
#include "infrastructure/db/DatabaseManager.h"
#include "infrastructure/logging/Logger.h"
#include "shared/Formatters.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>

namespace {
QVariantList makeDetails(const QList<QPair<QString, QString>> &items)
{
    QVariantList details;
    for (const auto &item : items) {
        QVariantMap row;
        row.insert(QStringLiteral("label"), item.first);
        row.insert(QStringLiteral("value"), item.second);
        details.append(row);
    }
    return details;
}

bool execOrLog(QSqlQuery &query, const QString &context)
{
    if (query.exec()) {
        return true;
    }
    Logger::warn(QStringLiteral("%1 查询失败：%2").arg(context, query.lastError().text()));
    return false;
}

void appendDetail(QList<QPair<QString, QString>> &details, const QString &label, const QString &value)
{
    if (!value.trimmed().isEmpty()) {
        details.append({label, value});
    }
}

QString jsonValueToText(const QJsonValue &value)
{
    if (value.isString()) {
        return value.toString();
    }
    if (value.isDouble()) {
        return QString::number(value.toDouble(), 'g', 12);
    }
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (value.isNull() || value.isUndefined()) {
        return QStringLiteral("空");
    }
    if (value.isObject()) {
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    }
    if (value.isArray()) {
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    }
    return {};
}

QString jsonString(const QJsonObject &object, const QString &key)
{
    return jsonValueToText(object.value(key));
}

qint64 jsonInteger(const QJsonObject &object, const QString &key)
{
    bool ok = false;
    const auto value = jsonString(object, key).toDouble(&ok);
    return ok ? static_cast<qint64>(value) : 0;
}

QString formattedSeconds(const QJsonObject &object, const QString &key)
{
    bool ok = false;
    const auto seconds = jsonString(object, key).toDouble(&ok);
    if (!ok || seconds <= 0) {
        return {};
    }
    return Formatters::formatDuration(static_cast<qint64>(seconds * 1000.0));
}

QString formattedBytes(const QJsonObject &object, const QString &key)
{
    const auto value = jsonInteger(object, key);
    return value > 0 ? Formatters::formatBytes(value) : QString();
}

QString formattedBitRate(const QJsonObject &object, const QString &key)
{
    const auto value = jsonInteger(object, key);
    return value > 0 ? Formatters::formatBitRate(value) : QString();
}

QString formattedFrameRate(const QString &value)
{
    if (value.isEmpty() || value == QStringLiteral("0/0")) {
        return {};
    }

    const auto parts = value.split(QLatin1Char('/'));
    if (parts.size() == 2) {
        bool numeratorOk = false;
        bool denominatorOk = false;
        const auto numerator = parts.at(0).toDouble(&numeratorOk);
        const auto denominator = parts.at(1).toDouble(&denominatorOk);
        if (numeratorOk && denominatorOk && denominator != 0.0) {
            return QStringLiteral("%1 fps (%2)").arg(QString::number(numerator / denominator, 'f', 3), value);
        }
    }
    return value;
}

QString tagLabel(const QString &key)
{
    if (key == QStringLiteral("creation_time")) return QStringLiteral("创建时间");
    if (key == QStringLiteral("timecode")) return QStringLiteral("时间码");
    if (key == QStringLiteral("reel_name")) return QStringLiteral("卷号");
    if (key == QStringLiteral("encoder")) return QStringLiteral("编码器");
    if (key == QStringLiteral("handler_name")) return QStringLiteral("轨道名称");
    if (key == QStringLiteral("language")) return QStringLiteral("语言");
    if (key == QStringLiteral("vendor_id")) return QStringLiteral("厂商ID");
    if (key == QStringLiteral("rotate")) return QStringLiteral("旋转");
    if (key == QStringLiteral("make") || key == QStringLiteral("com.apple.quicktime.make")) return QStringLiteral("相机厂商");
    if (key == QStringLiteral("model") || key == QStringLiteral("com.apple.quicktime.model")) return QStringLiteral("相机型号");
    if (key == QStringLiteral("software") || key == QStringLiteral("com.apple.quicktime.software")) return QStringLiteral("软件");
    if (key == QStringLiteral("com.apple.quicktime.creationdate")) return QStringLiteral("拍摄时间");
    if (key == QStringLiteral("com.apple.quicktime.location.ISO6709")) return QStringLiteral("GPS位置");
    return key;
}

void appendTagDetails(QList<QPair<QString, QString>> &details, const QString &section, const QJsonObject &object)
{
    const auto tags = object.value(QStringLiteral("tags")).toObject();
    auto keys = tags.keys();
    keys.sort(Qt::CaseInsensitive);
    for (const auto &key : keys) {
        appendDetail(details, QStringLiteral("%1 / %2").arg(section, tagLabel(key)), jsonValueToText(tags.value(key)));
    }
}

QString dispositionLabel(const QString &key)
{
    if (key == QStringLiteral("default")) return QStringLiteral("默认轨道");
    if (key == QStringLiteral("forced")) return QStringLiteral("强制轨道");
    if (key == QStringLiteral("dub")) return QStringLiteral("配音");
    if (key == QStringLiteral("original")) return QStringLiteral("原始轨道");
    if (key == QStringLiteral("comment")) return QStringLiteral("评论轨道");
    if (key == QStringLiteral("lyrics")) return QStringLiteral("歌词");
    if (key == QStringLiteral("karaoke")) return QStringLiteral("卡拉OK");
    if (key == QStringLiteral("hearing_impaired")) return QStringLiteral("听障辅助");
    if (key == QStringLiteral("visual_impaired")) return QStringLiteral("视障辅助");
    if (key == QStringLiteral("clean_effects")) return QStringLiteral("无对白效果声");
    if (key == QStringLiteral("attached_pic")) return QStringLiteral("内嵌封面");
    if (key == QStringLiteral("captions")) return QStringLiteral("字幕说明");
    if (key == QStringLiteral("descriptions")) return QStringLiteral("描述音轨");
    if (key == QStringLiteral("metadata")) return QStringLiteral("元数据轨道");
    return key;
}

void appendDispositionDetails(QList<QPair<QString, QString>> &details, const QString &section, const QJsonObject &object)
{
    const auto disposition = object.value(QStringLiteral("disposition")).toObject();
    QStringList enabled;
    auto keys = disposition.keys();
    keys.sort(Qt::CaseInsensitive);
    for (const auto &key : keys) {
        if (jsonInteger(disposition, key) != 0) {
            enabled.append(dispositionLabel(key));
        }
    }
    appendDetail(details, QStringLiteral("%1 / 轨道属性").arg(section), enabled.join(QStringLiteral("、")));
}

void appendSideDataDetails(QList<QPair<QString, QString>> &details, const QString &section, const QJsonObject &object)
{
    const auto sideDataList = object.value(QStringLiteral("side_data_list")).toArray();
    for (int i = 0; i < sideDataList.size(); ++i) {
        const auto sideData = sideDataList.at(i).toObject();
        QStringList lines;
        auto keys = sideData.keys();
        keys.sort(Qt::CaseInsensitive);
        for (const auto &key : keys) {
            lines.append(QStringLiteral("%1：%2").arg(key, jsonValueToText(sideData.value(key))));
        }
        const auto type = sideData.value(QStringLiteral("side_data_type")).toString(QStringLiteral("附加数据"));
        appendDetail(details, QStringLiteral("%1 / %2").arg(section, type), lines.join(QStringLiteral("\n")));
    }
}

QString streamSectionName(const QJsonObject &stream, int videoIndex, int audioIndex, int subtitleIndex, int dataIndex)
{
    const auto kind = stream.value(QStringLiteral("codec_type")).toString();
    if (kind == QStringLiteral("video")) return QStringLiteral("视频轨 %1").arg(videoIndex);
    if (kind == QStringLiteral("audio")) return QStringLiteral("音频轨 %1").arg(audioIndex);
    if (kind == QStringLiteral("subtitle")) return QStringLiteral("字幕轨 %1").arg(subtitleIndex);
    if (kind == QStringLiteral("data")) return QStringLiteral("数据轨 %1").arg(dataIndex);
    if (kind == QStringLiteral("attachment")) return QStringLiteral("附件轨");
    return QStringLiteral("媒体轨");
}

void appendFfprobeJsonDetails(QList<QPair<QString, QString>> &details, const QString &rawJson)
{
    if (rawJson.trimmed().isEmpty()) {
        return;
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(rawJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        appendDetail(details, QStringLiteral("高级 / 原始元数据状态"), QStringLiteral("原始 ffprobe JSON 已保存，但当前解析失败：%1").arg(parseError.errorString()));
        return;
    }

    const auto root = document.object();
    const auto format = root.value(QStringLiteral("format")).toObject();
    appendDetail(details, QStringLiteral("封装信息 / 格式"), jsonString(format, QStringLiteral("format_name")));
    appendDetail(details, QStringLiteral("封装信息 / 格式全称"), jsonString(format, QStringLiteral("format_long_name")));
    appendDetail(details, QStringLiteral("封装信息 / 总时长"), formattedSeconds(format, QStringLiteral("duration")));
    appendDetail(details, QStringLiteral("封装信息 / 总码率"), formattedBitRate(format, QStringLiteral("bit_rate")));
    appendDetail(details, QStringLiteral("封装信息 / 文件大小"), formattedBytes(format, QStringLiteral("size")));
    appendDetail(details, QStringLiteral("封装信息 / 起始时间"), jsonString(format, QStringLiteral("start_time")));
    appendDetail(details, QStringLiteral("封装信息 / 流数量"), jsonString(format, QStringLiteral("nb_streams")));
    appendDetail(details, QStringLiteral("封装信息 / 节目数量"), jsonString(format, QStringLiteral("nb_programs")));
    appendDetail(details, QStringLiteral("封装信息 / 探测置信度"), jsonString(format, QStringLiteral("probe_score")));
    appendTagDetails(details, QStringLiteral("封装标签"), format);

    const auto streams = root.value(QStringLiteral("streams")).toArray();
    int videoIndex = 0;
    int audioIndex = 0;
    int subtitleIndex = 0;
    int dataIndex = 0;
    for (int i = 0; i < streams.size(); ++i) {
        const auto stream = streams.at(i).toObject();
        const auto kind = stream.value(QStringLiteral("codec_type")).toString();
        if (kind == QStringLiteral("video")) ++videoIndex;
        else if (kind == QStringLiteral("audio")) ++audioIndex;
        else if (kind == QStringLiteral("subtitle")) ++subtitleIndex;
        else if (kind == QStringLiteral("data")) ++dataIndex;

        const auto section = streamSectionName(stream, videoIndex, audioIndex, subtitleIndex, dataIndex);
        appendDetail(details, QStringLiteral("%1 / 轨道索引").arg(section), jsonString(stream, QStringLiteral("index")));
        appendDetail(details, QStringLiteral("%1 / 编码格式").arg(section), jsonString(stream, QStringLiteral("codec_name")));
        appendDetail(details, QStringLiteral("%1 / 编码全称").arg(section), jsonString(stream, QStringLiteral("codec_long_name")));
        appendDetail(details, QStringLiteral("%1 / 编码配置").arg(section), jsonString(stream, QStringLiteral("profile")));
        appendDetail(details, QStringLiteral("%1 / 编码标签").arg(section), jsonString(stream, QStringLiteral("codec_tag_string")));
        appendDetail(details, QStringLiteral("%1 / 时基").arg(section), jsonString(stream, QStringLiteral("time_base")));
        appendDetail(details, QStringLiteral("%1 / 起始时间").arg(section), jsonString(stream, QStringLiteral("start_time")));
        appendDetail(details, QStringLiteral("%1 / 时长").arg(section), formattedSeconds(stream, QStringLiteral("duration")));
        appendDetail(details, QStringLiteral("%1 / 码率").arg(section), formattedBitRate(stream, QStringLiteral("bit_rate")));
        appendDetail(details, QStringLiteral("%1 / 最大码率").arg(section), formattedBitRate(stream, QStringLiteral("max_bit_rate")));
        appendDetail(details, QStringLiteral("%1 / 帧/样本数量").arg(section), jsonString(stream, QStringLiteral("nb_frames")));

        if (kind == QStringLiteral("video")) {
            const auto width = jsonInteger(stream, QStringLiteral("width"));
            const auto height = jsonInteger(stream, QStringLiteral("height"));
            if (width > 0 && height > 0) {
                appendDetail(details, QStringLiteral("%1 / 分辨率").arg(section), QStringLiteral("%1 x %2").arg(width).arg(height));
            }
            const auto codedWidth = jsonInteger(stream, QStringLiteral("coded_width"));
            const auto codedHeight = jsonInteger(stream, QStringLiteral("coded_height"));
            if (codedWidth > 0 && codedHeight > 0 && (codedWidth != width || codedHeight != height)) {
                appendDetail(details, QStringLiteral("%1 / 编码尺寸").arg(section), QStringLiteral("%1 x %2").arg(codedWidth).arg(codedHeight));
            }
            appendDetail(details, QStringLiteral("%1 / 显示宽高比").arg(section), jsonString(stream, QStringLiteral("display_aspect_ratio")));
            appendDetail(details, QStringLiteral("%1 / 像素宽高比").arg(section), jsonString(stream, QStringLiteral("sample_aspect_ratio")));
            appendDetail(details, QStringLiteral("%1 / 帧率").arg(section), formattedFrameRate(jsonString(stream, QStringLiteral("r_frame_rate"))));
            appendDetail(details, QStringLiteral("%1 / 平均帧率").arg(section), formattedFrameRate(jsonString(stream, QStringLiteral("avg_frame_rate"))));
            appendDetail(details, QStringLiteral("%1 / 像素格式").arg(section), jsonString(stream, QStringLiteral("pix_fmt")));
            appendDetail(details, QStringLiteral("%1 / 色彩范围").arg(section), jsonString(stream, QStringLiteral("color_range")));
            appendDetail(details, QStringLiteral("%1 / 色彩空间").arg(section), jsonString(stream, QStringLiteral("color_space")));
            appendDetail(details, QStringLiteral("%1 / 色彩转换").arg(section), jsonString(stream, QStringLiteral("color_transfer")));
            appendDetail(details, QStringLiteral("%1 / 色彩基色").arg(section), jsonString(stream, QStringLiteral("color_primaries")));
            appendDetail(details, QStringLiteral("%1 / 色度位置").arg(section), jsonString(stream, QStringLiteral("chroma_location")));
            appendDetail(details, QStringLiteral("%1 / 场序").arg(section), jsonString(stream, QStringLiteral("field_order")));
            appendDetail(details, QStringLiteral("%1 / 编码级别").arg(section), jsonString(stream, QStringLiteral("level")));
            appendDetail(details, QStringLiteral("%1 / B帧").arg(section), jsonString(stream, QStringLiteral("has_b_frames")));
            appendDetail(details, QStringLiteral("%1 / 参考帧").arg(section), jsonString(stream, QStringLiteral("refs")));
            appendDetail(details, QStringLiteral("%1 / 原始位深").arg(section), jsonString(stream, QStringLiteral("bits_per_raw_sample")));
            appendDetail(details, QStringLiteral("%1 / 隐藏字幕标记").arg(section), jsonString(stream, QStringLiteral("closed_captions")));
            appendDetail(details, QStringLiteral("%1 / 胶片颗粒标记").arg(section), jsonString(stream, QStringLiteral("film_grain")));
        } else if (kind == QStringLiteral("audio")) {
            appendDetail(details, QStringLiteral("%1 / 采样格式").arg(section), jsonString(stream, QStringLiteral("sample_fmt")));
            appendDetail(details, QStringLiteral("%1 / 采样率").arg(section), jsonString(stream, QStringLiteral("sample_rate")));
            appendDetail(details, QStringLiteral("%1 / 声道数").arg(section), jsonString(stream, QStringLiteral("channels")));
            appendDetail(details, QStringLiteral("%1 / 声道布局").arg(section), jsonString(stream, QStringLiteral("channel_layout")));
            appendDetail(details, QStringLiteral("%1 / 每样本位深").arg(section), jsonString(stream, QStringLiteral("bits_per_sample")));
            appendDetail(details, QStringLiteral("%1 / 初始填充").arg(section), jsonString(stream, QStringLiteral("initial_padding")));
        }

        appendDetail(details, QStringLiteral("%1 / 附加数据大小").arg(section), jsonString(stream, QStringLiteral("extradata_size")));
        appendTagDetails(details, QStringLiteral("%1 标签").arg(section), stream);
        appendDispositionDetails(details, section, stream);
        appendSideDataDetails(details, section, stream);
    }

    const auto chapters = root.value(QStringLiteral("chapters")).toArray();
    for (int i = 0; i < chapters.size(); ++i) {
        const auto chapter = chapters.at(i).toObject();
        const auto section = QStringLiteral("章节 %1").arg(i + 1);
        appendDetail(details, QStringLiteral("%1 / 起始时间").arg(section), jsonString(chapter, QStringLiteral("start_time")));
        appendDetail(details, QStringLiteral("%1 / 结束时间").arg(section), jsonString(chapter, QStringLiteral("end_time")));
        appendTagDetails(details, QStringLiteral("%1 标签").arg(section), chapter);
    }

    const auto programs = root.value(QStringLiteral("programs")).toArray();
    for (int i = 0; i < programs.size(); ++i) {
        const auto program = programs.at(i).toObject();
        const auto section = QStringLiteral("节目 %1").arg(i + 1);
        appendDetail(details, QStringLiteral("%1 / 节目ID").arg(section), jsonString(program, QStringLiteral("program_id")));
        appendDetail(details, QStringLiteral("%1 / 节目号").arg(section), jsonString(program, QStringLiteral("program_num")));
        appendDetail(details, QStringLiteral("%1 / 流数量").arg(section), QString::number(program.value(QStringLiteral("streams")).toArray().size()));
        appendTagDetails(details, QStringLiteral("%1 标签").arg(section), program);
    }

    appendDetail(details,
                 QStringLiteral("高级 / 原始元数据状态"),
                 QStringLiteral("完整 ffprobe JSON 已保存在项目数据库，用于后续导出、审计和高级诊断；默认界面仅显示中文整理字段。"));
}

QString makeTechnicalSummary(const AssetFile &asset)
{
    QStringList parts;
    if (!asset.container.isEmpty()) {
        parts.append(asset.container);
    }
    if (asset.durationMs > 0) {
        parts.append(Formatters::formatDuration(asset.durationMs));
    }
    if (asset.bitRate > 0) {
        parts.append(Formatters::formatBitRate(asset.bitRate));
    }
    return parts.join(QStringLiteral(" · "));
}

QString assetTypeCountColumn(AssetType type)
{
    switch (type) {
    case AssetType::Video: return QStringLiteral("video_count");
    case AssetType::Audio: return QStringLiteral("audio_count");
    case AssetType::Image: return QStringLiteral("image_count");
    default: return QStringLiteral("other_count");
    }
}
}

LibraryQueryService::LibraryQueryService(DatabaseManager *databaseManager, SearchEngine *searchEngine, QObject *parent)
    : QObject(parent)
    , m_databaseManager(databaseManager)
    , m_searchEngine(searchEngine)
{
}

QVector<SourceRoot> LibraryQueryService::fetchSourceRoots() const
{
    QVector<SourceRoot> rows;
    if (!m_databaseManager->hasOpenProject()) {
        return rows;
    }

    QSqlQuery query(m_databaseManager->database());
    if (!query.exec(QStringLiteral(
        "SELECT id, name, path, status, total_files, total_folders, total_size_bytes, video_count, audio_count, image_count, other_count, warning_count, COALESCE(scan_version, 0) "
        "FROM source_root ORDER BY created_at DESC"))) {
        Logger::warn(QStringLiteral("读取素材源列表失败：%1").arg(query.lastError().text()));
        return rows;
    }
    while (query.next()) {
        SourceRoot row;
        row.id = query.value(0).toLongLong();
        row.name = query.value(1).toString();
        row.path = query.value(2).toString();
        row.status = query.value(3).toString();
        row.totalFiles = query.value(4).toLongLong();
        row.totalFolders = query.value(5).toLongLong();
        row.totalSizeBytes = query.value(6).toLongLong();
        row.videoCount = query.value(7).toLongLong();
        row.audioCount = query.value(8).toLongLong();
        row.imageCount = query.value(9).toLongLong();
        row.otherCount = query.value(10).toLongLong();
        row.warningCount = query.value(11).toLongLong();
        row.scanVersion = query.value(12).toInt();
        rows.append(row);
    }
    return rows;
}

QVector<AssetFile> LibraryQueryService::fetchAssets(const QString &keyword,
                                                    std::optional<qint64> sourceRootId,
                                                    std::optional<AssetType> assetType,
                                                    bool favoritesOnly,
                                                    bool modifiedTimeAscending) const
{
    QVector<AssetFile> rows;
    if (!m_databaseManager->hasOpenProject()) {
        return rows;
    }

    QString sql = QStringLiteral(
        "SELECT af.id, af.source_root_id, af.name, af.extension, af.absolute_path, af.relative_path, af.parent_path, "
        "af.asset_type, af.size_bytes, af.modified_at, af.is_readable, "
        "af.is_favorite, "
        "CASE WHEN af.asset_type = %1 AND COALESCE(th.image_path, '') = '' THEN af.absolute_path "
        "WHEN COALESCE(th.status, 0) = 1 THEN COALESCE(th.image_path, '') "
        "ELSE '' END, "
        "COALESCE(th.status, 0), COALESCE(mm.container, ''), COALESCE(mm.duration_ms, 0), "
        "COALESCE(mm.bit_rate, 0), COALESCE(mm.probe_status, 0) "
        "FROM asset_file af "
        "LEFT JOIN media_metadata mm ON mm.asset_id = af.id "
        "LEFT JOIN thumbnail th ON th.asset_id = af.id "
        "WHERE 1 = 1").arg(static_cast<int>(AssetType::Image));

    QVariantList binds;
    if (sourceRootId.has_value()) {
        sql += QStringLiteral(" AND af.source_root_id = ?");
        binds.append(sourceRootId.value());
    }
    if (assetType.has_value()) {
        sql += QStringLiteral(" AND af.asset_type = ?");
        binds.append(static_cast<int>(assetType.value()));
    }
    if (favoritesOnly) {
        sql += QStringLiteral(" AND af.is_favorite = 1");
    }
    if (!keyword.trimmed().isEmpty()) {
        sql += QStringLiteral(" AND (af.name LIKE ? ESCAPE '\\' OR af.relative_path LIKE ? ESCAPE '\\')");
        const auto pattern = m_searchEngine->buildLikePattern(keyword);
        binds.append(pattern);
        binds.append(pattern);
    }
    sql += modifiedTimeAscending
        ? QStringLiteral(" ORDER BY af.modified_at ASC, af.id ASC LIMIT 5000")
        : QStringLiteral(" ORDER BY af.modified_at DESC, af.id DESC LIMIT 5000");

    QSqlQuery query(m_databaseManager->database());
    query.prepare(sql);
    for (const auto &bind : binds) {
        query.addBindValue(bind);
    }
    if (!execOrLog(query, QStringLiteral("读取素材列表"))) {
        return rows;
    }

    while (query.next()) {
        AssetFile row;
        row.id = query.value(0).toLongLong();
        row.sourceRootId = query.value(1).toLongLong();
        row.name = query.value(2).toString();
        row.extension = query.value(3).toString();
        row.absolutePath = query.value(4).toString();
        row.relativePath = query.value(5).toString();
        row.parentPath = query.value(6).toString();
        row.assetType = static_cast<AssetType>(query.value(7).toInt());
        row.sizeBytes = query.value(8).toLongLong();
        row.modifiedAt = query.value(9).toString();
        row.readable = query.value(10).toInt() == 1;
        row.favorite = query.value(11).toInt() == 1;
        row.thumbnailPath = query.value(12).toString();
        row.thumbnailStatus = static_cast<ThumbnailStatus>(query.value(13).toInt());
        row.container = query.value(14).toString();
        row.durationMs = query.value(15).toLongLong();
        row.bitRate = query.value(16).toLongLong();
        row.probeStatus = static_cast<ProbeStatus>(query.value(17).toInt());
        row.technicalSummary = makeTechnicalSummary(row);
        rows.append(row);
    }
    return rows;
}

InspectorState LibraryQueryService::buildSourceInspector(qint64 sourceRootId) const
{
    InspectorState state;
    state.title = QStringLiteral("检查器");
    state.subtitle = QStringLiteral("选择素材源查看统计");

    if (!m_databaseManager->hasOpenProject() || sourceRootId <= 0) {
        state.details = makeDetails({{QStringLiteral("状态"), QStringLiteral("未选择素材源")}});
        return state;
    }

    QSqlQuery query(m_databaseManager->database());
    query.prepare(QStringLiteral(
        "SELECT name, path, status, total_files, total_folders, total_size_bytes, video_count, audio_count, image_count, warning_count "
        "FROM source_root WHERE id = ?"));
    query.addBindValue(sourceRootId);
    if (!execOrLog(query, QStringLiteral("读取素材源检查器"))) {
        state.details = makeDetails({{QStringLiteral("状态"), QStringLiteral("读取素材源失败")}});
        return state;
    }
    if (!query.next()) {
        state.details = makeDetails({{QStringLiteral("状态"), QStringLiteral("素材源不存在")}});
        return state;
    }

    state.title = query.value(0).toString();
    state.subtitle = Formatters::statusLabel(query.value(2).toString());
    state.details = makeDetails({
        {QStringLiteral("源路径"), query.value(1).toString()},
        {QStringLiteral("文件数"), QString::number(query.value(3).toLongLong())},
        {QStringLiteral("文件夹数"), QString::number(query.value(4).toLongLong())},
        {QStringLiteral("总容量"), Formatters::formatBytes(query.value(5).toLongLong())},
        {QStringLiteral("视频数"), QString::number(query.value(6).toLongLong())},
        {QStringLiteral("音频数"), QString::number(query.value(7).toLongLong())},
        {QStringLiteral("图片数"), QString::number(query.value(8).toLongLong())},
        {QStringLiteral("警告数"), QString::number(query.value(9).toLongLong())}
    });
    return state;
}

InspectorState LibraryQueryService::buildAssetInspector(qint64 assetId) const
{
    InspectorState state;
    state.title = QStringLiteral("检查器");
    state.subtitle = QStringLiteral("选择素材查看属性");

    if (!m_databaseManager->hasOpenProject() || assetId <= 0) {
        state.details = makeDetails({{QStringLiteral("状态"), QStringLiteral("未选择素材")}});
        return state;
    }

    QSqlQuery query(m_databaseManager->database());
    query.prepare(QStringLiteral(
        "SELECT af.name, af.absolute_path, af.relative_path, af.asset_type, af.size_bytes, af.modified_at, af.is_readable, "
        "af.is_favorite, mm.probe_status, mm.container, mm.duration_ms, mm.bit_rate, mm.error_message, "
        "CASE WHEN af.asset_type = %1 AND COALESCE(th.image_path, '') = '' THEN af.absolute_path "
        "WHEN COALESCE(th.status, 0) = 1 THEN COALESCE(th.image_path, '') "
        "ELSE '' END, "
        "COALESCE(th.status, 0), "
        "mm.raw_json "
        "FROM asset_file af "
        "LEFT JOIN media_metadata mm ON mm.asset_id = af.id "
        "LEFT JOIN thumbnail th ON th.asset_id = af.id "
        "WHERE af.id = ?").arg(static_cast<int>(AssetType::Image)));
    query.addBindValue(assetId);
    if (!execOrLog(query, QStringLiteral("读取素材检查器"))) {
        state.details = makeDetails({{QStringLiteral("状态"), QStringLiteral("读取素材失败")}});
        return state;
    }
    if (!query.next()) {
        state.details = makeDetails({{QStringLiteral("状态"), QStringLiteral("素材不存在")}});
        return state;
    }

    state.title = query.value(0).toString();
    const auto assetType = static_cast<AssetType>(query.value(3).toInt());
    state.subtitle = Formatters::assetTypeLabel(assetType);

    QList<QPair<QString, QString>> details = {
        {QStringLiteral("绝对路径"), query.value(1).toString()},
        {QStringLiteral("相对路径"), query.value(2).toString()},
        {QStringLiteral("类型"), Formatters::assetTypeLabel(assetType)},
        {QStringLiteral("文件大小"), Formatters::formatBytes(query.value(4).toLongLong())},
        {QStringLiteral("修改时间"), query.value(5).toString()},
        {QStringLiteral("可读状态"), query.value(6).toInt() == 1 ? QStringLiteral("正常") : QStringLiteral("不可读")},
        {QStringLiteral("收藏状态"), query.value(7).toInt() == 1 ? QStringLiteral("已收藏") : QStringLiteral("未收藏")}
    };

    if (!query.value(8).isNull()) {
        const auto probeStatus = static_cast<ProbeStatus>(query.value(8).toInt());
        details.append({QStringLiteral("媒体状态"), Formatters::probeStatusLabel(probeStatus)});
        if (!query.value(9).toString().isEmpty()) {
            details.append({QStringLiteral("封装格式"), query.value(9).toString()});
        }
        if (query.value(10).toLongLong() > 0) {
            details.append({QStringLiteral("时长"), Formatters::formatDuration(query.value(10).toLongLong())});
        }
        if (query.value(11).toLongLong() > 0) {
            details.append({QStringLiteral("码率"), Formatters::formatBitRate(query.value(11).toLongLong())});
        }
        if (!query.value(13).toString().isEmpty()) {
            details.append({QStringLiteral("缩略图"), query.value(13).toString()});
        } else if (static_cast<ThumbnailStatus>(query.value(14).toInt()) == ThumbnailStatus::Running) {
            details.append({QStringLiteral("缩略图"), QStringLiteral("缩略图生成中")});
        }
        if (probeStatus != ProbeStatus::Success && !query.value(12).toString().isEmpty()) {
            details.append({QStringLiteral("媒体错误"), query.value(12).toString()});
        }
        appendFfprobeJsonDetails(details, query.value(15).toString());

        QSqlQuery streamQuery(m_databaseManager->database());
        streamQuery.prepare(QStringLiteral(
            "SELECT stream_index, stream_kind, codec, bit_rate, width, height, channels, sample_rate "
            "FROM media_stream WHERE asset_id = ? ORDER BY stream_index"));
        streamQuery.addBindValue(assetId);
        if (!execOrLog(streamQuery, QStringLiteral("读取媒体流信息"))) {
            state.details = makeDetails(details);
            return state;
        }
        while (streamQuery.next()) {
            QStringList streamParts;
            const auto kind = streamQuery.value(1).toString();
            const auto codec = streamQuery.value(2).toString();
            if (!codec.isEmpty()) {
                streamParts.append(codec);
            }
            const auto width = streamQuery.value(4).toInt();
            const auto height = streamQuery.value(5).toInt();
            if (width > 0 && height > 0) {
                streamParts.append(QStringLiteral("%1x%2").arg(width).arg(height));
            }
            const auto channels = streamQuery.value(6).toInt();
            if (channels > 0) {
                streamParts.append(QStringLiteral("%1声道").arg(channels));
            }
            const auto sampleRate = streamQuery.value(7).toInt();
            if (sampleRate > 0) {
                streamParts.append(QStringLiteral("%1 Hz").arg(sampleRate));
            }
            const auto bitRate = streamQuery.value(3).toLongLong();
            if (bitRate > 0) {
                streamParts.append(Formatters::formatBitRate(bitRate));
            }
            details.append({
                QStringLiteral("%1流 %2").arg(kind == QStringLiteral("audio") ? QStringLiteral("音频") : QStringLiteral("视频")).arg(streamQuery.value(0).toInt()),
                streamParts.isEmpty() ? QStringLiteral("无技术参数") : streamParts.join(QStringLiteral(" · "))
            });
        }
    }

    state.details = makeDetails(details);
    return state;
}

qint64 LibraryQueryService::assetCount(const QString &keyword, std::optional<qint64> sourceRootId, std::optional<AssetType> assetType, bool favoritesOnly) const
{
    if (!m_databaseManager->hasOpenProject()) {
        return 0;
    }

    QString sql = QStringLiteral("SELECT COUNT(*) FROM asset_file af WHERE 1 = 1");
    QVariantList binds;
    if (sourceRootId.has_value()) {
        sql += QStringLiteral(" AND af.source_root_id = ?");
        binds.append(sourceRootId.value());
    }
    if (assetType.has_value()) {
        sql += QStringLiteral(" AND af.asset_type = ?");
        binds.append(static_cast<int>(assetType.value()));
    }
    if (favoritesOnly) {
        sql += QStringLiteral(" AND af.is_favorite = 1");
    }
    if (!keyword.trimmed().isEmpty()) {
        sql += QStringLiteral(" AND (af.name LIKE ? ESCAPE '\\' OR af.relative_path LIKE ? ESCAPE '\\')");
        const auto pattern = m_searchEngine->buildLikePattern(keyword);
        binds.append(pattern);
        binds.append(pattern);
    }

    QSqlQuery query(m_databaseManager->database());
    query.prepare(sql);
    for (const auto &bind : binds) {
        query.addBindValue(bind);
    }
    if (!execOrLog(query, QStringLiteral("统计素材数量"))) {
        return 0;
    }
    return query.next() ? query.value(0).toLongLong() : 0;
}

bool LibraryQueryService::setAssetFavorite(qint64 assetId, bool favorite)
{
    if (!m_databaseManager->hasOpenProject() || assetId <= 0) {
        return false;
    }

    QSqlQuery query(m_databaseManager->database());
    query.prepare(QStringLiteral("UPDATE asset_file SET is_favorite = ? WHERE id = ?"));
    query.addBindValue(favorite ? 1 : 0);
    query.addBindValue(assetId);
    if (!execOrLog(query, QStringLiteral("更新素材收藏状态"))) {
        return false;
    }
    if (query.numRowsAffected() <= 0) {
        return false;
    }
    return true;
}

bool LibraryQueryService::removeAsset(qint64 assetId)
{
    if (!m_databaseManager->hasOpenProject() || assetId <= 0) {
        return false;
    }

    auto db = m_databaseManager->database();
    QSqlQuery read(db);
    read.prepare(QStringLiteral("SELECT source_root_id, asset_type, size_bytes, is_readable FROM asset_file WHERE id = ?"));
    read.addBindValue(assetId);
    if (!execOrLog(read, QStringLiteral("读取待移除素材"))) {
        return false;
    }
    if (!read.next()) {
        return false;
    }

    const auto sourceRootId = read.value(0).toLongLong();
    const auto assetType = static_cast<AssetType>(read.value(1).toInt());
    const auto sizeBytes = read.value(2).toLongLong();
    const bool readable = read.value(3).toInt() == 1;

    if (!db.transaction()) {
        Logger::warn(QStringLiteral("开始移除素材事务失败：%1").arg(db.lastError().text()));
        return false;
    }

    QSqlQuery remove(db);
    remove.prepare(QStringLiteral("DELETE FROM asset_file WHERE id = ?"));
    remove.addBindValue(assetId);
    if (!execOrLog(remove, QStringLiteral("移除素材记录"))) {
        db.rollback();
        return false;
    }
    if (remove.numRowsAffected() <= 0) {
        db.rollback();
        return false;
    }

    QStringList assignments = {
        QStringLiteral("total_files = MAX(total_files - 1, 0)"),
        QStringLiteral("total_size_bytes = MAX(total_size_bytes - ?, 0)")
    };
    assignments.append(QStringLiteral("%1 = MAX(%1 - 1, 0)").arg(assetTypeCountColumn(assetType)));
    if (!readable) {
        assignments.append(QStringLiteral("warning_count = MAX(warning_count - 1, 0)"));
    }
    assignments.append(QStringLiteral("updated_at = ?"));

    QSqlQuery updateSource(db);
    updateSource.prepare(QStringLiteral("UPDATE source_root SET %1 WHERE id = ?").arg(assignments.join(QStringLiteral(", "))));
    updateSource.addBindValue(sizeBytes);
    updateSource.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    updateSource.addBindValue(sourceRootId);
    if (!execOrLog(updateSource, QStringLiteral("更新素材源统计"))) {
        db.rollback();
        return false;
    }

    if (!db.commit()) {
        Logger::warn(QStringLiteral("提交移除素材事务失败：%1").arg(db.lastError().text()));
        db.rollback();
        return false;
    }

    emit dataChanged();
    return true;
}

bool LibraryQueryService::removeSourceRoot(qint64 sourceRootId)
{
    if (!m_databaseManager->hasOpenProject() || sourceRootId <= 0) {
        return false;
    }

    auto db = m_databaseManager->database();
    QSqlQuery read(db);
    read.prepare(QStringLiteral("SELECT id FROM source_root WHERE id = ?"));
    read.addBindValue(sourceRootId);
    if (!execOrLog(read, QStringLiteral("读取待移除素材源"))) {
        return false;
    }
    if (!read.next()) {
        return false;
    }

    if (!db.transaction()) {
        Logger::warn(QStringLiteral("开始移除素材源事务失败：%1").arg(db.lastError().text()));
        return false;
    }

    const QStringList statements = {
        QStringLiteral("DELETE FROM media_stream WHERE asset_id IN (SELECT id FROM asset_file WHERE source_root_id = ?)"),
        QStringLiteral("DELETE FROM media_metadata WHERE asset_id IN (SELECT id FROM asset_file WHERE source_root_id = ?)"),
        QStringLiteral("DELETE FROM thumbnail WHERE asset_id IN (SELECT id FROM asset_file WHERE source_root_id = ?)"),
        QStringLiteral("DELETE FROM folder_node WHERE source_root_id = ?"),
        QStringLiteral("DELETE FROM asset_file WHERE source_root_id = ?"),
        QStringLiteral("DELETE FROM source_root WHERE id = ?")
    };

    for (const auto &statement : statements) {
        QSqlQuery remove(db);
        remove.prepare(statement);
        remove.addBindValue(sourceRootId);
        if (!execOrLog(remove, QStringLiteral("移除素材源关联记录"))) {
            db.rollback();
            return false;
        }
    }

    if (!db.commit()) {
        Logger::warn(QStringLiteral("提交移除素材源事务失败：%1").arg(db.lastError().text()));
        db.rollback();
        return false;
    }

    emit dataChanged();
    return true;
}
