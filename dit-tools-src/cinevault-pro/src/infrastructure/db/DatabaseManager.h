#pragma once

#include <QObject>
#include <QString>

class QSqlDatabase;

class DatabaseManager : public QObject {
    Q_OBJECT

public:
    explicit DatabaseManager(QObject *parent = nullptr);

    static constexpr int CurrentSchemaVersion = 6;

    bool openProjectDatabase(const QString &databaseFilePath, QString *errorMessage);
    void closeProjectDatabase();
    bool hasOpenProject() const;
    QString databaseFilePath() const;
    int schemaVersion() const;
    QSqlDatabase database() const;
    QSqlDatabase openThreadConnection(const QString &connectionName, QString *errorMessage) const;
    QSqlDatabase openThreadConnectionForPath(const QString &databaseFilePath,
                                             const QString &connectionName,
                                             QString *errorMessage) const;
    void closeThreadConnection(const QString &connectionName) const;

private:
    bool initializeSchema(QSqlDatabase &db, bool databaseExistedBeforeOpen, QString *errorMessage);
    bool createBaseSchema(QSqlDatabase &db, QString *errorMessage) const;
    bool ensureBaseSchemaCompatibility(QSqlDatabase &db, QString *errorMessage) const;
    bool migrateToVersion2(QSqlDatabase &db, QString *errorMessage) const;
    bool ensureMediaSchemaCompatibility(QSqlDatabase &db, QString *errorMessage) const;
    bool migrateToVersion3(QSqlDatabase &db, QString *errorMessage) const;
    bool ensureFolderSchemaCompatibility(QSqlDatabase &db, QString *errorMessage) const;
    bool backfillFolderHierarchy(QSqlDatabase &db, QString *errorMessage) const;
    bool migrateToVersion4(QSqlDatabase &db, QString *errorMessage) const;
    bool ensureAtomicScanSchemaCompatibility(QSqlDatabase &db, QString *errorMessage) const;
    bool backfillAssetPathKeys(QSqlDatabase &db, QString *errorMessage) const;
    bool migrateToVersion5(QSqlDatabase &db, QString *errorMessage) const;
    bool ensureEmbeddedMetadataSchemaCompatibility(QSqlDatabase &db, QString *errorMessage) const;
    bool migrateToVersion6(QSqlDatabase &db, QString *errorMessage) const;
    bool ensureResumableScanSchemaCompatibility(QSqlDatabase &db, QString *errorMessage) const;
    int currentSchemaVersion(QSqlDatabase &db) const;
    bool setSchemaVersion(QSqlDatabase &db, int version, QString *errorMessage) const;

    QString m_mainConnectionName;
    QString m_databaseFilePath;
    int m_schemaVersion = 0;
};
