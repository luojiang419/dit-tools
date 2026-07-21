#pragma once

#include "domain/Entities.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>

#include <memory>
#include <vector>

struct SourceChangeBatch {
    qint64 sourceRootId = 0;
    QString sourcePath;
    QStringList changedPaths;
    bool overflowed = false;
};

Q_DECLARE_METATYPE(SourceChangeBatch)

class SourceChangeMonitor final : public QObject {
    Q_OBJECT

public:
    explicit SourceChangeMonitor(QObject *parent = nullptr);
    ~SourceChangeMonitor() override;

    void setSourceRoots(const QVector<SourceRoot> &sourceRoots);
    void stop();
    [[nodiscard]] int watchedSourceCount() const;
#ifdef CINEVAULT_TESTING
    void recordChangesForTesting(qint64 sourceRootId,
                                 const QString &sourcePath,
                                 const QStringList &changedPaths,
                                 bool overflowed = false);
#endif

signals:
    void sourceChangesDetected(const SourceChangeBatch &batch);
    void sourceChanged(qint64 sourceRootId, const QString &sourcePath);
    void sourceUnavailable(qint64 sourceRootId,
                           const QString &sourcePath,
                           const QString &message);

private:
    struct WatchRegistration;
    struct PendingChanges {
        QString sourcePath;
        QSet<QString> changedPaths;
        bool overflowed = false;
    };

    void postChanges(qint64 sourceRootId,
                     const QString &sourcePath,
                     const QStringList &changedPaths,
                     bool overflowed);
    void flushPendingChanges();
    void postUnavailable(qint64 sourceRootId,
                         const QString &sourcePath,
                         const QString &message);

    std::vector<std::unique_ptr<WatchRegistration>> m_watches;
    QHash<qint64, PendingChanges> m_pendingChanges;
    QTimer m_debounceTimer;
};
