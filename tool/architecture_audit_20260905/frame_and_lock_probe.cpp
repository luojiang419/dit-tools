// Diagnostic evidence, not a product correctness test. Links the current adapter unchanged.
#include "infrastructure/ffmpeg/FFmpegAdapter.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>
#include <stdexcept>

QByteArray run(const QString &program, const QStringList &arguments, bool stderrResult = false)
{
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForStarted(5000) || !process.waitForFinished(30000))
        throw std::runtime_error("fixture process failed or timed out");
    const auto error = process.readAllStandardError();
    if (process.exitCode() != 0 || process.exitStatus() != QProcess::NormalExit)
        throw std::runtime_error(error.constData());
    return stderrResult ? error : process.readAllStandardOutput();
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 2) return 2;
    try {
        const auto ffmpeg = QString::fromLocal8Bit(qgetenv("CINEVAULT_FFMPEG_PATH"));
        QTemporaryDir fixture;
        if (!fixture.isValid()) return 3;
        QJsonObject report{{"created_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                           {"qt_version", qVersion()},
                           {"ffmpeg_version", QString::fromUtf8(run(ffmpeg, {"-version"})).section('\n', 0, 0)},
                           {"scope", "isolated current-source adapter and SQLite event-loop probes; not live app reproduction"}};
        QJsonArray results;
        const auto base = QStringLiteral("color=c=gray:size=256x128:rate=10,drawgrid=w=16:h=128:t=1:c=black");
        auto generate = [&](const QString &name, const QString &filter, int count) {
            const auto path = fixture.filePath(name + ".mkv");
            run(ffmpeg, {"-y", "-v", "error", "-f", "lavfi", "-i", filter,
                         "-frames:v", QString::number(count), "-c:v", "ffv1", path});
            return path;
        };
        const auto stationary = generate("stationary", base, 73);
        const auto returning = generate("returning",
            "nullsrc=size=256x128:rate=10,geq=lum='if(between(N,10,19),if(lt(Y,H/2),64,192),if(lt(X,W/2),64,192))':cb=128:cr=128", 30);
        const auto shortVideo = generate("short", base, 1);
        FFmpegAdapter adapter;
        auto probe = [&](const QString &name, const QString &path, VideoFrameExtractionStrategy strategy,
                         double interval, qint64 lastPts, double minimumSharpness = 0.01) {
            FrameExtractionRequest request;
            request.sourcePath = path;
            request.outputDirectory = fixture.filePath(name);
            request.strategy = strategy;
            request.intervalSeconds = interval;
            request.maxWidth = 64;
            request.maxHeight = 64;
            request.minimumSharpness = minimumSharpness;
            QElapsedTimer elapsed;
            elapsed.start();
            const auto result = adapter.extractFrames(request);
            QJsonArray timestamps;
            for (const auto &frame : result.frames) timestamps.append(frame.timestampMs);
            QJsonObject row{{"case", name}, {"success", result.success}, {"error", result.errorMessage},
                            {"source_frames", result.sourceFrameCount}, {"source_last_pts_ms", lastPts},
                            {"retained_frames", static_cast<int>(result.frames.size())},
                            {"reported_timestamps_ms", timestamps}, {"elapsed_ms", elapsed.elapsed()},
                            {"minimum_sharpness", minimumSharpness}};
            if (!result.frames.isEmpty()) {
                const QImage image(result.frames.first().imagePath);
                row.insert("requested_max_size", "64x64");
                row.insert("actual_first_size", QString("%1x%2").arg(image.width()).arg(image.height()));
                row.insert("reported_tail_gap_ms", lastPts - result.frames.last().timestampMs);
            }
            results.append(row);
        };
        probe("stationary_scene_interval", stationary, VideoFrameExtractionStrategy::SceneAndInterval, 2.0, 7200);
        probe("stationary_per_frame", stationary, VideoFrameExtractionStrategy::PerFrame, 2.0, 7200);
        // Disable only sharpness for this case to isolate cross-time duplicate removal.
        probe("returning_A_B_A", returning, VideoFrameExtractionStrategy::PerFrame, 1.0, 2900, 0.0);
        probe("stationary_interval", stationary, VideoFrameExtractionStrategy::IntervalOnly, 0.5, 7200);
        probe("short_interval_larger_than_duration", shortVideo, VideoFrameExtractionStrategy::IntervalOnly, 2.0, 0);
        const auto showinfo = run(ffmpeg, {"-hide_banner", "-i", stationary, "-vf",
            "select=eq(n\\,0)+gt(scene\\,0.35)+gte(t-prev_selected_t\\,2.000),showinfo",
            "-fps_mode", "vfr", "-f", "null", "-"}, true);
        QJsonArray candidatePts;
        QRegularExpression expression("pts_time:([0-9]+(?:\\.[0-9]+)?)");
        auto matches = expression.globalMatch(QString::fromUtf8(showinfo));
        while (matches.hasNext()) candidatePts.append(qRound64(matches.next().captured(1).toDouble() * 1000));
        report.insert("raw_scene_filter_candidate_pts_ms", candidatePts);
        const auto shortInfo = run(ffmpeg, {"-hide_banner", "-i", shortVideo,
            "-vf", "fps=1/2,showinfo", "-f", "null", "-"}, true);
        report.insert("raw_short_interval_filter_has_frame", shortInfo.contains("pts_time:"));
        report.insert("frame_probes", results);

        // Same-thread SQL write under another connection's WAL write transaction.
        // This isolates why a synchronous UI progress write can stall its event loop.
        auto writer = QSqlDatabase::addDatabase("QSQLITE", "audit-writer");
        auto ui = QSqlDatabase::addDatabase("QSQLITE", "audit-ui");
        writer.setDatabaseName(fixture.filePath("lock.sqlite"));
        ui.setDatabaseName(writer.databaseName());
        if (!writer.open() || !ui.open()) throw std::runtime_error("SQLite open failed");
        QSqlQuery w(writer), q(ui);
        if (!w.exec("PRAGMA journal_mode=WAL") || !w.exec("CREATE TABLE item(id INTEGER PRIMARY KEY, progress INTEGER)")
            || !w.exec("INSERT INTO item VALUES(1,0)") || !q.exec("PRAGMA busy_timeout=5000")
            || !writer.transaction() || !w.exec("UPDATE item SET progress=1"))
            throw std::runtime_error("SQLite fixture setup failed");
        QTimer heartbeat;
        heartbeat.setInterval(20);
        int heartbeats = 0;
        QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++heartbeats; });
        heartbeat.start();
        QElapsedTimer clock;
        clock.start();
        const bool written = q.exec("UPDATE item SET progress=2");
        report.insert("synchronous_sql_busy_probe", QJsonObject{
            {"write_success", written}, {"error", q.lastError().text()},
            {"blocked_ms", clock.elapsed()}, {"heartbeats_during_write", heartbeats},
            {"busy_timeout_ms", 5000}});
        writer.rollback();
        ui.close();
        writer.close();
        QFile output(QString::fromLocal8Bit(argv[1]));
        if (!output.open(QIODevice::WriteOnly)) return 4;
        const auto bytes = QJsonDocument(report).toJson(QJsonDocument::Indented);
        if (output.write(bytes) != bytes.size()) return 5;
        std::fwrite(bytes.constData(), 1, bytes.size(), stdout);
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    return 0;
}
