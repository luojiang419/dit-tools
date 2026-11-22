#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <memory>

using EmbeddingProgressCallback = std::function<void(qsizetype processed, qsizetype total)>;

class BgeEmbeddingModel {
public:
    BgeEmbeddingModel();
    ~BgeEmbeddingModel();

    BgeEmbeddingModel(const BgeEmbeddingModel &) = delete;
    BgeEmbeddingModel &operator=(const BgeEmbeddingModel &) = delete;

    bool isAvailable(QString *errorMessage = nullptr) const;
    QVector<float> embedDocument(const QString &text, QString *errorMessage) const;
    QVector<QVector<float>> embedDocuments(const QStringList &texts,
                                            const EmbeddingProgressCallback &progressCallback,
                                            QString *errorMessage) const;
    QVector<float> embedQuery(const QString &text, QString *errorMessage) const;
    QString modelRoot() const;
    int inferenceThreadCount() const;
    int batchSize() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
