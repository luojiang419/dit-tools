#include "core/jobs/JobEngine.h"
#include "core/scan/ScanEngine.h"
#include "infrastructure/db/DatabaseManager.h"
#include "infrastructure/monitoring/PerformanceTelemetry.h"

#include <QtTest>

#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>

namespace {
qint64 insertSourceRoot(QSqlDatabase database, const QString &path)
{
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "INSERT INTO source_root (name, path, status, created_at, updated_at) "
        "VALUES ('LargeCatalogStress', ?, 'ok', ?, ?)"));
    query.addBindValue(QFileInfo(path).absoluteFilePath());
    const auto now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    query.addBindValue(now);
    query.addBindValue(now);
    if (!query.exec()) {
        return 0;
    }
    return query.lastInsertId().toLongLong();
}

SourceRoot readSourceRoot(QSqlDatabase database, qint64 sourceRootId)
{
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "SELECT id, name, path, status FROM source_root WHERE id = ?"));
    query.addBindValue(sourceRootId);
    if (!query.exec() || !query.next()) {
        return {};
    }
    SourceRoot sourceRoot;
    sourceRoot.id = query.value(0).toLongLong();
    sourceRoot.name = query.value(1).toString();
    sourceRoot.path = query.value(2).toString();
    sourceRoot.status = query.value(3).toString();
    return sourceRoot;
}
}

class LargeCatalogStressTest final : public QObject {
    Q_OBJECT

private slots:
    void scansExternalFixtureAndWritesMachineReadableBaseline()
    {
        const auto environment = QProcessEnvironment::systemEnvironment();
        const auto sourcePath = environment.value(QStringLiteral("CINEVAULT_STRESS_SOURCE"));
        if (sourcePath.isEmpty()) {
            QSKIP("设置 CINEVAULT_STRESS_SOURCE 后才运行外置巨量目录压力测试");
        }
        QVERIFY2(QFileInfo(sourcePath).isDir(), qPrintable(QStringLiteral("压力目录不存在：%1").arg(sourcePath)));

        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        const auto databasePath = temporaryDirectory.filePath(QStringLiteral("stress.cvdb"));

        DatabaseManager databaseManager;
        QString errorMessage;
        QVERIFY2(databaseManager.openProjectDatabase(databasePath, &errorMessage), qPrintable(errorMessage));
        const auto sourceRootId = insertSourceRoot(databaseManager.database(), sourcePath);
        QVERIFY(sourceRootId > 0);

        auto &telemetry = PerformanceTelemetry::global();
        telemetry.resetForTesting();
        JobEngine jobEngine(&databaseManager);
        ScanEngine scanEngine(&databaseManager, &jobEngine, nullptr, nullptr);
        QSignalSpy finishedSpy(&scanEngine, &ScanEngine::scanFinished);
        QSignalSpy failedSpy(&scanEngine, &ScanEngine::scanFailed);
        const auto jobId = jobEngine.createJob(
            JobType::Scan,
            QStringLiteral("巨量素材源基线"),
            QStringLiteral("外置合成目录压力扫描"),
            sourceRootId);

        QElapsedTimer elapsed;
        elapsed.start();
        scanEngine.startScan(readSourceRoot(databaseManager.database(), sourceRootId), jobId);
        scanEngine.waitForIdle();
        QCoreApplication::processEvents();

        QCOMPARE(failedSpy.count(), 0);
        QCOMPARE(finishedSpy.count(), 1);
        QSqlQuery countQuery(databaseManager.database());
        QVERIFY2(countQuery.exec(QStringLiteral("SELECT COUNT(*) FROM asset_file")) && countQuery.next(),
                 qPrintable(countQuery.lastError().text()));

        auto report = telemetry.snapshot();
        report.insert(QStringLiteral("schema_version"), 1);
        report.insert(QStringLiteral("source_path"), QFileInfo(sourcePath).absoluteFilePath());
        report.insert(QStringLiteral("asset_count"), countQuery.value(0).toLongLong());
        report.insert(QStringLiteral("scan_elapsed_ms"), elapsed.elapsed());
        const auto reportBytes = QJsonDocument(report).toJson(QJsonDocument::Indented);

        const auto reportPath = environment.value(
            QStringLiteral("CINEVAULT_STRESS_REPORT"),
            temporaryDirectory.filePath(QStringLiteral("large-catalog-baseline.json")));
        QFile reportFile(reportPath);
        QVERIFY2(reportFile.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(reportFile.errorString()));
        QCOMPARE(reportFile.write(reportBytes), reportBytes.size());
        reportFile.close();
        qInfo().noquote() << QString::fromUtf8(reportBytes);
    }
};

QTEST_GUILESS_MAIN(LargeCatalogStressTest)

#include "LargeCatalogStressTest.moc"
