#include "infrastructure/db/DatabaseMigration.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace {
struct DatabaseState {
    bool exists = false;
    bool valid = false;
    qint64 businessRows = 0;
    QString errorMessage;
};

bool executeStatement(QSqlDatabase &db, const QString &statement, QString *errorMessage)
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

QString uniqueConnectionName(const QString &prefix)
{
    return QStringLiteral("%1_%2")
        .arg(prefix, QUuid::createUuid().toString(QUuid::WithoutBraces));
}

bool tableExists(QSqlDatabase &db, const QString &tableName)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT 1 FROM sqlite_master WHERE type IN ('table', 'view') AND name = ? LIMIT 1"));
    query.addBindValue(tableName);
    return query.exec() && query.next();
}

DatabaseState inspectDatabase(const QString &databasePath)
{
    DatabaseState state;
    const QFileInfo info(databasePath);
    state.exists = info.exists() && info.isFile() && info.size() > 0;
    if (!state.exists) {
        return state;
    }

    const auto connectionName = uniqueConnectionName(QStringLiteral("inspect_database"));
    {
        auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        db.setDatabaseName(databasePath);
        if (!db.open()) {
            state.errorMessage = db.lastError().text();
        } else {
            QSqlQuery integrity(db);
            if (!integrity.exec(QStringLiteral("PRAGMA quick_check"))
                || !integrity.next()
                || integrity.value(0).toString().compare(QStringLiteral("ok"), Qt::CaseInsensitive) != 0) {
                state.errorMessage = integrity.lastError().text().trimmed();
                if (state.errorMessage.isEmpty() && integrity.isValid()) {
                    state.errorMessage = integrity.value(0).toString();
                }
                if (state.errorMessage.isEmpty()) {
                    state.errorMessage = QStringLiteral("SQLite 完整性检查失败");
                }
            } else {
                state.valid = true;
                const QStringList businessTables{
                    QStringLiteral("project_registry"),
                    QStringLiteral("global_video_asset"),
                    QStringLiteral("video_analysis_result"),
                    QStringLiteral("video_frame_analysis"),
                    QStringLiteral("video_analysis_task"),
                    QStringLiteral("material_dimension_analysis"),
                    QStringLiteral("material_dimension_frame_analysis")
                };
                for (const auto &tableName : businessTables) {
                    if (!tableExists(db, tableName)) {
                        continue;
                    }
                    QSqlQuery count(db);
                    if (!count.exec(QStringLiteral("SELECT COUNT(*) FROM \"%1\"").arg(tableName))
                        || !count.next()) {
                        state.valid = false;
                        state.errorMessage = count.lastError().text();
                        break;
                    }
                    state.businessRows += count.value(0).toLongLong();
                }
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return state;
}

QString escapedSqlitePath(QString path)
{
    path.replace(QLatin1Char('\''), QStringLiteral("''"));
    return path;
}

bool createConsistentSnapshot(const QString &sourcePath,
                              const QString &destinationPath,
                              QString *errorMessage)
{
    QFile::remove(destinationPath);
    QFile::remove(destinationPath + QStringLiteral("-wal"));
    QFile::remove(destinationPath + QStringLiteral("-shm"));

    const auto connectionName = uniqueConnectionName(QStringLiteral("snapshot_database"));
    bool success = false;
    {
        auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        db.setDatabaseName(sourcePath);
        if (!db.open()) {
            if (errorMessage) {
                *errorMessage = db.lastError().text();
            }
        } else {
            QSqlQuery snapshot(db);
            success = snapshot.exec(QStringLiteral("VACUUM INTO '%1'")
                                        .arg(escapedSqlitePath(destinationPath)));
            if (!success && errorMessage) {
                *errorMessage = snapshot.lastError().text();
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return success;
}

bool writeMigrationMarker(const QString &markerPath,
                          const QString &message,
                          QString *errorMessage)
{
    QSaveFile marker(markerPath);
    if (!marker.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法写入数据迁移标记：%1").arg(marker.errorString());
        }
        return false;
    }
    marker.write(message.toUtf8());
    marker.write("\n");
    if (!marker.commit()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法提交数据迁移标记：%1").arg(marker.errorString());
        }
        return false;
    }
    return true;
}

bool quarantineExistingDatabase(const QString &databasePath,
                                QString *quarantinePath,
                                QString *errorMessage)
{
    if (!QFileInfo::exists(databasePath)) {
        return true;
    }

    const auto timestamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmsszzz"));
    const auto targetPath = QStringLiteral("%1.replaced-%2.bak").arg(databasePath, timestamp);
    if (!QFile::rename(databasePath, targetPath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法保留待替换数据库：%1").arg(databasePath);
        }
        return false;
    }

    for (const auto &suffix : {QStringLiteral("-wal"), QStringLiteral("-shm")}) {
        const auto sidecarPath = databasePath + suffix;
        if (QFileInfo::exists(sidecarPath)) {
            QFile::rename(sidecarPath, targetPath + suffix);
        }
    }
    if (quarantinePath) {
        *quarantinePath = targetPath;
    }
    return true;
}
}

bool DatabaseMigration::configureSqlite(QSqlDatabase &db, QString *errorMessage)
{
    return executeStatement(db, QStringLiteral("PRAGMA journal_mode=WAL;"), errorMessage)
        && executeStatement(db, QStringLiteral("PRAGMA synchronous=NORMAL;"), errorMessage)
        && executeStatement(db, QStringLiteral("PRAGMA foreign_keys=ON;"), errorMessage)
        && executeStatement(db, QStringLiteral("PRAGMA busy_timeout=5000;"), errorMessage);
}

QString DatabaseMigration::backupFilePath(const QString &databaseFilePath, int targetVersion)
{
    return QStringLiteral("%1.pre-v%2.bak").arg(databaseFilePath).arg(targetVersion);
}

bool DatabaseMigration::createUpgradeBackup(QSqlDatabase &db,
                                            int targetVersion,
                                            bool databaseExistedBeforeOpen,
                                            QString *errorMessage)
{
    if (!databaseExistedBeforeOpen || db.databaseName().isEmpty() || db.databaseName() == QStringLiteral(":memory:")) {
        return true;
    }

    const auto backupPath = backupFilePath(db.databaseName(), targetVersion);
    const QFileInfo existingBackup(backupPath);
    if (existingBackup.exists()) {
        if (existingBackup.isFile() && existingBackup.size() > 0) {
            return true;
        }
        if (errorMessage) {
            *errorMessage = QStringLiteral("升级备份路径不可用：%1").arg(backupPath);
        }
        return false;
    }

    auto escapedPath = backupPath;
    escapedPath.replace(QLatin1Char('\''), QStringLiteral("''"));
    if (!executeStatement(db, QStringLiteral("VACUUM INTO '%1'").arg(escapedPath), errorMessage)) {
        if (errorMessage && !errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("创建升级前备份失败：%1，%2").arg(backupPath, *errorMessage);
        }
        return false;
    }
    return true;
}

QString DatabaseMigration::legacyMigrationMarkerPath(const QString &destinationFilePath)
{
    return QDir(QFileInfo(destinationFilePath).absolutePath())
        .filePath(QStringLiteral(".legacy-global-database-migrated-v1"));
}

bool DatabaseMigration::ensureUserDatabase(const QString &destinationFilePath,
                                           const QString &legacyDataRoot,
                                           QString *recoveryMessage,
                                           QString *errorMessage)
{
    if (recoveryMessage) {
        recoveryMessage->clear();
    }

    const QFileInfo destinationInfo(destinationFilePath);
    if (!QDir().mkpath(destinationInfo.absolutePath())) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法创建用户数据目录：%1").arg(destinationInfo.absolutePath());
        }
        return false;
    }

    const auto markerPath = legacyMigrationMarkerPath(destinationFilePath);
    if (QFileInfo::exists(markerPath)) {
        return true;
    }

    const auto destinationState = inspectDatabase(destinationFilePath);
    if (destinationState.valid && destinationState.businessRows > 0) {
        return writeMigrationMarker(markerPath,
                                    QStringLiteral("保留现有用户数据库 %1")
                                        .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)),
                                    errorMessage);
    }

    QStringList candidatePaths;
    const auto legacyMainPath = QDir(legacyDataRoot).filePath(QStringLiteral("material-center.sqlite"));
    if (QDir::cleanPath(legacyMainPath).compare(QDir::cleanPath(destinationFilePath),
                                               Qt::CaseInsensitive) != 0) {
        candidatePaths.append(legacyMainPath);
    }
    const QDir legacyDirectory(legacyDataRoot);
    const auto backupFiles = legacyDirectory.entryInfoList(
        {QStringLiteral("material-center.sqlite*.bak")},
        QDir::Files | QDir::Readable,
        QDir::Time);
    for (const auto &backup : backupFiles) {
        candidatePaths.append(backup.absoluteFilePath());
    }
    candidatePaths.removeDuplicates();

    QString bestSourcePath;
    DatabaseState bestSourceState;
    for (const auto &candidatePath : candidatePaths) {
        const auto candidateState = inspectDatabase(candidatePath);
        if (!candidateState.valid) {
            continue;
        }
        if (bestSourcePath.isEmpty() || candidateState.businessRows > bestSourceState.businessRows) {
            bestSourcePath = candidatePath;
            bestSourceState = candidateState;
        }
    }

    if (bestSourceState.valid && bestSourceState.businessRows > 0
        && (!destinationState.valid || destinationState.businessRows == 0)) {
        const auto temporaryPath = destinationFilePath + QStringLiteral(".legacy-migration-tmp");
        QString snapshotError;
        if (!createConsistentSnapshot(bestSourcePath, temporaryPath, &snapshotError)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("从旧版本数据库创建恢复快照失败：%1").arg(snapshotError);
            }
            return false;
        }
        const auto temporaryState = inspectDatabase(temporaryPath);
        if (!temporaryState.valid || temporaryState.businessRows != bestSourceState.businessRows) {
            QFile::remove(temporaryPath);
            if (errorMessage) {
                *errorMessage = QStringLiteral("旧版本数据库恢复快照校验失败，已停止替换用户数据。");
            }
            return false;
        }

        QString quarantinePath;
        if (!quarantineExistingDatabase(destinationFilePath, &quarantinePath, errorMessage)) {
            QFile::remove(temporaryPath);
            return false;
        }
        if (!QFile::rename(temporaryPath, destinationFilePath)) {
            if (!quarantinePath.isEmpty()) {
                QFile::rename(quarantinePath, destinationFilePath);
            }
            if (errorMessage) {
                *errorMessage = QStringLiteral("无法启用已校验的旧版本数据库恢复快照。");
            }
            return false;
        }

        const auto message = QStringLiteral("已从 %1 恢复旧版本解析数据，共校验 %2 条业务记录。")
                                 .arg(bestSourcePath)
                                 .arg(bestSourceState.businessRows);
        if (!writeMigrationMarker(markerPath, message, errorMessage)) {
            return false;
        }
        if (recoveryMessage) {
            *recoveryMessage = message;
        }
        return true;
    }

    if (destinationState.exists && !destinationState.valid) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("用户数据库完整性检查失败，且没有找到包含旧解析数据的有效备份：%1")
                                .arg(destinationState.errorMessage);
        }
        return false;
    }

    return writeMigrationMarker(markerPath,
                                QStringLiteral("未发现需要迁移的旧版本数据 %1")
                                    .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)),
                                errorMessage);
}
