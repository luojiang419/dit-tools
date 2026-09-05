#include "core/report/ReportRenderEngine.h"

#include "shared/Formatters.h"

#include <QFileInfo>
#include <QFontDatabase>
#include <QImage>
#include <QDir>
#include <QMap>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSize>
#include <QStringList>
#include <QVector>

#include <algorithm>

namespace {
constexpr qreal kHeaderHeight = 82.0;
constexpr qreal kFooterHeight = 34.0;
constexpr qreal kSectionGap = 18.0;

QString valueOrUnset(const QString &value)
{
    const auto trimmed = value.trimmed();
    return trimmed.isEmpty() ? QStringLiteral("未填写") : trimmed;
}

QString fileExtensionLabel(const ReportAssetRow &asset)
{
    auto extension = asset.extension.trimmed();
    if (extension.isEmpty()) {
        extension = QFileInfo(asset.name).suffix().trimmed();
    }
    if (extension.isEmpty() && !asset.container.isEmpty()) {
        extension = asset.container.section(QLatin1Char(','), 0, 0).trimmed();
    }
    return extension.isEmpty() ? QStringLiteral("-") : extension.toUpper();
}

QString fileDirectoryLabel(const ReportAssetRow &asset)
{
    if (!asset.parentPath.trimmed().isEmpty()) {
        return asset.parentPath;
    }
    if (!asset.absolutePath.trimmed().isEmpty()) {
        return QFileInfo(asset.absolutePath).absolutePath();
    }
    return QStringLiteral("-");
}

QString selectedFontFamily()
{
    const auto families = QFontDatabase::families();
    const QStringList candidates = {
        QStringLiteral("Microsoft YaHei UI"),
        QStringLiteral("Microsoft YaHei"),
        QStringLiteral("SimHei"),
        QStringLiteral("SimSun"),
        QStringLiteral("Noto Sans CJK SC"),
        QStringLiteral("Arial Unicode MS")
    };

    for (const auto &candidate : candidates) {
        if (families.contains(candidate, Qt::CaseInsensitive)) {
            return candidate;
        }
    }
    return QFont().defaultFamily();
}

QString assetTypeName(AssetType type)
{
    return Formatters::assetTypeLabel(type);
}

QString streamSummary(const ReportAssetRow &asset, const QString &kind)
{
    QStringList lines;
    for (const auto &stream : asset.streams) {
        if (!kind.isEmpty() && stream.kind != kind) {
            continue;
        }

        QStringList parts;
        if (!stream.codec.isEmpty()) {
            parts.append(stream.codec);
        }
        if (stream.width > 0 && stream.height > 0) {
            parts.append(QStringLiteral("%1x%2").arg(stream.width).arg(stream.height));
        }
        if (stream.sampleRate > 0) {
            parts.append(QStringLiteral("%1 Hz").arg(stream.sampleRate));
        }
        if (stream.channels > 0) {
            parts.append(QStringLiteral("%1 声道").arg(stream.channels));
        }
        if (stream.bitRate > 0) {
            parts.append(Formatters::formatBitRate(stream.bitRate));
        }
        if (!parts.isEmpty()) {
            lines.append(parts.join(QStringLiteral(" / ")));
        }
    }
    return lines.join(QStringLiteral("\n"));
}

QString technicalSummary(const ReportAssetRow &asset)
{
    QStringList parts;
    parts.append(QStringLiteral("封装：%1").arg(fileExtensionLabel(asset)));
    if (asset.durationMs > 0) {
        parts.append(QStringLiteral("时长：%1").arg(Formatters::formatDuration(asset.durationMs)));
    }
    if (asset.bitRate > 0) {
        parts.append(QStringLiteral("码率：%1").arg(Formatters::formatBitRate(asset.bitRate)));
    }

    for (const auto &stream : asset.streams) {
        if (stream.kind != QStringLiteral("video")) {
            continue;
        }
        if (!stream.codec.isEmpty()) {
            parts.append(QStringLiteral("编码：%1").arg(stream.codec));
        }
        if (stream.width > 0 && stream.height > 0) {
            parts.append(QStringLiteral("分辨率：%1X%2").arg(stream.width).arg(stream.height));
        }
        break;
    }
    if (!asset.metadataError.isEmpty()) {
        parts.append(QStringLiteral("异常：%1").arg(asset.metadataError));
    }
    if (parts.isEmpty()) {
        parts.append(Formatters::probeStatusLabel(asset.probeStatus));
    }
    return parts.join(QStringLiteral("\n"));
}

QVector<ReportAssetRow> assetsByType(const ReportDocument &document, AssetType type)
{
    QVector<ReportAssetRow> rows;
    for (const auto &asset : document.assets) {
        if (asset.assetType == type) {
            rows.append(asset);
        }
    }
    return rows;
}

QVector<QPair<QString, int>> extensionDistribution(const ReportDocument &document)
{
    QMap<QString, int> counts;
    for (const auto &asset : document.assets) {
        auto key = asset.extension.trimmed().toUpper();
        if (key.isEmpty()) {
            key = QStringLiteral("无扩展名");
        }
        counts[key] += 1;
    }

    QVector<QPair<QString, int>> rows;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        rows.append({it.key(), it.value()});
    }
    std::sort(rows.begin(), rows.end(), [](const auto &left, const auto &right) {
        if (left.second == right.second) {
            return left.first < right.first;
        }
        return left.second > right.second;
    });
    if (rows.size() > 10) {
        rows.resize(10);
    }
    return rows;
}

class PdfRenderer {
public:
    PdfRenderer(const ReportDocument &document, QPdfWriter &writer, QPainter &painter)
        : m_document(document)
        , m_mode(OutputMode::Pdf)
        , m_writer(&writer)
        , m_painter(painter)
        , m_fontFamily(selectedFontFamily())
    {
        updatePageRect();
    }

    PdfRenderer(const ReportDocument &document, const QString &previewDirectory)
        : m_document(document)
        , m_mode(OutputMode::PreviewImages)
        , m_previewDirectory(previewDirectory)
        , m_painter(m_previewPainter)
        , m_fontFamily(selectedFontFamily())
    {
        startPreviewPage();
        updatePageRect();
    }

    bool render(QString *errorMessage)
    {
        configurePainter();
        if (!m_painter.isActive()) {
            if (errorMessage) {
                *errorMessage = m_mode == OutputMode::Pdf
                    ? QStringLiteral("PDF 绘制器未启动。")
                    : QStringLiteral("预览页绘制器未启动。");
            }
            return false;
        }

        bool hasContent = false;
        if (m_document.sections.cover) {
            drawCover();
            hasContent = true;
        }

        const auto drawSection = [this, &hasContent](const QString &title, const auto &draw) {
            if (hasContent) {
                nextPage(title);
            } else {
                m_currentTitle = title;
                drawHeader(title);
            }
            draw();
            hasContent = true;
        };

        if (m_document.sections.summary) {
            drawSection(QStringLiteral("项目摘要"), [this]() { drawSummary(); });
        }
        if (m_document.sections.sourceOverview) {
            drawSection(QStringLiteral("素材源与扫描概览"), [this]() { drawSourceTable(); });
        }
        if (m_document.sections.formatDistribution) {
            drawSection(QStringLiteral("格式分布"), [this]() { drawExtensionDistribution(); });
        }
        if (m_document.sections.thumbnailIndex) {
            drawSection(QStringLiteral("视频缩略图索引"), [this]() { drawThumbnailIndex(); });
        }
        if (m_document.sections.videoMetadata) {
            drawSection(QStringLiteral("视频元数据明细"), [this]() { drawVideoMetadata(); });
        }
        if (m_document.sections.audioMetadata) {
            drawSection(QStringLiteral("音频元数据明细"), [this]() { drawAudioMetadata(); });
        }
        if (m_document.sections.folderTree) {
            drawSection(QStringLiteral("项目文件夹结构树状图"), [this]() { drawFolderTree(); });
        }
        if (!hasContent) {
            m_currentTitle = QStringLiteral("报表内容");
            drawHeader(m_currentTitle);
            drawEmptyBlock(QStringLiteral("未选择任何报表项，请返回报表页面勾选需要导出的内容。"));
        }
        drawFooter();
        if (!finishPage() || m_failed) {
            if (errorMessage) {
                *errorMessage = m_errorMessage;
            }
            return false;
        }
        return true;
    }

    QStringList previewPagePaths() const
    {
        return m_previewPagePaths;
    }

private:
    enum class OutputMode {
        Pdf,
        PreviewImages
    };

    void configurePainter()
    {
        m_painter.setRenderHint(QPainter::Antialiasing, true);
        m_painter.setRenderHint(QPainter::TextAntialiasing, true);
    }

    QFont font(qreal pointSize, QFont::Weight weight = QFont::Normal) const
    {
        QFont result(m_fontFamily);
        result.setPointSizeF(pointSize);
        result.setWeight(weight);
        return result;
    }

    void updatePageRect()
    {
        if (m_mode == OutputMode::PreviewImages) {
            m_pageRect = QRectF(68, 68, m_previewPageSize.width() - 136, m_previewPageSize.height() - 136);
        } else {
            m_pageRect = QRectF(m_writer->pageLayout().paintRectPixels(m_writer->resolution()));
        }
        if (!m_pageRect.isValid() || m_pageRect.width() <= 0 || m_pageRect.height() <= 0) {
            m_pageRect = QRectF(72, 72, 1540, 1040);
        }
        m_y = m_pageRect.top();
    }

    QRectF contentRect() const
    {
        return m_pageRect.adjusted(0, 0, 0, -kFooterHeight);
    }

    void drawFooter()
    {
        const auto rect = QRectF(m_pageRect.left(), m_pageRect.bottom() - kFooterHeight + 8, m_pageRect.width(), kFooterHeight - 8);
        m_painter.setPen(QColor("#8A95A8"));
        m_painter.setFont(font(7.5));
        m_painter.drawText(rect, Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("影资管家 CineVault / DIT PDF Report"));
        m_painter.drawText(rect, Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("第 %1 页").arg(m_pageNumber));
    }

    void nextPage(const QString &title)
    {
        drawFooter();
        if (m_mode == OutputMode::PreviewImages) {
            if (!finishPage()) {
                m_failed = true;
            }
            startPreviewPage();
        } else {
            m_writer->newPage();
        }
        ++m_pageNumber;
        updatePageRect();
        m_currentTitle = title;
        drawHeader(title);
    }

    void startPreviewPage()
    {
        if (m_mode != OutputMode::PreviewImages) {
            return;
        }

        if (m_previewPainter.isActive()) {
            m_previewPainter.end();
        }
        m_previewImage = QImage(m_previewPageSize, QImage::Format_ARGB32_Premultiplied);
        m_previewImage.fill(Qt::white);
        m_previewPainter.begin(&m_previewImage);
        configurePainter();
    }

    bool finishPage()
    {
        if (m_failed) {
            return false;
        }
        if (m_mode != OutputMode::PreviewImages) {
            return true;
        }
        if (m_previewImage.isNull()) {
            return true;
        }
        if (m_previewPainter.isActive()) {
            m_previewPainter.end();
        }

        QDir dir(m_previewDirectory);
        if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
            m_errorMessage = QStringLiteral("无法创建报表预览目录：%1").arg(m_previewDirectory);
            return false;
        }

        const auto filePath = dir.filePath(QStringLiteral("page_%1.png").arg(m_previewPagePaths.size() + 1, 3, 10, QLatin1Char('0')));
        if (!m_previewImage.save(filePath, "PNG")) {
            m_errorMessage = QStringLiteral("无法写入报表预览页：%1").arg(filePath);
            return false;
        }
        m_previewPagePaths.append(filePath);
        m_previewImage = {};
        return true;
    }

    void drawHeader(const QString &title)
    {
        const auto header = QRectF(m_pageRect.left(), m_pageRect.top(), m_pageRect.width(), kHeaderHeight);
        m_painter.fillRect(header, QColor("#172033"));
        m_painter.fillRect(QRectF(header.left(), header.bottom() - 5, header.width(), 5), QColor("#2F6FE0"));

        m_painter.setPen(Qt::white);
        m_painter.setFont(font(15, QFont::DemiBold));
        m_painter.drawText(QRectF(header.left() + 24, header.top() + 14, header.width() * 0.42, 28),
                           Qt::AlignLeft | Qt::AlignVCenter,
                           title);

        m_painter.setFont(font(8.2));
        const QString line1 = QStringLiteral("项目：%1    导出：%2    素材量：%3 个 / %4")
            .arg(m_document.project.projectName,
                 m_document.project.exportTime,
                 QString::number(m_document.totalFiles),
                 Formatters::formatBytes(m_document.totalSizeBytes));
        const QString line2 = QStringLiteral("时间：%1    地点：%2    导演：%3    摄影：%4    DIT：%5")
            .arg(valueOrUnset(m_document.project.shootTime),
                 valueOrUnset(m_document.project.location),
                 valueOrUnset(m_document.project.director),
                 valueOrUnset(m_document.project.cinematographer),
                 valueOrUnset(m_document.project.ditName));
        m_painter.drawText(QRectF(header.left() + header.width() * 0.42, header.top() + 12, header.width() * 0.56, 22),
                           Qt::AlignRight | Qt::AlignVCenter,
                           line1);
        m_painter.setPen(QColor("#CBD5E1"));
        m_painter.drawText(QRectF(header.left() + header.width() * 0.28, header.top() + 42, header.width() * 0.70, 22),
                           Qt::AlignRight | Qt::AlignVCenter,
                           line2);
        m_y = header.bottom() + kSectionGap;
    }

    bool ensureSpace(qreal height)
    {
        if (m_y + height <= contentRect().bottom()) {
            return false;
        }
        nextPage(m_currentTitle);
        return true;
    }

    QStringList wrappedLines(const QString &text, qreal width, const QFont &lineFont, int maxLines) const
    {
        QFontMetricsF metrics(lineFont);
        QStringList lines;
        const auto paragraphs = text.split(QLatin1Char('\n'));
        for (const auto &paragraph : paragraphs) {
            QString current;
            for (const auto &ch : paragraph) {
                const auto candidate = current + ch;
                if (!current.isEmpty() && metrics.horizontalAdvance(candidate) > width) {
                    lines.append(current.trimmed());
                    current = QString(ch);
                } else {
                    current = candidate;
                }
            }
            if (!current.trimmed().isEmpty()) {
                lines.append(current.trimmed());
            }
            if (lines.size() >= maxLines) {
                break;
            }
        }
        if (lines.isEmpty()) {
            lines.append(QStringLiteral("-"));
        }
        if (lines.size() > maxLines) {
            lines = lines.mid(0, maxLines);
        }
        if (!lines.isEmpty() && metrics.horizontalAdvance(lines.last()) > width) {
            lines.last() = metrics.elidedText(lines.last(), Qt::ElideRight, static_cast<int>(width));
        }
        return lines;
    }

    void drawCellText(const QRectF &rect, const QString &text, const QFont &lineFont, const QColor &color, int maxLines, Qt::Alignment alignment = Qt::AlignLeft)
    {
        m_painter.setFont(lineFont);
        m_painter.setPen(color);
        QFontMetricsF metrics(lineFont);
        const auto lines = wrappedLines(text, rect.width(), lineFont, maxLines);
        qreal y = rect.top();
        for (const auto &line : lines) {
            const QRectF lineRect(rect.left(), y, rect.width(), metrics.height() + 2);
            m_painter.drawText(lineRect, alignment | Qt::AlignVCenter, line);
            y += metrics.height() + 2;
            if (y > rect.bottom()) {
                break;
            }
        }
    }

    void drawSectionTitle(const QString &title, const QString &subtitle = {})
    {
        ensureSpace(52);
        m_painter.setPen(QColor("#111827"));
        m_painter.setFont(font(15, QFont::DemiBold));
        m_painter.drawText(QRectF(m_pageRect.left(), m_y, m_pageRect.width(), 26), Qt::AlignLeft | Qt::AlignVCenter, title);
        m_painter.fillRect(QRectF(m_pageRect.left(), m_y + 33, 86, 4), QColor("#2F6FE0"));
        if (!subtitle.isEmpty()) {
            m_painter.setPen(QColor("#64748B"));
            m_painter.setFont(font(8.5));
            m_painter.drawText(QRectF(m_pageRect.left() + 104, m_y + 27, m_pageRect.width() - 104, 18),
                               Qt::AlignLeft | Qt::AlignVCenter,
                               subtitle);
        }
        m_y += 54;
    }

    void drawCard(const QRectF &rect, const QString &label, const QString &value, const QColor &accent = QColor("#2F6FE0"))
    {
        m_painter.fillRect(rect, QColor("#F8FAFC"));
        m_painter.setPen(QPen(QColor("#D7DEE9"), 1));
        m_painter.drawRoundedRect(rect, 8, 8);
        m_painter.fillRect(QRectF(rect.left(), rect.top(), 5, rect.height()), accent);

        m_painter.setFont(font(8.5));
        m_painter.setPen(QColor("#64748B"));
        m_painter.drawText(QRectF(rect.left() + 16, rect.top() + 12, rect.width() - 24, 18), Qt::AlignLeft, label);

        m_painter.setFont(font(16, QFont::DemiBold));
        m_painter.setPen(QColor("#111827"));
        m_painter.drawText(QRectF(rect.left() + 16, rect.top() + 34, rect.width() - 24, 30), Qt::AlignLeft | Qt::AlignVCenter, value);
    }

    void drawCover()
    {
        m_painter.fillRect(m_pageRect, Qt::white);

        const auto band = QRectF(m_pageRect.left(), m_pageRect.top(), m_pageRect.width(), 160);
        m_painter.fillRect(band, QColor("#111827"));
        m_painter.fillRect(QRectF(band.left(), band.bottom() - 7, band.width(), 7), QColor("#2F6FE0"));

        m_painter.setPen(Qt::white);
        m_painter.setFont(font(25, QFont::Black));
        m_painter.drawText(QRectF(band.left() + 34, band.top() + 34, band.width() - 68, 42),
                           Qt::AlignLeft | Qt::AlignVCenter,
                           QStringLiteral("DIT 媒体资产 PDF 报表"));
        m_painter.setFont(font(10.5));
        m_painter.setPen(QColor("#CBD5E1"));
        m_painter.drawText(QRectF(band.left() + 36, band.top() + 88, band.width() - 72, 28),
                           Qt::AlignLeft | Qt::AlignVCenter,
                           QStringLiteral("视频帧缩略图 / 元数据明细 / 项目文件夹结构树状图"));

        m_y = band.bottom() + 36;
        drawSectionTitle(QStringLiteral("项目信息"), QStringLiteral("导出前填写字段不会写入项目数据库"));

        const qreal colWidth = (m_pageRect.width() - 28) / 2.0;
        const qreal rowHeight = 30.0;
        const QVector<QPair<QString, QString>> fields = {
            {QStringLiteral("项目名称"), m_document.project.projectName},
            {QStringLiteral("项目路径"), m_document.project.projectRoot},
            {QStringLiteral("项目时间"), valueOrUnset(m_document.project.shootTime)},
            {QStringLiteral("拍摄地点"), valueOrUnset(m_document.project.location)},
            {QStringLiteral("导演"), valueOrUnset(m_document.project.director)},
            {QStringLiteral("摄影"), valueOrUnset(m_document.project.cinematographer)},
            {QStringLiteral("DIT"), valueOrUnset(m_document.project.ditName)},
            {QStringLiteral("导出时间"), m_document.project.exportTime}
        };

        for (int i = 0; i < fields.size(); ++i) {
            const int col = i % 2;
            const int row = i / 2;
            const QRectF rect(m_pageRect.left() + col * (colWidth + 28), m_y + row * rowHeight, colWidth, rowHeight - 6);
            m_painter.setPen(QColor("#64748B"));
            m_painter.setFont(font(8.4, QFont::DemiBold));
            m_painter.drawText(QRectF(rect.left(), rect.top(), 78, rect.height()), Qt::AlignLeft | Qt::AlignVCenter, fields.at(i).first);
            drawCellText(QRectF(rect.left() + 82, rect.top(), rect.width() - 82, rect.height()),
                         fields.at(i).second,
                         font(8.8),
                         QColor("#111827"),
                         1);
        }
        m_y += 4 * rowHeight + 34;

        const qreal cardGap = 16;
        const qreal cardWidth = (m_pageRect.width() - cardGap * 3) / 4.0;
        const qreal cardHeight = 82;
        drawCard(QRectF(m_pageRect.left(), m_y, cardWidth, cardHeight), QStringLiteral("素材源"), QString::number(m_document.sources.size()));
        drawCard(QRectF(m_pageRect.left() + (cardWidth + cardGap), m_y, cardWidth, cardHeight), QStringLiteral("总文件"), QString::number(m_document.totalFiles), QColor("#0EA5E9"));
        drawCard(QRectF(m_pageRect.left() + (cardWidth + cardGap) * 2, m_y, cardWidth, cardHeight),
                 QStringLiteral("视频 / 音频"),
                 QStringLiteral("%1 / %2").arg(m_document.videoCount).arg(m_document.audioCount),
                 QColor("#22C55E"));
        drawCard(QRectF(m_pageRect.left() + (cardWidth + cardGap) * 3, m_y, cardWidth, cardHeight),
                 QStringLiteral("总容量"),
                 Formatters::formatBytes(m_document.totalSizeBytes),
                 QColor("#F59E0B"));
        m_y += cardHeight + 38;

        const auto noteRect = QRectF(m_pageRect.left(), m_y, m_pageRect.width(), 90);
        m_painter.fillRect(noteRect, QColor("#F8FAFC"));
        m_painter.setPen(QPen(QColor("#D7DEE9"), 1));
        m_painter.drawRoundedRect(noteRect, 8, 8);
        drawCellText(noteRect.adjusted(18, 14, -18, -14),
                     QStringLiteral("说明：本报表基于当前项目数据库生成。视频缩略图来自已生成的缓存帧；缩略图缺失时使用占位图。视频/音频文件进入元数据明细，所有素材路径进入文件夹结构附录。"),
                     font(9.2),
                     QColor("#334155"),
                     3);
        m_y = noteRect.bottom() + 28;
    }

    void drawSummary()
    {
        drawSectionTitle(QStringLiteral("项目摘要"));
        const qreal gap = 14;
        const qreal width = (m_pageRect.width() - gap * 4) / 5.0;
        drawCard(QRectF(m_pageRect.left(), m_y, width, 72), QStringLiteral("视频"), QString::number(m_document.videoCount));
        drawCard(QRectF(m_pageRect.left() + (width + gap), m_y, width, 72), QStringLiteral("音频"), QString::number(m_document.audioCount), QColor("#22C55E"));
        drawCard(QRectF(m_pageRect.left() + (width + gap) * 2, m_y, width, 72), QStringLiteral("图片"), QString::number(m_document.imageCount), QColor("#0EA5E9"));
        drawCard(QRectF(m_pageRect.left() + (width + gap) * 3, m_y, width, 72), QStringLiteral("其他"), QString::number(m_document.otherCount), QColor("#64748B"));
        drawCard(QRectF(m_pageRect.left() + (width + gap) * 4, m_y, width, 72),
                 QStringLiteral("警告"),
                 QString::number(m_document.warningCount + m_document.metadataFailedCount + m_document.thumbnailMissingCount),
                 QColor("#F59E0B"));
        m_y += 96;

    }

    void drawSourceTable()
    {
        drawSectionTitle(QStringLiteral("素材源与扫描概览"));
        drawTableHeader({QStringLiteral("素材源"), QStringLiteral("状态"), QStringLiteral("文件"), QStringLiteral("视频"), QStringLiteral("音频"), QStringLiteral("容量"), QStringLiteral("路径")},
                        {0.18, 0.10, 0.09, 0.08, 0.08, 0.12, 0.35});
        for (const auto &source : m_document.sources) {
            ensureSpace(38);
            const auto rect = QRectF(m_pageRect.left(), m_y, m_pageRect.width(), 34);
            m_painter.fillRect(rect, QColor("#FFFFFF"));
            m_painter.setPen(QPen(QColor("#E2E8F0"), 1));
            m_painter.drawRect(rect);
            drawRowText(rect, {
                            source.name,
                            Formatters::statusLabel(source.status),
                            QString::number(source.totalFiles),
                            QString::number(source.videoCount),
                            QString::number(source.audioCount),
                            Formatters::formatBytes(source.totalSizeBytes),
                            source.path
                        },
                        {0.18, 0.10, 0.09, 0.08, 0.08, 0.12, 0.35},
                        1);
            m_y += 34;
        }
        if (m_document.sources.isEmpty()) {
            drawEmptyBlock(QStringLiteral("当前项目还没有素材源。"));
        }
        m_y += 22;
    }

    void drawExtensionDistribution()
    {
        drawSectionTitle(QStringLiteral("格式分布"));
        const auto rows = extensionDistribution(m_document);
        if (rows.isEmpty()) {
            drawEmptyBlock(QStringLiteral("暂无素材格式统计。"));
            return;
        }

        const int maxCount = std::max(1, rows.first().second);
        for (const auto &row : rows) {
            ensureSpace(28);
            m_painter.setFont(font(8.8, QFont::DemiBold));
            m_painter.setPen(QColor("#111827"));
            m_painter.drawText(QRectF(m_pageRect.left(), m_y, 100, 22), Qt::AlignLeft | Qt::AlignVCenter, row.first);
            const auto barWidth = (m_pageRect.width() - 190) * (static_cast<qreal>(row.second) / maxCount);
            m_painter.fillRect(QRectF(m_pageRect.left() + 110, m_y + 5, barWidth, 12), QColor("#2F6FE0"));
            m_painter.setPen(QColor("#64748B"));
            m_painter.setFont(font(8.5));
            m_painter.drawText(QRectF(m_pageRect.left() + 120 + barWidth, m_y, 70, 22), Qt::AlignLeft | Qt::AlignVCenter, QString::number(row.second));
            m_y += 26;
        }
    }

    void drawThumbnailIndex()
    {
        drawSectionTitle(QStringLiteral("视频缩略图索引"), QStringLiteral("每个视频使用当前缓存帧；缺失时绘制占位图"));
        const auto videos = assetsByType(m_document, AssetType::Video);
        if (videos.isEmpty()) {
            drawEmptyBlock(QStringLiteral("当前项目没有视频素材。"));
            return;
        }

        const qreal gap = 16;
        const int columns = 4;
        const qreal cardWidth = (m_pageRect.width() - gap * (columns - 1)) / columns;
        const qreal cardHeight = 190;
        int column = 0;
        for (const auto &video : videos) {
            if (column == 0) {
                ensureSpace(cardHeight + 8);
            }
            const QRectF card(m_pageRect.left() + column * (cardWidth + gap), m_y, cardWidth, cardHeight);
            drawThumbnailCard(card, video);
            ++column;
            if (column >= columns) {
                column = 0;
                m_y += cardHeight + 18;
            }
        }
        if (column != 0) {
            m_y += cardHeight + 18;
        }
    }

    void drawVideoMetadata()
    {
        drawSectionTitle(QStringLiteral("视频元数据明细"));
        const auto videos = assetsByType(m_document, AssetType::Video);
        if (videos.isEmpty()) {
            drawEmptyBlock(QStringLiteral("当前项目没有视频元数据。"));
            return;
        }

        drawVideoTableHeader();
        int index = 1;
        for (const auto &video : videos) {
            if (ensureSpace(116)) {
                drawVideoTableHeader();
            }
            const QRectF rect(m_pageRect.left(), m_y, m_pageRect.width(), 108);
            m_painter.fillRect(rect, QColor(index % 2 == 0 ? "#F8FAFC" : "#FFFFFF"));
            m_painter.setPen(QPen(QColor("#E2E8F0"), 1));
            m_painter.drawRect(rect);

            drawCellText(QRectF(rect.left() + 8, rect.top() + 8, 36, rect.height() - 16),
                         QString::number(index).rightJustified(2, QLatin1Char('0')),
                         font(8.5, QFont::DemiBold),
                         QColor("#111827"),
                         1,
                         Qt::AlignHCenter);
            drawThumbnail(QRectF(rect.left() + 52, rect.top() + 10, 118, rect.height() - 20), video.thumbnailPath, video.name);
            drawCellText(QRectF(rect.left() + 184, rect.top() + 10, 340, rect.height() - 20),
                         QStringLiteral("文件名：%1\n文件路径：%2\n大小：%3\n修改：%4")
                             .arg(video.name, fileDirectoryLabel(video), Formatters::formatBytes(video.sizeBytes), video.modifiedAt),
                         font(8.2),
                         QColor("#111827"),
                         5);
            drawCellText(QRectF(rect.left() + 540, rect.top() + 10, rect.width() - 552, rect.height() - 20),
                         technicalSummary(video),
                         font(8.2),
                         QColor("#334155"),
                         5);
            m_y += rect.height();
            ++index;
        }
    }

    void drawAudioMetadata()
    {
        drawSectionTitle(QStringLiteral("音频元数据明细"));
        const auto audios = assetsByType(m_document, AssetType::Audio);
        if (audios.isEmpty()) {
            drawEmptyBlock(QStringLiteral("当前项目没有音频素材。"));
            return;
        }

        drawTableHeader({QStringLiteral("序号"), QStringLiteral("文件名"), QStringLiteral("时长"), QStringLiteral("编码/流"), QStringLiteral("码率"), QStringLiteral("大小"), QStringLiteral("相对路径")},
                        {0.06, 0.18, 0.10, 0.20, 0.10, 0.10, 0.26});
        int index = 1;
        for (const auto &audio : audios) {
            if (ensureSpace(46)) {
                drawTableHeader({QStringLiteral("序号"), QStringLiteral("文件名"), QStringLiteral("时长"), QStringLiteral("编码/流"), QStringLiteral("码率"), QStringLiteral("大小"), QStringLiteral("相对路径")},
                                {0.06, 0.18, 0.10, 0.20, 0.10, 0.10, 0.26});
            }
            const QRectF rect(m_pageRect.left(), m_y, m_pageRect.width(), 42);
            m_painter.fillRect(rect, QColor(index % 2 == 0 ? "#F8FAFC" : "#FFFFFF"));
            m_painter.setPen(QPen(QColor("#E2E8F0"), 1));
            m_painter.drawRect(rect);
            drawRowText(rect,
                        {
                            QString::number(index).rightJustified(2, QLatin1Char('0')),
                            audio.name,
                            audio.durationMs > 0 ? Formatters::formatDuration(audio.durationMs) : QStringLiteral("-"),
                            streamSummary(audio, QStringLiteral("audio")).isEmpty() ? audio.container : streamSummary(audio, QStringLiteral("audio")),
                            audio.bitRate > 0 ? Formatters::formatBitRate(audio.bitRate) : QStringLiteral("-"),
                            Formatters::formatBytes(audio.sizeBytes),
                            audio.relativePath
                        },
                        {0.06, 0.18, 0.10, 0.20, 0.10, 0.10, 0.26},
                        2);
            m_y += 42;
            ++index;
        }
    }

    void drawFolderTree()
    {
        drawSectionTitle(QStringLiteral("项目文件夹结构树状图"));
        if (m_document.treeLines.isEmpty()) {
            drawEmptyBlock(QStringLiteral("当前项目还没有可导出的文件树。"));
            return;
        }

        const qreal lineHeight = 19;
        int lineIndex = 0;
        for (const auto &line : m_document.treeLines) {
            ensureSpace(lineHeight + 2);
            const qreal indent = line.depth * 18.0;
            const QString prefix = line.depth == 0 ? QStringLiteral("") : QStringLiteral("└─ ");
            const QString icon = line.folder ? QStringLiteral("[目录] ") : QStringLiteral("[文件] ");
            const QColor color = line.folder ? QColor("#111827") : QColor("#475569");
            drawCellText(QRectF(m_pageRect.left() + indent, m_y, m_pageRect.width() - indent, lineHeight),
                         prefix + icon + line.text,
                         font(line.depth == 0 ? 8.7 : 8.0, line.folder ? QFont::DemiBold : QFont::Normal),
                         color,
                         1);
            m_y += lineHeight;
            ++lineIndex;
            if (lineIndex % 2 == 0) {
                m_y += 1;
            }
        }
    }

    void drawEmptyBlock(const QString &message)
    {
        ensureSpace(68);
        const QRectF rect(m_pageRect.left(), m_y, m_pageRect.width(), 56);
        m_painter.fillRect(rect, QColor("#F8FAFC"));
        m_painter.setPen(QPen(QColor("#D7DEE9"), 1));
        m_painter.drawRoundedRect(rect, 8, 8);
        drawCellText(rect.adjusted(16, 10, -16, -10), message, font(9), QColor("#64748B"), 2);
        m_y += 72;
    }

    void drawThumbnailCard(const QRectF &card, const ReportAssetRow &video)
    {
        m_painter.fillRect(card, QColor("#FFFFFF"));
        m_painter.setPen(QPen(QColor("#D7DEE9"), 1));
        m_painter.drawRoundedRect(card, 8, 8);
        drawThumbnail(card.adjusted(10, 10, -10, -64), video.thumbnailPath, video.name);
        drawCellText(QRectF(card.left() + 12, card.bottom() - 52, card.width() - 24, 18),
                     video.name,
                     font(8.4, QFont::DemiBold),
                     QColor("#111827"),
                     1);
        const QString meta = QStringList({
            video.durationMs > 0 ? Formatters::formatDuration(video.durationMs) : QStringLiteral("未知时长"),
            video.container.isEmpty() ? assetTypeName(video.assetType) : video.container,
            Formatters::formatBytes(video.sizeBytes)
        }).join(QStringLiteral(" / "));
        drawCellText(QRectF(card.left() + 12, card.bottom() - 30, card.width() - 24, 18),
                     meta,
                     font(7.6),
                     QColor("#64748B"),
                     1);
    }

    void drawThumbnail(const QRectF &rect, const QString &path, const QString &label)
    {
        m_painter.fillRect(rect, QColor("#E5EAF2"));
        m_painter.setPen(QPen(QColor("#CBD5E1"), 1));
        m_painter.drawRect(rect);

        const QFileInfo info(path);
        QImage image;
        if (info.exists() && info.isFile()) {
            image.load(path);
        }
        if (!image.isNull()) {
            const auto scaled = image.scaled(rect.size().toSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            const QRectF target(rect.left() + (rect.width() - scaled.width()) / 2.0,
                                rect.top() + (rect.height() - scaled.height()) / 2.0,
                                scaled.width(),
                                scaled.height());
            m_painter.drawImage(target, scaled);
            return;
        }

        m_painter.setPen(QColor("#64748B"));
        m_painter.setFont(font(8.2, QFont::DemiBold));
        m_painter.drawText(rect.adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWordWrap, QStringLiteral("缩略图缺失\n%1").arg(label));
    }

    void drawTableHeader(const QStringList &labels, const QVector<qreal> &widthRatios)
    {
        ensureSpace(34);
        const QRectF rect(m_pageRect.left(), m_y, m_pageRect.width(), 30);
        m_painter.fillRect(rect, QColor("#EAF1FF"));
        m_painter.setPen(QPen(QColor("#C7D2FE"), 1));
        m_painter.drawRect(rect);
        drawRowText(rect, labels, widthRatios, 1, font(8.2, QFont::DemiBold), QColor("#1E3A8A"));
        m_y += 30;
    }

    void drawVideoTableHeader()
    {
        ensureSpace(34);
        const QRectF rect(m_pageRect.left(), m_y, m_pageRect.width(), 30);
        m_painter.fillRect(rect, QColor("#EAF1FF"));
        m_painter.setPen(QPen(QColor("#C7D2FE"), 1));
        m_painter.drawRect(rect);
        drawCellText(QRectF(rect.left() + 8, rect.top() + 5, 36, 20), QStringLiteral("序号"), font(8.2, QFont::DemiBold), QColor("#1E3A8A"), 1);
        drawCellText(QRectF(rect.left() + 52, rect.top() + 5, 118, 20), QStringLiteral("缩略图"), font(8.2, QFont::DemiBold), QColor("#1E3A8A"), 1);
        drawCellText(QRectF(rect.left() + 184, rect.top() + 5, 340, 20), QStringLiteral("基础信息"), font(8.2, QFont::DemiBold), QColor("#1E3A8A"), 1);
        drawCellText(QRectF(rect.left() + 540, rect.top() + 5, rect.width() - 552, 20), QStringLiteral("技术元数据"), font(8.2, QFont::DemiBold), QColor("#1E3A8A"), 1);
        m_y += 30;
    }

    void drawRowText(const QRectF &rect,
                     const QStringList &values,
                     const QVector<qreal> &widthRatios,
                     int maxLines,
                     const QFont &rowFont = QFont(),
                     const QColor &color = QColor("#334155"))
    {
        qreal x = rect.left() + 8;
        const auto effectiveFont = rowFont.pointSizeF() <= 0 && rowFont.pixelSize() <= 0 ? font(8.1) : rowFont;
        for (int i = 0; i < values.size() && i < widthRatios.size(); ++i) {
            const qreal cellWidth = rect.width() * widthRatios.at(i) - 10;
            drawCellText(QRectF(x, rect.top() + 5, cellWidth, rect.height() - 10), values.at(i), effectiveFont, color, maxLines);
            x += rect.width() * widthRatios.at(i);
        }
    }

    const ReportDocument &m_document;
    OutputMode m_mode = OutputMode::Pdf;
    QPdfWriter *m_writer = nullptr;
    QPainter m_previewPainter;
    QPainter &m_painter;
    QString m_previewDirectory;
    QStringList m_previewPagePaths;
    QSize m_previewPageSize = QSize(1684, 1191);
    QImage m_previewImage;
    QString m_errorMessage;
    bool m_failed = false;
    QString m_fontFamily;
    QRectF m_pageRect;
    QString m_currentTitle;
    qreal m_y = 0.0;
    int m_pageNumber = 1;
};
}

bool ReportRenderEngine::renderPdf(const ReportDocument &document, const QString &outputPath, QString *errorMessage) const
{
    QPdfWriter writer(outputPath);
    writer.setCreator(QStringLiteral("CineVault"));
    writer.setTitle(QStringLiteral("%1 DIT PDF 报表").arg(document.project.projectName));
    writer.setResolution(144);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageOrientation(QPageLayout::Landscape);
    writer.setPageMargins(QMarginsF(12, 12, 12, 12), QPageLayout::Millimeter);

    QPainter painter(&writer);
    if (!painter.isActive()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法创建 PDF 文件：%1").arg(outputPath);
        }
        return false;
    }

    PdfRenderer renderer(document, writer, painter);
    return renderer.render(errorMessage);
}

bool ReportRenderEngine::renderPreviewImages(const ReportDocument &document, const QString &outputDirectory, QStringList *pagePaths, QString *errorMessage) const
{
    QDir dir(outputDirectory);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法创建报表预览目录：%1").arg(outputDirectory);
        }
        return false;
    }

    PdfRenderer renderer(document, outputDirectory);
    if (!renderer.render(errorMessage)) {
        return false;
    }

    const auto paths = renderer.previewPagePaths();
    if (paths.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("报表预览未生成任何页面。");
        }
        return false;
    }
    if (pagePaths) {
        *pagePaths = paths;
    }
    return true;
}
