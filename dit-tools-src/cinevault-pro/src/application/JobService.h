#pragma once

#include <QObject>

class JobEngine;

class JobService : public QObject {
    Q_OBJECT

public:
    explicit JobService(JobEngine *jobEngine, QObject *parent = nullptr);

    JobEngine *engine() const;

private:
    JobEngine *m_jobEngine = nullptr;
};
