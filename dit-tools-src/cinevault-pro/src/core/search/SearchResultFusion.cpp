#include "core/search/SearchResultFusion.h"

#include "core/search/SearchReliabilityEvaluator.h"

#include <QHash>

#include <algorithm>
#include <cmath>

namespace {
qsizetype hitCount(const MaterialSearchResult &result)
{
    return result.folders.size() + result.assets.size() + result.frames.size();
}

void appendWarning(QString *target, const QString &warning)
{
    const auto normalized = warning.trimmed();
    if (normalized.isEmpty() || target->contains(normalized)) {
        return;
    }
    if (!target->isEmpty()) {
        target->append(QStringLiteral("；"));
    }
    target->append(normalized);
}

bool containsAny(const QStringList &reasons, const QStringList &terms)
{
    return std::any_of(reasons.cbegin(), reasons.cend(), [&terms](const QString &reason) {
        return std::any_of(terms.cbegin(), terms.cend(), [&reason](const QString &term) {
            return reason.contains(term, Qt::CaseInsensitive);
        });
    });
}

double evidenceWeight(const QStringList &reasons, bool baseline)
{
    const bool direct = containsAny(reasons, {
        QStringLiteral("完整覆盖"), QStringLiteral("关键词"),
        QStringLiteral("OCR 命中"), QStringLiteral("同一帧"),
        QStringLiteral("同一视觉对象"), QStringLiteral("属性已验证")
    });
    const bool semanticOnly = containsAny(reasons, {
        QStringLiteral("本地语义命中"), QStringLiteral("视觉语义命中")
    }) && !direct;
    if (direct) {
        return baseline ? 1.25 : 1.15;
    }
    if (semanticOnly) {
        return baseline ? 0.90 : 0.75;
    }
    return baseline ? 1.0 : 0.95;
}

void appendUniqueReasons(QStringList *target, const QStringList &source)
{
    for (const auto &reason : source) {
        if (!reason.trimmed().isEmpty() && !target->contains(reason)) {
            target->append(reason);
        }
    }
}

template<typename Item>
struct RankedFusionEntry {
    Item item;
    double reciprocalRankScore = 0.0;
    int baselineRank = -1;
    int enhancedRank = -1;
};

template<typename Item, typename KeySelector, typename ReasonsSelector>
QVector<Item> fuseRanked(const QVector<Item> &baseline,
                        const QVector<Item> &enhanced,
                        KeySelector keySelector,
                        ReasonsSelector reasonsSelector,
                        qsizetype *preservedHitCount,
                        qsizetype *sharedHitCount,
                        qsizetype *enhancedOnlyHitCount)
{
    constexpr double kRrfOffset = 60.0;
    QHash<QString, RankedFusionEntry<Item>> entries;

    for (int rank = 0; rank < baseline.size(); ++rank) {
        const auto &item = baseline.at(rank);
        const auto key = keySelector(item);
        if (key.isEmpty()) {
            continue;
        }
        auto &entry = entries[key];
        entry.item = item;
        entry.baselineRank = rank;
        entry.reciprocalRankScore += evidenceWeight(reasonsSelector(item), true)
            / (kRrfOffset + rank + 1.0);
    }

    for (int rank = 0; rank < enhanced.size(); ++rank) {
        const auto &item = enhanced.at(rank);
        const auto key = keySelector(item);
        if (key.isEmpty()) {
            continue;
        }
        auto existing = entries.find(key);
        if (existing == entries.end()) {
            RankedFusionEntry<Item> entry;
            entry.item = item;
            entry.enhancedRank = rank;
            entry.reciprocalRankScore = evidenceWeight(reasonsSelector(item), false)
                / (kRrfOffset + rank + 1.0);
            entries.insert(key, std::move(entry));
            continue;
        }
        auto baselineReasons = reasonsSelector(existing->item);
        existing->item = item;
        appendUniqueReasons(&reasonsSelector(existing->item), baselineReasons);
        existing->enhancedRank = rank;
        existing->reciprocalRankScore += evidenceWeight(reasonsSelector(item), false)
            / (kRrfOffset + rank + 1.0);
    }

    QVector<RankedFusionEntry<Item>> ranked;
    ranked.reserve(entries.size());
    qsizetype preserved = 0;
    qsizetype shared = 0;
    qsizetype enhancedOnly = 0;
    for (auto iterator = entries.cbegin(); iterator != entries.cend(); ++iterator) {
        ranked.append(iterator.value());
        if (iterator->baselineRank >= 0 && iterator->enhancedRank < 0) {
            ++preserved;
        } else if (iterator->baselineRank >= 0 && iterator->enhancedRank >= 0) {
            ++shared;
        } else if (iterator->enhancedRank >= 0) {
            ++enhancedOnly;
        }
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto &left, const auto &right) {
        if (std::abs(left.reciprocalRankScore - right.reciprocalRankScore) > 0.0000001) {
            return left.reciprocalRankScore > right.reciprocalRankScore;
        }
        if ((left.enhancedRank >= 0) != (right.enhancedRank >= 0)) {
            return left.enhancedRank >= 0;
        }
        const auto leftRank = left.enhancedRank >= 0 ? left.enhancedRank : left.baselineRank;
        const auto rightRank = right.enhancedRank >= 0 ? right.enhancedRank : right.baselineRank;
        return leftRank < rightRank;
    });

    QVector<Item> result;
    result.reserve(ranked.size());
    for (auto &entry : ranked) {
        result.append(std::move(entry.item));
    }
    if (preservedHitCount) {
        *preservedHitCount = preserved;
    }
    if (sharedHitCount) {
        *sharedHitCount = shared;
    }
    if (enhancedOnlyHitCount) {
        *enhancedOnlyHitCount = enhancedOnly;
    }
    return result;
}

void mergeSearchMetadata(MaterialSearchResult *target,
                         const MaterialSearchResult &other)
{
    target->semanticSearchAvailable = target->semanticSearchAvailable
        || other.semanticSearchAvailable;
    target->excludedPartialCount = std::max(target->excludedPartialCount,
                                            other.excludedPartialCount);
    appendWarning(&target->warningMessage, other.warningMessage);
}
}

SearchResultFusionOutcome SearchResultFusion::preserveBaselineRecall(
    const MaterialSearchResult &baseline,
    const MaterialSearchResult &enhanced)
{
    SearchResultFusionOutcome outcome;
    const auto baselineCount = hitCount(baseline);
    const auto enhancedCount = hitCount(enhanced);

    if (baselineCount <= 0) {
        outcome.result = enhanced;
        outcome.enhancedOnlyHitCount = enhancedCount;
        outcome.result.reliability = SearchReliabilityEvaluator::evaluate(outcome.result);
        return outcome;
    }

    if (enhancedCount <= 0) {
        outcome.result = baseline;
        mergeSearchMetadata(&outcome.result, enhanced);
        outcome.protection = SearchRecallProtection::EnhancedResultEmpty;
        outcome.preservedHitCount = baselineCount;
        outcome.result.reliability = SearchReliabilityEvaluator::evaluate(outcome.result);
        return outcome;
    }

    if (baseline.parsedQuery.resultTarget != enhanced.parsedQuery.resultTarget) {
        outcome.result = baseline;
        mergeSearchMetadata(&outcome.result, enhanced);
        outcome.protection = SearchRecallProtection::ResultTargetChanged;
        outcome.preservedHitCount = baselineCount;
        outcome.result.reliability = SearchReliabilityEvaluator::evaluate(outcome.result);
        return outcome;
    }

    outcome.result = enhanced;
    switch (enhanced.parsedQuery.resultTarget) {
    case SearchResultTarget::Folders:
        outcome.result.folders = fuseRanked(
            baseline.folders,
            enhanced.folders,
            [](const FolderSearchHit &item) { return item.folderKey; },
            [](auto &item) -> decltype(auto) { return (item.reasons); },
            &outcome.preservedHitCount,
            &outcome.sharedHitCount,
            &outcome.enhancedOnlyHitCount);
        break;
    case SearchResultTarget::Frames:
        outcome.result.frames = fuseRanked(
            baseline.frames,
            enhanced.frames,
            [](const FrameSearchHit &item) { return item.frameKey; },
            [](auto &item) -> decltype(auto) { return (item.reasons); },
            &outcome.preservedHitCount,
            &outcome.sharedHitCount,
            &outcome.enhancedOnlyHitCount);
        break;
    case SearchResultTarget::Assets:
        outcome.result.assets = fuseRanked(
            baseline.assets,
            enhanced.assets,
            [](const GlobalVideoAsset &item) { return item.videoKey; },
            [](auto &item) -> decltype(auto) { return (item.searchReasons); },
            &outcome.preservedHitCount,
            &outcome.sharedHitCount,
            &outcome.enhancedOnlyHitCount);
        break;
    }
    mergeSearchMetadata(&outcome.result, baseline);
    if (outcome.preservedHitCount > 0) {
        outcome.protection = SearchRecallProtection::BaselineHitsAdded;
    }
    outcome.result.reliability = SearchReliabilityEvaluator::evaluate(outcome.result);
    return outcome;
}
