#pragma once

#include "domain/SearchTypes.h"

class SearchReliabilityEvaluator {
public:
    static SearchReliabilityAssessment evaluate(const MaterialSearchResult &result);
};
