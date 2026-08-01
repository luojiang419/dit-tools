#include "core/search/SearchEngine.h"
#include "core/search/SemanticSearchProvider.h"
#include "infrastructure/db/GlobalDatabaseManager.h"
#include "shared/Paths.h"

#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>

namespace {
QString globalDatabasePath()
{
    return QDir(Paths::resolvedDataRoot()).filePath(QStringLiteral("material-center.sqlite"));
}

bool execute(QSqlDatabase db, const QString &statement, QString *errorMessage)
{
    QSqlQuery query(db);
    if (query.exec(statement)) {
        return true;
    }
    if (errorMessage) {
        *errorMessage = query.lastError().text();
    }
    return false;
}

class FakeSemanticSearchProvider final : public SemanticSearchProvider {
public:
    QVector<SemanticSearchHit> hits;
    QStringList queries;
    QString error;
    bool ready = true;

    QVector<SemanticSearchHit> search(const QString &queryText,
                                      qsizetype limit,
                                      QString *errorMessage) override
    {
        queries.append(queryText);
        if (errorMessage) {
            *errorMessage = error;
        }
        auto result = hits;
        if (result.size() > limit) {
            result.resize(limit);
        }
        return error.isEmpty() ? result : QVector<SemanticSearchHit>{};
    }

    [[nodiscard]] bool isReady() const override { return ready; }
};

class Fixture {
public:
    Fixture()
    {
        QFile::remove(globalDatabasePath());
        if (!manager.openDatabase(&errorMessage)) {
            return;
        }
        valid = seed();
    }

    ~Fixture()
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
        const QStringList statements{
            QStringLiteral(
                "INSERT INTO project_registry(project_uuid, project_name, project_database_path, sync_status) "
                "VALUES ('project-a', '上海项目', 'G:/projects/a/project.sqlite', 'success'), "
                "('project-b', '其他项目', 'G:/projects/b/project.sqlite', 'success')"),
            QStringLiteral(
                "INSERT INTO global_folder_node("
                "folder_key, project_uuid, project_name, project_database_path, source_root_id, source_root_name, "
                "name, absolute_path, path_key, relative_path, normalized_date, is_available) VALUES "
                "('folder-shanghai', 'project-a', '上海项目', 'G:/projects/a/project.sqlite', 1, '主素材', "
                "'上海夜景航拍', 'G:/projects/a/2026-07-14/上海夜景航拍', 'g:/projects/a/2026-07-14/上海夜景航拍', "
                "'2026-07-14/上海夜景航拍', '2026-07-14', 1), "
                "('folder-old', 'project-a', '上海项目', 'G:/projects/a/project.sqlite', 1, '主素材', "
                "'上海夜景航拍旧版', 'G:/projects/a/2026-07-13/上海夜景航拍旧版', "
                "'g:/projects/a/2026-07-13/上海夜景航拍旧版', '2026-07-13/上海夜景航拍旧版', '2026-07-13', 1)"),
            QStringLiteral(
                "INSERT INTO global_video_asset("
                "video_key, project_uuid, project_name, project_database_path, source_root_id, source_root_name, "
                "folder_key, asset_id, file_name, extension, absolute_path, relative_path, asset_type, modified_at, "
                "analysis_status, confirmation_status, technical_summary, source_text, updated_at, is_available) VALUES "
                "('asset-night-video', 'project-a', '上海项目', 'G:/projects/a/project.sqlite', 1, '主素材', 'folder-shanghai', 1, "
                "'上海夜景航拍.mp4', 'mp4', 'G:/projects/a/2026-07-14/上海夜景航拍.mp4', "
                "'2026-07-14/上海夜景航拍.mp4', 1, '2026-07-14T22:00:00', 2, 0, '4K 视频', '', "
                "'2026-07-14T22:00:00', 1), "
                "('asset-night-image', 'project-a', '上海项目', 'G:/projects/a/project.sqlite', 1, '主素材', 'folder-shanghai', 2, "
                "'上海夜景航拍.jpg', 'jpg', 'G:/projects/a/2026-07-14/上海夜景航拍.jpg', "
                "'2026-07-14/上海夜景航拍.jpg', 3, '2026-07-14T22:00:00', 2, 0, '图片', '', "
                "'2026-07-14T22:00:00', 1), "
                "('asset-semantic', 'project-a', '上海项目', 'G:/projects/a/project.sqlite', 1, '主素材', '', 3, "
                "'clip-003.mp4', 'mp4', 'G:/projects/a/clip-003.mp4', 'clip-003.mp4', 1, "
                "'2026-07-14T08:00:00', 2, 0, '4K 视频', '', '2026-07-14T08:00:00', 1), "
                "('asset-other-project', 'project-b', '其他项目', 'G:/projects/b/project.sqlite', 1, '主素材', '', 4, "
                "'clip-004.mp4', 'mp4', 'G:/projects/b/clip-004.mp4', 'clip-004.mp4', 1, "
                "'2026-07-14T08:00:00', 2, 0, '4K 视频', '', '2026-07-14T08:00:00', 1)"),
            QStringLiteral(
                "UPDATE global_video_asset SET capture_time = '2026-07-13T23:30:00+08:00', "
                "capture_date = '2026-07-13', capture_time_source = 'quicktime_creation_date', "
                "capture_time_confidence = 1.0 WHERE video_key = 'asset-semantic'"),
            QStringLiteral(
                "INSERT INTO global_video_asset("
                "video_key, project_uuid, project_name, project_database_path, source_root_id, source_root_name, "
                "folder_key, asset_id, file_name, extension, absolute_path, relative_path, asset_type, modified_at, "
                "analysis_status, confirmation_status, technical_summary, source_text, updated_at, is_available) VALUES "
                "('asset-last-year', 'project-a', '上海项目', 'G:/projects/a/project.sqlite', 1, '主素材', '', 5, "
                "'去年素材.mp4', 'mp4', 'G:/projects/a/2025/去年素材.mp4', '2025/去年素材.mp4', 1, "
                "'2025-11-20T22:00:00', 2, 0, '4K 视频', '', '2025-11-20T22:00:00', 1)"),
            QStringLiteral(
                "UPDATE global_video_asset SET capture_time = '2025-11-20T14:00:00+08:00', "
                "capture_date = '2025-11-20', capture_time_source = 'quicktime_creation_date', "
                "capture_time_confidence = 1.0 WHERE video_key = 'asset-last-year'"),
            QStringLiteral(
                "UPDATE global_video_asset SET embedded_metadata_text = "
                "'EXIF:Make Sony EXIF:Model AlphaA7M4 EXIF:LensModel FE24-70GM2' "
                "WHERE video_key = 'asset-night-image'"),
            QStringLiteral(
                "INSERT INTO video_analysis_result(video_key, search_text) VALUES "
                "('asset-night-video', '上海夜景航拍 城市灯光'), "
                "('asset-night-image', '上海夜景航拍')"),
            QStringLiteral(
                "INSERT INTO video_frame_analysis(video_key, frame_number, timestamp_ms, image_path, caption, "
                "tags_json, objects_json, entities_json, ocr_text, structured_profile_version, facts_complete, analysis_state) VALUES "
                "('asset-night-video', 25, 84000, 'G:/frames/25.jpg', '雨夜里人物撑着红伞', "
                "'[\"雨夜\"]', '[\"雨伞\"]', '[]', '新品发布', 2, 1, 1), "
                "('asset-night-video', 26, 87000, 'G:/frames/26.jpg', '模特穿着深蓝色牛仔裤坐在窗边', "
                "'[\"蓝色\",\"牛仔\"]', '[\"牛仔裤\",\"椅子\"]', "
                "'[{\"label\":\"牛仔裤\",\"colors\":[\"蓝色\"],\"materials\":[\"牛仔\"],\"attributes\":[]}]', "
                "'', 2, 1, 1), "
                "('asset-night-image', 1, 0, 'G:/frames/poster.jpg', '蓝色发布会海报', "
                "'[\"海报\"]', '[\"标题\"]', '[]', '其他文字', 2, 1, 1)"),
            QStringLiteral(
                "INSERT INTO search_document(document_key, document_type, entity_key, content_text) VALUES "
                "('asset:asset-semantic', 2, 'asset-semantic', '海岸晨雾与日出'), "
                "('asset:asset-other-project', 2, 'asset-other-project', '海岸晨雾与日出'), "
                "('frame:asset-night-video:25', 3, 'asset-night-video', '雨夜里人物撑着红伞'), "
                "('frame:asset-night-video:26', 3, 'asset-night-video', '深蓝色牛仔裤 窗边 模特')")
        };
        for (const auto &statement : statements) {
            if (!execute(db, statement, &errorMessage)) {
                return false;
            }
        }
        return true;
    }
};

const HybridSearchHit *findHit(const HybridSearchResult &result,
                               SearchDocumentType type,
                               const QString &entityKey)
{
    for (const auto &hit : result.hits) {
        if (hit.documentType == type && hit.entityKey == entityKey) {
            return &hit;
        }
    }
    return nullptr;
}
}

class SearchEngineTest : public QObject {
    Q_OBJECT

private slots:
    void naturalLanguageFiltersDateAndAssetTypeAndReturnsOnlyAssets()
    {
        Fixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        SearchEngine engine(&fixture.manager);

        const auto result = engine.searchMaterials(
            QStringLiteral("帮我找 2026年7月14日 上海夜景航拍视频"),
            {},
            QDate(2026, 7, 20));

        const auto *video = findHit(result, SearchDocumentType::Asset, QStringLiteral("asset-night-video"));
        QVERIFY(video);
        QCOMPARE(video->dateScore, 0.25);
        QCOMPARE(video->dateSource, QStringLiteral("legacy_file_modified_time"));
        QCOMPARE(video->typeScore, 1.0);
        QVERIFY(!findHit(result, SearchDocumentType::Asset, QStringLiteral("asset-night-image")));
        QVERIFY(!findHit(result, SearchDocumentType::Folder, QStringLiteral("folder-shanghai")));
        QVERIFY(!findHit(result, SearchDocumentType::Folder, QStringLiteral("folder-old")));
    }

    void multipleAssetTypesAreUnionedAndFoldersAreExcluded()
    {
        Fixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        SearchEngine engine(&fixture.manager);
        MaterialSearchScope scope;
        scope.projectUuid = QStringLiteral("project-a");

        const auto result = engine.searchMaterials(
            QStringLiteral("昨天拍摄的视频、图片"),
            scope,
            QDate(2026, 7, 15));

        QVERIFY(findHit(result, SearchDocumentType::Asset, QStringLiteral("asset-night-video")));
        QVERIFY(findHit(result, SearchDocumentType::Asset, QStringLiteral("asset-night-image")));
        for (const auto &hit : result.hits) {
            QCOMPARE(hit.documentType, SearchDocumentType::Asset);
        }
    }

    void lastCalendarYearFiltersSemanticAssetCandidates()
    {
        Fixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        SearchEngine engine(&fixture.manager);

        const auto result = engine.searchMaterials(
            QStringLiteral("去年的视频"),
            {},
            QDate(2026, 7, 31));

        const auto *lastYear = findHit(result,
                                       SearchDocumentType::Asset,
                                       QStringLiteral("asset-last-year"));
        QVERIFY(lastYear);
        QCOMPARE(lastYear->dateSource, QStringLiteral("quicktime_creation_date"));
        for (const auto &hit : result.hits) {
            QCOMPARE(hit.documentType, SearchDocumentType::Asset);
            QVERIFY(hit.entityKey == QStringLiteral("asset-last-year"));
        }
    }

    void completeLexicalCoverageOutranksPartialCoverage()
    {
        Fixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        SearchEngine engine(&fixture.manager);

        const auto result = engine.searchMaterials(QStringLiteral("上海 城市灯光"));

        QCOMPARE(result.hits.first().entityKey, QStringLiteral("asset-night-video"));
        const auto *complete = findHit(result,
                                       SearchDocumentType::Asset,
                                       QStringLiteral("asset-night-video"));
        const auto *partial = findHit(result,
                                      SearchDocumentType::Asset,
                                      QStringLiteral("asset-night-image"));
        QVERIFY(complete);
        QVERIFY(partial);
        QVERIFY(complete->lexicalScore > partial->lexicalScore);
        QVERIFY(complete->reasons.contains(QStringLiteral("查询关键词完整覆盖")));
    }

    void embeddedExifMetadataIsLexicallySearchable()
    {
        Fixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        SearchEngine engine(&fixture.manager);

        const auto result = engine.searchMaterials(QStringLiteral("AlphaA7M4"));

        QVERIFY(findHit(result,
                        SearchDocumentType::Asset,
                        QStringLiteral("asset-night-image")));
    }

    void captureDateTakesPriorityOverFileModifiedDate()
    {
        Fixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        SearchEngine engine(&fixture.manager);
        MaterialSearchScope scope;
        scope.projectUuid = QStringLiteral("project-a");

        const auto result = engine.searchMaterials(
            QStringLiteral("昨天拍摄的视频"),
            scope,
            QDate(2026, 7, 14));

        QVERIFY(findHit(result, SearchDocumentType::Asset, QStringLiteral("asset-semantic")));
        QVERIFY(!findHit(result, SearchDocumentType::Asset, QStringLiteral("asset-night-video")));
        const auto *hit = findHit(result,
                                  SearchDocumentType::Asset,
                                  QStringLiteral("asset-semantic"));
        QVERIFY(hit);
        QCOMPARE(hit->dateSource, QStringLiteral("quicktime_creation_date"));
        QVERIFY(hit->confidence > 0.9);
        QVERIFY(std::any_of(hit->reasons.cbegin(), hit->reasons.cend(), [](const auto &reason) {
            return reason.contains(QStringLiteral("QuickTime 拍摄时间"));
        }));
    }

    void explicitFolderQueryReturnsOnlyFolders()
    {
        Fixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        SearchEngine engine(&fixture.manager);

        const auto result = engine.searchMaterials(
            QStringLiteral("昨天的文件夹"),
            {},
            QDate(2026, 7, 15));

        QVERIFY(findHit(result, SearchDocumentType::Folder, QStringLiteral("folder-shanghai")));
        QVERIFY(!findHit(result, SearchDocumentType::Folder, QStringLiteral("folder-old")));
        for (const auto &hit : result.hits) {
            QCOMPARE(hit.documentType, SearchDocumentType::Folder);
        }
    }

    void assetCriteriaCanReturnContainingFolder()
    {
        Fixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        SearchEngine engine(&fixture.manager);

        const auto result = engine.searchMaterials(
            QStringLiteral("昨天拍摄的视频和图片所在的文件夹"),
            {},
            QDate(2026, 7, 15));

        QVERIFY(findHit(result, SearchDocumentType::Folder, QStringLiteral("folder-shanghai")));
        for (const auto &hit : result.hits) {
            QCOMPARE(hit.documentType, SearchDocumentType::Folder);
        }
    }

    void semanticCandidatesAreFusedAndRanked()
    {
        Fixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        FakeSemanticSearchProvider semantic;
        semantic.hits = {{QStringLiteral("asset:asset-semantic"), 0.98}};
        SearchEngine engine(&fixture.manager, &semantic);

        const auto result = engine.searchMaterials(QStringLiteral("海边晨雾"));

        QVERIFY(result.semanticSearchAvailable);
        const auto *hit = findHit(result, SearchDocumentType::Asset, QStringLiteral("asset-semantic"));
        QVERIFY(hit);
        QVERIFY(hit->semanticScore > 0.9);
        QCOMPARE(result.hits.first().entityKey, QStringLiteral("asset-semantic"));
    }

    void modelSemanticVariantsCreateAdditionalRecallChannels()
    {
        Fixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        FakeSemanticSearchProvider semantic;
        semantic.hits = {{QStringLiteral("asset:asset-semantic"), 0.98}};
        SearchEngine engine(&fixture.manager, &semantic);
        ModelSearchUnderstanding model;
        model.confidence = 0.92;
        model.semanticVariants = {
            {QStringLiteral("海岸晨雾日出"), 0.80}
        };

        const auto result = engine.searchMaterials(
            QStringLiteral("海边晨雾"),
            {},
            QDate(2026, 7, 15),
            &model);

        QCOMPARE(semantic.queries.size(), 2);
        QCOMPARE(semantic.queries.first(), QStringLiteral("海边晨雾"));
        QCOMPARE(semantic.queries.last(), QStringLiteral("海岸晨雾日出"));
        QVERIFY(findHit(result, SearchDocumentType::Asset, QStringLiteral("asset-semantic")));
    }

    void requiredLexicalGroupsUseOrWithinGroupAndAndAcrossGroups()
    {
        Fixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        SearchEngine engine(&fixture.manager);
        ModelSearchUnderstanding model;
        model.confidence = 0.94;
        model.lexicalTerms = {
            QStringLiteral("模特"), QStringLiteral("人物"),
            QStringLiteral("牛仔裤"), QStringLiteral("丹宁裤")
        };
        model.lexicalGroups = {
            {SearchLexicalGroupMode::Required,
             {QStringLiteral("模特"), QStringLiteral("人物")}},
            {SearchLexicalGroupMode::Required,
             {QStringLiteral("牛仔裤"), QStringLiteral("丹宁裤")}}
        };

        const auto result = engine.searchMaterials(
            QStringLiteral("模特穿牛仔裤"),
            {},
            QDate(2026, 7, 15),
            &model);

        QVERIFY(findHit(result,
                        SearchDocumentType::Asset,
                        QStringLiteral("asset-night-video")));
        QVERIFY(!findHit(result,
                         SearchDocumentType::Asset,
                         QStringLiteral("asset-night-image")));
    }

    void semanticCandidatesRespectExplicitScope()
    {
        Fixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        FakeSemanticSearchProvider semantic;
        semantic.hits = {
            {QStringLiteral("asset:asset-semantic"), 0.9},
            {QStringLiteral("asset:asset-other-project"), 0.99}
        };
        SearchEngine engine(&fixture.manager, &semantic);
        MaterialSearchScope scope;
        scope.projectUuid = QStringLiteral("project-a");

        const auto result = engine.searchMaterials(QStringLiteral("海边晨雾"), scope);

        QVERIFY(findHit(result, SearchDocumentType::Asset, QStringLiteral("asset-semantic")));
        QVERIFY(!findHit(result, SearchDocumentType::Asset, QStringLiteral("asset-other-project")));
    }

    void frameLevelVisualSemanticHitMapsBackToAsset()
    {
        Fixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        FakeSemanticSearchProvider semantic;
        semantic.hits = {{QStringLiteral("frame:asset-night-video:25"), 0.97}};
        SearchEngine engine(&fixture.manager, &semantic);

        const auto result = engine.searchMaterials(QStringLiteral("雨夜里有人撑红伞"));

        const auto *hit = findHit(result,
                                  SearchDocumentType::Asset,
                                  QStringLiteral("asset-night-video"));
        QVERIFY(hit);
        QVERIFY(hit->semanticScore > 0.9);
        QCOMPARE(hit->matchedFrameNumber, 25);
        QCOMPARE(hit->matchedTimestampMs, qint64{84000});
        QCOMPARE(hit->matchedFrameCaption, QStringLiteral("雨夜里人物撑着红伞"));
        QVERIFY(hit->reasons.contains(QStringLiteral("视觉帧命中：00:01:24")));
    }

    void quotedOcrIntentRequiresActualFrameOcrEvidence()
    {
        Fixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        SearchEngine engine(&fixture.manager);

        const auto result = engine.searchMaterials(
            QStringLiteral("找画面文字写着“新品发布”的视频"));

        const auto *hit = findHit(result,
                                  SearchDocumentType::Asset,
                                  QStringLiteral("asset-night-video"));
        QVERIFY(hit);
        QVERIFY(!findHit(result,
                         SearchDocumentType::Asset,
                         QStringLiteral("asset-night-image")));
        QVERIFY(hit->reasons.contains(QStringLiteral("画面 OCR 命中：新品发布")));
    }

    void explicitFrameIntentReturnsIndividualFrameHitsOnly()
    {
        Fixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        SearchEngine engine(&fixture.manager);

        const auto result = engine.searchMaterials(
            QStringLiteral("包含了蓝色牛仔裤的帧"));

        const auto *frame = findHit(result,
                                    SearchDocumentType::VisualEntity,
                                    QStringLiteral("frame:asset-night-video:26"));
        QVERIFY(frame);
        QCOMPARE(frame->assetEntityKey, QStringLiteral("asset-night-video"));
        QCOMPARE(frame->matchedFrameNumber, 26);
        QCOMPARE(frame->matchedTimestampMs, qint64{87000});
        QCOMPARE(frame->matchedFrameCaption, QStringLiteral("模特穿着深蓝色牛仔裤坐在窗边"));
        for (const auto &hit : result.hits) {
            QCOMPARE(hit.documentType, SearchDocumentType::VisualEntity);
        }
    }

    void semanticFailureFallsBackToLexicalResults()
    {
        Fixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        FakeSemanticSearchProvider semantic;
        semantic.error = QStringLiteral("测试模型不可用");
        semantic.ready = false;
        SearchEngine engine(&fixture.manager, &semantic);

        const auto result = engine.searchMaterials(QStringLiteral("上海夜景航拍"));

        QVERIFY(!result.semanticSearchAvailable);
        QVERIFY(result.warningMessage.contains(QStringLiteral("测试模型不可用")));
        QVERIFY(findHit(result, SearchDocumentType::Asset, QStringLiteral("asset-night-video")));
    }

    void folderIntentBoostsFolderResult()
    {
        Fixture fixture;
        QVERIFY2(fixture.valid, qPrintable(fixture.errorMessage));
        SearchEngine engine(&fixture.manager);

        const auto result = engine.searchMaterials(QStringLiteral("上海夜景航拍文件夹"));

        QVERIFY(!result.hits.isEmpty());
        QCOMPARE(result.hits.first().documentType, SearchDocumentType::Folder);
        QCOMPARE(result.hits.first().entityKey, QStringLiteral("folder-shanghai"));
    }
};

QTEST_GUILESS_MAIN(SearchEngineTest)

#include "SearchEngineTest.moc"
