#pragma once

#include "domain/Entities.h"
#include "domain/SearchTypes.h"

#include <QDate>
#include <QObject>
#include <QVariantList>

class GlobalDatabaseManager;
class SearchEngine;

class MaterialCenterQueryService : public QObject {
    Q_OBJECT

public:
    explicit MaterialCenterQueryService(GlobalDatabaseManager *globalDatabaseManager,
                                        SearchEngine *searchEngine,
                                        QObject *parent = nullptr);

    QString databaseFilePath() const;

    QVariantList fetchProjectOptions() const;
    QVariantList fetchSourceOptions(const QString &projectUuid) const;
    QVariantList fetchAssetTypeOptions() const;
    QVector<GlobalVideoAsset> fetchAssets(const QString &keyword,
                                          const QString &projectUuid,
                                          const QString &sourceName,
                                          int analysisStatusFilter,
                                          int confirmationStatusFilter,
                                          int assetTypeFilter = -1) const;
    MaterialSearchResult searchMaterials(const QString &naturalLanguageQuery,
                                         const MaterialSearchScope &scope = {},
                                         const QDate &referenceDate = QDate::currentDate(),
                                         const ModelSearchUnderstanding *modelUnderstanding = nullptr) const;
    VideoAnalysisDetail fetchDetail(const QString &videoKey) const;
    VideoAnalysisDetailPage fetchDetailPage(const QString &videoKey,
                                            int frameLimit,
                                            int afterFrameNumber = 0,
                                            int preferredFrameNumber = -1) const;

private:
    GlobalDatabaseManager *m_globalDatabaseManager = nullptr;
    SearchEngine *m_searchEngine = nullptr;
};
