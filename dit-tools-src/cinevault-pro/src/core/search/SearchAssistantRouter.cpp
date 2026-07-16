#include "core/search/SearchAssistantRouter.h"

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

bool hasComplexLanguage(const QString &text)
{
    return containsAny(text, {
        QStringLiteral("不要"), QStringLiteral("排除"), QStringLiteral("不包含"),
        QStringLiteral("不是"), QStringLiteral("除了"), QStringLiteral("但不要"),
        QStringLiteral("同时"), QStringLiteral("并且"), QStringLiteral("以及"),
        QStringLiteral("或者"), QStringLiteral("或是"), QStringLiteral("穿着"),
        QStringLiteral("戴着"), QStringLiteral("拿着"), QStringLiteral("站在"),
        QStringLiteral("旁边"), QStringLiteral("前面"), QStringLiteral("后面"),
        QStringLiteral("类似"), QStringLiteral("那个"), QStringLiteral("大概")
    });
}

bool hasStrongDirectEvidence(const SearchReliabilityAssessment &assessment)
{
    return std::any_of(
        assessment.reasons.cbegin(), assessment.reasons.cend(), [](const QString &reason) {
        return containsAny(reason, {
            QStringLiteral("完整覆盖"), QStringLiteral("OCR 命中"),
            QStringLiteral("同一帧"), QStringLiteral("同一视觉对象"),
            QStringLiteral("关键词或视觉文本命中"), QStringLiteral("直接证据")
        });
    });
}

qsizetype resultCount(const MaterialSearchResult &result)
{
    return result.folders.size() + result.assets.size() + result.frames.size();
}
}

SearchAssistantRoutingDecision SearchAssistantRouter::decide(
    const MaterialSearchResult &baseline,
    bool assistantReady)
{
    SearchAssistantRoutingDecision decision;
    const auto &query = baseline.parsedQuery;
    const auto text = query.originalText.simplified();
    if (text.isEmpty()) {
        decision.reasons.append(QStringLiteral("空查询无需模型辅助"));
        return decision;
    }

    const bool hasContent = !query.semanticText.trimmed().isEmpty()
        || !query.strictEntities.isEmpty()
        || !query.explicitEntityLabels.isEmpty()
        || !query.ocrText.trimmed().isEmpty();
    if (!hasContent) {
        decision.reasons.append(QStringLiteral("本地规则已完整解析结构化条件"));
        return decision;
    }
    if (isExactLookup(query)) {
        decision.reasons.append(QStringLiteral("精确文件、路径或 OCR 查询不进行语义改写"));
        return decision;
    }

    const bool multiEntity = query.explicitEntityLabels.size() >= 2
        || query.strictEntities.size() >= 2;
    if (hasComplexLanguage(text) || multiEntity) {
        decision.route = SearchAssistantRoute::Required;
        decision.reasons.append(multiEntity
            ? QStringLiteral("查询包含需要同帧理解的多个实体")
            : QStringLiteral("查询包含关系、否定、并列或模糊表达"));
        return decision;
    }

    const auto count = resultCount(baseline);
    if (count <= 0) {
        decision.route = SearchAssistantRoute::Required;
        decision.reasons.append(QStringLiteral("首轮内容搜索没有返回结果"));
        return decision;
    }

    const auto &reliability = baseline.reliability;
    const bool strongEvidence = hasStrongDirectEvidence(reliability);
    if (reliability.score >= 0.82 && strongEvidence) {
        decision.reasons.append(QStringLiteral("首轮结果具有高可靠直接证据"));
        return decision;
    }
    if (reliability.score < 0.68 || reliability.shouldUseAssistant) {
        decision.route = SearchAssistantRoute::Required;
        decision.reasons.append(QStringLiteral("首轮查询或结果可靠性不足"));
        return decision;
    }

    decision.route = SearchAssistantRoute::ReadyOnly;
    decision.reasons.append(assistantReady
        ? QStringLiteral("首轮结果处于可靠性灰区，使用已就绪模型补充理解")
        : QStringLiteral("首轮结果处于可靠性灰区，不为本次查询冷启动模型"));
    return decision;
}
