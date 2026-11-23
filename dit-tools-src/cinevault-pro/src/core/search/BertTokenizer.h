#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <vector>

struct BertTokenizedInput {
    std::vector<std::int64_t> inputIds;
    std::vector<std::int64_t> attentionMask;
    std::vector<std::int64_t> tokenTypeIds;
};

class BertTokenizer {
public:
    bool loadVocabulary(const QString &vocabularyPath, QString *errorMessage);
    bool isLoaded() const;
    BertTokenizedInput encode(const QString &text, int maxTokens, QString *errorMessage) const;

private:
    QStringList basicTokens(const QString &text) const;
    QStringList wordPieces(const QString &token) const;

    QHash<QString, std::int64_t> m_vocabulary;
    std::int64_t m_unknownId = 100;
    std::int64_t m_classificationId = 101;
    std::int64_t m_separatorId = 102;
};
