#pragma once

#include <string_view>

namespace cinevault::searchconfig {

inline constexpr int kSearchIndexSchemaVersion = 1;
inline constexpr int kStructuredVisionProfileVersion = 2;
inline constexpr std::string_view kStructuredVisionPromptVersion = "v4-filmstoryboard-multidimensional";

// 保留供搜索依赖锁与历史测试识别；素材中心的视频解析运行时使用
// VideoAnalysisService::fixedSamplingPolicy() 中的新策略标识。
inline constexpr std::string_view kSamplingPolicy = "fixed_interval";


inline constexpr std::string_view kEmbeddingModelId = "BAAI/bge-small-zh-v1.5";
inline constexpr std::string_view kEmbeddingArtifactId = "Xenova/bge-small-zh-v1.5";
inline constexpr std::string_view kEmbeddingArtifactRevision =
    "75c43b069aac4d136ba6bc1122f995fedcfd2781";
inline constexpr int kEmbeddingDimensions = 512;
inline constexpr int kEmbeddingMaxTokens = 512;

[[nodiscard]] constexpr bool isValidFixedFrameInterval(int interval) noexcept
{
    return interval > 0;
}

static_assert(kSearchIndexSchemaVersion > 0);
static_assert(kStructuredVisionProfileVersion > 1,
              "Version 1 results are legacy partial-search records");
static_assert(kEmbeddingDimensions == 512);

} // namespace cinevault::searchconfig
