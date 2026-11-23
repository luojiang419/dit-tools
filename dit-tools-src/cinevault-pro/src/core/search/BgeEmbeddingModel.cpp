#include "core/search/BgeEmbeddingModel.h"

#include "core/search/BertTokenizer.h"
#include "shared/SearchConfiguration.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#if defined(CINEVAULT_HAS_LOCAL_SEARCH) && CINEVAULT_HAS_LOCAL_SEARCH
#include <onnxruntime_cxx_api.h>
#endif

namespace {
QString configuredModelRoot()
{
    const auto environmentRoot = QString::fromLocal8Bit(qgetenv("CINEVAULT_BGE_MODEL_ROOT")).trimmed();
    if (!environmentRoot.isEmpty()) {
        return QDir::cleanPath(environmentRoot);
    }
    const auto installedRoot = QDir(QCoreApplication::applicationDirPath())
                                   .filePath(QStringLiteral("data/models/bge-small-zh-v1.5"));
    if (QFileInfo::exists(QDir(installedRoot).filePath(QStringLiteral("onnx/model_quantized.onnx")))) {
        return QDir::cleanPath(installedRoot);
    }
#if defined(CINEVAULT_LOCAL_SEARCH_MODEL_ROOT)
    return QDir::cleanPath(QStringLiteral(CINEVAULT_LOCAL_SEARCH_MODEL_ROOT));
#else
    return {};
#endif
}

QString queryInstruction(const QString &text)
{
    return QStringLiteral("为这个句子生成表示以用于检索相关文章：%1").arg(text.trimmed());
}

int configuredInferenceThreads()
{
    const auto idealThreads = qMax(1, QThread::idealThreadCount());
    const auto defaultThreads = qMax(1, idealThreads - 2);
    bool ok = false;
    const auto configured = QString::fromLocal8Bit(qgetenv("CINEVAULT_BGE_THREADS")).toInt(&ok);
    return ok && configured > 0
        ? qBound(1, configured, 256)
        : defaultThreads;
}

int configuredBatchSize()
{
    bool ok = false;
    const auto configured = QString::fromLocal8Bit(qgetenv("CINEVAULT_BGE_BATCH_SIZE")).toInt(&ok);
    return ok && configured > 0
        ? qBound(1, configured, 64)
        : 32;
}
}

struct BgeEmbeddingModel::Impl {
    QString root = configuredModelRoot();
    int inferenceThreads = configuredInferenceThreads();
    int configuredBatch = configuredBatchSize();
    mutable QMutex mutex;
    mutable bool initialized = false;
    mutable QString initializationError;
    mutable BertTokenizer tokenizer;

#if defined(CINEVAULT_HAS_LOCAL_SEARCH) && CINEVAULT_HAS_LOCAL_SEARCH
    mutable std::unique_ptr<Ort::Env> environment;
    mutable std::unique_ptr<Ort::Session> session;
    mutable std::vector<std::string> inputNames;
    mutable std::vector<std::string> outputNames;
#endif

    bool ensureInitialized() const
    {
        if (initialized) {
            return initializationError.isEmpty();
        }
        initialized = true;
#if !defined(CINEVAULT_HAS_LOCAL_SEARCH) || !CINEVAULT_HAS_LOCAL_SEARCH
        initializationError = QStringLiteral("当前构建未启用本地语义搜索");
        return false;
#else
        const auto modelPath = QDir(root).filePath(QStringLiteral("onnx/model_quantized.onnx"));
        const auto vocabularyPath = QDir(root).filePath(QStringLiteral("vocab.txt"));
        if (!QFileInfo::exists(modelPath) || !QFileInfo::exists(vocabularyPath)) {
            initializationError = QStringLiteral("BGE 模型资产不完整：%1").arg(root);
            return false;
        }
        if (!tokenizer.loadVocabulary(vocabularyPath, &initializationError)) {
            return false;
        }
        try {
            environment = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "CineVaultBge");
            Ort::SessionOptions options;
            options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
            options.SetIntraOpNumThreads(inferenceThreads);
            options.SetInterOpNumThreads(1);
#if defined(Q_OS_WIN)
            const auto nativeModelPath = QDir::toNativeSeparators(modelPath).toStdWString();
            session = std::make_unique<Ort::Session>(*environment, nativeModelPath.c_str(), options);
#else
            const auto nativeModelPath = QFile::encodeName(modelPath).toStdString();
            session = std::make_unique<Ort::Session>(*environment, nativeModelPath.c_str(), options);
#endif
            Ort::AllocatorWithDefaultOptions allocator;
            for (std::size_t index = 0; index < session->GetInputCount(); ++index) {
                auto name = session->GetInputNameAllocated(index, allocator);
                inputNames.emplace_back(name.get());
            }
            for (std::size_t index = 0; index < session->GetOutputCount(); ++index) {
                auto name = session->GetOutputNameAllocated(index, allocator);
                outputNames.emplace_back(name.get());
            }
            if (inputNames.empty() || outputNames.empty()) {
                initializationError = QStringLiteral("BGE ONNX 输入或输出为空");
                session.reset();
                return false;
            }
        } catch (const Ort::Exception &exception) {
            initializationError = QStringLiteral("加载 BGE ONNX 模型失败：%1")
                                      .arg(QString::fromUtf8(exception.what()));
            session.reset();
            return false;
        }
        return true;
#endif
    }

    QVector<QVector<float>> embedBatch(const QStringList &texts,
                                       bool query,
                                       const EmbeddingProgressCallback &progressCallback,
                                       QString *errorMessage) const
    {
        QMutexLocker locker(&mutex);
        QVector<QVector<float>> embeddings;
        if (texts.isEmpty()) {
            return embeddings;
        }
        if (!ensureInitialized()) {
            if (errorMessage) *errorMessage = initializationError;
            return embeddings;
        }
#if !defined(CINEVAULT_HAS_LOCAL_SEARCH) || !CINEVAULT_HAS_LOCAL_SEARCH
        return embeddings;
#else
        embeddings.reserve(texts.size());
        for (qsizetype offset = 0; offset < texts.size(); offset += configuredBatch) {
            const auto count = qMin<qsizetype>(configuredBatch, texts.size() - offset);
            std::vector<BertTokenizedInput> encodedInputs;
            encodedInputs.reserve(static_cast<std::size_t>(count));
            std::size_t sequenceLength = 0;
            for (qsizetype index = 0; index < count; ++index) {
                auto input = tokenizer.encode(
                    query ? queryInstruction(texts.at(offset + index)) : texts.at(offset + index),
                    cinevault::searchconfig::kEmbeddingMaxTokens,
                    errorMessage);
                if (input.inputIds.empty()) {
                    if (errorMessage && errorMessage->isEmpty()) {
                        *errorMessage = QStringLiteral("BGE 分词结果为空：第 %1 条文本")
                                            .arg(offset + index + 1);
                    }
                    return {};
                }
                sequenceLength = std::max(sequenceLength, input.inputIds.size());
                encodedInputs.emplace_back(std::move(input));
            }

            const auto elementCount = static_cast<std::size_t>(count) * sequenceLength;
            std::vector<std::int64_t> inputIds(elementCount, 0);
            std::vector<std::int64_t> attentionMask(elementCount, 0);
            std::vector<std::int64_t> tokenTypeIds(elementCount, 0);
            for (std::size_t row = 0; row < encodedInputs.size(); ++row) {
                const auto &input = encodedInputs.at(row);
                const auto rowOffset = row * sequenceLength;
                std::copy(input.inputIds.cbegin(), input.inputIds.cend(), inputIds.begin() + rowOffset);
                std::copy(input.attentionMask.cbegin(), input.attentionMask.cend(), attentionMask.begin() + rowOffset);
                std::copy(input.tokenTypeIds.cbegin(), input.tokenTypeIds.cend(), tokenTypeIds.begin() + rowOffset);
            }

            try {
                const std::array<std::int64_t, 2> shape{
                    static_cast<std::int64_t>(count),
                    static_cast<std::int64_t>(sequenceLength)};
                auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
                std::vector<Ort::Value> inputValues;
                std::vector<const char *> inputNamePointers;
                inputValues.reserve(inputNames.size());
                inputNamePointers.reserve(inputNames.size());
                for (const auto &name : inputNames) {
                    std::vector<std::int64_t> *values = &inputIds;
                    if (name == "attention_mask") {
                        values = &attentionMask;
                    } else if (name == "token_type_ids") {
                        values = &tokenTypeIds;
                    } else if (name != "input_ids") {
                        if (errorMessage) {
                            *errorMessage = QStringLiteral("BGE ONNX 包含不支持的输入：%1")
                                                .arg(QString::fromStdString(name));
                        }
                        return {};
                    }
                    inputValues.emplace_back(Ort::Value::CreateTensor<std::int64_t>(
                        memoryInfo,
                        values->data(),
                        values->size(),
                        shape.data(),
                        shape.size()));
                    inputNamePointers.push_back(name.c_str());
                }
                std::vector<const char *> outputNamePointers;
                outputNamePointers.reserve(outputNames.size());
                for (const auto &name : outputNames) {
                    outputNamePointers.push_back(name.c_str());
                }
                auto outputs = session->Run(Ort::RunOptions{nullptr},
                                            inputNamePointers.data(),
                                            inputValues.data(),
                                            inputValues.size(),
                                            outputNamePointers.data(),
                                            outputNamePointers.size());
                if (outputs.empty() || !outputs.front().IsTensor()) {
                    if (errorMessage) *errorMessage = QStringLiteral("BGE ONNX 未返回张量");
                    return {};
                }
                const auto info = outputs.front().GetTensorTypeAndShapeInfo();
                const auto outputShape = info.GetShape();
                const auto outputElementCount = info.GetElementCount();
                if (outputShape.empty()
                    || outputShape.front() != count
                    || outputShape.back() != cinevault::searchconfig::kEmbeddingDimensions
                    || outputElementCount % static_cast<std::size_t>(count) != 0) {
                    if (errorMessage) *errorMessage = QStringLiteral("BGE 批量输出不符合 512 维契约");
                    return {};
                }

                const auto valuesPerItem = outputElementCount / static_cast<std::size_t>(count);
                if (valuesPerItem < static_cast<std::size_t>(cinevault::searchconfig::kEmbeddingDimensions)) {
                    if (errorMessage) *errorMessage = QStringLiteral("BGE 批量输出数据不完整");
                    return {};
                }
                const auto *values = outputs.front().GetTensorData<float>();
                for (qsizetype row = 0; row < count; ++row) {
                    QVector<float> embedding(cinevault::searchconfig::kEmbeddingDimensions);
                    const auto *rowValues = values + (static_cast<std::size_t>(row) * valuesPerItem);
                    double squaredNorm = 0.0;
                    for (int index = 0; index < embedding.size(); ++index) {
                        embedding[index] = rowValues[index];
                        squaredNorm += static_cast<double>(embedding[index]) * embedding[index];
                    }
                    const auto norm = std::sqrt(squaredNorm);
                    if (norm <= 0.0) {
                        if (errorMessage) *errorMessage = QStringLiteral("BGE 输出向量范数为零");
                        return {};
                    }
                    for (auto &value : embedding) {
                        value = static_cast<float>(value / norm);
                    }
                    embeddings.append(std::move(embedding));
                }
            } catch (const Ort::Exception &exception) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("BGE ONNX 推理失败：%1")
                                        .arg(QString::fromUtf8(exception.what()));
                }
                return {};
            }
            if (progressCallback) {
                progressCallback(offset + count, texts.size());
            }
        }
        return embeddings;
#endif
    }

    QVector<float> embed(const QString &text, bool query, QString *errorMessage) const
    {
        const auto embeddings = embedBatch({text}, query, {}, errorMessage);
        return embeddings.isEmpty() ? QVector<float>{} : embeddings.first();
    }
};

BgeEmbeddingModel::BgeEmbeddingModel()
    : m_impl(std::make_unique<Impl>())
{
}

BgeEmbeddingModel::~BgeEmbeddingModel() = default;

bool BgeEmbeddingModel::isAvailable(QString *errorMessage) const
{
    QMutexLocker locker(&m_impl->mutex);
    const auto available = m_impl->ensureInitialized();
    if (!available && errorMessage) {
        *errorMessage = m_impl->initializationError;
    }
    return available;
}

QVector<float> BgeEmbeddingModel::embedDocument(const QString &text, QString *errorMessage) const
{
    return m_impl->embed(text, false, errorMessage);
}

QVector<QVector<float>> BgeEmbeddingModel::embedDocuments(
    const QStringList &texts,
    const EmbeddingProgressCallback &progressCallback,
    QString *errorMessage) const
{
    return m_impl->embedBatch(texts, false, progressCallback, errorMessage);
}

QVector<float> BgeEmbeddingModel::embedQuery(const QString &text, QString *errorMessage) const
{
    return m_impl->embed(text, true, errorMessage);
}

QString BgeEmbeddingModel::modelRoot() const
{
    return m_impl->root;
}

int BgeEmbeddingModel::inferenceThreadCount() const
{
    return m_impl->inferenceThreads;
}

int BgeEmbeddingModel::batchSize() const
{
    return m_impl->configuredBatch;
}
