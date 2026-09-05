#include "application/MaterialCenterQueryService.h"
#include "core/search/SearchEngine.h"
#include "domain/Enums.h"
#include "infrastructure/db/GlobalDatabaseManager.h"
#include "shared/Paths.h"

#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include <algorithm>

namespace {
bool execSql(QSqlDatabase db, const QString &sql, QString *errorMessage = nullptr)
{
    QSqlQuery query(db);
    if (query.exec(sql)) {
        return true;
    }
    if (errorMessage) {
        *errorMessage = query.lastError().text();
    }
    return false;
}

QString jsonArray(std::initializer_list<QString> values)
{
    QJsonArray array;
    for (const auto &value : values) {
        array.append(value);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QString globalDatabasePath()
{
    return QDir(Paths::resolvedDataRoot()).filePath(QStringLiteral("material-center.sqlite"));
}

QStringList keysFor(const QVector<GlobalVideoAsset> &assets)
{
    QStringList keys;
    for (const auto &asset : assets) {
        keys.append(asset.videoKey);
    }
    keys.sort();
    return keys;
}

QStringList keysFor(const QVector<FolderSearchHit> &folders)
{
    QStringList keys;
    for (const auto &folder : folders) {
        keys.append(folder.folderKey);
    }
    keys.sort();
    return keys;
}

QStringList keysFor(const QVector<FrameSearchHit> &frames)
{
    QStringList keys;
    for (const auto &frame : frames) {
        keys.append(frame.frameKey);
    }
    keys.sort();
    return keys;
}

class GlobalDbFixture {
public:
    GlobalDbFixture()
    {
        QFile::remove(globalDatabasePath());
        QString errorMessage;
        if (!manager.openDatabase(&errorMessage)) {
            this->errorMessage = errorMessage;
            return;
        }
        valid = seed();
    }

    ~GlobalDbFixture()
    {
        manager.closeDatabase();
        QFile::remove(globalDatabasePath());
    }

    GlobalDatabaseManager manager;
    bool valid = false;
    QString errorMessage;

private:
    bool seed()
    {
        auto db = manager.database();

        if (!execSql(db,
                     QStringLiteral("INSERT INTO project_registry "
                                    "(project_uuid, project_name, project_database_path, last_synced_at, sync_status, error_message) "
                                    "VALUES ('project-alpha', 'Project Alpha', 'G:/projects/alpha/project.cinevault.sqlite', "
                                    "'2026-07-04T10:00:00', 'success', '')"),
                     &errorMessage)) {
            return false;
        }

        if (!insertAsset(QStringLiteral("video-1"),
                         QStringLiteral("interview.mov"),
                         QStringLiteral("mov"),
                         AssetType::Video,
                         VideoAnalysisStatus::Ready,
                         QStringLiteral("G:/projects/alpha/camera/interview.mov"),
                         QStringLiteral("camera/interview.mov"),
                         QStringLiteral("ProRes 4K 视频素材"),
                         QString())
            || !insertAsset(QStringLiteral("image-1"),
                            QStringLiteral("poster.webp"),
                            QStringLiteral("webp"),
                            AssetType::Image,
                            VideoAnalysisStatus::Ready,
                            QStringLiteral("G:/projects/alpha/design/poster.webp"),
                            QStringLiteral("design/poster.webp"),
                            QStringLiteral("WebP 图片素材"),
                            QString())
            || !insertAsset(QStringLiteral("doc-1"),
                            QStringLiteral("notes.md"),
                            QStringLiteral("md"),
                            AssetType::Document,
                            VideoAnalysisStatus::Ready,
                            QStringLiteral("G:/projects/alpha/docs/notes.md"),
                            QStringLiteral("docs/notes.md"),
                            QStringLiteral("Markdown 文本文档"),
                            QStringLiteral("合同备注包含独家授权词条，供全文搜索验证。"))
            || !insertAsset(QStringLiteral("archive-1"),
                            QStringLiteral("bundle.zip"),
                            QStringLiteral("zip"),
                            AssetType::Archive,
                            VideoAnalysisStatus::IndexedOnly,
                            QStringLiteral("G:/projects/alpha/package/bundle.zip"),
                            QStringLiteral("package/bundle.zip"),
                            QStringLiteral("ZIP archive metadata only"),
                            QString())
            || !insertAsset(QStringLiteral("audio-1"),
                            QStringLiteral("dialogue.wav"),
                            QStringLiteral("wav"),
                            AssetType::Audio,
                            VideoAnalysisStatus::IndexedOnly,
                            QStringLiteral("G:/projects/alpha/audio/dialogue.wav"),
                            QStringLiteral("audio/dialogue.wav"),
                            QStringLiteral("WAV audio metadata only"),
                            QString())
            || !insertAsset(QStringLiteral("subtitle-1"),
                            QStringLiteral("captions.srt"),
                            QStringLiteral("srt"),
                            AssetType::Subtitle,
                            VideoAnalysisStatus::Pending,
                            QStringLiteral("G:/projects/alpha/subtitles/captions.srt"),
                            QStringLiteral("subtitles/captions.srt"),
                            QStringLiteral("SRT subtitle text"),
                            QString())
            || !insertAsset(QStringLiteral("project-file-1"),
                            QStringLiteral("edit.prproj"),
                            QStringLiteral("prproj"),
                            AssetType::ProjectFile,
                            VideoAnalysisStatus::IndexedOnly,
                            QStringLiteral("G:/projects/alpha/edit/edit.prproj"),
                            QStringLiteral("edit/edit.prproj"),
                            QStringLiteral("Premiere project metadata only"),
                            QString())
            || !insertAsset(QStringLiteral("other-1"),
                            QStringLiteral("lut.cube"),
                            QStringLiteral("cube"),
                            AssetType::Other,
                            VideoAnalysisStatus::IndexedOnly,
                            QStringLiteral("G:/projects/alpha/color/lut.cube"),
                            QStringLiteral("color/lut.cube"),
                            QStringLiteral("Color LUT metadata only"),
                            QString())
            || !insertAsset(QStringLiteral("unknown-1"),
                            QStringLiteral("README"),
                            QStringLiteral(""),
                            AssetType::Unknown,
                            VideoAnalysisStatus::IndexedOnly,
                            QStringLiteral("G:/projects/alpha/README"),
                            QStringLiteral("README"),
                            QStringLiteral("Unknown extension metadata only"),
                            QString())) {
            return false;
        }

        if (!insertResult(QStringLiteral("video-1"),
                          QStringLiteral("主持人在采访区讲解拍摄计划。"),
                          jsonArray({QStringLiteral("采访"), QStringLiteral("灯光")}),
                          jsonArray({QStringLiteral("棚内")}),
                          QStringLiteral("主持人在采访区讲解拍摄计划 采访 灯光"))
            || !insertResult(QStringLiteral("image-1"),
                             QStringLiteral("海报包含蓝色标题和品牌标签。"),
                             jsonArray({QStringLiteral("海报"), QStringLiteral("品牌标签")}),
                             jsonArray({QStringLiteral("设计稿")}),
                             QStringLiteral("海报包含蓝色标题和品牌标签 海报 品牌标签"))
            || !insertResult(QStringLiteral("doc-1"),
                             QStringLiteral("项目授权说明摘要。"),
                             jsonArray({QStringLiteral("授权")}),
                             jsonArray({QStringLiteral("文档")}),
                             QStringLiteral("项目授权说明摘要 授权"))) {
            return false;
        }

        if (!insertDimension(QStringLiteral("image-1"),
                             QStringLiteral("色彩风格"),
                             QStringLiteral("冷蓝色调，海报标题对比强，适合品牌视觉延展。"))) {
            return false;
        }

        if (!execSql(db,
                     QStringLiteral("INSERT INTO video_frame_analysis "
                                    "(video_key, frame_number, timestamp_ms, caption, tags_json, objects_json, actions, setting_text, "
                                    "entities_json, structured_profile_version, facts_complete, analysis_state) "
                                    "VALUES ('video-1', 1, 1000, '特写镜头里有场记板', '[\"场记板\"]', '[\"摄影机\"]', "
                                    "'记录镜头信息', '棚内', "
                                    "'[{\"label\":\"短裤\",\"colors\":[\"红色\"],\"materials\":[\"牛仔\"],\"attributes\":[]}]', "
                                    "2, 1, 1)"),
                     &errorMessage)) {
            return false;
        }

        if (!execSql(db,
                     QStringLiteral(
                         "INSERT INTO video_analysis_plan "
                         "(video_key, sampling_policy, frame_interval, structured_profile_version, source_frame_count, "
                         "planned_frame_count, asset_size_bytes, asset_modified_at, created_at, updated_at) "
                         "VALUES ('video-1', 'fixed_interval', 60, 2, 61, 2, 1024, "
                         "'2026-07-04T10:00:00', '2026-07-04T10:01:00', '2026-07-04T10:01:00')"),
                     &errorMessage)) {
            return false;
        }

        if (!execSql(db,
                     QStringLiteral(
                         "INSERT INTO video_frame_analysis "
                         "(video_key, frame_number, timestamp_ms, image_path, caption, tags_json, objects_json, "
                         "actions, setting_text, entities_json, structured_profile_version, facts_complete, analysis_state) "
                         "VALUES ('video-1', 61, 2000, 'G:/projects/alpha/cache/frame-61.jpg', "
                         "'模特穿着深蓝色牛仔裤坐在窗边', '[\"蓝色\",\"牛仔\",\"室内\"]', "
                         "'[\"牛仔裤\",\"藤编椅\",\"窗户\"]', '坐在藤编椅上', '室内窗边', "
                         "'[{\"label\":\"男人\",\"colors\":[],\"materials\":[],\"attributes\":[]},"
                         "{\"label\":\"长裤\",\"colors\":[\"蓝色\"],\"materials\":[\"牛仔\"],\"attributes\":[]}]', "
                         "2, 1, 1)"),
                     &errorMessage)) {
            return false;
        }

        if (!execSql(db,
                     QStringLiteral(
                         "INSERT INTO video_frame_analysis "
                         "(video_key, frame_number, timestamp_ms, caption, tags_json, objects_json, "
                         "entities_json, structured_profile_version, "
                         "facts_complete, analysis_state) VALUES "
                         "('other-1', 1, 1000, '红色上衣和蓝色牛仔短裤', '[]', '[]', "
                         "'[{\"label\":\"上衣\",\"colors\":[\"红色\"],\"materials\":[],\"attributes\":[]},"
                         "{\"label\":\"短裤\",\"colors\":[\"蓝色\"],\"materials\":[\"牛仔\"],\"attributes\":[]}]', "
                         "2, 1, 1), "
                         "('image-1', 1, 1000, '普通商品展示', '[\"紫色\"]', '[\"帽子\"]', "
                         "'[{\"label\":\"帽子\",\"colors\":[\"紫色\"],\"materials\":[],\"attributes\":[]}]', "
                         "2, 0, 1)"),
                     &errorMessage)) {
            return false;
        }

        if (!execSql(db,
                     QStringLiteral(
                         "INSERT INTO global_folder_node("
                         "folder_key, project_uuid, project_name, project_database_path, source_root_id, source_root_name, "
                         "name, absolute_path, path_key, relative_path, normalized_date, is_available) VALUES "
                         "('folder-camera', 'project-alpha', 'Project Alpha', "
                         "'G:/projects/alpha/project.cinevault.sqlite', 7, 'Camera A', 'Camera A', "
                         "'G:/projects/alpha/camera', 'g:/projects/alpha/camera', 'camera', '2026-07-04', 1)"),
                     &errorMessage)) {
            return false;
        }

        if (manager.hasFts5()) {
            if (!insertFts(QStringLiteral("video-1"),
                           QStringLiteral("Project Alpha"),
                           QStringLiteral("Camera A"),
                           QStringLiteral("interview.mov"),
                           QStringLiteral("camera/interview.mov"),
                           QStringLiteral("G:/projects/alpha/camera/interview.mov"),
                           QStringLiteral("视频"),
                           QStringLiteral("mov"),
                           QStringLiteral("ProRes 4K 视频素材"),
                           QStringLiteral("主持人在采访区讲解拍摄计划。"),
                           QStringLiteral("采访 灯光"),
                           QStringLiteral("特写镜头里有场记板"),
                           QString())
                || !insertFts(QStringLiteral("image-1"),
                              QStringLiteral("Project Alpha"),
                              QStringLiteral("Camera A"),
                              QStringLiteral("poster.webp"),
                              QStringLiteral("design/poster.webp"),
                              QStringLiteral("G:/projects/alpha/design/poster.webp"),
                              QStringLiteral("图片"),
                              QStringLiteral("webp"),
                              QStringLiteral("WebP 图片素材"),
                              QStringLiteral("海报包含蓝色标题和品牌标签。"),
                              QStringLiteral("海报 品牌标签"),
                              QString(),
                              QString())
                || !insertFts(QStringLiteral("doc-1"),
                              QStringLiteral("Project Alpha"),
                              QStringLiteral("Camera A"),
                              QStringLiteral("notes.md"),
                              QStringLiteral("docs/notes.md"),
                              QStringLiteral("G:/projects/alpha/docs/notes.md"),
                              QStringLiteral("文档"),
                              QStringLiteral("md"),
                              QStringLiteral("Markdown 文本文档"),
                              QStringLiteral("项目授权说明摘要。"),
                              QStringLiteral("授权"),
                              QString(),
                              QStringLiteral("合同备注包含独家授权词条，供全文搜索验证。"))
                || !insertFts(QStringLiteral("archive-1"),
                              QStringLiteral("Project Alpha"),
                              QStringLiteral("Camera A"),
                              QStringLiteral("bundle.zip"),
                              QStringLiteral("package/bundle.zip"),
                              QStringLiteral("G:/projects/alpha/package/bundle.zip"),
                              QStringLiteral("压缩包"),
                              QStringLiteral("zip"),
                              QStringLiteral("ZIP archive metadata only"),
                              QString(),
                              QString(),
                              QString(),
                              QString())) {
                return false;
            }
        }
        return true;
    }

    bool insertAsset(const QString &key,
                     const QString &fileName,
                     const QString &extension,
                     AssetType assetType,
                     VideoAnalysisStatus status,
                     const QString &absolutePath,
                     const QString &relativePath,
                     const QString &technicalSummary,
                     const QString &sourceText)
    {
        auto db = manager.database();
        QSqlQuery query(db);
        query.prepare(QStringLiteral(
            "INSERT INTO global_video_asset "
            "(video_key, project_uuid, project_name, project_database_path, source_root_id, source_root_name, "
            "asset_id, file_name, extension, absolute_path, relative_path, asset_type, size_bytes, modified_at, duration_ms, "
            "thumbnail_path, thumbnail_status, analysis_status, confirmation_status, technical_summary, source_text, "
            "error_message, last_synced_at, updated_at) "
            "VALUES (?, 'project-alpha', 'Project Alpha', 'G:/projects/alpha/project.cinevault.sqlite', 7, 'Camera A', "
            "?, ?, ?, ?, ?, ?, 1024, '2026-07-04T10:00:00', 0, '', 1, ?, 0, ?, ?, '', "
            "'2026-07-04T10:00:00', '2026-07-04T10:00:00')"));
        query.addBindValue(key);
        query.addBindValue(static_cast<qint64>(qHash(key) % 1000000));
        query.addBindValue(fileName);
        query.addBindValue(extension);
        query.addBindValue(absolutePath);
        query.addBindValue(relativePath);
        query.addBindValue(static_cast<int>(assetType));
        query.addBindValue(static_cast<int>(status));
        query.addBindValue(technicalSummary);
        query.addBindValue(sourceText.isNull() ? QStringLiteral("") : sourceText);
        if (!query.exec()) {
            errorMessage = query.lastError().text();
            return false;
        }
        return true;
    }

    bool insertResult(const QString &key,
                      const QString &summary,
                      const QString &keywordsJson,
                      const QString &scenesJson,
                      const QString &searchText)
    {
        QSqlQuery query(manager.database());
        query.prepare(QStringLiteral(
            "INSERT INTO video_analysis_result "
            "(video_key, summary, keywords_json, scenes_json, search_text, model_name, prompt_version, analyzed_at, confirmed_at) "
            "VALUES (?, ?, ?, ?, ?, 'test-model', 'test', '2026-07-04T10:05:00', '')"));
        query.addBindValue(key);
        query.addBindValue(summary);
        query.addBindValue(keywordsJson);
        query.addBindValue(scenesJson);
        query.addBindValue(searchText);
        if (!query.exec()) {
            errorMessage = query.lastError().text();
            return false;
        }
        return true;
    }

    bool insertFts(const QString &key,
                   const QString &projectName,
                   const QString &sourceName,
                   const QString &fileName,
                   const QString &relativePath,
                   const QString &absolutePath,
                   const QString &assetTypeLabel,
                   const QString &extension,
                   const QString &technicalSummary,
                   const QString &summary,
                   const QString &keywords,
                   const QString &captions,
                   const QString &sourceText)
    {
        QSqlQuery query(manager.database());
        query.prepare(QStringLiteral(
            "INSERT INTO video_search_fts "
            "(video_key, project_name, source_root_name, file_name, relative_path, absolute_path, asset_type_label, "
            "extension, technical_summary, summary, keywords, captions, source_text) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
        query.addBindValue(key);
        query.addBindValue(projectName);
        query.addBindValue(sourceName);
        query.addBindValue(fileName);
        query.addBindValue(relativePath);
        query.addBindValue(absolutePath);
        query.addBindValue(assetTypeLabel);
        query.addBindValue(extension);
        query.addBindValue(technicalSummary);
        query.addBindValue(summary);
        query.addBindValue(keywords);
        query.addBindValue(captions);
        query.addBindValue(sourceText);
        if (!query.exec()) {
            errorMessage = query.lastError().text();
            return false;
        }
        return true;
    }

    bool insertDimension(const QString &key, const QString &name, const QString &detail)
    {
        QSqlQuery query(manager.database());
        query.prepare(QStringLiteral(
            "INSERT INTO material_dimension_analysis "
            "(video_key, dimension_key, dimension_name, detail, model_name, prompt_version, analyzed_at) "
            "VALUES (?, ?, ?, ?, 'test-model', 'test', '2026-07-04T10:08:00')"));
        query.addBindValue(key);
        query.addBindValue(name.toCaseFolded());
        query.addBindValue(name);
        query.addBindValue(detail);
        if (!query.exec()) {
            errorMessage = query.lastError().text();
            return false;
        }
        return true;
    }
};
}

class MaterialCenterQueryServiceTest : public QObject {
    Q_OBJECT

private slots:
    void fetchAssets_returnsAllAssetTypes()
    {
        GlobalDbFixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        SearchEngine searchEngine;
        MaterialCenterQueryService service(&fixture.manager, &searchEngine);

        const auto assets = service.fetchAssets(QString(), QString(), QString(), -1, -1, -1);

        QCOMPARE(assets.size(), 9);
        QSet<int> types;
        for (const auto &asset : assets) {
            types.insert(static_cast<int>(asset.assetType));
        }
        QVERIFY(types.contains(static_cast<int>(AssetType::Unknown)));
        QVERIFY(types.contains(static_cast<int>(AssetType::Video)));
        QVERIFY(types.contains(static_cast<int>(AssetType::Audio)));
        QVERIFY(types.contains(static_cast<int>(AssetType::Image)));
        QVERIFY(types.contains(static_cast<int>(AssetType::Subtitle)));
        QVERIFY(types.contains(static_cast<int>(AssetType::ProjectFile)));
        QVERIFY(types.contains(static_cast<int>(AssetType::Document)));
        QVERIFY(types.contains(static_cast<int>(AssetType::Archive)));
        QVERIFY(types.contains(static_cast<int>(AssetType::Other)));
    }

    void fetchAssets_searchesAcrossMetadataSummarySourceTextAndFrames()
    {
        GlobalDbFixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        SearchEngine searchEngine;
        MaterialCenterQueryService service(&fixture.manager, &searchEngine);

        QCOMPARE(keysFor(service.fetchAssets(QStringLiteral("品牌标签"), QString(), QString(), -1, -1, -1)),
                 QStringList{QStringLiteral("image-1")});
        QCOMPARE(keysFor(service.fetchAssets(QStringLiteral("冷蓝色调"), QString(), QString(), -1, -1, -1)),
                 QStringList{QStringLiteral("image-1")});
        QCOMPARE(keysFor(service.fetchAssets(QStringLiteral("独家授权词条"), QString(), QString(), -1, -1, -1)),
                 QStringList{QStringLiteral("doc-1")});
        QCOMPARE(keysFor(service.fetchAssets(QStringLiteral("场记板"), QString(), QString(), -1, -1, -1)),
                 QStringList{QStringLiteral("video-1")});
        QCOMPARE(keysFor(service.fetchAssets(QStringLiteral("zip"), QString(), QString(), -1, -1, -1)),
                 QStringList{QStringLiteral("archive-1")});
    }

    void fetchAssets_filtersByAssetType()
    {
        GlobalDbFixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        SearchEngine searchEngine;
        MaterialCenterQueryService service(&fixture.manager, &searchEngine);

        const auto images = service.fetchAssets(QString(), QString(), QString(), -1, -1, static_cast<int>(AssetType::Image));

        QCOMPARE(images.size(), 1);
        QCOMPARE(images.first().videoKey, QStringLiteral("image-1"));

        const auto typeOptions = service.fetchAssetTypeOptions();
        QSet<int> values;
        for (const auto &option : typeOptions) {
            values.insert(option.toMap().value(QStringLiteral("value")).toInt());
        }
        QVERIFY(values.contains(static_cast<int>(AssetType::Unknown)));
        QVERIFY(values.contains(static_cast<int>(AssetType::Video)));
        QVERIFY(values.contains(static_cast<int>(AssetType::Audio)));
        QVERIFY(values.contains(static_cast<int>(AssetType::Image)));
        QVERIFY(values.contains(static_cast<int>(AssetType::Subtitle)));
        QVERIFY(values.contains(static_cast<int>(AssetType::ProjectFile)));
        QVERIFY(values.contains(static_cast<int>(AssetType::Document)));
        QVERIFY(values.contains(static_cast<int>(AssetType::Archive)));
        QVERIFY(values.contains(static_cast<int>(AssetType::Other)));
    }

    void fetchDetail_exposesTextAndTechnicalSummary()
    {
        GlobalDbFixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        SearchEngine searchEngine;
        MaterialCenterQueryService service(&fixture.manager, &searchEngine);

        const auto detail = service.fetchDetail(QStringLiteral("doc-1"));

        QCOMPARE(detail.asset.videoKey, QStringLiteral("doc-1"));
        QCOMPARE(static_cast<int>(detail.asset.assetType), static_cast<int>(AssetType::Document));
        QVERIFY(detail.asset.sourceText.contains(QStringLiteral("独家授权词条")));
        QCOMPARE(detail.asset.technicalSummary, QStringLiteral("Markdown 文本文档"));
        QCOMPARE(detail.asset.summary, QStringLiteral("项目授权说明摘要。"));
    }

    void fetchDetail_exposesDimensionAnalyses()
    {
        GlobalDbFixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        SearchEngine searchEngine;
        MaterialCenterQueryService service(&fixture.manager, &searchEngine);

        const auto detail = service.fetchDetail(QStringLiteral("image-1"));

        QCOMPARE(detail.dimensionAnalyses.size(), 1);
        QCOMPARE(detail.dimensionAnalyses.first().name, QStringLiteral("色彩风格"));
        QVERIFY(detail.dimensionAnalyses.first().detail.contains(QStringLiteral("冷蓝色调")));
    }

    void fetchDetail_exposesPersistedVisualAnalysisPlan()
    {
        GlobalDbFixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        SearchEngine searchEngine;
        MaterialCenterQueryService service(&fixture.manager, &searchEngine);

        const auto detail = service.fetchDetail(QStringLiteral("video-1"));

        QVERIFY(detail.hasVisualAnalysisPlan);
        QCOMPARE(detail.visualAnalysisPlan.samplingPolicy, QStringLiteral("fixed_interval"));
        QCOMPARE(detail.visualAnalysisPlan.frameInterval, 60);
        QCOMPARE(detail.visualAnalysisPlan.sourceFrameCount, 61);
        QCOMPARE(detail.visualAnalysisPlan.plannedFrameCount, 2);
    }

    void fetchDetailPage_usesKeysetAndIncludesPreferredFrame()
    {
        GlobalDbFixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        SearchEngine searchEngine;
        MaterialCenterQueryService service(&fixture.manager, &searchEngine);

        const auto first = service.fetchDetailPage(QStringLiteral("video-1"), 1, 0, 61);
        QCOMPARE(first.totalFrameCount, 2);
        QVERIFY(first.hasMoreFrames);
        QCOMPARE(first.nextFrameNumber, 1);
        QCOMPARE(first.detail.frames.size(), 2);
        QCOMPARE(first.detail.frames.at(0).frameNumber, 1);
        QCOMPARE(first.detail.frames.at(1).frameNumber, 61);

        const auto second = service.fetchDetailPage(QStringLiteral("video-1"), 1, first.nextFrameNumber);
        QCOMPARE(second.totalFrameCount, 2);
        QVERIFY(!second.hasMoreFrames);
        QCOMPARE(second.nextFrameNumber, 61);
        QCOMPARE(second.detail.frames.size(), 1);
        QCOMPARE(second.detail.frames.first().frameNumber, 61);
    }

    void readOnlyManager_opensIndependentQueryConnection()
    {
        GlobalDbFixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        GlobalDatabaseManager readOnlyManager;
        QString errorMessage;
        QVERIFY2(readOnlyManager.openReadOnlyDatabase(
                     fixture.manager.databaseFilePath(), &errorMessage),
                 qPrintable(errorMessage));
        QVERIFY(readOnlyManager.isOpen());
        QVERIFY(readOnlyManager.hasFts5());
        QSqlQuery query(readOnlyManager.database());
        QVERIFY2(query.exec(QStringLiteral("SELECT COUNT(*) FROM global_video_asset")),
                 qPrintable(query.lastError().text()));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 9);
        readOnlyManager.closeDatabase();
    }

    void searchMaterials_partitionsFolderAndAssetResults()
    {
        GlobalDbFixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        SearchEngine searchEngine(&fixture.manager);
        MaterialCenterQueryService service(&fixture.manager, &searchEngine);

        const auto result = service.searchMaterials(
            QStringLiteral("2026年7月4日 Camera A 文件夹"),
            {},
            QDate(2026, 7, 14));

        QCOMPARE(result.folders.size(), 1);
        QCOMPARE(result.folders.first().folderKey, QStringLiteral("folder-camera"));
        QCOMPARE(result.folders.first().normalizedDate, QStringLiteral("2026-07-04"));
        QVERIFY(result.folders.first().score > 0.0);
    }

    void searchMaterials_requiresPropertiesOnTheSameEntity()
    {
        GlobalDbFixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        SearchEngine searchEngine(&fixture.manager);
        MaterialCenterQueryService service(&fixture.manager, &searchEngine);

        const auto result = service.searchMaterials(QStringLiteral("红色牛仔短裤"));

        QCOMPARE(keysFor(result.assets), QStringList{QStringLiteral("video-1")});
        QVERIFY(!keysFor(result.assets).contains(QStringLiteral("other-1")));
        QCOMPARE(result.excludedPartialCount, 0);
    }

    void searchMaterials_usesSameFrameTextFallbackForIncompleteAssetFacts()
    {
        GlobalDbFixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        SearchEngine searchEngine(&fixture.manager);
        MaterialCenterQueryService service(&fixture.manager, &searchEngine);

        const auto result = service.searchMaterials(QStringLiteral("紫色帽子"));

        QCOMPARE(keysFor(result.assets), QStringList{QStringLiteral("image-1")});
        QCOMPARE(result.excludedPartialCount, 0);
        QVERIFY(result.assets.first().searchReasons.contains(
            QStringLiteral("同一帧文本证据命中（结构化事实不完整）")));
        QCOMPARE(result.assets.first().matchedFrameNumber, 1);
        QVERIFY(result.assets.first().matchedFrameCaption.contains(QStringLiteral("紫色")));
        QVERIFY(result.assets.first().matchedFrameCaption.contains(QStringLiteral("帽子")));
        QVERIFY(!result.assets.first().matchedFrameCaption.contains(QStringLiteral("普通商品展示")));
    }

    void searchMaterials_doesNotCombineCompleteFactsAcrossFrames()
    {
        GlobalDbFixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        SearchEngine searchEngine(&fixture.manager);
        MaterialCenterQueryService service(&fixture.manager, &searchEngine);

        StrictEntityConstraint redShorts;
        redShorts.label = QStringLiteral("短裤");
        redShorts.colors = {QStringLiteral("红色")};
        redShorts.materials = {QStringLiteral("牛仔")};
        StrictEntityConstraint blueJeans;
        blueJeans.label = QStringLiteral("牛仔裤");
        blueJeans.colors = {QStringLiteral("蓝色")};

        ModelSearchUnderstanding assetUnderstanding;
        assetUnderstanding.confidence = 0.95;
        assetUnderstanding.strictEntities = {redShorts, blueJeans};
        const auto assetResult = service.searchMaterials(
            QStringLiteral("interview 红色牛仔短裤 蓝色牛仔裤"),
            {},
            QDate::currentDate(),
            &assetUnderstanding);
        QVERIFY(!keysFor(assetResult.assets).contains(QStringLiteral("video-1")));

        ModelSearchUnderstanding folderUnderstanding = assetUnderstanding;
        folderUnderstanding.resultTarget = SearchResultTarget::Folders;
        folderUnderstanding.resultTargetSpecified = true;
        folderUnderstanding.folderByAssetCriteria = true;
        const auto folderResult = service.searchMaterials(
            QStringLiteral("camera 红色牛仔短裤 蓝色牛仔裤"),
            {},
            QDate::currentDate(),
            &folderUnderstanding);
        QVERIFY(!keysFor(folderResult.folders).contains(QStringLiteral("folder-camera")));
    }

    void searchMaterials_doesNotCombineSummaryAndFrameIntoFalseSameFrameMatch()
    {
        GlobalDbFixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        SearchEngine searchEngine(&fixture.manager);
        MaterialCenterQueryService service(&fixture.manager, &searchEngine);

        const auto result = service.searchMaterials(QStringLiteral("蓝色帽子"));

        QVERIFY(result.assets.isEmpty());
        QCOMPARE(result.excludedPartialCount, 1);
    }

    void searchMaterials_returnsDetailedFrameCardsForFrameIntent()
    {
        GlobalDbFixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        SearchEngine searchEngine(&fixture.manager);
        MaterialCenterQueryService service(&fixture.manager, &searchEngine);

        const auto result = service.searchMaterials(QStringLiteral("包含了蓝色牛仔裤的帧"));

        QCOMPARE(result.parsedQuery.resultTarget, SearchResultTarget::Frames);
        QVERIFY(result.assets.isEmpty());
        QVERIFY(result.folders.isEmpty());
        QCOMPARE(result.frames.size(), 1);
        const auto &frame = result.frames.first();
        QCOMPARE(frame.frameKey, QStringLiteral("frame:video-1:61"));
        QCOMPARE(frame.videoKey, QStringLiteral("video-1"));
        QCOMPARE(frame.fileName, QStringLiteral("interview.mov"));
        QCOMPARE(frame.frameNumber, 61);
        QCOMPARE(frame.timestampMs, qint64{2000});
        QCOMPARE(frame.imagePath, QStringLiteral("G:/projects/alpha/cache/frame-61.jpg"));
        QVERIFY(frame.caption.contains(QStringLiteral("深蓝色牛仔裤")));
        QVERIFY(frame.tags.contains(QStringLiteral("牛仔")));
        QVERIFY(frame.objects.contains(QStringLiteral("牛仔裤")));
        QVERIFY(frame.reasons.contains(QStringLiteral("同一帧、同一视觉对象属性已验证")));
    }

    void searchMaterials_requiresManAndJeansInTheSamePicture()
    {
        GlobalDbFixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        SearchEngine searchEngine(&fixture.manager);
        MaterialCenterQueryService service(&fixture.manager, &searchEngine);

        QVERIFY(execSql(
            fixture.manager.database(),
            QStringLiteral(
                "INSERT INTO video_frame_analysis "
                "(video_key, frame_number, timestamp_ms, caption, tags_json, objects_json, "
                "entities_json, structured_profile_version, facts_complete, analysis_state) "
                "VALUES ('image-1', 2, 2000, '一名男子穿着牛仔裤站在户外', "
                "'[\"男子\",\"牛仔裤\"]', '[\"人物\",\"长裤\"]', '[]', 2, 0, 1)")));

        ModelSearchUnderstanding understanding;
        understanding.confidence = 0.95;
        understanding.resultTarget = SearchResultTarget::Frames;
        understanding.resultTargetSpecified = true;
        StrictEntityConstraint man;
        man.label = QStringLiteral("男性");
        man.attributes = {QStringLiteral("胡须")};
        StrictEntityConstraint jeans;
        jeans.label = QStringLiteral("牛仔裤");
        understanding.strictEntities = {man, jeans};

        const auto result = service.searchMaterials(
            QStringLiteral("有男人穿着牛仔裤的画面"),
            {},
            QDate(2026, 7, 15),
            &understanding);

        QCOMPARE(result.parsedQuery.resultTarget, SearchResultTarget::Frames);
        const QStringList expectedFrameKeys{
            QStringLiteral("frame:image-1:2"),
            QStringLiteral("frame:video-1:61")
        };
        QCOMPARE(keysFor(result.frames), expectedFrameKeys);
        const auto manConstraint = std::find_if(
            result.parsedQuery.strictEntities.cbegin(),
            result.parsedQuery.strictEntities.cend(), [](const auto &entity) {
                return entity.label == QStringLiteral("男人")
                    || entity.label == QStringLiteral("男性");
            });
        QVERIFY(manConstraint != result.parsedQuery.strictEntities.cend());
        QVERIFY(manConstraint->attributes.isEmpty());
        QVERIFY(std::any_of(result.frames.cbegin(), result.frames.cend(), [](const auto &frame) {
            return frame.reasons.contains(QStringLiteral("同一帧、同一视觉对象属性已验证"));
        }));
        QVERIFY(std::any_of(result.frames.cbegin(), result.frames.cend(), [](const auto &frame) {
            return frame.reasons.contains(
                QStringLiteral("同一帧文本证据命中（结构化事实不完整）"));
        }));
    }

    void searchMaterials_quickFiltersOverrideTheNaturalLanguageResultType()
    {
        GlobalDbFixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        SearchEngine searchEngine(&fixture.manager);
        MaterialCenterQueryService service(&fixture.manager, &searchEngine);

        MaterialSearchScope scope;
        scope.resultQuickFilter = SearchResultQuickFilter::Video;
        const auto videos = service.searchMaterials(
            QStringLiteral("蓝色牛仔裤的画面"), scope, QDate(2026, 7, 15));
        QCOMPARE(videos.parsedQuery.resultTarget, SearchResultTarget::Assets);
        QCOMPARE(keysFor(videos.assets), QStringList{QStringLiteral("video-1")});
        QVERIFY(videos.parsedQuery.interpretationLabels.contains(
            QStringLiteral("快捷筛选：视频")));

        scope.resultQuickFilter = SearchResultQuickFilter::Frames;
        const auto frames = service.searchMaterials(
            QStringLiteral("蓝色牛仔裤视频"), scope, QDate(2026, 7, 15));
        QCOMPARE(frames.parsedQuery.resultTarget, SearchResultTarget::Frames);
        QCOMPARE(frames.frames.size(), 1);
        QCOMPARE(frames.frames.first().frameKey, QStringLiteral("frame:video-1:61"));

        scope.resultQuickFilter = SearchResultQuickFilter::Image;
        const auto images = service.searchMaterials(
            QStringLiteral("品牌标签的视频"), scope, QDate(2026, 7, 15));
        QCOMPARE(images.parsedQuery.resultTarget, SearchResultTarget::Assets);
        QCOMPARE(keysFor(images.assets), QStringList{QStringLiteral("image-1")});
        QVERIFY(images.parsedQuery.interpretationLabels.contains(
            QStringLiteral("快捷筛选：图片")));

        scope.resultQuickFilter = SearchResultQuickFilter::Document;
        const auto documents = service.searchMaterials(
            QStringLiteral("授权的画面"), scope, QDate(2026, 7, 15));
        QCOMPARE(documents.parsedQuery.resultTarget, SearchResultTarget::Assets);
        QCOMPARE(keysFor(documents.assets), QStringList{QStringLiteral("doc-1")});
        QVERIFY(documents.parsedQuery.interpretationLabels.contains(
            QStringLiteral("快捷筛选：文档")));
    }

    void searchMaterials_usesSameFrameTextFallbackForLegacyFrameFacts()
    {
        GlobalDbFixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        SearchEngine searchEngine(&fixture.manager);
        MaterialCenterQueryService service(&fixture.manager, &searchEngine);

        const auto result = service.searchMaterials(QStringLiteral("紫色帽子的帧"));

        QCOMPARE(result.frames.size(), 1);
        QCOMPARE(result.frames.first().frameKey, QStringLiteral("frame:image-1:1"));
        QVERIFY(!result.frames.first().factsComplete);
        QVERIFY(result.frames.first().reasons.contains(
            QStringLiteral("同一帧文本证据命中（结构化事实不完整）")));
    }
};

QTEST_GUILESS_MAIN(MaterialCenterQueryServiceTest)

#include "MaterialCenterQueryServiceTest.moc"
