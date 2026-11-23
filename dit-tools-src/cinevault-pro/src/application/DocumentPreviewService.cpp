#include "application/DocumentPreviewService.h"

#include <QtCore/private/qzipreader_p.h>

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMap>
#include <QMimeDatabase>
#include <QPdfDocument>
#include <QPdfSelection>
#include <QRegularExpression>
#include <QStringConverter>
#include <QStringList>
#include <QTextStream>
#include <QUrl>
#include <QXmlStreamReader>
#include <algorithm>

namespace {
constexpr qint64 kMaxPreviewBytes = 1024 * 1024;
constexpr int kMaxSummaryTextChars = 64000;
constexpr int kMaxPreviewRows = 180;
constexpr int kMaxTableColumns = 24;

struct PreviewPayload {
    QString content;
    bool isPdf = false;
    bool isMarkdown = false;
    bool isRichText = false;
    bool truncated = false;
    QString errorMessage;
};

QString escapeHtml(const QString &text)
{
    return text.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br/>"));
}

QString wrapRichTextDocument(const QString &bodyHtml)
{
    return QStringLiteral(
               "<html><head><meta charset=\"utf-8\"/>"
               "<style>"
               "body{font-family:'Microsoft YaHei UI','Segoe UI',sans-serif;font-size:14px;line-height:1.72;color:#0f172a;margin:0;}"
               "h1,h2,h3{margin:24px 0 12px;color:#0f172a;}"
               "p{margin:0 0 14px;}"
               "ul{margin:0 0 14px 20px;padding:0;}"
               "li{margin:0 0 8px;}"
               "table{border-collapse:collapse;width:100%;margin:0 0 18px;}"
               "th,td{border:1px solid #dbe4f0;padding:8px 10px;vertical-align:top;text-align:left;}"
               "th{background:#eef4fb;font-weight:700;}"
               ".hint{padding:14px 16px;background:#f8fafc;border:1px solid #dbe4f0;border-radius:12px;color:#334155;}"
               ".section{margin:0 0 24px;}"
               ".slide{padding:14px 16px;border:1px solid #dbe4f0;border-radius:14px;margin:0 0 16px;background:#ffffff;}"
               "pre{white-space:pre-wrap;word-wrap:break-word;}"
               "</style></head><body>%1</body></html>")
        .arg(bodyHtml);
}

QString infoBlockHtml(const QString &title, const QString &body)
{
    return wrapRichTextDocument(
        QStringLiteral("<div class=\"hint\"><strong>%1</strong><br/>%2</div>")
            .arg(title.toHtmlEscaped(), escapeHtml(body)));
}

QString decodeText(const QByteArray &bytes)
{
    if (bytes.startsWith("\xFF\xFE")) {
        QStringDecoder decoder(QStringDecoder::Utf16LE);
        return decoder.decode(bytes);
    }
    if (bytes.startsWith("\xFE\xFF")) {
        QStringDecoder decoder(QStringDecoder::Utf16BE);
        return decoder.decode(bytes);
    }

    QStringDecoder decoder(QStringDecoder::Utf8);
    return decoder.decode(bytes.startsWith("\xEF\xBB\xBF") ? bytes.mid(3) : bytes);
}

PreviewPayload readPlainTextPreview(const QString &path)
{
    PreviewPayload payload;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        payload.errorMessage = QStringLiteral("无法读取文档：%1").arg(path);
        return payload;
    }

    QByteArray bytes = file.read(kMaxPreviewBytes + 1);
    payload.truncated = bytes.size() > kMaxPreviewBytes || file.size() > kMaxPreviewBytes;
    if (bytes.size() > kMaxPreviewBytes) {
        bytes.truncate(kMaxPreviewBytes);
    }
    payload.content = decodeText(bytes);
    return payload;
}

QStringList extractXmlTextRuns(const QByteArray &xmlData, const QString &paragraphTag, const QString &textTag)
{
    QStringList paragraphs;
    QXmlStreamReader reader(xmlData);
    QString currentText;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            const auto name = reader.name();
            if (name == paragraphTag) {
                currentText.clear();
            } else if (name == textTag) {
                currentText += reader.readElementText(QXmlStreamReader::IncludeChildElements);
            } else if (name == QStringLiteral("tab")) {
                currentText += QLatin1Char('\t');
            } else if (name == QStringLiteral("br") || name == QStringLiteral("cr")) {
                currentText += QLatin1Char('\n');
            }
        } else if (reader.isEndElement() && reader.name() == paragraphTag) {
            if (!currentText.trimmed().isEmpty()) {
                paragraphs.append(currentText.trimmed());
            }
            currentText.clear();
        }
    }
    return paragraphs;
}

QVector<QStringList> parseDelimitedRows(const QString &text, QChar separator, bool *truncated)
{
    QVector<QStringList> rows;
    QStringList currentRow;
    QString currentCell;
    bool inQuotes = false;

    auto finishRow = [&]() {
        currentRow.append(currentCell);
        currentCell.clear();
        rows.append(currentRow);
        currentRow.clear();
    };

    for (int index = 0; index < text.size(); ++index) {
        const auto ch = text.at(index);
        if (ch == QLatin1Char('"')) {
            if (inQuotes && index + 1 < text.size() && text.at(index + 1) == QLatin1Char('"')) {
                currentCell += QLatin1Char('"');
                ++index;
            } else {
                inQuotes = !inQuotes;
            }
            continue;
        }

        if (!inQuotes && ch == separator) {
            currentRow.append(currentCell);
            currentCell.clear();
            continue;
        }

        if (!inQuotes && (ch == QLatin1Char('\n') || ch == QLatin1Char('\r'))) {
            if (ch == QLatin1Char('\r') && index + 1 < text.size() && text.at(index + 1) == QLatin1Char('\n')) {
                ++index;
            }
            finishRow();
            if (rows.size() >= kMaxPreviewRows) {
                if (truncated) {
                    *truncated = true;
                }
                return rows;
            }
            continue;
        }

        currentCell += ch;
    }

    if (!currentCell.isEmpty() || !currentRow.isEmpty()) {
        finishRow();
    }
    return rows;
}

QString tableHtml(const QVector<QStringList> &rows)
{
    if (rows.isEmpty()) {
        return QStringLiteral("<div class=\"hint\">当前文档没有可展示的表格内容。</div>");
    }

    QString html = QStringLiteral("<table>");
    const auto header = rows.first();
    html += QStringLiteral("<thead><tr>");
    for (int column = 0; column < qMin(header.size(), kMaxTableColumns); ++column) {
        html += QStringLiteral("<th>%1</th>").arg(header.at(column).toHtmlEscaped());
    }
    html += QStringLiteral("</tr></thead><tbody>");
    for (int row = 1; row < rows.size(); ++row) {
        html += QStringLiteral("<tr>");
        const auto &cells = rows.at(row);
        const auto cellCount = qMin(cells.size(), kMaxTableColumns);
        for (int column = 0; column < cellCount; ++column) {
            html += QStringLiteral("<td>%1</td>").arg(cells.at(column).toHtmlEscaped());
        }
        html += QStringLiteral("</tr>");
    }
    html += QStringLiteral("</tbody></table>");
    return html;
}

PreviewPayload buildCsvPreview(const QString &path, QChar separator)
{
    auto payload = readPlainTextPreview(path);
    if (!payload.errorMessage.isEmpty()) {
        return payload;
    }

    bool rowTruncated = false;
    const auto rows = parseDelimitedRows(payload.content, separator, &rowTruncated);
    payload.isRichText = true;
    payload.truncated = payload.truncated || rowTruncated;
    payload.content = wrapRichTextDocument(tableHtml(rows));
    return payload;
}

PreviewPayload buildJsonPreview(const QString &path)
{
    auto payload = readPlainTextPreview(path);
    if (!payload.errorMessage.isEmpty()) {
        return payload;
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(payload.content.toUtf8(), &parseError);
    if (parseError.error == QJsonParseError::NoError) {
        payload.content = QString::fromUtf8(document.toJson(QJsonDocument::Indented));
    }
    return payload;
}

int numericSuffix(const QString &text, const QRegularExpression &expression)
{
    const auto match = expression.match(text);
    return match.hasMatch() ? match.captured(1).toInt() : 0;
}

QStringList parseSharedStrings(const QByteArray &xmlData)
{
    QStringList values;
    QXmlStreamReader reader(xmlData);
    QString current;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            if (reader.name() == QStringLiteral("si")) {
                current.clear();
            } else if (reader.name() == QStringLiteral("t")) {
                current += reader.readElementText(QXmlStreamReader::IncludeChildElements);
            }
        } else if (reader.isEndElement() && reader.name() == QStringLiteral("si")) {
            values.append(current);
        }
    }
    return values;
}

QStringList parseWorkbookSheetNames(const QByteArray &xmlData)
{
    QStringList names;
    QXmlStreamReader reader(xmlData);
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == QStringLiteral("sheet")) {
            const auto name = reader.attributes().value(QStringLiteral("name")).toString().trimmed();
            names.append(name.isEmpty() ? QStringLiteral("工作表 %1").arg(names.size() + 1) : name);
        }
    }
    return names;
}

int spreadsheetColumnIndex(const QString &cellReference)
{
    int result = 0;
    for (const auto ch : cellReference) {
        if (ch.isDigit()) {
            break;
        }
        const auto upper = ch.toUpper().unicode();
        if (upper < 'A' || upper > 'Z') {
            continue;
        }
        result = result * 26 + (upper - 'A' + 1);
    }
    return qMax(0, result - 1);
}

QVector<QStringList> parseWorksheetRows(const QByteArray &xmlData, const QStringList &sharedStrings, bool *truncated)
{
    QVector<QStringList> rows;
    QXmlStreamReader reader(xmlData);
    QMap<int, QString> rowCells;
    int currentColumn = 0;
    QString currentType;
    QString currentValue;
    QString currentInlineValue;

    auto flushRow = [&]() {
        if (rowCells.isEmpty()) {
            return;
        }
        const auto lastColumn = qMin(rowCells.lastKey(), kMaxTableColumns - 1);
        QStringList row(lastColumn + 1);
        for (auto it = rowCells.cbegin(); it != rowCells.cend(); ++it) {
            if (it.key() >= 0 && it.key() < row.size()) {
                row[it.key()] = it.value();
            }
        }
        rows.append(row);
        rowCells.clear();
    };

    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            const auto name = reader.name();
            if (name == QStringLiteral("row")) {
                rowCells.clear();
            } else if (name == QStringLiteral("c")) {
                currentColumn = spreadsheetColumnIndex(reader.attributes().value(QStringLiteral("r")).toString());
                currentType = reader.attributes().value(QStringLiteral("t")).toString();
                currentValue.clear();
                currentInlineValue.clear();
            } else if (name == QStringLiteral("v")) {
                currentValue = reader.readElementText(QXmlStreamReader::IncludeChildElements);
            } else if (name == QStringLiteral("t") && currentType == QStringLiteral("inlineStr")) {
                currentInlineValue += reader.readElementText(QXmlStreamReader::IncludeChildElements);
            }
        } else if (reader.isEndElement()) {
            const auto name = reader.name();
            if (name == QStringLiteral("c")) {
                QString resolvedValue = currentValue;
                if (currentType == QStringLiteral("s")) {
                    bool ok = false;
                    const auto index = currentValue.toInt(&ok);
                    resolvedValue = ok && index >= 0 && index < sharedStrings.size() ? sharedStrings.at(index) : currentValue;
                } else if (currentType == QStringLiteral("inlineStr")) {
                    resolvedValue = currentInlineValue;
                } else if (currentType == QStringLiteral("b")) {
                    resolvedValue = currentValue == QStringLiteral("1") ? QStringLiteral("TRUE") : QStringLiteral("FALSE");
                }

                if (!resolvedValue.isEmpty() && currentColumn < kMaxTableColumns) {
                    rowCells.insert(currentColumn, resolvedValue);
                }
            } else if (name == QStringLiteral("row")) {
                flushRow();
                if (rows.size() >= kMaxPreviewRows) {
                    if (truncated) {
                        *truncated = true;
                    }
                    return rows;
                }
            }
        }
    }

    flushRow();
    return rows;
}

PreviewPayload buildDocxPreview(const QString &path)
{
    PreviewPayload payload;
    QZipReader reader(path);
    if (!reader.exists() || !reader.isReadable()) {
        payload.errorMessage = QStringLiteral("无法解析文档容器：%1").arg(path);
        return payload;
    }

    const auto paragraphs = extractXmlTextRuns(reader.fileData(QStringLiteral("word/document.xml")),
                                               QStringLiteral("p"),
                                               QStringLiteral("t"));
    if (paragraphs.isEmpty()) {
        payload.errorMessage = QStringLiteral("当前 DOCX 没有可预览的正文内容。");
        return payload;
    }

    QStringList htmlBlocks;
    int visibleCount = 0;
    for (const auto &paragraph : paragraphs) {
        htmlBlocks.append(QStringLiteral("<p>%1</p>").arg(paragraph.toHtmlEscaped()));
        ++visibleCount;
        if (visibleCount >= kMaxPreviewRows) {
            payload.truncated = paragraphs.size() > visibleCount;
            break;
        }
    }
    payload.isRichText = true;
    payload.content = wrapRichTextDocument(htmlBlocks.join(QString()));
    return payload;
}

PreviewPayload buildPptxPreview(const QString &path)
{
    PreviewPayload payload;
    QZipReader reader(path);
    if (!reader.exists() || !reader.isReadable()) {
        payload.errorMessage = QStringLiteral("无法解析演示文稿容器：%1").arg(path);
        return payload;
    }

    auto entries = reader.fileInfoList();
    const QRegularExpression slideExpression(QStringLiteral("^ppt/slides/slide(\\d+)\\.xml$"));
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const QZipReader::FileInfo &entry) {
        return !slideExpression.match(entry.filePath).hasMatch();
    }), entries.end());
    std::sort(entries.begin(), entries.end(), [&](const QZipReader::FileInfo &left, const QZipReader::FileInfo &right) {
        return numericSuffix(left.filePath, slideExpression) < numericSuffix(right.filePath, slideExpression);
    });

    QStringList slideBlocks;
    for (int index = 0; index < entries.size() && index < kMaxPreviewRows; ++index) {
        const auto paragraphs = extractXmlTextRuns(reader.fileData(entries.at(index).filePath),
                                                   QStringLiteral("p"),
                                                   QStringLiteral("t"));
        QStringList items;
        for (const auto &paragraph : paragraphs) {
            items.append(QStringLiteral("<li>%1</li>").arg(paragraph.toHtmlEscaped()));
        }
        if (!items.isEmpty()) {
            slideBlocks.append(QStringLiteral("<div class=\"slide\"><h3>第 %1 页</h3><ul>%2</ul></div>")
                                   .arg(index + 1)
                                   .arg(items.join(QString())));
        }
    }

    if (slideBlocks.isEmpty()) {
        payload.errorMessage = QStringLiteral("当前 PPTX 没有可预览的文本内容。");
        return payload;
    }

    payload.isRichText = true;
    payload.truncated = entries.size() > kMaxPreviewRows;
    payload.content = wrapRichTextDocument(slideBlocks.join(QString()));
    return payload;
}

PreviewPayload buildXlsxPreview(const QString &path)
{
    PreviewPayload payload;
    QZipReader reader(path);
    if (!reader.exists() || !reader.isReadable()) {
        payload.errorMessage = QStringLiteral("无法解析表格容器：%1").arg(path);
        return payload;
    }

    const auto sharedStrings = parseSharedStrings(reader.fileData(QStringLiteral("xl/sharedStrings.xml")));
    const auto sheetNames = parseWorkbookSheetNames(reader.fileData(QStringLiteral("xl/workbook.xml")));
    auto entries = reader.fileInfoList();
    const QRegularExpression sheetExpression(QStringLiteral("^xl/worksheets/sheet(\\d+)\\.xml$"));
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const QZipReader::FileInfo &entry) {
        return !sheetExpression.match(entry.filePath).hasMatch();
    }), entries.end());
    std::sort(entries.begin(), entries.end(), [&](const QZipReader::FileInfo &left, const QZipReader::FileInfo &right) {
        return numericSuffix(left.filePath, sheetExpression) < numericSuffix(right.filePath, sheetExpression);
    });

    QStringList sections;
    int processedRows = 0;
    for (int index = 0; index < entries.size(); ++index) {
        bool rowTruncated = false;
        const auto rows = parseWorksheetRows(reader.fileData(entries.at(index).filePath), sharedStrings, &rowTruncated);
        if (rows.isEmpty()) {
            continue;
        }

        const auto sheetName = index < sheetNames.size() ? sheetNames.at(index) : QStringLiteral("工作表 %1").arg(index + 1);
        sections.append(QStringLiteral("<div class=\"section\"><h3>%1</h3>%2</div>")
                            .arg(sheetName.toHtmlEscaped(), tableHtml(rows)));
        processedRows += rows.size();
        payload.truncated = payload.truncated || rowTruncated;
        if (processedRows >= kMaxPreviewRows) {
            payload.truncated = true;
            break;
        }
    }

    if (sections.isEmpty()) {
        payload.errorMessage = QStringLiteral("当前 XLSX 没有可预览的单元格内容。");
        return payload;
    }

    payload.isRichText = true;
    payload.content = wrapRichTextDocument(sections.join(QString()));
    return payload;
}

PreviewPayload buildLegacyOfficeFallback(const QString &suffix)
{
    return PreviewPayload{
        infoBlockHtml(QStringLiteral("暂不支持该旧版文档的内嵌解析"),
                      QStringLiteral("当前版本已经支持 PDF、Markdown、TXT、JSON、CSV、DOCX、XLSX 和 PPTX 预览；`.%1` 这类旧版二进制 Office 文档暂时只能显示文件信息。").arg(suffix)),
        false,
        false,
        true,
        false,
        {}
    };
}

QString boundedSummaryText(QString text, bool *truncated)
{
    text = text.trimmed();
    if (text.size() <= kMaxSummaryTextChars) {
        return text;
    }
    if (truncated) {
        *truncated = true;
    }
    return text.left(kMaxSummaryTextChars).trimmed();
}

QString extractDocxPlainText(const QString &path, QString *errorMessage)
{
    QZipReader reader(path);
    if (!reader.exists() || !reader.isReadable()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法解析文档容器：%1").arg(path);
        }
        return {};
    }

    const auto paragraphs = extractXmlTextRuns(reader.fileData(QStringLiteral("word/document.xml")),
                                               QStringLiteral("p"),
                                               QStringLiteral("t"));
    if (paragraphs.isEmpty() && errorMessage) {
        *errorMessage = QStringLiteral("当前 DOCX 没有可提取的正文内容。");
    }
    return paragraphs.join(QStringLiteral("\n"));
}

QString extractPptxPlainText(const QString &path, bool *truncated, QString *errorMessage)
{
    QZipReader reader(path);
    if (!reader.exists() || !reader.isReadable()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法解析演示文稿容器：%1").arg(path);
        }
        return {};
    }

    auto entries = reader.fileInfoList();
    const QRegularExpression slideExpression(QStringLiteral("^ppt/slides/slide(\\d+)\\.xml$"));
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const QZipReader::FileInfo &entry) {
        return !slideExpression.match(entry.filePath).hasMatch();
    }), entries.end());
    std::sort(entries.begin(), entries.end(), [&](const QZipReader::FileInfo &left, const QZipReader::FileInfo &right) {
        return numericSuffix(left.filePath, slideExpression) < numericSuffix(right.filePath, slideExpression);
    });

    QStringList slides;
    for (int index = 0; index < entries.size() && index < kMaxPreviewRows; ++index) {
        const auto paragraphs = extractXmlTextRuns(reader.fileData(entries.at(index).filePath),
                                                   QStringLiteral("p"),
                                                   QStringLiteral("t"));
        if (!paragraphs.isEmpty()) {
            slides.append(QStringLiteral("第 %1 页\n%2").arg(index + 1).arg(paragraphs.join(QStringLiteral("\n"))));
        }
    }
    if (truncated && entries.size() > kMaxPreviewRows) {
        *truncated = true;
    }
    if (slides.isEmpty() && errorMessage) {
        *errorMessage = QStringLiteral("当前 PPTX 没有可提取的文本内容。");
    }
    return slides.join(QStringLiteral("\n\n"));
}

QString extractXlsxPlainText(const QString &path, bool *truncated, QString *errorMessage)
{
    QZipReader reader(path);
    if (!reader.exists() || !reader.isReadable()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法解析表格容器：%1").arg(path);
        }
        return {};
    }

    const auto sharedStrings = parseSharedStrings(reader.fileData(QStringLiteral("xl/sharedStrings.xml")));
    const auto sheetNames = parseWorkbookSheetNames(reader.fileData(QStringLiteral("xl/workbook.xml")));
    auto entries = reader.fileInfoList();
    const QRegularExpression sheetExpression(QStringLiteral("^xl/worksheets/sheet(\\d+)\\.xml$"));
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const QZipReader::FileInfo &entry) {
        return !sheetExpression.match(entry.filePath).hasMatch();
    }), entries.end());
    std::sort(entries.begin(), entries.end(), [&](const QZipReader::FileInfo &left, const QZipReader::FileInfo &right) {
        return numericSuffix(left.filePath, sheetExpression) < numericSuffix(right.filePath, sheetExpression);
    });

    QStringList sections;
    int processedRows = 0;
    for (int index = 0; index < entries.size(); ++index) {
        bool rowTruncated = false;
        const auto rows = parseWorksheetRows(reader.fileData(entries.at(index).filePath), sharedStrings, &rowTruncated);
        if (rows.isEmpty()) {
            continue;
        }

        const auto sheetName = index < sheetNames.size() ? sheetNames.at(index) : QStringLiteral("工作表 %1").arg(index + 1);
        QStringList rowTexts;
        for (const auto &row : rows) {
            rowTexts.append(row.join(QStringLiteral("\t")).trimmed());
        }
        sections.append(QStringLiteral("%1\n%2").arg(sheetName, rowTexts.join(QStringLiteral("\n"))));
        processedRows += rows.size();
        if (truncated && rowTruncated) {
            *truncated = true;
        }
        if (processedRows >= kMaxPreviewRows) {
            if (truncated) {
                *truncated = true;
            }
            break;
        }
    }
    if (sections.isEmpty() && errorMessage) {
        *errorMessage = QStringLiteral("当前 XLSX 没有可提取的单元格内容。");
    }
    return sections.join(QStringLiteral("\n\n"));
}

QString extractPdfPlainText(const QString &path, bool *truncated, QString *errorMessage)
{
    QPdfDocument document;
    const auto loadError = document.load(path);
    if (loadError != QPdfDocument::Error::None || document.status() != QPdfDocument::Status::Ready) {
        if (errorMessage) {
            *errorMessage = loadError == QPdfDocument::Error::IncorrectPassword
                ? QStringLiteral("PDF 已加密，需要密码后才能提取正文。")
                : QStringLiteral("无法读取 PDF 正文（错误代码 %1）。").arg(static_cast<int>(loadError));
        }
        return {};
    }

    QStringList pages;
    qsizetype totalCharacters = 0;
    for (int page = 0; page < document.pageCount(); ++page) {
        auto pageText = document.getAllText(page).text().trimmed();
        if (pageText.isEmpty()) {
            continue;
        }
        const auto remaining = kMaxSummaryTextChars - totalCharacters;
        if (remaining <= 0) {
            if (truncated) *truncated = true;
            break;
        }
        if (pageText.size() > remaining) {
            pageText.truncate(remaining);
            if (truncated) *truncated = true;
        }
        pages.append(pageText);
        totalCharacters += pageText.size() + 2;
        if (totalCharacters >= kMaxSummaryTextChars) {
            if (page + 1 < document.pageCount() && truncated) *truncated = true;
            break;
        }
    }
    if (pages.isEmpty() && errorMessage) {
        *errorMessage = QStringLiteral("PDF 没有可提取的文字，可能是扫描图片文档。可先执行 OCR 后再生成摘要。");
    }
    return pages.join(QStringLiteral("\n\n"));
}

PreviewPayload buildPreviewPayload(const QString &path)
{
    const auto suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QStringLiteral("pdf")) {
        PreviewPayload payload;
        payload.isPdf = true;
        return payload;
    }
    if (suffix == QStringLiteral("md")) {
        auto payload = readPlainTextPreview(path);
        payload.isMarkdown = payload.errorMessage.isEmpty();
        return payload;
    }
    if (suffix == QStringLiteral("json")) {
        return buildJsonPreview(path);
    }
    if (suffix == QStringLiteral("csv")) {
        return buildCsvPreview(path, QLatin1Char(','));
    }
    if (suffix == QStringLiteral("tsv")) {
        return buildCsvPreview(path, QLatin1Char('\t'));
    }
    if (suffix == QStringLiteral("docx")) {
        return buildDocxPreview(path);
    }
    if (suffix == QStringLiteral("xlsx")) {
        return buildXlsxPreview(path);
    }
    if (suffix == QStringLiteral("pptx")) {
        return buildPptxPreview(path);
    }
    if (suffix == QStringLiteral("doc") || suffix == QStringLiteral("xls") || suffix == QStringLiteral("ppt")) {
        return buildLegacyOfficeFallback(suffix);
    }

    QMimeDatabase mimeDatabase;
    const auto mime = mimeDatabase.mimeTypeForFile(path, QMimeDatabase::MatchExtension);
    const bool plainTextLike = mime.name().startsWith(QStringLiteral("text/"))
        || suffix == QStringLiteral("txt")
        || suffix == QStringLiteral("log")
        || suffix == QStringLiteral("xml")
        || suffix == QStringLiteral("yaml")
        || suffix == QStringLiteral("yml");
    if (plainTextLike) {
        return readPlainTextPreview(path);
    }

    PreviewPayload payload;
    payload.errorMessage = QStringLiteral("当前文件类型暂不支持内容预览。");
    return payload;
}
}

DocumentPreviewService::DocumentPreviewService(QObject *parent)
    : QObject(parent)
{
}

QString DocumentPreviewService::title() const
{
    return m_title;
}

QUrl DocumentPreviewService::sourceUrl() const
{
    return m_sourceUrl;
}

QString DocumentPreviewService::content() const
{
    return m_content;
}

bool DocumentPreviewService::isPdf() const
{
    return m_isPdf;
}

bool DocumentPreviewService::isMarkdown() const
{
    return m_isMarkdown;
}

bool DocumentPreviewService::isRichText() const
{
    return m_isRichText;
}

bool DocumentPreviewService::truncated() const
{
    return m_truncated;
}

QString DocumentPreviewService::errorMessage() const
{
    return m_errorMessage;
}

bool DocumentPreviewService::hasError() const
{
    return !m_errorMessage.trimmed().isEmpty();
}

bool DocumentPreviewService::hasContent() const
{
    return m_isPdf || !m_content.trimmed().isEmpty();
}

QString DocumentPreviewService::extractTextForSummary(const QString &path, bool *truncated, QString *errorMessage)
{
    if (truncated) {
        *truncated = false;
    }
    if (errorMessage) {
        errorMessage->clear();
    }

    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("文档不存在：%1").arg(info.absoluteFilePath());
        }
        return {};
    }

    const auto suffix = info.suffix().toLower();
    QString text;
    if (suffix == QStringLiteral("doc")
        || suffix == QStringLiteral("xls")
        || suffix == QStringLiteral("ppt")) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("当前文件类型本阶段只进入元数据搜索，不做正文摘要。");
        }
        return {};
    }
    if (suffix == QStringLiteral("pdf")) {
        text = extractPdfPlainText(info.absoluteFilePath(), truncated, errorMessage);
    } else if (suffix == QStringLiteral("docx")) {
        text = extractDocxPlainText(info.absoluteFilePath(), errorMessage);
    } else if (suffix == QStringLiteral("xlsx")) {
        text = extractXlsxPlainText(info.absoluteFilePath(), truncated, errorMessage);
    } else if (suffix == QStringLiteral("pptx")) {
        text = extractPptxPlainText(info.absoluteFilePath(), truncated, errorMessage);
    } else {
        QMimeDatabase mimeDatabase;
        const auto mime = mimeDatabase.mimeTypeForFile(info.absoluteFilePath(), QMimeDatabase::MatchExtension);
        const bool plainTextLike = mime.name().startsWith(QStringLiteral("text/"))
            || suffix == QStringLiteral("txt")
            || suffix == QStringLiteral("log")
            || suffix == QStringLiteral("md")
            || suffix == QStringLiteral("json")
            || suffix == QStringLiteral("csv")
            || suffix == QStringLiteral("tsv")
            || suffix == QStringLiteral("xml")
            || suffix == QStringLiteral("yaml")
            || suffix == QStringLiteral("yml")
            || suffix == QStringLiteral("srt")
            || suffix == QStringLiteral("ass")
            || suffix == QStringLiteral("vtt");
        if (!plainTextLike) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("当前文件类型本阶段只进入元数据搜索，不做正文摘要。");
            }
            return {};
        }

        auto payload = readPlainTextPreview(info.absoluteFilePath());
        if (!payload.errorMessage.isEmpty()) {
            if (errorMessage) {
                *errorMessage = payload.errorMessage;
            }
            return {};
        }
        if (truncated && payload.truncated) {
            *truncated = true;
        }
        text = payload.content;
    }

    text = boundedSummaryText(text, truncated);
    if (text.isEmpty() && errorMessage && errorMessage->isEmpty()) {
        *errorMessage = QStringLiteral("当前文档没有可提取的文本内容。");
    }
    return text;
}

void DocumentPreviewService::loadFromFile(const QUrl &sourceUrl, const QString &title)
{
    clear();

    const auto localPath = sourceUrl.isLocalFile() ? sourceUrl.toLocalFile() : sourceUrl.toString();
    if (localPath.trimmed().isEmpty()) {
        m_errorMessage = QStringLiteral("没有可预览的文档路径。");
        emit stateChanged();
        return;
    }

    const QFileInfo info(localPath);
    m_title = title.trimmed().isEmpty() ? info.fileName() : title.trimmed();
    m_sourceUrl = QUrl::fromLocalFile(info.absoluteFilePath());
    if (!info.exists() || !info.isFile()) {
        m_errorMessage = QStringLiteral("文档不存在：%1").arg(info.absoluteFilePath());
        emit stateChanged();
        return;
    }

    const auto payload = buildPreviewPayload(info.absoluteFilePath());
    m_content = payload.content;
    m_isPdf = payload.isPdf;
    m_isMarkdown = payload.isMarkdown;
    m_isRichText = payload.isRichText;
    m_truncated = payload.truncated;
    m_errorMessage = payload.errorMessage;
    emit stateChanged();
}

void DocumentPreviewService::clear()
{
    m_title.clear();
    m_sourceUrl = {};
    m_content.clear();
    m_isPdf = false;
    m_isMarkdown = false;
    m_isRichText = false;
    m_truncated = false;
    m_errorMessage.clear();
    emit stateChanged();
}
