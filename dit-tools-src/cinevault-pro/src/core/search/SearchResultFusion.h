#pragma once

#include "domain/SearchTypes.h"

enum class SearchRecallProtection {
    None = 0,
    EnhancedResultEmpty,
    ResultTargetChanged,
    BaselineHitsAdded
};

struct SearchResultFusionOutcome {
    MaterialSearchResult result;
    SearchRecallProtection protection = SearchRecallProtection::None;
    qsizetype preservedHitCount = 0;
    qsizetype sharedHitCount = 0;
    qsizetype enhancedOnlyHitCount = 0;
};

class SearchResultFusion {
public:
    static SearchResultFusionOutcome preserveBaselineRecall(
        const MaterialSearchResult &baseline,
        const MaterialSearchResult &enhanced);
};
