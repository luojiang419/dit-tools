#include "core/search/SearchReliabilityEvaluator.h"

#include <QRegularExpression>

#include <algorithm>

namespace {
bool containsAny(const QString &text, const QStringList &terms)
{
    return std::any_of(terms.cbegin(), terms.cend(), [&text](const QString &term) {
        return text.contains(term, Qt::CaseInsensitive);
    });
}

bool isExactLookup(const ParsedMaterialQuery &query)
{
    const auto text = query.originalText.trimmed();
    if (!query.ocrText.trimmed().isEmpty()
        || text.contains(QLatin1Char('"'))
        || text.contains(QLatin1Char('\''))
        || text.contains(QStringLiteral("“"))
        || text.contains(QStringLiteral("”"))) {
        return true;
    }
    static const QRegularExpression windowsPath(
        QStringLiteral("[A-Za-z]:[\\\\/]|[\\\\/]{2}[^\\s]+"));
    static const QRegularExpression fileExtension(
        QStringLiteral("\\.[A-Za-z0-9]{2,8}(?:\\s|$)"));
    return windowsPath.match(text).hasMatch() || fileExtension.match(text).hasMatch();
}

bool hasComplexRelation(const QString &text)
{
    return containsAny(text, {
        QStringLiteral("不要"), QStringLiteral("排除"), QStringLiteral("不包含"),
        QStringLiteral("不是"), QStringLiteral("除了"), QStringLiteral("但是"),
        QStringLiteral("同时"), QStringLiteral("并且"), QStringLiteral("以及"),
        QStringLiteral("类似"), QStringLiteral("那个"), QStringLiteral("大概"),
        QStringLiteral("左右"), QStringLiteral("更像")
    });
}

bool hasStrongEvidence(const QStringList &reasons)
{
    return std::any_of(reasons.cbegin(), reasons.cend(), [](const QString &reason) {
        return containsAny(reason, {
            QStringLiteral("完整覆盖"), QStringLiteral("OCR 命中"),
            QStringLiteral("同一帧"), QStringLiteral("同一视觉对象"),
            QStringLiteral("关键词或视觉文本命中")
        });
    });
}

bool hasSemanticEvidence(const QStringList &reasons)
{
    return std::any_of(reasons.cbegin(), reasons.cend(), [](const QString &reason) {
        return reason.contains(QStringLiteral("语义命中"));
    });
}

struct ResultEvidence {
    double score = 0.0;
    double confidence = 0.0;
    QStringList reasons;
};

QVector<ResultEvidence> evidenceFor(const MaterialSearchResult &result)
{
    QVector<ResultEvidence> evidence;
    if (result.parsedQuery.resultTarget == SearchResultTarget::Folders) {
        evidence.reserve(result.folders.size());
        for (const auto &item : result.folders) {
            evidence.append({item.score, item.confidence, item.reasons});
        }
    } else if (result.parsedQuery.resultTarget == SearchResultTarget::Frames) {
        evidence.reserve(result.frames.size());
        for (const auto &item : result.frames) {
            evidence.append({item.score, item.confidence, item.reasons});
        }
    } else {
        evidence.reserve(result.assets.size());
        for (const auto &item : result.assets) {
            evidence.append({item.searchScore, item.searchConfidence, item.searchReasons});
        }
    }
    std::stable_sort(evidence.begin(), evidence.end(), [](const auto &left, const auto &right) {
        return left.score > right.score;
    });
    return evidence;
}
}

SearchReliabilityAssessment SearchReliabilityEvaluator::evaluate(
    const MaterialSearchResult &result)
{
    SearchReliabilityAssessment assessment;
    const auto &query = result.parsedQuery;
    const auto originalText = query.originalText.simplified();
    if (originalText.isEmpty()) {
        assessment.reasons.append(QStringLiteral("空查询无需模型辅助"));
        return assessment;
    }

    if (result.warningMessage.contains(QStringLiteral("数据库尚未打开"))
        || result.warningMessage.contains(QStringLiteral("检索引擎尚未初始化"))) {
        assessment.score = 0.0;
        assessment.reasons.append(QStringLiteral("搜索基础设施不可用，模型无法补救"));
        return assessment;
    }

    const bool hasContentQuery = !query.semanticText.trimmed().isEmpty()
        || !query.strictEntities.isEmpty()
        || !query.ocrText.trimmed().isEmpty();
    if (!hasContentQuery) {
        assessment.reasons.append(QStringLiteral("日期、类型和结果目标已由本地规则完整解析"));
        return assessment;
    }

    const auto evidence = evidenceFor(result);
    assessment.resultCount = evidence.size();
    if (!evidence.isEmpty()) {
        assessment.bestResultScore = std::clamp(evidence.first().score, 0.0, 1.0);
        assessment.bestResultConfidence = std::clamp(evidence.first().confidence, 0.0, 1.0);
    }

    if (isExactLookup(query)) {
        assessment.score = evidence.isEmpty() ? 0.45 : 0.95;
        assessment.reasons.append(QStringLiteral("精确文件、路径或 OCR 查询不适合语义改写"));
        return assessment;
    }

    const bool complexRelation = hasComplexRelation(originalText);
    if (evidence.isEmpty()) {
        assessment.score = complexRelation ? 0.05 : 0.15;
        assessment.shouldUseAssistant = true;
        assessment.reasons.append(QStringLiteral("本地内容搜索没有返回结果"));
        if (complexRelation) {
            assessment.reasons.append(QStringLiteral("查询包含需要补充理解的复杂关系"));
        }
        return assessment;
    }

    const bool strongEvidence = hasStrongEvidence(evidence.first().reasons);
    const bool semanticEvidence = hasSemanticEvidence(evidence.first().reasons);
    double score = 0.25
        + (0.45 * assessment.bestResultConfidence)
        + (0.25 * assessment.bestResultScore);
    if (strongEvidence) {
        score += 0.12;
        assessment.reasons.append(QStringLiteral("首项具有关键词、实体或 OCR 直接证据"));
    } else if (semanticEvidence) {
        score -= 0.10;
        assessment.reasons.append(QStringLiteral("首项主要依赖语义相似度"));
    }
    if (!query.strictEntities.isEmpty() && strongEvidence) {
        score += 0.08;
    }
    if (evidence.size() >= 5) {
        const auto topFiveMargin = evidence.first().score - evidence.at(4).score;
        if (topFiveMargin < 0.04 && !strongEvidence) {
            score -= 0.08;
            assessment.reasons.append(QStringLiteral("前五项分差过小且缺少直接证据"));
        }
    }
    if (complexRelation) {
        score -= 0.28;
        assessment.reasons.append(QStringLiteral("查询包含需要补充理解的复杂关系"));
    }

    assessment.score = std::clamp(score, 0.0, 1.0);
    assessment.shouldUseAssistant = complexRelation || assessment.score < 0.62;
    if (assessment.shouldUseAssistant) {
        assessment.reasons.prepend(QStringLiteral("本地查询或结果可靠性不足"));
    } else {
        assessment.reasons.prepend(QStringLiteral("本地查询和结果证据可靠"));
    }
    return assessment;
}
