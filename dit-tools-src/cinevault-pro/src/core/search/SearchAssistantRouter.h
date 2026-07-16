#pragma once

#include "domain/SearchTypes.h"

class SearchAssistantRouter {
public:
    static SearchAssistantRoutingDecision decide(
        const MaterialSearchResult &baseline,
        bool assistantReady);
};
