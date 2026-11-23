#include "infrastructure/db/GlobalDatabaseManager.h"

#include "domain/Enums.h"
#include "infrastructure/db/DatabaseMigration.h"
#include "shared/Paths.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QList>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>

namespace {
bool executeBatch(QSqlDatabase &db, const QStringList &statements, QString *errorMessage)
{
    QSqlQuery query(db);
    for (const auto &statement : statements) {
        if (!query.exec(statement)) {
            if (errorMessage) {
                *errorMessage = query.lastError().text();
            }
            return false;
        }
    }
    return true;
}

QStringList tableColumns(QSqlDatabase &db, const QString &tableName, QString *errorMessage)
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("PRAGMA table_info(%1)").arg(tableName))) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("读取素材管理表结构失败：%1，%2").arg(tableName, query.lastError().text());
        }
        return {};
    }

    QStringList columns;
    while (query.next()) {
        columns.append(query.value(1).toString());
    }
    return columns;
}

bool ensureColumn(QSqlDatabase &db,
                  const QString &tableName,
                  const QString &columnName,
                  const QString &columnDefinition,
                  QString *errorMessage)
{
    const auto columns = tableColumns(db, tableName, errorMessage);
    if (columns.contains(columnName, Qt::CaseInsensitive)) {
        return true;
    }

    QSqlQuery query(db);
    const auto statement = QStringLiteral("ALTER TABLE %1 ADD COLUMN %2").arg(tableName, columnDefinition);
    if (!query.exec(statement)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("迁移素材管理字段失败：%1.%2，%3").arg(tableName, columnName, query.lastError().text());
        }
        return false;
    }
    return true;
}

bool ensureColumns(QSqlDatabase &db,
                   const QString &tableName,
                   const QList<QPair<QString, QString>> &columns,
                   QString *errorMessage)
{
    for (const auto &column : columns) {
        if (!ensureColumn(db, tableName, column.first, column.second, errorMessage)) {
            return false;
        }
    }
    return true;
}

QString createSearchDocumentTableStatement()
{
    return QStringLiteral("CREATE TABLE IF NOT EXISTS search_document ("
                          "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                          "document_key TEXT NOT NULL,"
                          "document_type INTEGER NOT NULL DEFAULT 0,"
                          "entity_key TEXT NOT NULL DEFAULT '',"
                          "content_hash TEXT NOT NULL DEFAULT '',"
                          "content_text TEXT NOT NULL DEFAULT '',"
                          "source_updated_at TEXT NOT NULL DEFAULT '',"
                          "model_id TEXT NOT NULL DEFAULT '',"
                          "index_schema_version INTEGER NOT NULL DEFAULT 0,"
                          "indexed_at TEXT NOT NULL DEFAULT ''"
                          ");");
}

QString createSearchIndexStateTableStatement()
{
    return QStringLiteral("CREATE TABLE IF NOT EXISTS search_index_state ("
                          "singleton INTEGER PRIMARY KEY CHECK(singleton = 1),"
                          "schema_version INTEGER NOT NULL DEFAULT 0,"
                          "model_id TEXT NOT NULL DEFAULT '',"
                          "dimensions INTEGER NOT NULL DEFAULT 0,"
                          "generation INTEGER NOT NULL DEFAULT 0,"
                          "status TEXT NOT NULL DEFAULT 'dirty',"
                          "document_count INTEGER NOT NULL DEFAULT 0,"
                          "updated_at TEXT NOT NULL DEFAULT '',"
                          "last_error TEXT NOT NULL DEFAULT ''"
                          ");");
}

QString createSearchFtsStatement()
{
    return QStringLiteral(
        "CREATE VIRTUAL TABLE IF NOT EXISTS video_search_fts USING fts5("
        "video_key UNINDEXED,"
        "project_name,"
        "source_root_name,"
        "file_name,"
        "relative_path,"
        "absolute_path,"
        "asset_type_label,"
        "extension,"
        "technical_summary,"
        "summary,"
        "keywords,"
        "captions,"
        "source_text,"
        "tokenize='unicode61'"
        ");");
}

bool ensureSearchFtsSchema(QSqlDatabase &db, bool *hasFts5, QString *errorMessage)
{
    QSqlQuery query(db);
    if (!query.exec(createSearchFtsStatement())) {
        query.exec(QStringLiteral("DROP TABLE IF EXISTS video_search_fts"));
        if (hasFts5) {
            *hasFts5 = false;
        }
        return true;
    }

    const auto columns = tableColumns(db, QStringLiteral("video_search_fts"), errorMessage);
    const QStringList requiredColumns = {
        QStringLiteral("video_key"),
        QStringLiteral("project_name"),
        QStringLiteral("source_root_name"),
        QStringLiteral("file_name"),
        QStringLiteral("relative_path"),
        QStringLiteral("absolute_path"),
        QStringLiteral("asset_type_label"),
        QStringLiteral("extension"),
        QStringLiteral("technical_summary"),
        QStringLiteral("summary"),
        QStringLiteral("keywords"),
        QStringLiteral("captions"),
        QStringLiteral("source_text")
    };
    for (const auto &column : requiredColumns) {
        if (!columns.contains(column, Qt::CaseInsensitive)) {
            if (!query.exec(QStringLiteral("DROP TABLE IF EXISTS video_search_fts"))
                || !query.exec(createSearchFtsStatement())) {
                if (errorMessage) {
                    *errorMessage = query.lastError().text();
                }
                if (hasFts5) {
                    *hasFts5 = false;
                }
                return false;
            }
            break;
        }
    }

    if (hasFts5) {
        *hasFts5 = true;
    }
    return true;
}
}

GlobalDatabaseManager::GlobalDatabaseManager(QObject *parent)
    : QObject(parent)
    , m_connectionName(QStringLiteral("cinevault_global_%1")
                           .arg(reinterpret_cast<quintptr>(this), 0, 16))
{
}

bool GlobalDatabaseManager::openDatabase(QString *errorMessage)
{
    closeDatabase();

    QString pathError;
    if (!Paths::ensureBaseDirectories(&pathError)) {
        if (errorMessage) {
            *errorMessage = pathError;
        }
        return false;
    }

    m_databaseFilePath = QDir(Paths::resolvedDataRoot()).filePath(QStringLiteral("material-center.sqlite"));
    QString recoveryMessage;
    if (!DatabaseMigration::ensureUserDatabase(m_databaseFilePath,
                                               Paths::installDataRoot(),
                                               &recoveryMessage,
                                               errorMessage)) {
        m_databaseFilePath.clear();
        return false;
    }
    if (!recoveryMessage.isEmpty()) {
        qInfo().noquote() << recoveryMessage;
    }
    const QFileInfo databaseInfo(m_databaseFilePath);
    const bool databaseExistedBeforeOpen = databaseInfo.exists() && databaseInfo.isFile() && databaseInfo.size() > 0;

    auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(m_databaseFilePath);
    if (!db.open()) {
        if (errorMessage) {
            *errorMessage = db.lastError().text();
        }
        closeDatabase();
        return false;
    }

    if (!initializeSchema(db, databaseExistedBeforeOpen, errorMessage)) {
        closeDatabase();
        return false;
    }

    return true;
}

void GlobalDatabaseManager::closeDatabase()
{
    if (!QSqlDatabase::contains(m_connectionName)) {
        m_databaseFilePath.clear();
        m_hasFts5 = false;
        return;
    }

    {
        auto db = QSqlDatabase::database(m_connectionName);
        db.close();
    }
    QSqlDatabase::removeDatabase(m_connectionName);
    m_databaseFilePath.clear();
    m_hasFts5 = false;
}

bool GlobalDatabaseManager::isOpen() const
{
    return !m_databaseFilePath.isEmpty() && QSqlDatabase::contains(m_connectionName);
}

bool GlobalDatabaseManager::hasFts5() const
{
    return m_hasFts5;
}

QString GlobalDatabaseManager::databaseFilePath() const
{
    return m_databaseFilePath;
}

QSqlDatabase GlobalDatabaseManager::database() const
{
    return QSqlDatabase::database(m_connectionName);
}

QSqlDatabase GlobalDatabaseManager::openThreadConnection(const QString &connectionName, QString *errorMessage) const
{
    auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    db.setDatabaseName(m_databaseFilePath);
    if (!db.open()) {
        if (errorMessage) {
            *errorMessage = db.lastError().text();
        }
        return db;
    }
    if (!DatabaseMigration::configureSqlite(db, errorMessage)) {
        db.close();
    }
    return db;
}

void GlobalDatabaseManager::closeThreadConnection(const QString &connectionName) const
{
    if (!QSqlDatabase::contains(connectionName)) {
        return;
    }
    {
        auto db = QSqlDatabase::database(connectionName);
        db.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
}

bool GlobalDatabaseManager::updateProjectReference(const QString &projectUuid,
                                                   const QString &projectName,
                                                   const QString &oldDatabasePath,
                                                   const QString &newDatabasePath,
                                                   QString *errorMessage)
{
    if (!isOpen()) {
        return true;
    }

    auto db = database();
    if (!db.transaction()) {
        if (errorMessage) {
            *errorMessage = db.lastError().text();
        }
        return false;
    }

    if (m_hasFts5) {
        QSqlQuery updateFts(db);
        updateFts.prepare(QStringLiteral(
            "UPDATE video_search_fts SET project_name = ? "
            "WHERE video_key IN ("
            "SELECT video_key FROM global_video_asset WHERE project_uuid = ? OR project_database_path = ?"
            ")"));
        updateFts.addBindValue(projectName);
        updateFts.addBindValue(projectUuid);
        updateFts.addBindValue(oldDatabasePath);
        if (!updateFts.exec()) {
            if (errorMessage) {
                *errorMessage = updateFts.lastError().text();
            }
            db.rollback();
            return false;
        }
    }

    QSqlQuery updateAssets(db);
    updateAssets.prepare(QStringLiteral(
        "UPDATE global_video_asset SET project_name = ?, project_database_path = ?, updated_at = ? "
        "WHERE project_uuid = ? OR project_database_path = ?"));
    updateAssets.addBindValue(projectName);
    updateAssets.addBindValue(newDatabasePath);
    updateAssets.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    updateAssets.addBindValue(projectUuid);
    updateAssets.addBindValue(oldDatabasePath);
    if (!updateAssets.exec()) {
        if (errorMessage) {
            *errorMessage = updateAssets.lastError().text();
        }
        db.rollback();
        return false;
    }

    QSqlQuery updateFolders(db);
    updateFolders.prepare(QStringLiteral(
        "UPDATE global_folder_node SET project_name = ?, project_database_path = ?, updated_at = ? "
        "WHERE project_uuid = ? OR project_database_path = ?"));
    updateFolders.addBindValue(projectName);
    updateFolders.addBindValue(newDatabasePath);
    updateFolders.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    updateFolders.addBindValue(projectUuid);
    updateFolders.addBindValue(oldDatabasePath);
    if (!updateFolders.exec()) {
        if (errorMessage) {
            *errorMessage = updateFolders.lastError().text();
        }
        db.rollback();
        return false;
    }

    QSqlQuery updateRegistry(db);
    updateRegistry.prepare(QStringLiteral(
        "UPDATE project_registry SET project_name = ?, project_database_path = ?, last_synced_at = ?, error_message = '' "
        "WHERE project_uuid = ? OR project_database_path = ?"));
    updateRegistry.addBindValue(projectName);
    updateRegistry.addBindValue(newDatabasePath);
    updateRegistry.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    updateRegistry.addBindValue(projectUuid);
    updateRegistry.addBindValue(oldDatabasePath);
    if (!updateRegistry.exec()) {
        if (errorMessage) {
            *errorMessage = updateRegistry.lastError().text();
        }
        db.rollback();
        return false;
    }

    if (!db.commit()) {
        if (errorMessage) {
            *errorMessage = db.lastError().text();
        }
        db.rollback();
        return false;
    }
    return true;
}

bool GlobalDatabaseManager::removeProjectReference(const QString &projectUuid, const QString &databasePath, QString *errorMessage)
{
    if (!isOpen()) {
        return true;
    }

    auto db = database();
    if (!db.transaction()) {
        if (errorMessage) {
            *errorMessage = db.lastError().text();
        }
        return false;
    }

    if (m_hasFts5) {
        QSqlQuery deleteFts(db);
        deleteFts.prepare(QStringLiteral(
            "DELETE FROM video_search_fts WHERE video_key IN ("
            "SELECT video_key FROM global_video_asset WHERE project_uuid = ? OR project_database_path = ?"
            ")"));
        deleteFts.addBindValue(projectUuid);
        deleteFts.addBindValue(databasePath);
        if (!deleteFts.exec()) {
            if (errorMessage) {
                *errorMessage = deleteFts.lastError().text();
            }
            db.rollback();
            return false;
        }
    }

    QSqlQuery deleteAssets(db);
    deleteAssets.prepare(QStringLiteral("DELETE FROM global_video_asset WHERE project_uuid = ? OR project_database_path = ?"));
    deleteAssets.addBindValue(projectUuid);
    deleteAssets.addBindValue(databasePath);
    if (!deleteAssets.exec()) {
        if (errorMessage) {
            *errorMessage = deleteAssets.lastError().text();
        }
        db.rollback();
        return false;
    }

    QSqlQuery deleteFolders(db);
    deleteFolders.prepare(QStringLiteral("DELETE FROM global_folder_node WHERE project_uuid = ? OR project_database_path = ?"));
    deleteFolders.addBindValue(projectUuid);
    deleteFolders.addBindValue(databasePath);
    if (!deleteFolders.exec()) {
        if (errorMessage) {
            *errorMessage = deleteFolders.lastError().text();
        }
        db.rollback();
        return false;
    }

    QSqlQuery deleteRegistry(db);
    deleteRegistry.prepare(QStringLiteral("DELETE FROM project_registry WHERE project_uuid = ? OR project_database_path = ?"));
    deleteRegistry.addBindValue(projectUuid);
    deleteRegistry.addBindValue(databasePath);
    if (!deleteRegistry.exec()) {
        if (errorMessage) {
            *errorMessage = deleteRegistry.lastError().text();
        }
        db.rollback();
        return false;
    }

    if (!db.commit()) {
        if (errorMessage) {
            *errorMessage = db.lastError().text();
        }
        db.rollback();
        return false;
    }
    return true;
}

bool GlobalDatabaseManager::initializeSchema(QSqlDatabase &db, bool databaseExistedBeforeOpen, QString *errorMessage)
{
    auto version = currentSchemaVersion(db);
    if (version < CurrentSchemaVersion
        && !DatabaseMigration::createUpgradeBackup(db,
                                                    CurrentSchemaVersion,
                                                    databaseExistedBeforeOpen,
                                                    errorMessage)) {
        return false;
    }
    if (!DatabaseMigration::configureSqlite(db, errorMessage)) {
        return false;
    }
    if (!db.transaction()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法开始全局数据库迁移事务：%1").arg(db.lastError().text());
        }
        return false;
    }
    auto rollback = [&db]() {
        db.rollback();
        return false;
    };

    if (!createSchema(db, errorMessage)) {
        return rollback();
    }

    if (!ensureSchemaCompatibility(db, errorMessage)) {
        return rollback();
    }

    if (version < 6) {
        if (!setSchemaVersion(db, 6, errorMessage)) {
            return rollback();
        }
        version = 6;
    }
    if (version < 7) {
        QSqlQuery successBackfill(db);
        if (!successBackfill.exec(QStringLiteral(
                "UPDATE video_frame_analysis SET analysis_state = 1 "
                "WHERE COALESCE(analysis_state, 0) = 0 "
                "AND TRIM(COALESCE(error_message, '')) = '' "
                "AND (TRIM(COALESCE(caption, '')) <> '' "
                "OR COALESCE(tags_json, '') NOT IN ('', '[]') "
                "OR COALESCE(objects_json, '') NOT IN ('', '[]') "
                "OR TRIM(COALESCE(actions, '')) <> '' "
                "OR TRIM(COALESCE(setting_text, '')) <> '')"))) {
            if (errorMessage) {
                *errorMessage = successBackfill.lastError().text();
            }
            return rollback();
        }

        QSqlQuery failureBackfill(db);
        if (!failureBackfill.exec(QStringLiteral(
                "UPDATE video_frame_analysis SET analysis_state = 2 "
                "WHERE COALESCE(analysis_state, 0) = 0 "
                "AND TRIM(COALESCE(error_message, '')) <> ''"))) {
            if (errorMessage) {
                *errorMessage = failureBackfill.lastError().text();
            }
            return rollback();
        }

        if (!setSchemaVersion(db, 7, errorMessage)) {
            return rollback();
        }
        version = 7;
    }
    if (version < 8) {
        if (!migrateToVersion8(db, errorMessage)) {
            return rollback();
        }
        version = 8;
    } else if (!ensureFolderSchemaCompatibility(db, errorMessage)) {
        return rollback();
    }
    if (version < 9) {
        if (!migrateToVersion9(db, errorMessage)) {
            return rollback();
        }
        version = 9;
    } else if (!ensureVisualAnalysisSchemaCompatibility(db, errorMessage)) {
        return rollback();
    }
    if (version < 10) {
        if (!migrateToVersion10(db, errorMessage)) {
            return rollback();
        }
        version = 10;
    } else if (!ensureSemanticSearchSchemaCompatibility(db, errorMessage)) {
        return rollback();
    }
    if (version < 11) {
        if (!migrateToVersion11(db, errorMessage)) {
            return rollback();
        }
        version = 11;
    } else if (!ensureCaptureTimeSchemaCompatibility(db, errorMessage)) {
        return rollback();
    }
    if (version < 12) {
        if (!migrateToVersion12(db, errorMessage)) {
            return rollback();
        }
        version = 12;
    }
    if (version < 13) {
        if (!migrateToVersion13(db, errorMessage)) {
            return rollback();
        }
        version = 13;
    }
    if (version < 14) {
        if (!migrateToVersion14(db, errorMessage)) {
            return rollback();
        }
        version = 14;
    } else if (!ensureCatalogGenerationSchemaCompatibility(db, errorMessage)) {
        return rollback();
    }

    if (!db.commit()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("提交全局数据库迁移失败：%1").arg(db.lastError().text());
        }
        return rollback();
    }
    return true;
}

bool GlobalDatabaseManager::createSchema(QSqlDatabase &db, QString *errorMessage)
{
    const QStringList statements = {
        QStringLiteral("CREATE TABLE IF NOT EXISTS schema_version (version INTEGER NOT NULL);"),
        QStringLiteral("INSERT INTO schema_version(version) SELECT 1 WHERE NOT EXISTS (SELECT 1 FROM schema_version);"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS project_registry ("
                       "project_uuid TEXT PRIMARY KEY,"
                       "project_name TEXT NOT NULL,"
                       "project_database_path TEXT NOT NULL,"
                       "last_synced_at TEXT,"
                       "sync_status TEXT NOT NULL DEFAULT 'pending',"
                       "active_sync_generation INTEGER NOT NULL DEFAULT 0,"
                       "error_message TEXT"
                       ");"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS global_video_asset ("
                       "video_key TEXT PRIMARY KEY,"
                       "project_uuid TEXT NOT NULL,"
                       "project_name TEXT NOT NULL,"
                       "project_database_path TEXT NOT NULL,"
                       "source_root_id INTEGER NOT NULL DEFAULT 0,"
                       "source_root_name TEXT NOT NULL DEFAULT '',"
                       "folder_key TEXT NOT NULL DEFAULT '',"
                       "is_available INTEGER NOT NULL DEFAULT 1,"
                       "asset_id INTEGER NOT NULL,"
                       "file_name TEXT NOT NULL,"
                       "extension TEXT NOT NULL DEFAULT '',"
                       "absolute_path TEXT NOT NULL,"
                       "relative_path TEXT NOT NULL,"
                       "asset_type INTEGER NOT NULL DEFAULT 1,"
                       "size_bytes INTEGER NOT NULL DEFAULT 0,"
                       "modified_at TEXT NOT NULL DEFAULT '',"
                       "capture_time TEXT NOT NULL DEFAULT '',"
                       "capture_date TEXT NOT NULL DEFAULT '',"
                       "capture_time_source TEXT NOT NULL DEFAULT '',"
                       "capture_time_confidence REAL NOT NULL DEFAULT 0,"
                       "duration_ms INTEGER NOT NULL DEFAULT 0,"
                       "thumbnail_path TEXT,"
                       "thumbnail_status INTEGER NOT NULL DEFAULT 0,"
                       "analysis_status INTEGER NOT NULL DEFAULT 0,"
                       "confirmation_status INTEGER NOT NULL DEFAULT 0,"
                       "technical_summary TEXT NOT NULL DEFAULT '',"
                       "embedded_metadata_text TEXT NOT NULL DEFAULT '',"
                       "source_text TEXT NOT NULL DEFAULT '',"
                       "error_message TEXT,"
                       "sync_generation INTEGER NOT NULL DEFAULT 0,"
                       "last_synced_at TEXT NOT NULL DEFAULT '',"
                       "updated_at TEXT NOT NULL DEFAULT '',"
                       "FOREIGN KEY(project_uuid) REFERENCES project_registry(project_uuid) ON DELETE CASCADE"
                       ");"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS global_folder_node ("
                       "folder_key TEXT PRIMARY KEY,"
                       "project_uuid TEXT NOT NULL,"
                       "project_name TEXT NOT NULL DEFAULT '',"
                       "project_database_path TEXT NOT NULL DEFAULT '',"
                       "source_root_id INTEGER NOT NULL DEFAULT 0,"
                       "source_root_name TEXT NOT NULL DEFAULT '',"
                       "source_root_path TEXT NOT NULL DEFAULT '',"
                       "source_root_path_key TEXT NOT NULL DEFAULT '',"
                       "folder_id INTEGER NOT NULL DEFAULT 0,"
                       "name TEXT NOT NULL DEFAULT '',"
                       "absolute_path TEXT NOT NULL DEFAULT '',"
                       "path_key TEXT NOT NULL DEFAULT '',"
                       "relative_path TEXT NOT NULL DEFAULT '',"
                       "parent_relative_path TEXT NOT NULL DEFAULT '',"
                       "depth INTEGER NOT NULL DEFAULT 0,"
                       "direct_file_count INTEGER NOT NULL DEFAULT 0,"
                       "recursive_file_count INTEGER NOT NULL DEFAULT 0,"
                       "normalized_date TEXT NOT NULL DEFAULT '',"
                       "date_anchor TEXT NOT NULL DEFAULT '',"
                       "is_available INTEGER NOT NULL DEFAULT 1,"
                       "sync_generation INTEGER NOT NULL DEFAULT 0,"
                       "last_synced_at TEXT NOT NULL DEFAULT '',"
                       "updated_at TEXT NOT NULL DEFAULT '',"
                       "FOREIGN KEY(project_uuid) REFERENCES project_registry(project_uuid) ON DELETE CASCADE"
                       ");"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS video_analysis_result ("
                       "video_key TEXT PRIMARY KEY,"
                       "summary TEXT,"
                       "keywords_json TEXT,"
                       "scenes_json TEXT,"
                       "search_text TEXT,"
                       "model_name TEXT,"
                       "prompt_version TEXT,"
                       "analyzed_at TEXT,"
                       "confirmed_at TEXT,"
                       "FOREIGN KEY(video_key) REFERENCES global_video_asset(video_key) ON DELETE CASCADE"
                       ");"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS video_frame_analysis ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "video_key TEXT NOT NULL,"
                       "frame_number INTEGER NOT NULL DEFAULT 0,"
                       "timestamp_ms INTEGER NOT NULL DEFAULT 0,"
                       "image_path TEXT,"
                       "caption TEXT,"
                       "tags_json TEXT,"
                       "objects_json TEXT,"
                       "actions TEXT,"
                       "setting_text TEXT,"
                       "entities_json TEXT NOT NULL DEFAULT '[]',"
                       "ocr_text TEXT NOT NULL DEFAULT '',"
                       "ocr_blocks_json TEXT NOT NULL DEFAULT '[]',"
                       "structured_profile_version INTEGER NOT NULL DEFAULT 1,"
                       "facts_complete INTEGER NOT NULL DEFAULT 0,"
                       "model_name TEXT NOT NULL DEFAULT '',"
                       "prompt_version TEXT NOT NULL DEFAULT '',"
                       "analyzed_at TEXT NOT NULL DEFAULT '',"
                       "error_message TEXT,"
                       "analysis_state INTEGER NOT NULL DEFAULT 0,"
                       "retry_count INTEGER NOT NULL DEFAULT 0,"
                       "last_http_status INTEGER NOT NULL DEFAULT 0,"
                       "last_attempt_at TEXT NOT NULL DEFAULT '',"
                       "FOREIGN KEY(video_key) REFERENCES global_video_asset(video_key) ON DELETE CASCADE"
                       ");"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS video_analysis_plan ("
                       "video_key TEXT PRIMARY KEY,"
                       "sampling_policy TEXT NOT NULL DEFAULT 'fixed_interval',"
                       "frame_interval INTEGER NOT NULL DEFAULT 1,"
                       "structured_profile_version INTEGER NOT NULL DEFAULT 1,"
                       "source_frame_count INTEGER NOT NULL DEFAULT 0,"
                       "planned_frame_count INTEGER NOT NULL DEFAULT 0,"
                       "asset_size_bytes INTEGER NOT NULL DEFAULT 0,"
                       "asset_modified_at TEXT NOT NULL DEFAULT '',"
                       "created_at TEXT NOT NULL DEFAULT '',"
                       "updated_at TEXT NOT NULL DEFAULT '',"
                       "FOREIGN KEY(video_key) REFERENCES global_video_asset(video_key) ON DELETE CASCADE"
                       ");"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS video_analysis_task ("
                       "video_key TEXT PRIMARY KEY,"
                       "stage INTEGER NOT NULL DEFAULT 0,"
                       "total_frames INTEGER NOT NULL DEFAULT 0,"
                       "completed_frames INTEGER NOT NULL DEFAULT 0,"
                       "successful_frames INTEGER NOT NULL DEFAULT 0,"
                       "skipped_frames INTEGER NOT NULL DEFAULT 0,"
                       "summary_retry_count INTEGER NOT NULL DEFAULT 0,"
                       "last_error_message TEXT NOT NULL DEFAULT '',"
                       "last_updated_at TEXT NOT NULL DEFAULT '',"
                       "FOREIGN KEY(video_key) REFERENCES global_video_asset(video_key) ON DELETE CASCADE"
                       ");"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS material_dimension_analysis ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "video_key TEXT NOT NULL,"
                       "dimension_key TEXT NOT NULL,"
                       "dimension_name TEXT NOT NULL,"
                       "detail TEXT NOT NULL DEFAULT '',"
                       "model_name TEXT NOT NULL DEFAULT '',"
                       "prompt_version TEXT NOT NULL DEFAULT '',"
                       "analyzed_at TEXT NOT NULL DEFAULT '',"
                       "FOREIGN KEY(video_key) REFERENCES global_video_asset(video_key) ON DELETE CASCADE,"
                       "UNIQUE(video_key, dimension_key)"
                       ");"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS material_dimension_frame_analysis ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "video_key TEXT NOT NULL,"
                       "dimension_key TEXT NOT NULL,"
                       "dimension_name TEXT NOT NULL,"
                       "frame_number INTEGER NOT NULL DEFAULT 0,"
                       "timestamp_ms INTEGER NOT NULL DEFAULT 0,"
                       "image_path TEXT NOT NULL DEFAULT '',"
                       "detail TEXT NOT NULL DEFAULT '',"
                       "error_message TEXT NOT NULL DEFAULT '',"
                       "analysis_state INTEGER NOT NULL DEFAULT 0,"
                       "model_name TEXT NOT NULL DEFAULT '',"
                       "prompt_version TEXT NOT NULL DEFAULT '',"
                       "analyzed_at TEXT NOT NULL DEFAULT '',"
                       "FOREIGN KEY(video_key) REFERENCES global_video_asset(video_key) ON DELETE CASCADE,"
                       "UNIQUE(video_key, dimension_key, frame_number)"
                       ");"),
        createSearchDocumentTableStatement(),
        createSearchIndexStateTableStatement(),
        QStringLiteral("INSERT INTO search_index_state(singleton) "
                       "SELECT 1 WHERE NOT EXISTS ("
                       "SELECT 1 FROM search_index_state WHERE singleton = 1);"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_global_video_project ON global_video_asset(project_uuid);"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_global_video_source ON global_video_asset(source_root_name);"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_global_video_status ON global_video_asset(analysis_status, confirmation_status);"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_frame_video_key ON video_frame_analysis(video_key);"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_material_dimension_video ON material_dimension_analysis(video_key);"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_material_dimension_frame_video ON material_dimension_frame_analysis(video_key, dimension_key, analysis_state);")
    };

    if (!executeBatch(db, statements, errorMessage)) {
        return false;
    }

    return ensureSearchFtsSchema(db, &m_hasFts5, errorMessage);
}

bool GlobalDatabaseManager::ensureSchemaCompatibility(QSqlDatabase &db, QString *errorMessage)
{
    const QStringList statements = {
        QStringLiteral("CREATE TABLE IF NOT EXISTS video_analysis_task ("
                       "video_key TEXT PRIMARY KEY,"
                       "stage INTEGER NOT NULL DEFAULT 0,"
                       "total_frames INTEGER NOT NULL DEFAULT 0,"
                       "completed_frames INTEGER NOT NULL DEFAULT 0,"
                       "successful_frames INTEGER NOT NULL DEFAULT 0,"
                       "skipped_frames INTEGER NOT NULL DEFAULT 0,"
                       "summary_retry_count INTEGER NOT NULL DEFAULT 0,"
                       "last_error_message TEXT NOT NULL DEFAULT '',"
                       "last_updated_at TEXT NOT NULL DEFAULT '',"
                       "FOREIGN KEY(video_key) REFERENCES global_video_asset(video_key) ON DELETE CASCADE"
                       ");"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS material_dimension_analysis ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "video_key TEXT NOT NULL,"
                       "dimension_key TEXT NOT NULL,"
                       "dimension_name TEXT NOT NULL,"
                       "detail TEXT NOT NULL DEFAULT '',"
                       "model_name TEXT NOT NULL DEFAULT '',"
                       "prompt_version TEXT NOT NULL DEFAULT '',"
                       "analyzed_at TEXT NOT NULL DEFAULT '',"
                       "FOREIGN KEY(video_key) REFERENCES global_video_asset(video_key) ON DELETE CASCADE,"
                       "UNIQUE(video_key, dimension_key)"
                       ");"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS material_dimension_frame_analysis ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "video_key TEXT NOT NULL,"
                       "dimension_key TEXT NOT NULL,"
                       "dimension_name TEXT NOT NULL,"
                       "frame_number INTEGER NOT NULL DEFAULT 0,"
                       "timestamp_ms INTEGER NOT NULL DEFAULT 0,"
                       "image_path TEXT NOT NULL DEFAULT '',"
                       "detail TEXT NOT NULL DEFAULT '',"
                       "error_message TEXT NOT NULL DEFAULT '',"
                       "analysis_state INTEGER NOT NULL DEFAULT 0,"
                       "model_name TEXT NOT NULL DEFAULT '',"
                       "prompt_version TEXT NOT NULL DEFAULT '',"
                       "analyzed_at TEXT NOT NULL DEFAULT '',"
                       "FOREIGN KEY(video_key) REFERENCES global_video_asset(video_key) ON DELETE CASCADE,"
                       "UNIQUE(video_key, dimension_key, frame_number)"
                       ");")
    };
    if (!executeBatch(db, statements, errorMessage)) {
        return false;
    }

    if (!ensureColumns(db,
                       QStringLiteral("global_video_asset"),
                       {
                           {QStringLiteral("extension"), QStringLiteral("extension TEXT NOT NULL DEFAULT ''")},
                           {QStringLiteral("asset_type"), QStringLiteral("asset_type INTEGER NOT NULL DEFAULT 1")},
                           {QStringLiteral("thumbnail_status"), QStringLiteral("thumbnail_status INTEGER NOT NULL DEFAULT 0")},
                           {QStringLiteral("technical_summary"), QStringLiteral("technical_summary TEXT NOT NULL DEFAULT ''")},
                           {QStringLiteral("embedded_metadata_text"), QStringLiteral("embedded_metadata_text TEXT NOT NULL DEFAULT ''")},
                           {QStringLiteral("source_text"), QStringLiteral("source_text TEXT NOT NULL DEFAULT ''")},
                           {QStringLiteral("capture_time"), QStringLiteral("capture_time TEXT NOT NULL DEFAULT ''")},
                           {QStringLiteral("capture_date"), QStringLiteral("capture_date TEXT NOT NULL DEFAULT ''")},
                           {QStringLiteral("capture_time_source"), QStringLiteral("capture_time_source TEXT NOT NULL DEFAULT ''")},
                           {QStringLiteral("capture_time_confidence"), QStringLiteral("capture_time_confidence REAL NOT NULL DEFAULT 0")}
                       },
                       errorMessage)) {
        return false;
    }

    if (!ensureColumns(db,
                       QStringLiteral("video_frame_analysis"),
                       {
                           {QStringLiteral("analysis_state"), QStringLiteral("analysis_state INTEGER NOT NULL DEFAULT 0")},
                           {QStringLiteral("retry_count"), QStringLiteral("retry_count INTEGER NOT NULL DEFAULT 0")},
                           {QStringLiteral("last_http_status"), QStringLiteral("last_http_status INTEGER NOT NULL DEFAULT 0")},
                           {QStringLiteral("last_attempt_at"), QStringLiteral("last_attempt_at TEXT NOT NULL DEFAULT ''")}
                       },
                       errorMessage)) {
        return false;
    }

    if (!ensureColumns(db,
                       QStringLiteral("video_analysis_task"),
                       {
                           {QStringLiteral("stage"), QStringLiteral("stage INTEGER NOT NULL DEFAULT 0")},
                           {QStringLiteral("total_frames"), QStringLiteral("total_frames INTEGER NOT NULL DEFAULT 0")},
                           {QStringLiteral("completed_frames"), QStringLiteral("completed_frames INTEGER NOT NULL DEFAULT 0")},
                           {QStringLiteral("successful_frames"), QStringLiteral("successful_frames INTEGER NOT NULL DEFAULT 0")},
                           {QStringLiteral("skipped_frames"), QStringLiteral("skipped_frames INTEGER NOT NULL DEFAULT 0")},
                           {QStringLiteral("summary_retry_count"), QStringLiteral("summary_retry_count INTEGER NOT NULL DEFAULT 0")},
                           {QStringLiteral("last_error_message"), QStringLiteral("last_error_message TEXT NOT NULL DEFAULT ''")},
                           {QStringLiteral("last_updated_at"), QStringLiteral("last_updated_at TEXT NOT NULL DEFAULT ''")}
                       },
                       errorMessage)) {
        return false;
    }

    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_frame_video_state ON video_frame_analysis(video_key, analysis_state, frame_number);"))) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }
    if (!query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_global_video_asset_type ON global_video_asset(asset_type);"))) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }
    if (!query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_material_dimension_video ON material_dimension_analysis(video_key);"))) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }
    if (!query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_material_dimension_frame_video ON material_dimension_frame_analysis(video_key, dimension_key, analysis_state);"))) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }

    if (!ensureSearchFtsSchema(db, &m_hasFts5, errorMessage)) {
        return false;
    }

    return true;
}

bool GlobalDatabaseManager::migrateToVersion8(QSqlDatabase &db, QString *errorMessage)
{
    if (!ensureFolderSchemaCompatibility(db, errorMessage)) {
        return false;
    }
    return setSchemaVersion(db, 8, errorMessage);
}

bool GlobalDatabaseManager::migrateToVersion9(QSqlDatabase &db, QString *errorMessage)
{
    if (!ensureVisualAnalysisSchemaCompatibility(db, errorMessage)) {
        return false;
    }
    return setSchemaVersion(db, 9, errorMessage);
}

bool GlobalDatabaseManager::migrateToVersion10(QSqlDatabase &db, QString *errorMessage)
{
    if (!ensureSemanticSearchSchemaCompatibility(db, errorMessage)) {
        return false;
    }
    return setSchemaVersion(db, 10, errorMessage);
}

bool GlobalDatabaseManager::migrateToVersion11(QSqlDatabase &db, QString *errorMessage)
{
    if (!ensureCaptureTimeSchemaCompatibility(db, errorMessage)) {
        return false;
    }
    return setSchemaVersion(db, 11, errorMessage);
}

bool GlobalDatabaseManager::migrateToVersion12(QSqlDatabase &db, QString *errorMessage)
{
    const auto confirmed = static_cast<int>(ConfirmationStatus::Confirmed);
    const auto ready = static_cast<int>(VideoAnalysisStatus::Ready);

    QSqlQuery assets(db);
    assets.prepare(QStringLiteral(
        "UPDATE global_video_asset SET confirmation_status = ?, updated_at = ? "
        "WHERE analysis_status = ? AND confirmation_status <> ?"));
    assets.addBindValue(confirmed);
    assets.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    assets.addBindValue(ready);
    assets.addBindValue(confirmed);
    if (!assets.exec()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("迁移历史解析结果为自动生效失败：%1")
                                .arg(assets.lastError().text());
        }
        return false;
    }

    QSqlQuery results(db);
    results.prepare(QStringLiteral(
        "UPDATE video_analysis_result SET confirmed_at = COALESCE(NULLIF(confirmed_at, ''), analyzed_at) "
        "WHERE video_key IN (SELECT video_key FROM global_video_asset WHERE analysis_status = ?)"));
    results.addBindValue(ready);
    if (!results.exec()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("回填历史解析生效时间失败：%1")
                                .arg(results.lastError().text());
        }
        return false;
    }
    return setSchemaVersion(db, 12, errorMessage);
}

bool GlobalDatabaseManager::migrateToVersion13(QSqlDatabase &db, QString *errorMessage)
{
    if (!ensureColumns(db,
                       QStringLiteral("global_video_asset"),
                       {{QStringLiteral("embedded_metadata_text"),
                         QStringLiteral("embedded_metadata_text TEXT NOT NULL DEFAULT ''")}},
                       errorMessage)) {
        return false;
    }

    QSqlQuery invalidate(db);
    if (!invalidate.exec(QStringLiteral(
            "UPDATE search_index_state SET status = 'dirty', last_error = '', updated_at = '' "
            "WHERE singleton = 1"))) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("标记真实元数据搜索索引待重建失败：%1")
                                .arg(invalidate.lastError().text());
        }
        return false;
    }
    return setSchemaVersion(db, 13, errorMessage);
}

bool GlobalDatabaseManager::migrateToVersion14(QSqlDatabase &db, QString *errorMessage)
{
    if (!ensureCatalogGenerationSchemaCompatibility(db, errorMessage)) {
        return false;
    }
    if (!executeBatch(db,
                      {QStringLiteral("UPDATE global_video_asset SET sync_generation = 1 "
                                      "WHERE sync_generation = 0;"),
                       QStringLiteral("UPDATE global_folder_node SET sync_generation = 1 "
                                      "WHERE sync_generation = 0;"),
                       QStringLiteral("UPDATE project_registry SET active_sync_generation = 1 "
                                      "WHERE active_sync_generation = 0 AND ("
                                      "EXISTS (SELECT 1 FROM global_video_asset a "
                                      "WHERE a.project_uuid = project_registry.project_uuid) OR "
                                      "EXISTS (SELECT 1 FROM global_folder_node f "
                                      "WHERE f.project_uuid = project_registry.project_uuid));")},
                      errorMessage)) {
        return false;
    }
    return setSchemaVersion(db, 14, errorMessage);
}

bool GlobalDatabaseManager::ensureCatalogGenerationSchemaCompatibility(
    QSqlDatabase &db,
    QString *errorMessage)
{
    if (!ensureColumns(db,
                       QStringLiteral("project_registry"),
                       {{QStringLiteral("active_sync_generation"),
                         QStringLiteral("active_sync_generation INTEGER NOT NULL DEFAULT 0")}},
                       errorMessage)
        || !ensureColumns(db,
                          QStringLiteral("global_video_asset"),
                          {{QStringLiteral("sync_generation"),
                            QStringLiteral("sync_generation INTEGER NOT NULL DEFAULT 0")}},
                          errorMessage)
        || !ensureColumns(db,
                          QStringLiteral("global_folder_node"),
                          {{QStringLiteral("sync_generation"),
                            QStringLiteral("sync_generation INTEGER NOT NULL DEFAULT 0")}},
                          errorMessage)) {
        return false;
    }
    return executeBatch(
        db,
        {QStringLiteral("CREATE INDEX IF NOT EXISTS idx_global_video_project_generation "
                        "ON global_video_asset(project_uuid, sync_generation);"),
         QStringLiteral("CREATE INDEX IF NOT EXISTS idx_global_folder_project_generation "
                        "ON global_folder_node(project_uuid, sync_generation);"),
         QStringLiteral("CREATE INDEX IF NOT EXISTS idx_global_video_project_path "
                        "ON global_video_asset(project_uuid, absolute_path COLLATE NOCASE);")},
        errorMessage);
}

bool GlobalDatabaseManager::ensureCaptureTimeSchemaCompatibility(QSqlDatabase &db,
                                                                 QString *errorMessage)
{
    if (!ensureColumns(db,
                       QStringLiteral("global_video_asset"),
                       {
                           {QStringLiteral("capture_time"), QStringLiteral("capture_time TEXT NOT NULL DEFAULT ''")},
                           {QStringLiteral("capture_date"), QStringLiteral("capture_date TEXT NOT NULL DEFAULT ''")},
                           {QStringLiteral("capture_time_source"), QStringLiteral("capture_time_source TEXT NOT NULL DEFAULT ''")},
                           {QStringLiteral("capture_time_confidence"), QStringLiteral("capture_time_confidence REAL NOT NULL DEFAULT 0")}
                       },
                       errorMessage)) {
        return false;
    }
    return executeBatch(db,
                        {
                            QStringLiteral("CREATE INDEX IF NOT EXISTS idx_global_video_asset_capture_date ON global_video_asset(capture_date)"),
                            QStringLiteral("CREATE INDEX IF NOT EXISTS idx_global_video_asset_type_capture_date ON global_video_asset(asset_type, capture_date)")
                        },
                        errorMessage);
}

bool GlobalDatabaseManager::ensureSemanticSearchSchemaCompatibility(QSqlDatabase &db,
                                                                    QString *errorMessage)
{
    if (!executeBatch(db,
                      {createSearchDocumentTableStatement(),
                       createSearchIndexStateTableStatement()},
                      errorMessage)) {
        return false;
    }

    auto documentColumns = tableColumns(db, QStringLiteral("search_document"), errorMessage);
    auto stateColumns = tableColumns(db, QStringLiteral("search_index_state"), errorMessage);
    bool schemaRepaired = false;

    if (!documentColumns.contains(QStringLiteral("id"), Qt::CaseInsensitive)
        || !documentColumns.contains(QStringLiteral("document_key"), Qt::CaseInsensitive)) {
        if (!executeBatch(db,
                          {QStringLiteral("DROP TABLE search_document;"),
                           createSearchDocumentTableStatement()},
                          errorMessage)) {
            return false;
        }
        schemaRepaired = true;
        documentColumns = tableColumns(db, QStringLiteral("search_document"), errorMessage);
    }
    if (!stateColumns.contains(QStringLiteral("singleton"), Qt::CaseInsensitive)) {
        if (!executeBatch(db,
                          {QStringLiteral("DROP TABLE search_index_state;"),
                           createSearchIndexStateTableStatement()},
                          errorMessage)) {
            return false;
        }
        schemaRepaired = true;
        stateColumns = tableColumns(db, QStringLiteral("search_index_state"), errorMessage);
    }

    const QList<QPair<QString, QString>> documentColumnDefinitions{
        {QStringLiteral("document_type"), QStringLiteral("document_type INTEGER NOT NULL DEFAULT 0")},
        {QStringLiteral("entity_key"), QStringLiteral("entity_key TEXT NOT NULL DEFAULT ''")},
        {QStringLiteral("content_hash"), QStringLiteral("content_hash TEXT NOT NULL DEFAULT ''")},
        {QStringLiteral("content_text"), QStringLiteral("content_text TEXT NOT NULL DEFAULT ''")},
        {QStringLiteral("source_updated_at"), QStringLiteral("source_updated_at TEXT NOT NULL DEFAULT ''")},
        {QStringLiteral("model_id"), QStringLiteral("model_id TEXT NOT NULL DEFAULT ''")},
        {QStringLiteral("index_schema_version"), QStringLiteral("index_schema_version INTEGER NOT NULL DEFAULT 0")},
        {QStringLiteral("indexed_at"), QStringLiteral("indexed_at TEXT NOT NULL DEFAULT ''")}
    };
    const QList<QPair<QString, QString>> stateColumnDefinitions{
        {QStringLiteral("schema_version"), QStringLiteral("schema_version INTEGER NOT NULL DEFAULT 0")},
        {QStringLiteral("model_id"), QStringLiteral("model_id TEXT NOT NULL DEFAULT ''")},
        {QStringLiteral("dimensions"), QStringLiteral("dimensions INTEGER NOT NULL DEFAULT 0")},
        {QStringLiteral("generation"), QStringLiteral("generation INTEGER NOT NULL DEFAULT 0")},
        {QStringLiteral("status"), QStringLiteral("status TEXT NOT NULL DEFAULT 'dirty'")},
        {QStringLiteral("document_count"), QStringLiteral("document_count INTEGER NOT NULL DEFAULT 0")},
        {QStringLiteral("updated_at"), QStringLiteral("updated_at TEXT NOT NULL DEFAULT ''")},
        {QStringLiteral("last_error"), QStringLiteral("last_error TEXT NOT NULL DEFAULT ''")}
    };
    for (const auto &column : documentColumnDefinitions) {
        if (!documentColumns.contains(column.first, Qt::CaseInsensitive)) {
            schemaRepaired = true;
        }
    }
    for (const auto &column : stateColumnDefinitions) {
        if (!stateColumns.contains(column.first, Qt::CaseInsensitive)) {
            schemaRepaired = true;
        }
    }
    if (!ensureColumns(db,
                       QStringLiteral("search_document"),
                       documentColumnDefinitions,
                       errorMessage)
        || !ensureColumns(db,
                          QStringLiteral("search_index_state"),
                          stateColumnDefinitions,
                          errorMessage)) {
        return false;
    }

    QSqlQuery normalizeKeys(db);
    if (!normalizeKeys.exec(QStringLiteral(
            "UPDATE search_document SET document_key = 'legacy:' || id "
            "WHERE TRIM(COALESCE(document_key, '')) = ''"))) {
        if (errorMessage) *errorMessage = normalizeKeys.lastError().text();
        return false;
    }
    schemaRepaired = schemaRepaired || normalizeKeys.numRowsAffected() > 0;

    QSqlQuery deduplicateDocuments(db);
    if (!deduplicateDocuments.exec(QStringLiteral(
            "DELETE FROM search_document WHERE id NOT IN ("
            "SELECT MIN(id) FROM search_document GROUP BY document_key)"))) {
        if (errorMessage) *errorMessage = deduplicateDocuments.lastError().text();
        return false;
    }
    schemaRepaired = schemaRepaired || deduplicateDocuments.numRowsAffected() > 0;

    QSqlQuery normalizeState(db);
    if (!normalizeState.exec(QStringLiteral(
            "DELETE FROM search_index_state WHERE singleton <> 1 OR rowid NOT IN ("
            "SELECT MIN(rowid) FROM search_index_state WHERE singleton = 1)"))) {
        if (errorMessage) *errorMessage = normalizeState.lastError().text();
        return false;
    }
    schemaRepaired = schemaRepaired || normalizeState.numRowsAffected() > 0;

    if (!executeBatch(db,
                      {QStringLiteral("INSERT INTO search_index_state(singleton) "
                                      "SELECT 1 WHERE NOT EXISTS ("
                                      "SELECT 1 FROM search_index_state WHERE singleton = 1);"),
                       QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_search_document_key "
                                      "ON search_document(document_key);"),
                       QStringLiteral("CREATE INDEX IF NOT EXISTS idx_search_document_type "
                                      "ON search_document(document_type, entity_key);"),
                       QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_search_index_state_singleton "
                                      "ON search_index_state(singleton);")},
                      errorMessage)) {
        return false;
    }

    if (schemaRepaired) {
        QSqlQuery invalidateState(db);
        if (!invalidateState.exec(QStringLiteral(
                "UPDATE search_index_state SET schema_version = 0, model_id = '', dimensions = 0, "
                "generation = 0, status = 'dirty', "
                "document_count = (SELECT COUNT(*) FROM search_document), "
                "updated_at = '', last_error = '' WHERE singleton = 1"))) {
            if (errorMessage) *errorMessage = invalidateState.lastError().text();
            return false;
        }
    }
    return true;
}

bool GlobalDatabaseManager::ensureVisualAnalysisSchemaCompatibility(QSqlDatabase &db, QString *errorMessage)
{
    const QStringList statements = {
        QStringLiteral("CREATE TABLE IF NOT EXISTS video_analysis_plan ("
                       "video_key TEXT PRIMARY KEY,"
                       "sampling_policy TEXT NOT NULL DEFAULT 'fixed_interval',"
                       "frame_interval INTEGER NOT NULL DEFAULT 1,"
                       "structured_profile_version INTEGER NOT NULL DEFAULT 1,"
                       "source_frame_count INTEGER NOT NULL DEFAULT 0,"
                       "planned_frame_count INTEGER NOT NULL DEFAULT 0,"
                       "asset_size_bytes INTEGER NOT NULL DEFAULT 0,"
                       "asset_modified_at TEXT NOT NULL DEFAULT '',"
                       "created_at TEXT NOT NULL DEFAULT '',"
                       "updated_at TEXT NOT NULL DEFAULT '',"
                       "FOREIGN KEY(video_key) REFERENCES global_video_asset(video_key) ON DELETE CASCADE"
                       ");")
    };
    if (!executeBatch(db, statements, errorMessage)) {
        return false;
    }
    if (!ensureColumns(db,
                       QStringLiteral("video_frame_analysis"),
                       {
                           {QStringLiteral("entities_json"), QStringLiteral("entities_json TEXT NOT NULL DEFAULT '[]'")},
                           {QStringLiteral("ocr_text"), QStringLiteral("ocr_text TEXT NOT NULL DEFAULT ''")},
                           {QStringLiteral("ocr_blocks_json"), QStringLiteral("ocr_blocks_json TEXT NOT NULL DEFAULT '[]'")},
                           {QStringLiteral("structured_profile_version"), QStringLiteral("structured_profile_version INTEGER NOT NULL DEFAULT 1")},
                           {QStringLiteral("facts_complete"), QStringLiteral("facts_complete INTEGER NOT NULL DEFAULT 0")},
                           {QStringLiteral("model_name"), QStringLiteral("model_name TEXT NOT NULL DEFAULT ''")},
                           {QStringLiteral("prompt_version"), QStringLiteral("prompt_version TEXT NOT NULL DEFAULT ''")},
                           {QStringLiteral("analyzed_at"), QStringLiteral("analyzed_at TEXT NOT NULL DEFAULT ''")}
                       },
                       errorMessage)) {
        return false;
    }

    QSqlQuery query(db);
    if (!query.exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_frame_visual_completeness "
            "ON video_frame_analysis(video_key, facts_complete, structured_profile_version, frame_number)"))) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }
    return true;
}

bool GlobalDatabaseManager::ensureFolderSchemaCompatibility(QSqlDatabase &db, QString *errorMessage)
{
    const QStringList statements = {
        QStringLiteral("CREATE TABLE IF NOT EXISTS global_folder_node ("
                       "folder_key TEXT PRIMARY KEY,"
                       "project_uuid TEXT NOT NULL,"
                       "project_name TEXT NOT NULL DEFAULT '',"
                       "project_database_path TEXT NOT NULL DEFAULT '',"
                       "source_root_id INTEGER NOT NULL DEFAULT 0,"
                       "source_root_name TEXT NOT NULL DEFAULT '',"
                       "source_root_path TEXT NOT NULL DEFAULT '',"
                       "source_root_path_key TEXT NOT NULL DEFAULT '',"
                       "folder_id INTEGER NOT NULL DEFAULT 0,"
                       "name TEXT NOT NULL DEFAULT '',"
                       "absolute_path TEXT NOT NULL DEFAULT '',"
                       "path_key TEXT NOT NULL DEFAULT '',"
                       "relative_path TEXT NOT NULL DEFAULT '',"
                       "parent_relative_path TEXT NOT NULL DEFAULT '',"
                       "depth INTEGER NOT NULL DEFAULT 0,"
                       "direct_file_count INTEGER NOT NULL DEFAULT 0,"
                       "recursive_file_count INTEGER NOT NULL DEFAULT 0,"
                       "normalized_date TEXT NOT NULL DEFAULT '',"
                       "date_anchor TEXT NOT NULL DEFAULT '',"
                       "is_available INTEGER NOT NULL DEFAULT 1,"
                       "last_synced_at TEXT NOT NULL DEFAULT '',"
                       "updated_at TEXT NOT NULL DEFAULT '',"
                       "FOREIGN KEY(project_uuid) REFERENCES project_registry(project_uuid) ON DELETE CASCADE"
                       ");")
    };
    if (!executeBatch(db, statements, errorMessage)) {
        return false;
    }
    if (!ensureColumns(db,
                       QStringLiteral("global_folder_node"),
                       {
                           {QStringLiteral("project_name"), QStringLiteral("project_name TEXT NOT NULL DEFAULT ''")},
                           {QStringLiteral("project_database_path"), QStringLiteral("project_database_path TEXT NOT NULL DEFAULT ''")},
                           {QStringLiteral("source_root_path"), QStringLiteral("source_root_path TEXT NOT NULL DEFAULT ''")},
                           {QStringLiteral("source_root_path_key"), QStringLiteral("source_root_path_key TEXT NOT NULL DEFAULT ''")},
                           {QStringLiteral("folder_id"), QStringLiteral("folder_id INTEGER NOT NULL DEFAULT 0")},
                           {QStringLiteral("name"), QStringLiteral("name TEXT NOT NULL DEFAULT ''")},
                           {QStringLiteral("absolute_path"), QStringLiteral("absolute_path TEXT NOT NULL DEFAULT ''")},
                           {QStringLiteral("path_key"), QStringLiteral("path_key TEXT NOT NULL DEFAULT ''")},
                           {QStringLiteral("relative_path"), QStringLiteral("relative_path TEXT NOT NULL DEFAULT ''")},
                           {QStringLiteral("parent_relative_path"), QStringLiteral("parent_relative_path TEXT NOT NULL DEFAULT ''")},
                           {QStringLiteral("depth"), QStringLiteral("depth INTEGER NOT NULL DEFAULT 0")},
                           {QStringLiteral("direct_file_count"), QStringLiteral("direct_file_count INTEGER NOT NULL DEFAULT 0")},
                           {QStringLiteral("recursive_file_count"), QStringLiteral("recursive_file_count INTEGER NOT NULL DEFAULT 0")},
                           {QStringLiteral("normalized_date"), QStringLiteral("normalized_date TEXT NOT NULL DEFAULT ''")},
                           {QStringLiteral("date_anchor"), QStringLiteral("date_anchor TEXT NOT NULL DEFAULT ''")},
                           {QStringLiteral("is_available"), QStringLiteral("is_available INTEGER NOT NULL DEFAULT 1")},
                           {QStringLiteral("last_synced_at"), QStringLiteral("last_synced_at TEXT NOT NULL DEFAULT ''")},
                           {QStringLiteral("updated_at"), QStringLiteral("updated_at TEXT NOT NULL DEFAULT ''")}
                       },
                       errorMessage)
        || !ensureColumns(db,
                          QStringLiteral("global_video_asset"),
                          {
                              {QStringLiteral("folder_key"), QStringLiteral("folder_key TEXT NOT NULL DEFAULT ''")},
                              {QStringLiteral("is_available"), QStringLiteral("is_available INTEGER NOT NULL DEFAULT 1")}
                          },
                          errorMessage)) {
        return false;
    }

    if (!executeBatch(db,
                      {
                          QStringLiteral("CREATE INDEX IF NOT EXISTS idx_global_folder_project ON global_folder_node(project_uuid);"),
                          QStringLiteral("CREATE INDEX IF NOT EXISTS idx_global_folder_source ON global_folder_node(project_uuid, source_root_id, depth);"),
                          QStringLiteral("CREATE INDEX IF NOT EXISTS idx_global_folder_parent ON global_folder_node(project_uuid, source_root_id, parent_relative_path, depth);"),
                          QStringLiteral("CREATE INDEX IF NOT EXISTS idx_global_folder_path_key ON global_folder_node(path_key);"),
                          QStringLiteral("CREATE INDEX IF NOT EXISTS idx_global_folder_date ON global_folder_node(normalized_date, date_anchor);"),
                          QStringLiteral("CREATE INDEX IF NOT EXISTS idx_global_video_folder ON global_video_asset(folder_key);"),
                          QStringLiteral("CREATE INDEX IF NOT EXISTS idx_global_video_available ON global_video_asset(is_available);")
                      },
                      errorMessage)) {
        return false;
    }

    QSqlQuery cleanupFolders(db);
    if (!cleanupFolders.exec(QStringLiteral(
            "DELETE FROM global_folder_node WHERE NOT EXISTS ("
            "SELECT 1 FROM project_registry pr WHERE pr.project_uuid = global_folder_node.project_uuid)"))) {
        if (errorMessage) {
            *errorMessage = cleanupFolders.lastError().text();
        }
        return false;
    }

    QSqlQuery cleanupAssetLinks(db);
    if (!cleanupAssetLinks.exec(QStringLiteral(
            "UPDATE global_video_asset SET folder_key = '' WHERE folder_key <> '' AND NOT EXISTS ("
            "SELECT 1 FROM global_folder_node gf WHERE gf.folder_key = global_video_asset.folder_key)"))) {
        if (errorMessage) {
            *errorMessage = cleanupAssetLinks.lastError().text();
        }
        return false;
    }
    return true;
}

int GlobalDatabaseManager::currentSchemaVersion(QSqlDatabase &db) const
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("SELECT version FROM schema_version LIMIT 1"))) {
        return 0;
    }
    return query.next() ? query.value(0).toInt() : 0;
}

bool GlobalDatabaseManager::setSchemaVersion(QSqlDatabase &db, int version, QString *errorMessage) const
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("DELETE FROM schema_version"))) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }

    query.prepare(QStringLiteral("INSERT INTO schema_version(version) VALUES (?)"));
    query.addBindValue(version);
    if (!query.exec()) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }
    return true;
}
