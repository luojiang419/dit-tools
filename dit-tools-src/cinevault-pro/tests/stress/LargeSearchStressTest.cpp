#include "core/search/SearchEngine.h"
#include "infrastructure/db/GlobalDatabaseManager.h"
#include "shared/Paths.h"

#include <QtTest>

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>

#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

namespace {
qint64 percentile(QVector<qint64> samples, double ratio)
{
    if (samples.isEmpty()) {
        return 0;
    }
    std::sort(samples.begin(), samples.end());
    const auto index = std::clamp<qsizetype>(
        static_cast<qsizetype>((samples.size() - 1) * ratio), 0, samples.size() - 1);
    return samples.at(index);
}

quint64 currentWorkingSetBytes()
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
                             sizeof(counters))) {
        return counters.WorkingSetSize;
    }
#endif
    return 0;
}

bool exec(QSqlQuery &query, QString *errorMessage)
{
    if (query.exec()) {
        return true;
    }
    if (errorMessage) {
        *errorMessage = query.lastError().text();
    }
    return false;
}
}

class LargeSearchStressTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void searchesLargePersistentIndex()
    {
        const auto environment = QProcessEnvironment::systemEnvironment();
        bool countOk = false;
        const auto assetCount = environment.value(QStringLiteral("CINEVAULT_SEARCH_STRESS_COUNT"))
                                    .toLongLong(&countOk);
        if (!countOk || assetCount <= 0) {
            QSKIP("设置 CINEVAULT_SEARCH_STRESS_COUNT 后才运行巨量搜索压力测试");
        }

        const auto databasePath = QDir(Paths::resolvedDataRoot())
                                      .filePath(QStringLiteral("material-center.sqlite"));
        QFile::remove(databasePath);
        QFile::remove(databasePath + QStringLiteral("-wal"));
        QFile::remove(databasePath + QStringLiteral("-shm"));

        GlobalDatabaseManager manager;
        QString errorMessage;
        QVERIFY2(manager.openDatabase(&errorMessage), qPrintable(errorMessage));
        QVERIFY(manager.hasFts5());
        QVERIFY(manager.hasFastFileSearch());
        auto db = manager.database();

        QSqlQuery project(db);
        QVERIFY2(project.exec(QStringLiteral(
                     "INSERT INTO project_registry(project_uuid, project_name, project_database_path, sync_status) "
                     "VALUES ('stress-project', '压力测试', 'G:/stress/project.sqlite', 'success')")),
                 qPrintable(project.lastError().text()));

        QSqlQuery asset(db);
        asset.prepare(QStringLiteral(
            "INSERT INTO global_video_asset(video_key, project_uuid, project_name, project_database_path, "
            "asset_id, file_name, extension, absolute_path, relative_path, asset_type, modified_at, updated_at) "
            "VALUES (?, 'stress-project', '压力测试', 'G:/stress/project.sqlite', ?, ?, 'mp4', ?, ?, 1, "
            "'2026-09-07T00:00:00', '2026-09-07T00:00:00')"));
        QSqlQuery contentFts(db);
        contentFts.prepare(QStringLiteral(
            "INSERT INTO video_search_fts(video_key, project_name, source_root_name, file_name, "
            "relative_path, absolute_path, asset_type_label, extension, technical_summary, summary, "
            "keywords, captions, source_text) VALUES (?, '压力测试', '', ?, ?, ?, '视频', 'mp4', '', '', '', '', '')"));

        QElapsedTimer seedTimer;
        seedTimer.start();
        QVERIFY(db.transaction());
        for (qint64 index = 0; index < assetCount; ++index) {
            const auto marker = index % 10000 == 0
                ? QStringLiteral("needle%1_").arg((index / 10000) % 100, 2, 10, QLatin1Char('0'))
                : QString{};
            const auto fileName = QStringLiteral("archive_%1%2_camera_master.mp4")
                                      .arg(marker)
                                      .arg(index, 9, 10, QLatin1Char('0'));
            const auto videoKey = QStringLiteral("stress:%1").arg(index);
            const auto relativePath = QStringLiteral("catalog/%1/%2").arg(index / 1000).arg(fileName);
            const auto absolutePath = QStringLiteral("G:/stress/%1").arg(relativePath);

            asset.addBindValue(videoKey);
            asset.addBindValue(index + 1);
            asset.addBindValue(fileName);
            asset.addBindValue(absolutePath);
            asset.addBindValue(relativePath);
            QVERIFY2(exec(asset, &errorMessage), qPrintable(errorMessage));
            asset.finish();

            contentFts.addBindValue(videoKey);
            contentFts.addBindValue(fileName);
            contentFts.addBindValue(relativePath);
            contentFts.addBindValue(absolutePath);
            QVERIFY2(exec(contentFts, &errorMessage), qPrintable(errorMessage));
            contentFts.finish();

            if ((index + 1) % 10000 == 0 && index + 1 < assetCount) {
                QVERIFY(db.commit());
                QVERIFY(db.transaction());
            }
        }
        QVERIFY(db.commit());
        const auto seedElapsedMs = seedTimer.elapsed();

        QSqlQuery count(db);
        QVERIFY(count.exec(QStringLiteral("SELECT COUNT(*) FROM global_video_asset")));
        QVERIFY(count.next());
        QCOMPARE(count.value(0).toLongLong(), assetCount);
        QVERIFY(count.exec(QStringLiteral("SELECT COUNT(*) FROM file_name_search_fts")));
        QVERIFY(count.next());
        QCOMPARE(count.value(0).toLongLong(), assetCount);

        SearchEngine engine(&manager);
        MaterialSearchScope scope;
        scope.limit = 50;
        const auto markerCount = std::max<qint64>(1, (assetCount + 9999) / 10000);
        QVector<qint64> hitSamples;
        hitSamples.reserve(50);
        for (int sample = 0; sample < 50; ++sample) {
            const auto marker = QStringLiteral("needle%1")
                                    .arg(sample % std::min<qint64>(markerCount, 100),
                                         2,
                                         10,
                                         QLatin1Char('0'));
            QElapsedTimer timer;
            timer.start();
            const auto result = engine.searchMaterials(marker, scope);
            hitSamples.append(timer.elapsed());
            QVERIFY2(!result.hits.isEmpty(), qPrintable(QStringLiteral("未找到压力测试标记：%1").arg(marker)));
        }

        QElapsedTimer missTimer;
        missTimer.start();
        const auto missing = engine.searchMaterials(QStringLiteral("definitely-no-such-material-7f42"), scope);
        const auto missElapsedMs = missTimer.elapsed();
        QVERIFY(missing.hits.isEmpty());

        QElapsedTimer broadTimer;
        broadTimer.start();
        const auto broad = engine.searchMaterials(QStringLiteral("camera"), scope);
        const auto broadHitElapsedMs = broadTimer.elapsed();
        QVERIFY(!broad.hits.isEmpty());

        const auto outputPath = environment.value(QStringLiteral("CINEVAULT_SEARCH_STRESS_OUTPUT"));
        QJsonObject report{
            {QStringLiteral("schema_version"), 2},
            {QStringLiteral("asset_count"), assetCount},
            {QStringLiteral("seed_elapsed_ms"), seedElapsedMs},
            {QStringLiteral("hit_query_samples"), hitSamples.size()},
            {QStringLiteral("hit_query_p50_ms"), percentile(hitSamples, 0.50)},
            {QStringLiteral("hit_query_p95_ms"), percentile(hitSamples, 0.95)},
            {QStringLiteral("hit_query_p99_ms"), percentile(hitSamples, 0.99)},
            {QStringLiteral("broad_hit_query_ms"), broadHitElapsedMs},
            {QStringLiteral("miss_query_ms"), missElapsedMs},
            {QStringLiteral("working_set_bytes"), static_cast<qint64>(currentWorkingSetBytes())},
            {QStringLiteral("database_bytes"), QFileInfo(databasePath).size()}
        };
        qInfo().noquote() << QJsonDocument(report).toJson(QJsonDocument::Compact);
        if (!outputPath.isEmpty()) {
            QFile output(outputPath);
            QVERIFY2(output.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(output.errorString()));
            QCOMPARE(output.write(QJsonDocument(report).toJson(QJsonDocument::Indented)),
                     QJsonDocument(report).toJson(QJsonDocument::Indented).size());
        }

        const auto p95Limit = environment.value(QStringLiteral("CINEVAULT_SEARCH_P95_LIMIT_MS"),
                                                 QStringLiteral("100")).toLongLong();
        QVERIFY2(percentile(hitSamples, 0.95) <= p95Limit,
                 qPrintable(QStringLiteral("命中查询 P95 超过 %1ms").arg(p95Limit)));

        manager.closeDatabase();
        QFile::remove(databasePath);
        QFile::remove(databasePath + QStringLiteral("-wal"));
        QFile::remove(databasePath + QStringLiteral("-shm"));
    }
};

QTEST_GUILESS_MAIN(LargeSearchStressTest)

#include "LargeSearchStressTest.moc"
