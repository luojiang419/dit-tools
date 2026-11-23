#include "core/search/SemanticVectorIndex.h"

#include <QByteArray>
#include <QFile>
#include <QSaveFile>

#include <algorithm>
#include <limits>
#include <vector>

#if defined(CINEVAULT_HAS_LOCAL_SEARCH) && CINEVAULT_HAS_LOCAL_SEARCH
extern "C" {
#include <usearch.h>
}
#endif

struct SemanticVectorIndex::Impl {
#if defined(CINEVAULT_HAS_LOCAL_SEARCH) && CINEVAULT_HAS_LOCAL_SEARCH
    usearch_index_t handle = nullptr;
#endif
    int dimensions = 0;

    void clear()
    {
#if defined(CINEVAULT_HAS_LOCAL_SEARCH) && CINEVAULT_HAS_LOCAL_SEARCH
        if (handle) {
            usearch_error_t error = nullptr;
            usearch_free(handle, &error);
            handle = nullptr;
        }
#endif
        dimensions = 0;
    }

    ~Impl() { clear(); }
};

namespace {
bool setError(const char *error, QString *errorMessage, const QString &prefix)
{
    if (!error) {
        return true;
    }
    if (errorMessage) {
        *errorMessage = QStringLiteral("%1：%2").arg(prefix, QString::fromUtf8(error));
    }
    return false;
}
}

SemanticVectorIndex::SemanticVectorIndex()
    : m_impl(std::make_unique<Impl>())
{
}

SemanticVectorIndex::~SemanticVectorIndex() = default;

SemanticVectorIndex::SemanticVectorIndex(SemanticVectorIndex &&) noexcept = default;

SemanticVectorIndex &SemanticVectorIndex::operator=(SemanticVectorIndex &&) noexcept = default;

bool SemanticVectorIndex::reset(int dimensions, QString *errorMessage)
{
    m_impl->clear();
    if (dimensions <= 0) {
        if (errorMessage) *errorMessage = QStringLiteral("USearch 向量维度必须为正整数");
        return false;
    }
#if !defined(CINEVAULT_HAS_LOCAL_SEARCH) || !CINEVAULT_HAS_LOCAL_SEARCH
    if (errorMessage) *errorMessage = QStringLiteral("当前构建未启用 USearch");
    return false;
#else
    usearch_init_options_t options{};
    options.metric_kind = usearch_metric_cos_k;
    options.quantization = usearch_scalar_f32_k;
    options.dimensions = static_cast<std::size_t>(dimensions);
    options.connectivity = 16;
    options.expansion_add = 128;
    options.expansion_search = 64;
    options.multi = false;
    usearch_error_t error = nullptr;
    m_impl->handle = usearch_init(&options, &error);
    if (!setError(error, errorMessage, QStringLiteral("初始化 USearch 失败")) || !m_impl->handle) {
        m_impl->clear();
        return false;
    }
    m_impl->dimensions = dimensions;
    return true;
#endif
}

bool SemanticVectorIndex::load(const QString &filePath, int dimensions, QString *errorMessage)
{
    if (!reset(dimensions, errorMessage)) {
        return false;
    }
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = QStringLiteral("无法读取 USearch 索引：%1").arg(filePath);
        m_impl->clear();
        return false;
    }
    const auto bytes = file.readAll();
    if (bytes.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("USearch 索引文件为空：%1").arg(filePath);
        m_impl->clear();
        return false;
    }
#if defined(CINEVAULT_HAS_LOCAL_SEARCH) && CINEVAULT_HAS_LOCAL_SEARCH
    usearch_error_t error = nullptr;
    usearch_load_buffer(m_impl->handle,
                        bytes.constData(),
                        static_cast<std::size_t>(bytes.size()),
                        &error);
    if (!setError(error, errorMessage, QStringLiteral("加载 USearch 索引失败"))) {
        m_impl->clear();
        return false;
    }
    const auto loadedDimensions = usearch_dimensions(m_impl->handle, &error);
    if (!setError(error, errorMessage, QStringLiteral("读取 USearch 索引维度失败"))) {
        m_impl->clear();
        return false;
    }
    if (loadedDimensions != static_cast<std::size_t>(dimensions)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("USearch 索引维度不匹配：期望 %1，实际 %2")
                                .arg(dimensions)
                                .arg(static_cast<qulonglong>(loadedDimensions));
        }
        m_impl->clear();
        return false;
    }
    return true;
#else
    Q_UNUSED(bytes);
    return false;
#endif
}

bool SemanticVectorIndex::save(const QString &filePath, QString *errorMessage) const
{
#if !defined(CINEVAULT_HAS_LOCAL_SEARCH) || !CINEVAULT_HAS_LOCAL_SEARCH
    if (errorMessage) *errorMessage = QStringLiteral("当前构建未启用 USearch");
    return false;
#else
    if (!m_impl->handle) {
        if (errorMessage) *errorMessage = QStringLiteral("USearch 索引尚未初始化");
        return false;
    }
    usearch_error_t error = nullptr;
    const auto length = usearch_serialized_length(m_impl->handle, &error);
    if (!setError(error, errorMessage, QStringLiteral("计算 USearch 索引长度失败")) || length == 0) {
        return false;
    }
    if (length > static_cast<std::size_t>(std::numeric_limits<qsizetype>::max())) {
        if (errorMessage) *errorMessage = QStringLiteral("USearch 索引过大，无法写入 Qt 缓冲区");
        return false;
    }
    QByteArray bytes;
    bytes.resize(static_cast<qsizetype>(length));
    usearch_save_buffer(m_impl->handle, bytes.data(), length, &error);
    if (!setError(error, errorMessage, QStringLiteral("序列化 USearch 索引失败"))) {
        return false;
    }
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(bytes) != bytes.size()
        || !file.commit()) {
        if (errorMessage) *errorMessage = QStringLiteral("保存 USearch 索引失败：%1").arg(filePath);
        return false;
    }
    return true;
#endif
}

bool SemanticVectorIndex::reserve(qsizetype capacity, QString *errorMessage)
{
#if defined(CINEVAULT_HAS_LOCAL_SEARCH) && CINEVAULT_HAS_LOCAL_SEARCH
    if (!m_impl->handle) {
        if (errorMessage) *errorMessage = QStringLiteral("USearch 索引尚未初始化");
        return false;
    }
    usearch_error_t error = nullptr;
    usearch_reserve(m_impl->handle, static_cast<std::size_t>(qMax<qsizetype>(0, capacity)), &error);
    return setError(error, errorMessage, QStringLiteral("扩容 USearch 索引失败"));
#else
    Q_UNUSED(capacity);
    if (errorMessage) *errorMessage = QStringLiteral("当前构建未启用 USearch");
    return false;
#endif
}

bool SemanticVectorIndex::add(quint64 key,
                              const QVector<float> &embedding,
                              QString *errorMessage)
{
    if (embedding.size() != m_impl->dimensions) {
        if (errorMessage) *errorMessage = QStringLiteral("USearch 写入向量维度不匹配");
        return false;
    }
#if defined(CINEVAULT_HAS_LOCAL_SEARCH) && CINEVAULT_HAS_LOCAL_SEARCH
    if (!m_impl->handle) {
        if (errorMessage) *errorMessage = QStringLiteral("USearch 索引尚未初始化");
        return false;
    }
    usearch_error_t error = nullptr;
    usearch_add(m_impl->handle,
                static_cast<usearch_key_t>(key),
                embedding.constData(),
                usearch_scalar_f32_k,
                &error);
    return setError(error, errorMessage, QStringLiteral("写入 USearch 向量失败"));
#else
    Q_UNUSED(key);
    if (errorMessage) *errorMessage = QStringLiteral("当前构建未启用 USearch");
    return false;
#endif
}

bool SemanticVectorIndex::remove(quint64 key, QString *errorMessage)
{
#if defined(CINEVAULT_HAS_LOCAL_SEARCH) && CINEVAULT_HAS_LOCAL_SEARCH
    if (!m_impl->handle) {
        if (errorMessage) *errorMessage = QStringLiteral("USearch 索引尚未初始化");
        return false;
    }
    usearch_error_t error = nullptr;
    usearch_remove(m_impl->handle, static_cast<usearch_key_t>(key), &error);
    return setError(error, errorMessage, QStringLiteral("删除 USearch 向量失败"));
#else
    Q_UNUSED(key);
    if (errorMessage) *errorMessage = QStringLiteral("当前构建未启用 USearch");
    return false;
#endif
}

QVector<QPair<quint64, double>> SemanticVectorIndex::search(const QVector<float> &embedding,
                                                            qsizetype limit,
                                                            QString *errorMessage) const
{
    QVector<QPair<quint64, double>> hits;
    if (embedding.size() != m_impl->dimensions || limit <= 0) {
        return hits;
    }
#if defined(CINEVAULT_HAS_LOCAL_SEARCH) && CINEVAULT_HAS_LOCAL_SEARCH
    if (!m_impl->handle) {
        if (errorMessage) *errorMessage = QStringLiteral("USearch 索引尚未初始化");
        return hits;
    }
    usearch_error_t error = nullptr;
    const auto indexSize = usearch_size(m_impl->handle, &error);
    if (!setError(error, errorMessage, QStringLiteral("读取 USearch 索引大小失败"))) {
        return {};
    }
    const auto requested = std::min(static_cast<std::size_t>(limit), indexSize);
    if (requested == 0) {
        return hits;
    }
    std::vector<usearch_key_t> keys(requested);
    std::vector<usearch_distance_t> distances(requested);
    error = nullptr;
    const auto count = usearch_search(m_impl->handle,
                                      embedding.constData(),
                                      usearch_scalar_f32_k,
                                      requested,
                                      keys.data(),
                                      distances.data(),
                                      &error);
    if (!setError(error, errorMessage, QStringLiteral("查询 USearch 索引失败"))) {
        return {};
    }
    hits.reserve(static_cast<qsizetype>(count));
    for (std::size_t index = 0; index < count; ++index) {
        const auto similarity = std::clamp(1.0 - static_cast<double>(distances[index]), -1.0, 1.0);
        hits.append(qMakePair(static_cast<quint64>(keys[index]), similarity));
    }
#else
    Q_UNUSED(errorMessage);
#endif
    return hits;
}

qsizetype SemanticVectorIndex::size(QString *errorMessage) const
{
#if defined(CINEVAULT_HAS_LOCAL_SEARCH) && CINEVAULT_HAS_LOCAL_SEARCH
    if (!m_impl->handle) {
        if (errorMessage) *errorMessage = QStringLiteral("USearch 索引尚未初始化");
        return 0;
    }
    usearch_error_t error = nullptr;
    const auto count = usearch_size(m_impl->handle, &error);
    if (!setError(error, errorMessage, QStringLiteral("读取 USearch 索引大小失败"))) {
        return 0;
    }
    return static_cast<qsizetype>(count);
#else
    Q_UNUSED(errorMessage);
    return 0;
#endif
}

int SemanticVectorIndex::dimensions() const
{
    return m_impl->dimensions;
}
