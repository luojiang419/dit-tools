#include "infrastructure/db/GlobalDatabaseManager.h"

#include "shared/Paths.h"

#include <QDateTime>
#include <QDir>
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
    , m_connectionName(QStringLiteral("cinevault_global"))
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

    auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(m_databaseFilePath);
    if (!db.open()) {
        if (errorMessage) {
            *errorMessage = db.lastError().text();
        }
        closeDatabase();
        return false;
    }

    if (!initializeSchema(db, errorMessage)) {
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

bool GlobalDatabaseManager::initializeSchema(QSqlDatabase &db, QString *errorMessage)
{
    if (!createSchema(db, errorMessage)) {
        return false;
    }

    if (!ensureSchemaCompatibility(db, errorMessage)) {
        return false;
    }

    auto version = currentSchemaVersion(db);
    if (version < 6) {
        if (!setSchemaVersion(db, 6, errorMessage)) {
            return false;
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
            return false;
        }

        QSqlQuery failureBackfill(db);
        if (!failureBackfill.exec(QStringLiteral(
                "UPDATE video_frame_analysis SET analysis_state = 2 "
                "WHERE COALESCE(analysis_state, 0) = 0 "
                "AND TRIM(COALESCE(error_message, '')) <> ''"))) {
            if (errorMessage) {
                *errorMessage = failureBackfill.lastError().text();
            }
            return false;
        }

        if (!setSchemaVersion(db, 7, errorMessage)) {
            return false;
        }
    }
    return true;
}

bool GlobalDatabaseManager::createSchema(QSqlDatabase &db, QString *errorMessage)
{
    const QStringList statements = {
        QStringLiteral("PRAGMA journal_mode=WAL;"),
        QStringLiteral("PRAGMA synchronous=NORMAL;"),
        QStringLiteral("PRAGMA foreign_keys=ON;"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS schema_version (version INTEGER NOT NULL);"),
        QStringLiteral("INSERT INTO schema_version(version) SELECT 1 WHERE NOT EXISTS (SELECT 1 FROM schema_version);"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS project_registry ("
                       "project_uuid TEXT PRIMARY KEY,"
                       "project_name TEXT NOT NULL,"
                       "project_database_path TEXT NOT NULL,"
                       "last_synced_at TEXT,"
                       "sync_status TEXT NOT NULL DEFAULT 'pending',"
                       "error_message TEXT"
                       ");"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS global_video_asset ("
                       "video_key TEXT PRIMARY KEY,"
                       "project_uuid TEXT NOT NULL,"
                       "project_name TEXT NOT NULL,"
                       "project_database_path TEXT NOT NULL,"
                       "source_root_id INTEGER NOT NULL DEFAULT 0,"
                       "source_root_name TEXT NOT NULL DEFAULT '',"
                       "asset_id INTEGER NOT NULL,"
                       "file_name TEXT NOT NULL,"
                       "extension TEXT NOT NULL DEFAULT '',"
                       "absolute_path TEXT NOT NULL,"
                       "relative_path TEXT NOT NULL,"
                       "asset_type INTEGER NOT NULL DEFAULT 1,"
                       "size_bytes INTEGER NOT NULL DEFAULT 0,"
                       "modified_at TEXT NOT NULL DEFAULT '',"
                       "duration_ms INTEGER NOT NULL DEFAULT 0,"
                       "thumbnail_path TEXT,"
                       "thumbnail_status INTEGER NOT NULL DEFAULT 0,"
                       "analysis_status INTEGER NOT NULL DEFAULT 0,"
                       "confirmation_status INTEGER NOT NULL DEFAULT 0,"
                       "technical_summary TEXT NOT NULL DEFAULT '',"
                       "source_text TEXT NOT NULL DEFAULT '',"
                       "error_message TEXT,"
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
                       "error_message TEXT,"
                       "analysis_state INTEGER NOT NULL DEFAULT 0,"
                       "retry_count INTEGER NOT NULL DEFAULT 0,"
                       "last_http_status INTEGER NOT NULL DEFAULT 0,"
                       "last_attempt_at TEXT NOT NULL DEFAULT '',"
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
                           {QStringLiteral("source_text"), QStringLiteral("source_text TEXT NOT NULL DEFAULT ''")}
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
