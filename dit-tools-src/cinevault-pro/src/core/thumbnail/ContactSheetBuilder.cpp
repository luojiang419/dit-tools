#include "core/thumbnail/ContactSheetBuilder.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QtMath>
#include <limits>

namespace {
constexpr int kTileSize = 160;
constexpr int kMaxFrameCount = 64;
constexpr qreal kTargetGridRatio = 1.5;

QStringList validImagePaths(const QStringList &imagePaths)
{
    QStringList validPaths;
    for (const auto &path : imagePaths) {
        const QFileInfo info(path.trimmed());
        if (info.exists() && info.isFile()) {
            validPaths.append(info.absoluteFilePath());
        }
    }
    return validPaths;
}

QStringList evenlySampledPaths(const QStringList &imagePaths, int frameCount)
{
    const auto validPaths = validImagePaths(imagePaths);
    if (validPaths.isEmpty()) {
        return {};
    }

    const auto targetCount = qMin(qBound(1, frameCount, kMaxFrameCount), validPaths.size());
    if (targetCount == validPaths.size()) {
        return validPaths;
    }

    QStringList sampled;
    sampled.reserve(targetCount);
    for (int index = 0; index < targetCount; ++index) {
        const auto sourceIndex = targetCount == 1
            ? 0
            : qRound((static_cast<double>(validPaths.size() - 1) * index) / (targetCount - 1));
        sampled.append(validPaths.at(qBound(0, sourceIndex, validPaths.size() - 1)));
    }
    return sampled;
}

QSize gridSizeForCount(int count)
{
    int bestColumns = count;
    int bestRows = 1;
    qreal bestScore = std::numeric_limits<qreal>::max();

    for (int rows = 1; rows <= count; ++rows) {
        const int columns = qCeil(static_cast<qreal>(count) / rows);
        const int emptyCells = columns * rows - count;
        const qreal ratio = static_cast<qreal>(columns) / rows;
        const qreal score = qAbs(ratio - kTargetGridRatio) + emptyCells * 0.1;
        if (score < bestScore) {
            bestScore = score;
            bestColumns = columns;
            bestRows = rows;
        }
    }

    return QSize(bestColumns, bestRows);
}
}

bool ContactSheetBuilder::build(const QStringList &imagePaths, int frameCount, const QString &outputPath, QString *errorMessage)
{
    const auto sampledPaths = evenlySampledPaths(imagePaths, frameCount);
    if (sampledPaths.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("没有可用于生成多宫格拼图的解析帧图片");
        }
        return false;
    }

    const QFileInfo outputInfo(outputPath.trimmed());
    if (outputInfo.absolutePath().trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("多宫格拼图输出路径无效");
        }
        return false;
    }
    QDir dir;
    if (!dir.mkpath(outputInfo.absolutePath())) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法创建多宫格拼图目录：%1").arg(outputInfo.absolutePath());
        }
        return false;
    }

    const auto gridSize = gridSizeForCount(sampledPaths.size());
    QImage sheet(gridSize.width() * kTileSize, gridSize.height() * kTileSize, QImage::Format_RGB32);
    sheet.fill(QColor(12, 16, 24));

    QPainter painter(&sheet);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setPen(QPen(QColor(12, 16, 24), 2));

    for (int index = 0; index < sampledPaths.size(); ++index) {
        const QImage frame(sampledPaths.at(index));
        if (frame.isNull()) {
            continue;
        }

        const int column = index % gridSize.width();
        const int row = index / gridSize.width();
        const QRect targetRect(column * kTileSize, row * kTileSize, kTileSize, kTileSize);
        const auto scaled = frame.scaled(kTileSize, kTileSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        const QRect sourceRect((scaled.width() - kTileSize) / 2,
                               (scaled.height() - kTileSize) / 2,
                               kTileSize,
                               kTileSize);
        painter.drawImage(targetRect, scaled, sourceRect);
        painter.drawRect(targetRect.adjusted(0, 0, -1, -1));
    }
    painter.end();

    if (!sheet.save(outputInfo.absoluteFilePath(), "JPG", 88)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("保存多宫格拼图失败：%1").arg(outputInfo.absoluteFilePath());
        }
        return false;
    }
    return true;
}
