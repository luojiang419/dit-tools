#include "application/JobService.h"

JobService::JobService(JobEngine *jobEngine, QObject *parent)
    : QObject(parent)
    , m_jobEngine(jobEngine)
{
}

JobEngine *JobService::engine() const
{
    return m_jobEngine;
}
