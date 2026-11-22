#include "core/search/BertTokenizer.h"

#include <QFile>
#include <QStringConverter>
#include <QTextStream>

namespace {
bool isChineseCharacter(QChar character)
{
    const auto value = character.unicode();
    return (value >= 0x3400 && value <= 0x4DBF)
        || (value >= 0x4E00 && value <= 0x9FFF)
        || (value >= 0xF900 && value <= 0xFAFF);
}

bool isDiscardedControl(QChar character)
{
    return character.category() == QChar::Other_Control
        && character != QLatin1Char('\t')
        && character != QLatin1Char('\n')
        && character != QLatin1Char('\r');
}
}

bool BertTokenizer::loadVocabulary(const QString &vocabularyPath, QString *errorMessage)
{
    m_vocabulary.clear();
    QFile file(vocabularyPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法打开 BGE 词表：%1").arg(vocabularyPath);
        }
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    std::int64_t id = 0;
    while (!stream.atEnd()) {
        auto token = stream.readLine();
        if (token.endsWith(QLatin1Char('\r'))) {
            token.chop(1);
        }
        if (!token.isEmpty() && !m_vocabulary.contains(token)) {
            m_vocabulary.insert(token, id);
        }
        ++id;
    }
    if (m_vocabulary.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("BGE 词表为空：%1").arg(vocabularyPath);
        }
        return false;
    }
    m_unknownId = m_vocabulary.value(QStringLiteral("[UNK]"), 100);
    m_classificationId = m_vocabulary.value(QStringLiteral("[CLS]"), 101);
    m_separatorId = m_vocabulary.value(QStringLiteral("[SEP]"), 102);
    return true;
}

bool BertTokenizer::isLoaded() const
{
    return !m_vocabulary.isEmpty();
}

QStringList BertTokenizer::basicTokens(const QString &text) const
{
    QStringList tokens;
    QString current;
    const auto flush = [&]() {
        if (!current.isEmpty()) {
            tokens.append(current);
            current.clear();
        }
    };

    for (const auto character : text.normalized(QString::NormalizationForm_C).toLower()) {
        if (isDiscardedControl(character)) {
            continue;
        }
        if (character.isSpace()) {
            flush();
            continue;
        }
        if (isChineseCharacter(character) || character.isPunct() || character.isSymbol()) {
            flush();
            tokens.append(QString(character));
            continue;
        }
        current.append(character);
    }
    flush();
    return tokens;
}

QStringList BertTokenizer::wordPieces(const QString &token) const
{
    if (token.isEmpty()) {
        return {};
    }
    if (token.size() > 100) {
        return {QStringLiteral("[UNK]")};
    }

    QStringList pieces;
    int start = 0;
    while (start < token.size()) {
        auto end = token.size();
        QString matched;
        while (end > start) {
            auto candidate = token.mid(start, end - start);
            if (start > 0) {
                candidate.prepend(QStringLiteral("##"));
            }
            if (m_vocabulary.contains(candidate)) {
                matched = candidate;
                break;
            }
            --end;
        }
        if (matched.isEmpty()) {
            return {QStringLiteral("[UNK]")};
        }
        pieces.append(matched);
        start = end;
    }
    return pieces;
}

BertTokenizedInput BertTokenizer::encode(const QString &text,
                                         int maxTokens,
                                         QString *errorMessage) const
{
    BertTokenizedInput input;
    if (!isLoaded()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("BGE 分词器尚未加载");
        }
        return input;
    }

    const auto boundedMaxTokens = qMax(2, maxTokens);
    input.inputIds.reserve(boundedMaxTokens);
    input.inputIds.push_back(m_classificationId);
    for (const auto &token : basicTokens(text)) {
        for (const auto &piece : wordPieces(token)) {
            if (static_cast<int>(input.inputIds.size()) >= boundedMaxTokens - 1) {
                break;
            }
            input.inputIds.push_back(m_vocabulary.value(piece, m_unknownId));
        }
        if (static_cast<int>(input.inputIds.size()) >= boundedMaxTokens - 1) {
            break;
        }
    }
    input.inputIds.push_back(m_separatorId);
    input.attentionMask.assign(input.inputIds.size(), 1);
    input.tokenTypeIds.assign(input.inputIds.size(), 0);
    return input;
}
