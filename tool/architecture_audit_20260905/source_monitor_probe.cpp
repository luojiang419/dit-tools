// Uses the existing testing seam; no OS watchers or user files are touched.
#include "application/SourceChangeMonitor.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 2) return 2;
    SourceChangeMonitor monitor;
    QJsonArray batches;
    QObject::connect(&monitor, &SourceChangeMonitor::sourceChangesDetected,
        [&](const SourceChangeBatch &batch) {
            batches.append(QJsonObject{{"source_root", batch.sourcePath},
                {"changed_paths", QJsonArray::fromStringList(batch.changedPaths)},
                {"overflowed", batch.overflowed}});
            if (batches.size() == 2) app.quit();
        });
    monitor.recordChangesForTesting(1, "G:/", {"G:/DCIM/sample.mov"});
    monitor.recordChangesForTesting(2, "G:/DCIM", {"G:/DCIM/sample.mov"});
    QTimer::singleShot(4000, &app, &QCoreApplication::quit);
    app.exec();
    if (batches.size() != 2) return 3;
    QFile output(QString::fromLocal8Bit(argv[1]));
    if (!output.open(QIODevice::WriteOnly)) return 4;
    const auto bytes = QJsonDocument(QJsonObject{
        {"scope", "current SourceChangeMonitor with synthetic injected Windows paths; no filesystem writes"},
        {"batches", batches}}).toJson(QJsonDocument::Indented);
    return output.write(bytes) == bytes.size() ? 0 : 5;
}
