#include "core/search/SearchQueryUnderstanding.h"

#include <QDate>
#include <QJsonArray>
#include <QSet>

#include <algorithm>

namespace {
constexpr int kMaxTextLength = 240;
constexpr int kMaxTermCount = 24;
constexpr int kMaxEntityCount = 4;
constexpr int kMaxSemanticVariantCount = 2;
constexpr int kMaxLexicalGroupCount = 6;
constexpr int kMaxGroupAlternativeCount = 6;
constexpr double kMinimumConfidence = 0.55;

QString boundedText(const QJsonValue &value, int maxLength = kMaxTextLength)
{
    if (!value.isString()) {
        return {};
    }
    auto text = value.toString().simplified();
    if (text.size() > maxLength) {
        text = text.left(maxLength).trimmed();
    }
    return text;
}

QStringList uniqueTerms(const QStringList &values, int limit = kMaxTermCount)
{
    QStringList result;
    QSet<QString> seen;
    for (auto value : values) {
        value = value.simplified();
        if (value.isEmpty()) {
            continue;
        }
        if (value.size() > 48) {
            value = value.left(48).trimmed();
        }
        const auto key = value.toCaseFolded();
        if (seen.contains(key)) {
            continue;
        }
        seen.insert(key);
        result.append(value);
        if (result.size() >= limit) {
            break;
        }
    }
    return result;
}

bool labelsDescribeSameEntity(const QString &left, const QString &right)
{
    const auto normalizedLeft = left.simplified().toCaseFolded();
    const auto normalizedRight = right.simplified().toCaseFolded();
    if (normalizedLeft.isEmpty() || normalizedRight.isEmpty()) {
        return false;
    }
    if (normalizedLeft == normalizedRight
        || normalizedLeft.contains(normalizedRight)
        || normalizedRight.contains(normalizedLeft)) {
        return true;
    }

    const QVector<QStringList> aliases{
        {QStringLiteral("牛仔裤"), QStringLiteral("长裤"), QStringLiteral("裤子"),
         QStringLiteral("丹宁裤")},
        {QStringLiteral("男人"), QStringLiteral("男性"), QStringLiteral("男子"),
         QStringLiteral("男士")},
        {QStringLiteral("女人"), QStringLiteral("女性"), QStringLiteral("女子"),
         QStringLiteral("女士")}
    };
    return std::any_of(aliases.cbegin(), aliases.cend(), [&](const QStringList &group) {
        return group.contains(normalizedLeft) && group.contains(normalizedRight);
    });
}

bool queryExplicitlyRequiresEntity(const QString &queryText, const QString &label)
{
    const auto normalizedQuery = queryText.simplified().toCaseFolded();
    const auto normalizedLabel = label.simplified().toCaseFolded();
    if (normalizedQuery.contains(normalizedLabel)) {
        return true;
    }
    const QVector<QStringList> aliases{
        {QStringLiteral("男人"), QStringLiteral("男性"), QStringLiteral("男子"),
         QStringLiteral("男士")},
        {QStringLiteral("女人"), QStringLiteral("女性"), QStringLiteral("女子"),
         QStringLiteral("女士")}
    };
    return std::any_of(aliases.cbegin(), aliases.cend(), [&](const QStringList &group) {
        if (!group.contains(normalizedLabel)) {
            return false;
        }
        return std::any_of(group.cbegin(), group.cend(), [&](const QString &alias) {
            return normalizedQuery.contains(alias);
        });
    });
}

bool queryExplicitlyRequiresAssetType(const QString &queryText, int assetType)
{
    const auto normalizedQuery = queryText.simplified().toCaseFolded();
    QStringList aliases;
    switch (static_cast<AssetType>(assetType)) {
    case AssetType::Video:
        aliases = {QStringLiteral("视频"), QStringLiteral("影片"), QStringLiteral("录像"),
                   QStringLiteral("片段")};
        break;
    case AssetType::Audio:
        aliases = {QStringLiteral("音频"), QStringLiteral("录音"), QStringLiteral("声音")};
        break;
    case AssetType::Image:
        aliases = {QStringLiteral("图片"), QStringLiteral("照片"), QStringLiteral("图像"),
                   QStringLiteral("相片"), QStringLiteral("海报")};
        break;
    case AssetType::Document:
        aliases = {QStringLiteral("文档"), QStringLiteral("文本"), QStringLiteral("表格")};
        break;
    case AssetType::Subtitle:
        aliases = {QStringLiteral("字幕")};
        break;
    case AssetType::Archive:
        aliases = {QStringLiteral("压缩包"), QStringLiteral("归档")};
        break;
    case AssetType::ProjectFile:
        aliases = {QStringLiteral("工程文件"), QStringLiteral("项目文件")};
        break;
    default:
        return false;
    }
    return std::any_of(aliases.cbegin(), aliases.cend(), [&normalizedQuery](const QString &alias) {
        return normalizedQuery.contains(alias);
    });
}

bool queryExplicitlyContains(const QString &queryText, const QString &candidate)
{
    const auto normalizedCandidate = candidate.simplified();
    return !normalizedCandidate.isEmpty()
        && queryText.contains(normalizedCandidate, Qt::CaseInsensitive);
}

QStringList groundedModelProperties(const QStringList &values,
                                    const QString &queryText,
                                    bool rejectRelationalAttributes = false)
{
    const auto normalizedQuery = queryText.simplified().toCaseFolded();
    const QStringList relationalTerms{
        QStringLiteral("穿着"), QStringLiteral("穿戴"), QStringLiteral("戴着"),
        QStringLiteral("拿着"), QStringLiteral("手持"), QStringLiteral("正在"),
        QStringLiteral("坐着"), QStringLiteral("站着"), QStringLiteral("走着")
    };

    QStringList grounded;
    for (const auto &value : values) {
        const auto normalized = value.simplified().toCaseFolded();
        if (normalized.isEmpty() || !normalizedQuery.contains(normalized)) {
            continue;
        }
        if (rejectRelationalAttributes
            && std::any_of(relationalTerms.cbegin(), relationalTerms.cend(),
                           [&normalized](const QString &term) {
                return normalized.contains(term);
            })) {
            continue;
        }
        grounded.append(value);
    }
    return uniqueTerms(grounded, 8);
}

StrictEntityConstraint groundedModelEntity(const StrictEntityConstraint &entity,
                                           const QString &queryText)
{
    auto grounded = entity;
    // Model-supplied properties become hard database constraints, so only keep
    // values that are explicitly grounded in the user's text. The entity label
    // is validated separately by queryExplicitlyRequiresEntity/known aliases.
    grounded.colors = groundedModelProperties(entity.colors, queryText);
    grounded.materials = groundedModelProperties(entity.materials, queryText);
    grounded.attributes = groundedModelProperties(entity.attributes, queryText, true);
    return grounded;
}

bool lexicalGroupIsGrounded(const SearchLexicalGroup &group,
                            const ParsedMaterialQuery &localQuery)
{
    return std::any_of(
        group.alternatives.cbegin(), group.alternatives.cend(), [&](const QString &term) {
        if (queryExplicitlyContains(localQuery.originalText, term)) {
            return true;
        }
        return std::any_of(
            localQuery.explicitEntityLabels.cbegin(),
            localQuery.explicitEntityLabels.cend(),
            [&](const QString &label) { return labelsDescribeSameEntity(label, term); });
    });
}

bool appendLexicalGroup(QVector<SearchLexicalGroup> *groups,
                        const SearchLexicalGroup &incoming)
{
    const auto normalizedIncoming = uniqueTerms(incoming.alternatives,
                                                kMaxGroupAlternativeCount);
    if (normalizedIncoming.isEmpty()) {
        return false;
    }
    const auto duplicate = std::any_of(
        groups->cbegin(), groups->cend(), [&](const SearchLexicalGroup &existing) {
        if (existing.mode != incoming.mode) {
            return false;
        }
        const auto existingTerms = uniqueTerms(existing.alternatives,
                                               kMaxGroupAlternativeCount);
        return std::any_of(
            normalizedIncoming.cbegin(), normalizedIncoming.cend(), [&](const QString &term) {
            return existingTerms.contains(term, Qt::CaseInsensitive);
        });
    });
    if (duplicate || groups->size() >= kMaxLexicalGroupCount) {
        return false;
    }
    auto grounded = incoming;
    grounded.alternatives = normalizedIncoming;
    groups->append(std::move(grounded));
    return true;
}

QStringList stringArray(const QJsonValue &value, int limit = kMaxTermCount)
{
    if (!value.isArray()) {
        return {};
    }
    QStringList values;
    const auto array = value.toArray();
    for (const auto &item : array) {
        if (item.isString()) {
            values.append(item.toString());
        }
        if (values.size() >= limit) {
            break;
        }
    }
    return uniqueTerms(values, limit);
}

int assetTypeFromName(const QString &name)
{
    const auto key = name.trimmed().toLower();
    if (key == QStringLiteral("video")) return static_cast<int>(AssetType::Video);
    if (key == QStringLiteral("audio")) return static_cast<int>(AssetType::Audio);
    if (key == QStringLiteral("image")) return static_cast<int>(AssetType::Image);
    if (key == QStringLiteral("document")) return static_cast<int>(AssetType::Document);
    if (key == QStringLiteral("subtitle")) return static_cast<int>(AssetType::Subtitle);
    if (key == QStringLiteral("archive")) return static_cast<int>(AssetType::Archive);
    if (key == QStringLiteral("project_file")) return static_cast<int>(AssetType::ProjectFile);
    return -1;
}

SearchDateField dateFieldFromName(const QString &name)
{
    const auto key = name.trimmed().toLower();
    if (key == QStringLiteral("captured")) return SearchDateField::CapturedTime;
    if (key == QStringLiteral("folder")) return SearchDateField::FolderDate;
    if (key == QStringLiteral("modified")) return SearchDateField::FileModifiedTime;
    return SearchDateField::Any;
}

bool hasExplicitAssetTarget(const ParsedMaterialQuery &query)
{
    if (!query.assetTypeFilters.isEmpty()) {
        return true;
    }
    const auto text = query.originalText;
    return text.contains(QStringLiteral("素材"), Qt::CaseInsensitive)
        || text.contains(QStringLiteral("文件"), Qt::CaseInsensitive)
        || text.contains(QStringLiteral("视频"), Qt::CaseInsensitive)
        || text.contains(QStringLiteral("图片"), Qt::CaseInsensitive)
        || text.contains(QStringLiteral("照片"), Qt::CaseInsensitive)
        || text.contains(QStringLiteral("音频"), Qt::CaseInsensitive)
        || text.contains(QStringLiteral("文档"), Qt::CaseInsensitive);
}

QString targetLabel(SearchResultTarget target, bool folderByAssetCriteria)
{
    if (target == SearchResultTarget::Folders) {
        return folderByAssetCriteria
            ? QStringLiteral("目标：匹配素材所在的文件夹")
            : QStringLiteral("目标：文件夹");
    }
    if (target == SearchResultTarget::Frames) {
        return QStringLiteral("目标：视觉帧");
    }
    return QStringLiteral("目标：素材");
}

QString assetTypeLabel(int type)
{
    switch (static_cast<AssetType>(type)) {
    case AssetType::Video: return QStringLiteral("视频");
    case AssetType::Audio: return QStringLiteral("音频");
    case AssetType::Image: return QStringLiteral("图片");
    case AssetType::Document: return QStringLiteral("文档");
    case AssetType::Subtitle: return QStringLiteral("字幕");
    case AssetType::Archive: return QStringLiteral("压缩包");
    case AssetType::ProjectFile: return QStringLiteral("工程文件");
    default: return {};
    }
}

void replaceInterpretationLabel(QStringList *labels,
                                const QStringList &prefixes,
                                const QString &replacement)
{
    labels->erase(std::remove_if(labels->begin(), labels->end(), [&prefixes](const QString &label) {
        for (const auto &prefix : prefixes) {
            if (label.startsWith(prefix)) {
                return true;
            }
        }
        return false;
    }), labels->end());
    if (!replacement.trimmed().isEmpty()) {
        labels->append(replacement);
    }
}

bool mergeEntity(QVector<StrictEntityConstraint> *entities,
                 const StrictEntityConstraint &incoming)
{
    for (auto &entity : *entities) {
        if (!labelsDescribeSameEntity(entity.label, incoming.label)) {
            continue;
        }
        const auto previousColors = entity.colors;
        const auto previousMaterials = entity.materials;
        const auto previousAttributes = entity.attributes;
        entity.colors = uniqueTerms(entity.colors + incoming.colors);
        entity.materials = uniqueTerms(entity.materials + incoming.materials);
        entity.attributes = uniqueTerms(entity.attributes + incoming.attributes);
        return entity.colors != previousColors
            || entity.materials != previousMaterials
            || entity.attributes != previousAttributes;
    }
    if (entities->size() < kMaxEntityCount) {
        entities->append(incoming);
        return true;
    }
    return false;
}

void removeTermsImpliedByLabel(QStringList *terms, const QString &label)
{
    const auto normalizedLabel = label.simplified().toCaseFolded();
    terms->erase(std::remove_if(terms->begin(), terms->end(), [&](const QString &term) {
        const auto normalizedTerm = term.simplified().toCaseFolded();
        return !normalizedTerm.isEmpty()
            && normalizedLabel != normalizedTerm
            && normalizedLabel.contains(normalizedTerm);
    }), terms->end());
}

void normalizeEntityConstraints(QVector<StrictEntityConstraint> *entities)
{
    for (auto &entity : *entities) {
        removeTermsImpliedByLabel(&entity.materials, entity.label);
        entity.colors = uniqueTerms(entity.colors);
        entity.materials = uniqueTerms(entity.materials);
        entity.attributes = uniqueTerms(entity.attributes);
    }
}
}

QJsonObject SearchQueryUnderstanding::responseSchema()
{
    const auto stringArraySchema = QJsonObject{
        {QStringLiteral("type"), QStringLiteral("array")},
        {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
        {QStringLiteral("maxItems"), kMaxTermCount}
    };
    const auto shortStringArraySchema = QJsonObject{
        {QStringLiteral("type"), QStringLiteral("array")},
        {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
        {QStringLiteral("maxItems"), kMaxGroupAlternativeCount}
    };
    const auto semanticVariantSchema = QJsonObject{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("additionalProperties"), false},
        {QStringLiteral("properties"), QJsonObject{
            {QStringLiteral("text"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
            {QStringLiteral("weight"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
                                                   {QStringLiteral("minimum"), 0.1},
                                                   {QStringLiteral("maximum"), 1.0}}}
        }},
        {QStringLiteral("required"), QJsonArray{QStringLiteral("text"), QStringLiteral("weight")}}
    };
    const auto lexicalGroupSchema = QJsonObject{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("additionalProperties"), false},
        {QStringLiteral("properties"), QJsonObject{
            {QStringLiteral("mode"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                 {QStringLiteral("enum"), QJsonArray{
                                                     QStringLiteral("required"),
                                                     QStringLiteral("optional"),
                                                     QStringLiteral("excluded")}}}},
            {QStringLiteral("alternatives"), shortStringArraySchema}
        }},
        {QStringLiteral("required"), QJsonArray{QStringLiteral("mode"), QStringLiteral("alternatives")}}
    };
    const auto entitySchema = QJsonObject{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("additionalProperties"), false},
        {QStringLiteral("properties"), QJsonObject{
            {QStringLiteral("label"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
            {QStringLiteral("colors"), stringArraySchema},
            {QStringLiteral("materials"), stringArraySchema},
            {QStringLiteral("attributes"), stringArraySchema}
        }},
        {QStringLiteral("required"), QJsonArray{QStringLiteral("label"), QStringLiteral("colors"), QStringLiteral("materials"), QStringLiteral("attributes")}}
    };
    return QJsonObject{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("additionalProperties"), false},
        {QStringLiteral("properties"), QJsonObject{
            {QStringLiteral("version"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("const"), 2}}},
            {QStringLiteral("result_target"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("enum"), QJsonArray{QStringLiteral("unspecified"), QStringLiteral("assets"), QStringLiteral("folders"), QStringLiteral("frames")}}}},
            {QStringLiteral("semantic_variants"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("items"), semanticVariantSchema}, {QStringLiteral("maxItems"), kMaxSemanticVariantCount}}},
            {QStringLiteral("lexical_groups"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("items"), lexicalGroupSchema}, {QStringLiteral("maxItems"), kMaxLexicalGroupCount}}},
            {QStringLiteral("asset_types"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("enum"), QJsonArray{QStringLiteral("video"), QStringLiteral("audio"), QStringLiteral("image"), QStringLiteral("document"), QStringLiteral("subtitle"), QStringLiteral("archive"), QStringLiteral("project_file")}}}}}},
            {QStringLiteral("folder_by_asset_criteria"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
            {QStringLiteral("ocr_text"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
            {QStringLiteral("entities"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("items"), entitySchema}, {QStringLiteral("maxItems"), kMaxEntityCount}}},
            {QStringLiteral("cooccurrence"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("enum"), QJsonArray{QStringLiteral("none"), QStringLiteral("same_asset"), QStringLiteral("same_frame")}}}},
            {QStringLiteral("confidence"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}, {QStringLiteral("minimum"), 0.0}, {QStringLiteral("maximum"), 1.0}}},
            {QStringLiteral("ambiguities"), stringArraySchema},
            {QStringLiteral("explanation"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}
        }},
        {QStringLiteral("required"), QJsonArray{
            QStringLiteral("version"), QStringLiteral("result_target"), QStringLiteral("semantic_variants"),
            QStringLiteral("lexical_groups"), QStringLiteral("asset_types"),
            QStringLiteral("folder_by_asset_criteria"), QStringLiteral("ocr_text"), QStringLiteral("entities"),
            QStringLiteral("cooccurrence"), QStringLiteral("confidence"),
            QStringLiteral("ambiguities"), QStringLiteral("explanation")
        }}
    };
}

std::optional<ModelSearchUnderstanding> SearchQueryUnderstanding::parseModelPayload(
    const QJsonObject &payload,
    QString *errorMessage)
{
    const auto version = payload.value(QStringLiteral("version")).toInt(-1);
    if (version != 1 && version != 2) {
        if (errorMessage) *errorMessage = QStringLiteral("模型查询理解协议版本不受支持");
        return std::nullopt;
    }
    const auto confidenceValue = payload.value(QStringLiteral("confidence"));
    if (!confidenceValue.isDouble()) {
        if (errorMessage) *errorMessage = QStringLiteral("模型查询理解缺少有效置信度");
        return std::nullopt;
    }

    ModelSearchUnderstanding result;
    result.protocolVersion = version;
    result.confidence = confidenceValue.toDouble();
    if (result.confidence < 0.0 || result.confidence > 1.0) {
        if (errorMessage) *errorMessage = QStringLiteral("模型查询理解置信度越界");
        return std::nullopt;
    }
    if (version == 1) {
        result.semanticText = boundedText(payload.value(QStringLiteral("semantic_text")));
        result.lexicalTerms = stringArray(payload.value(QStringLiteral("lexical_terms")));
    } else {
        const auto semanticArray = payload.value(QStringLiteral("semantic_variants")).toArray();
        for (const auto &value : semanticArray) {
            const auto object = value.toObject();
            SearchSemanticVariant variant;
            variant.text = boundedText(object.value(QStringLiteral("text")));
            variant.weight = object.value(QStringLiteral("weight")).toDouble(0.75);
            if (!variant.isEmpty() && variant.weight >= 0.1 && variant.weight <= 1.0) {
                result.semanticVariants.append(std::move(variant));
            }
            if (result.semanticVariants.size() >= kMaxSemanticVariantCount) {
                break;
            }
        }
        if (!result.semanticVariants.isEmpty()) {
            result.semanticText = result.semanticVariants.first().text;
        }

        const auto groupArray = payload.value(QStringLiteral("lexical_groups")).toArray();
        for (const auto &value : groupArray) {
            const auto object = value.toObject();
            SearchLexicalGroup group;
            const auto mode = object.value(QStringLiteral("mode")).toString().trimmed().toLower();
            if (mode == QStringLiteral("required")) {
                group.mode = SearchLexicalGroupMode::Required;
            } else if (mode == QStringLiteral("excluded")) {
                group.mode = SearchLexicalGroupMode::Excluded;
            } else if (mode == QStringLiteral("optional")) {
                group.mode = SearchLexicalGroupMode::Optional;
            } else {
                continue;
            }
            group.alternatives = stringArray(
                object.value(QStringLiteral("alternatives")),
                kMaxGroupAlternativeCount);
            if (group.isEmpty()) {
                continue;
            }
            if (group.mode == SearchLexicalGroupMode::Excluded) {
                result.excludedTerms.append(group.alternatives);
            } else {
                result.lexicalTerms.append(group.alternatives);
            }
            result.lexicalGroups.append(std::move(group));
            if (result.lexicalGroups.size() >= kMaxLexicalGroupCount) {
                break;
            }
        }
        result.lexicalTerms = uniqueTerms(result.lexicalTerms);
        result.excludedTerms = uniqueTerms(result.excludedTerms);

        const auto cooccurrence = payload.value(QStringLiteral("cooccurrence"))
                                      .toString().trimmed().toLower();
        if (cooccurrence == QStringLiteral("same_frame")) {
            result.cooccurrence = SearchCooccurrenceScope::SameFrame;
        } else if (cooccurrence == QStringLiteral("same_asset")) {
            result.cooccurrence = SearchCooccurrenceScope::SameAsset;
        }
        result.ambiguities = stringArray(payload.value(QStringLiteral("ambiguities")), 8);
    }
    result.ocrText = boundedText(payload.value(QStringLiteral("ocr_text")), 160);
    result.explanation = boundedText(payload.value(QStringLiteral("explanation")), 200);
    result.folderByAssetCriteria = payload.value(QStringLiteral("folder_by_asset_criteria")).toBool(false);

    const auto target = payload.value(QStringLiteral("result_target")).toString().trimmed().toLower();
    if (target == QStringLiteral("assets")) {
        result.resultTarget = SearchResultTarget::Assets;
        result.resultTargetSpecified = true;
    } else if (target == QStringLiteral("folders")) {
        result.resultTarget = SearchResultTarget::Folders;
        result.resultTargetSpecified = true;
    } else if (target == QStringLiteral("frames")) {
        result.resultTarget = SearchResultTarget::Frames;
        result.resultTargetSpecified = true;
    } else if (target != QStringLiteral("unspecified")) {
        if (errorMessage) *errorMessage = QStringLiteral("模型返回了未知结果类型");
        return std::nullopt;
    }

    const auto typeNames = stringArray(payload.value(QStringLiteral("asset_types")));
    for (const auto &name : typeNames) {
        const auto type = assetTypeFromName(name);
        if (type < 0) {
            if (errorMessage) *errorMessage = QStringLiteral("模型返回了未知素材类型：%1").arg(name);
            return std::nullopt;
        }
        if (!result.assetTypeFilters.contains(type)) {
            result.assetTypeFilters.append(type);
        }
    }

    if (version == 1) {
        const auto dateObject = payload.value(QStringLiteral("date")).toObject();
        const auto startText = boundedText(dateObject.value(QStringLiteral("start")), 10);
        const auto endText = boundedText(dateObject.value(QStringLiteral("end")), 10);
        if (!startText.isEmpty() || !endText.isEmpty()) {
            const auto start = QDate::fromString(startText, Qt::ISODate);
            const auto end = QDate::fromString(endText, Qt::ISODate);
            if (!start.isValid() || !end.isValid() || start > end || start.daysTo(end) > 3660) {
                if (errorMessage) *errorMessage = QStringLiteral("模型返回了无效或过宽的日期范围");
                return std::nullopt;
            }
            result.dateConstraint.startDate = start.toString(Qt::ISODate);
            result.dateConstraint.endDate = end.toString(Qt::ISODate);
            result.dateConstraint.matchedText = boundedText(dateObject.value(QStringLiteral("matched_text")), 80);
            result.dateConstraint.preferredField = dateFieldFromName(dateObject.value(QStringLiteral("preferred_field")).toString());
        }
    }

    const auto entityArray = payload.value(QStringLiteral("entities")).toArray();
    for (const auto &value : entityArray) {
        if (!value.isObject()) {
            continue;
        }
        const auto object = value.toObject();
        StrictEntityConstraint entity;
        entity.label = boundedText(object.value(QStringLiteral("label")), 48);
        entity.colors = stringArray(object.value(QStringLiteral("colors")), 8);
        entity.materials = stringArray(object.value(QStringLiteral("materials")), 8);
        entity.attributes = stringArray(object.value(QStringLiteral("attributes")), 8);
        if (!entity.isEmpty()) {
            result.strictEntities.append(entity);
        }
        if (result.strictEntities.size() >= kMaxEntityCount) {
            break;
        }
    }
    return result;
}

ParsedMaterialQuery SearchQueryUnderstanding::merge(
    const ParsedMaterialQuery &localQuery,
    const ModelSearchUnderstanding &modelUnderstanding,
    bool *modelApplied)
{
    if (modelApplied) {
        *modelApplied = false;
    }
    auto merged = localQuery;
    if (modelUnderstanding.confidence < kMinimumConfidence) {
        return merged;
    }

    bool changed = false;
    for (auto variant : modelUnderstanding.semanticVariants) {
        variant.text = variant.text.simplified();
        variant.weight = std::clamp(variant.weight, 0.1, 0.85);
        if (variant.text.isEmpty()
            || variant.text.compare(merged.semanticText.simplified(), Qt::CaseInsensitive) == 0
            || std::any_of(merged.semanticVariants.cbegin(),
                           merged.semanticVariants.cend(),
                           [&variant](const SearchSemanticVariant &existing) {
            return existing.text.compare(variant.text, Qt::CaseInsensitive) == 0;
        })) {
            continue;
        }
        merged.semanticVariants.append(std::move(variant));
        changed = true;
        if (merged.semanticVariants.size() >= kMaxSemanticVariantCount) {
            break;
        }
    }
    if (!modelUnderstanding.semanticText.isEmpty()) {
        const auto localSemantic = merged.semanticText.simplified();
        const auto modelSemantic = modelUnderstanding.semanticText.simplified();
        if (localSemantic.isEmpty()) {
            merged.semanticText = modelSemantic;
            changed = true;
        }
    }

    if (merged.dateConstraint.isEmpty()
        && !modelUnderstanding.dateConstraint.isEmpty()
        && queryExplicitlyContains(localQuery.originalText,
                                   modelUnderstanding.dateConstraint.matchedText)) {
        merged.dateConstraint = modelUnderstanding.dateConstraint;
        merged.normalizedDate = merged.dateConstraint.isExactDate()
            ? merged.dateConstraint.startDate
            : QString();
        changed = true;
        const auto dateValue = merged.dateConstraint.isExactDate()
            ? merged.dateConstraint.startDate
            : QStringLiteral("%1 至 %2").arg(merged.dateConstraint.startDate, merged.dateConstraint.endDate);
        replaceInterpretationLabel(&merged.interpretationLabels,
                                   {QStringLiteral("日期："), QStringLiteral("拍摄日期："), QStringLiteral("目录日期："), QStringLiteral("文件修改日期：")},
                                   QStringLiteral("模型日期：%1").arg(dateValue));
    }

    if (merged.assetTypeFilters.isEmpty() && !modelUnderstanding.assetTypeFilters.isEmpty()) {
        for (const auto type : modelUnderstanding.assetTypeFilters) {
            if (queryExplicitlyRequiresAssetType(localQuery.originalText, type)
                && !merged.assetTypeFilters.contains(type)) {
                merged.assetTypeFilters.append(type);
            }
        }
    }
    if (merged.assetTypeFilter < 0 && !merged.assetTypeFilters.isEmpty()) {
        merged.assetTypeFilter = merged.assetTypeFilters.first();
        QStringList labels;
        for (const auto type : merged.assetTypeFilters) {
            labels.append(assetTypeLabel(type));
        }
        replaceInterpretationLabel(&merged.interpretationLabels,
                                   {QStringLiteral("类型：")},
                                   QStringLiteral("类型：%1").arg(labels.join(QStringLiteral(" / "))));
        changed = true;
    }

    const bool localTargetLocked = merged.folderIntent || merged.frameIntent || hasExplicitAssetTarget(localQuery);
    if (!localTargetLocked && modelUnderstanding.resultTargetSpecified) {
        merged.resultTarget = modelUnderstanding.resultTarget;
        merged.folderIntent = merged.resultTarget == SearchResultTarget::Folders;
        merged.frameIntent = merged.resultTarget == SearchResultTarget::Frames;
        merged.folderByAssetCriteria = merged.folderIntent && modelUnderstanding.folderByAssetCriteria;
        replaceInterpretationLabel(&merged.interpretationLabels,
                                   {QStringLiteral("目标：")},
                                   targetLabel(merged.resultTarget, merged.folderByAssetCriteria));
        changed = true;
    }

    if (merged.ocrText.isEmpty()
        && !modelUnderstanding.ocrText.isEmpty()
        && queryExplicitlyContains(localQuery.originalText, modelUnderstanding.ocrText)) {
        merged.ocrText = modelUnderstanding.ocrText;
        replaceInterpretationLabel(&merged.interpretationLabels,
                                   {QStringLiteral("画面文字：")},
                                   QStringLiteral("画面文字：%1").arg(merged.ocrText));
        changed = true;
    }

    for (const auto &entity : modelUnderstanding.strictEntities) {
        const bool matchesKnownEntity = std::any_of(
            merged.strictEntities.cbegin(),
            merged.strictEntities.cend(),
            [&entity](const auto &knownEntity) {
                return labelsDescribeSameEntity(knownEntity.label, entity.label);
            });
        if (matchesKnownEntity
            || queryExplicitlyRequiresEntity(localQuery.originalText, entity.label)) {
            changed = mergeEntity(
                          &merged.strictEntities,
                          groundedModelEntity(entity, localQuery.originalText))
                || changed;
        }
    }
    normalizeEntityConstraints(&merged.strictEntities);

    QStringList groundedModelLexicalTerms;
    for (auto group : modelUnderstanding.lexicalGroups) {
        if (!lexicalGroupIsGrounded(group, localQuery)) {
            continue;
        }
        if (group.mode == SearchLexicalGroupMode::Excluded) {
            const auto groundedExcluded = groundedModelProperties(
                group.alternatives,
                localQuery.originalText);
            const auto previousExcluded = merged.excludedTerms;
            merged.excludedTerms = uniqueTerms(merged.excludedTerms + groundedExcluded);
            changed = merged.excludedTerms != previousExcluded || changed;
            group.alternatives = groundedExcluded;
            if (group.alternatives.isEmpty()) {
                continue;
            }
        } else {
            groundedModelLexicalTerms.append(group.alternatives);
        }
        changed = appendLexicalGroup(&merged.lexicalGroups, group) || changed;
    }

    const auto entityCount = std::max(merged.explicitEntityLabels.size(),
                                      merged.strictEntities.size());
    if (entityCount >= 2
        && modelUnderstanding.cooccurrence != SearchCooccurrenceScope::None) {
        auto cooccurrence = modelUnderstanding.cooccurrence;
        if (merged.resultTarget == SearchResultTarget::Frames) {
            cooccurrence = SearchCooccurrenceScope::SameFrame;
        }
        if (merged.cooccurrence != cooccurrence) {
            merged.cooccurrence = cooccurrence;
            changed = true;
        }
    }

    const auto previousLexicalTerms = merged.lexicalTerms;
    QStringList lexical = previousLexicalTerms;
    lexical.append(modelUnderstanding.protocolVersion >= 2
                       ? uniqueTerms(groundedModelLexicalTerms)
                       : modelUnderstanding.lexicalTerms);
    for (const auto &entity : modelUnderstanding.strictEntities) {
        lexical.append(entity.allTerms());
    }
    for (const auto &entity : merged.strictEntities) {
        lexical.append(entity.allTerms());
    }
    if (!merged.ocrText.isEmpty()) {
        lexical.append(merged.ocrText);
    }
    merged.lexicalTerms = uniqueTerms(lexical);
    changed = merged.lexicalTerms != previousLexicalTerms || changed;

    if (!modelUnderstanding.strictEntities.isEmpty()) {
        QStringList entityLabels;
        for (const auto &entity : merged.strictEntities) {
            entityLabels.append(entity.allTerms().join(QLatin1Char(' ')));
        }
        replaceInterpretationLabel(&merged.interpretationLabels,
                                   {QStringLiteral("同一对象：")},
                                   QStringLiteral("同一对象：%1").arg(entityLabels.join(QStringLiteral("；"))));
    }
    if (merged.cooccurrence == SearchCooccurrenceScope::SameFrame) {
        merged.interpretationLabels.append(QStringLiteral("约束：多个实体需在同一帧出现"));
    } else if (merged.cooccurrence == SearchCooccurrenceScope::SameAsset) {
        merged.interpretationLabels.append(QStringLiteral("约束：多个实体需在同一素材出现"));
    }
    if (!merged.semanticText.isEmpty()) {
        replaceInterpretationLabel(&merged.interpretationLabels,
                                   {QStringLiteral("内容：")},
                                   QStringLiteral("内容：%1").arg(merged.semanticText));
    }
    if (changed) {
        merged.interpretationLabels.append(QStringLiteral("内置文本模型辅助理解"));
    }
    merged.interpretationLabels = uniqueTerms(merged.interpretationLabels, 32);
    if (modelApplied) {
        *modelApplied = changed;
    }
    return merged;
}
