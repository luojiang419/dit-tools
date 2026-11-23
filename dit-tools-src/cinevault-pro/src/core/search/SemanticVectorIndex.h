#pragma once

#include "domain/SearchTypes.h"

#include <QPair>
#include <QString>
#include <QVector>

#include <memory>

class SemanticVectorIndex {
public:
    SemanticVectorIndex();
    ~SemanticVectorIndex();

    SemanticVectorIndex(const SemanticVectorIndex &) = delete;
    SemanticVectorIndex &operator=(const SemanticVectorIndex &) = delete;
    SemanticVectorIndex(SemanticVectorIndex &&) noexcept;
    SemanticVectorIndex &operator=(SemanticVectorIndex &&) noexcept;

    bool reset(int dimensions, QString *errorMessage);
    bool load(const QString &filePath, int dimensions, QString *errorMessage);
    bool save(const QString &filePath, QString *errorMessage) const;
    bool reserve(qsizetype capacity, QString *errorMessage);
    bool add(quint64 key, const QVector<float> &embedding, QString *errorMessage);
    bool remove(quint64 key, QString *errorMessage);
    QVector<QPair<quint64, double>> search(const QVector<float> &embedding,
                                           qsizetype limit,
                                           QString *errorMessage) const;
    qsizetype size(QString *errorMessage = nullptr) const;
    int dimensions() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
