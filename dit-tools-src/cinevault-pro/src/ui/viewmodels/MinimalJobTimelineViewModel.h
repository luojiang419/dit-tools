#pragma once

#include <QObject>
#include <QVariantList>
#include <QVector>

#include "domain/Entities.h"

class JobListModel;

class MinimalJobTimelineViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(JobListModel* model READ model CONSTANT)
    Q_PROPERTY(QVariantList timelineItems READ timelineItems NOTIFY timelineChanged)
    Q_PROPERTY(bool canClearCompletedJobs READ canClearCompletedJobs NOTIFY timelineChanged)

public:
    explicit MinimalJobTimelineViewModel(QObject *parent = nullptr);

    JobListModel *model() const;
    QVariantList timelineItems() const;
    bool canClearCompletedJobs() const;

public slots:
    void reload();
    Q_INVOKABLE void clearCompletedJobs();

signals:
    void timelineChanged();

private:
    void seedJobs();

    JobListModel *m_model = nullptr;
    QVector<Job> m_jobs;
    QVariantList m_timelineItems;
};
