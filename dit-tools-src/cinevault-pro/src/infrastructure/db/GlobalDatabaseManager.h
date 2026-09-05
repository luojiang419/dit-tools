#pragma once

#include <QObject>
#include <QString>

class QSqlDatabase;

class GlobalDatabaseManager : public QObject {
    Q_OBJECT

public:
    explicit GlobalDatabaseManager(QObject *parent = nullptr);

    static constexpr int CurrentSchemaVersion = 14;

    bool openDatabase(QString *errorMessage);
    bool openReadOnlyDatabase(const QString &databaseFilePath, QString *errorMessage);
    void closeDatabase();
    bool isOpen() const;
    bool hasFts5() const;
    QString databaseFilePath() const;
    QSqlDatabase database() const;
    QSqlDatabase openThreadConnection(const QString &connectionName, QString *errorMessage) const;
    void closeThreadConnection(const QString &connectionName) const;
    bool updateProjectReference(const QString &projectUuid,
                                const QString &projectName,
                                const QString &oldDatabasePath,
                                const QString &newDatabasePath,
                                QString *errorMessage);
    bool removeProjectReference(const QString &projectUuid, const QString &databasePath, QString *errorMessage);

private:
    bool initializeSchema(QSqlDatabase &db, bool databaseExistedBeforeOpen, QString *errorMessage);
    bool createSchema(QSqlDatabase &db, QString *errorMessage);
    bool ensureSchemaCompatibility(QSqlDatabase &db, QString *errorMessage);
    bool migrateToVersion8(QSqlDatabase &db, QString *errorMessage);
    bool migrateToVersion9(QSqlDatabase &db, QString *errorMessage);
    bool migrateToVersion10(QSqlDatabase &db, QString *errorMessage);
    bool migrateToVersion11(QSqlDatabase &db, QString *errorMessage);
    bool migrateToVersion12(QSqlDatabase &db, QString *errorMessage);
    bool migrateToVersion13(QSqlDatabase &db, QString *errorMessage);
    bool migrateToVersion14(QSqlDatabase &db, QString *errorMessage);
    bool ensureFolderSchemaCompatibility(QSqlDatabase &db, QString *errorMessage);
    bool ensureVisualAnalysisSchemaCompatibility(QSqlDatabase &db, QString *errorMessage);
    bool ensureSemanticSearchSchemaCompatibility(QSqlDatabase &db, QString *errorMessage);
    bool ensureCaptureTimeSchemaCompatibility(QSqlDatabase &db, QString *errorMessage);
    bool ensureCatalogGenerationSchemaCompatibility(QSqlDatabase &db, QString *errorMessage);
    int currentSchemaVersion(QSqlDatabase &db) const;
    bool setSchemaVersion(QSqlDatabase &db, int version, QString *errorMessage) const;

    QString m_connectionName;
    QString m_databaseFilePath;
    bool m_hasFts5 = false;
};
