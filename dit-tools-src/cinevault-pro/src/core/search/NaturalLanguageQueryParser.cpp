#include "core/search/NaturalLanguageQueryParser.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <utility>

namespace {
QStringList uniqueTerms(const QStringList &values)
{
    QStringList result;
    QSet<QString> seen;
    for (const auto &value : values) {
        const auto term = value.simplified();
        const auto key = term.toCaseFolded();
        if (term.isEmpty() || seen.contains(key)) {
            continue;
        }
        seen.insert(key);
        result.append(term);
    }
    return result;
}

QStringList matchingTerms(const QString &text, const QStringList &dictionary)
{
    QStringList matches;
    for (const auto &term : dictionary) {
        if (text.contains(term, Qt::CaseInsensitive)) {
            matches.append(term);
        }
    }
    return uniqueTerms(matches);
}

QStringList mostSpecificTerms(const QStringList &terms)
{
    QStringList result;
    for (const auto &candidate : terms) {
        const auto normalizedCandidate = candidate.simplified().toCaseFolded();
        if (normalizedCandidate.isEmpty()) {
            continue;
        }
        const bool impliedByMoreSpecificTerm = std::any_of(
            terms.cbegin(), terms.cend(), [&](const QString &other) {
                const auto normalizedOther = other.simplified().toCaseFolded();
                return normalizedOther != normalizedCandidate
                    && normalizedOther.size() > normalizedCandidate.size()
                    && normalizedOther.contains(normalizedCandidate);
            });
        if (!impliedByMoreSpecificTerm) {
            result.append(candidate);
        }
    }
    return uniqueTerms(result);
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

struct ParsedDateText {
    QString startDate;
    QString endDate;
    QString matchedText;
};

ParsedDateText exactDate(const QDate &date, const QString &matchedText)
{
    if (!date.isValid()) {
        return {};
    }
    const auto normalized = date.toString(Qt::ISODate);
    return {normalized, normalized, matchedText};
}

ParsedDateText dateRange(const QDate &startDate,
                         const QDate &endDate,
                         const QString &matchedText)
{
    if (!startDate.isValid() || !endDate.isValid() || startDate > endDate) {
        return {};
    }
    return {startDate.toString(Qt::ISODate), endDate.toString(Qt::ISODate), matchedText};
}

ParsedDateText normalizedDateFromText(const QString &text,
                                      const QDate &referenceDate)
{
    if (text.contains(QStringLiteral("大前天"))) {
        return exactDate(referenceDate.addDays(-3), QStringLiteral("大前天"));
    }
    if (text.contains(QStringLiteral("前天"))) {
        return exactDate(referenceDate.addDays(-2), QStringLiteral("前天"));
    }
    if (text.contains(QStringLiteral("昨天"))) {
        return exactDate(referenceDate.addDays(-1), QStringLiteral("昨天"));
    }
    if (text.contains(QStringLiteral("今天"))) {
        return exactDate(referenceDate, QStringLiteral("今天"));
    }

    const auto calendarYear = [](int year, const QString &matchedText) {
        return dateRange(QDate(year, 1, 1), QDate(year, 12, 31), matchedText);
    };
    if (text.contains(QStringLiteral("前年"))) {
        return calendarYear(referenceDate.year() - 2, QStringLiteral("前年"));
    }
    if (text.contains(QStringLiteral("去年"))) {
        return calendarYear(referenceDate.year() - 1, QStringLiteral("去年"));
    }
    if (text.contains(QStringLiteral("上年度"))) {
        return calendarYear(referenceDate.year() - 1, QStringLiteral("上年度"));
    }
    if (text.contains(QStringLiteral("上一年"))) {
        return calendarYear(referenceDate.year() - 1, QStringLiteral("上一年"));
    }
    if (text.contains(QStringLiteral("今年"))) {
        return dateRange(QDate(referenceDate.year(), 1, 1),
                         referenceDate,
                         QStringLiteral("今年"));
    }
    if (text.contains(QStringLiteral("本年度"))) {
        return dateRange(QDate(referenceDate.year(), 1, 1),
                         referenceDate,
                         QStringLiteral("本年度"));
    }
    if (text.contains(QStringLiteral("本年"))) {
        return dateRange(QDate(referenceDate.year(), 1, 1),
                         referenceDate,
                         QStringLiteral("本年"));
    }

    static const QRegularExpression daysAgo(
        QStringLiteral("(?<!\\d)(\\d{1,3})\\s*天前"));
    auto match = daysAgo.match(text);
    if (match.hasMatch()) {
        const auto days = match.captured(1).toInt();
        if (days > 0) {
            return exactDate(referenceDate.addDays(-days), match.captured(0));
        }
    }

    static const QRegularExpression recentDays(
        QStringLiteral("(?:最近|近)\\s*(\\d{1,3})\\s*天"));
    match = recentDays.match(text);
    if (match.hasMatch()) {
        const auto days = match.captured(1).toInt();
        if (days > 0) {
            return dateRange(referenceDate.addDays(-(days - 1)),
                             referenceDate,
                             match.captured(0));
        }
    }

    const auto monthCount = [](const QString &value) {
        bool ok = false;
        const auto numeric = value.toInt(&ok);
        if (ok) {
            return numeric;
        }
        static const QHash<QString, int> chineseNumbers{
            {QStringLiteral("一"), 1}, {QStringLiteral("两"), 2},
            {QStringLiteral("二"), 2}, {QStringLiteral("三"), 3},
            {QStringLiteral("四"), 4}, {QStringLiteral("五"), 5},
            {QStringLiteral("六"), 6}, {QStringLiteral("七"), 7},
            {QStringLiteral("八"), 8}, {QStringLiteral("九"), 9},
            {QStringLiteral("十"), 10},
        };
        return chineseNumbers.value(value, 0);
    };
    static const QString monthNumberPattern = QStringLiteral("(\\d{1,2}|一|两|二|三|四|五|六|七|八|九|十)");
    static const QRegularExpression recentMonths(
        QStringLiteral("(?:最近|近)\\s*%1\\s*个?月(?:内)?").arg(monthNumberPattern));
    match = recentMonths.match(text);
    if (match.hasMatch()) {
        const auto months = monthCount(match.captured(1));
        if (months > 0) {
            return dateRange(referenceDate.addMonths(-months),
                             referenceDate,
                             match.captured(0));
        }
    }

    static const QRegularExpression monthsBefore(
        QStringLiteral("%1\\s*个?月之前").arg(monthNumberPattern));
    match = monthsBefore.match(text);
    if (match.hasMatch()) {
        const auto months = monthCount(match.captured(1));
        if (months > 0) {
            return dateRange(QDate(1, 1, 1),
                             referenceDate.addMonths(-months).addDays(-1),
                             match.captured(0));
        }
    }

    static const QRegularExpression monthsAgo(
        QStringLiteral("%1\\s*个?月前").arg(monthNumberPattern));
    match = monthsAgo.match(text);
    if (match.hasMatch()) {
        const auto months = monthCount(match.captured(1));
        if (months > 0) {
            return exactDate(referenceDate.addMonths(-months), match.captured(0));
        }
    }

    if (text.contains(QStringLiteral("上周"))) {
        const auto currentMonday = referenceDate.addDays(1 - referenceDate.dayOfWeek());
        return dateRange(currentMonday.addDays(-7),
                         currentMonday.addDays(-1),
                         QStringLiteral("上周"));
    }
    if (text.contains(QStringLiteral("本周")) || text.contains(QStringLiteral("这周"))) {
        return dateRange(referenceDate.addDays(1 - referenceDate.dayOfWeek()),
                         referenceDate,
                         text.contains(QStringLiteral("本周"))
                             ? QStringLiteral("本周")
                             : QStringLiteral("这周"));
    }
    if (text.contains(QStringLiteral("上个月")) || text.contains(QStringLiteral("上月"))) {
        const auto currentMonth = QDate(referenceDate.year(), referenceDate.month(), 1);
        const auto previousMonth = currentMonth.addMonths(-1);
        return dateRange(previousMonth,
                         currentMonth.addDays(-1),
                         text.contains(QStringLiteral("上个月"))
                             ? QStringLiteral("上个月")
                             : QStringLiteral("上月"));
    }
    if (text.contains(QStringLiteral("本月")) || text.contains(QStringLiteral("这个月"))) {
        return dateRange(QDate(referenceDate.year(), referenceDate.month(), 1),
                         referenceDate,
                         text.contains(QStringLiteral("本月"))
                             ? QStringLiteral("本月")
                             : QStringLiteral("这个月"));
    }

    static const QRegularExpression separated(
        QStringLiteral("(?<!\\d)(\\d{4})\\s*[年./\\-]\\s*(\\d{1,2})\\s*[月./\\-]\\s*(\\d{1,2})\\s*(?:日|号)?(?!\\d)"));
    match = separated.match(text);
    if (match.hasMatch()) {
        const QDate date(match.captured(1).toInt(), match.captured(2).toInt(), match.captured(3).toInt());
        if (date.isValid()) {
            return exactDate(date, match.captured(0));
        }
    }

    static const QRegularExpression yearOnly(
        QStringLiteral("(?<!\\d)((?:19|20)\\d{2})\\s*年(?!\\s*\\d{1,2}\\s*月)"));
    match = yearOnly.match(text);
    if (match.hasMatch()) {
        return calendarYear(match.captured(1).toInt(), match.captured(0));
    }

    static const QRegularExpression compact(QStringLiteral("(?<!\\d)(\\d{4})(\\d{2})(\\d{2})(?!\\d)"));
    match = compact.match(text);
    if (match.hasMatch()) {
        const QDate date(match.captured(1).toInt(), match.captured(2).toInt(), match.captured(3).toInt());
        if (date.isValid()) {
            return exactDate(date, match.captured(0));
        }
    }

    static const QRegularExpression monthDay(
        QStringLiteral("(?<!\\d)(\\d{1,2})\\s*月\\s*(\\d{1,2})\\s*(?:日|号)(?!\\d)"));
    match = monthDay.match(text);
    if (match.hasMatch()) {
        const QDate date(referenceDate.year(), match.captured(1).toInt(), match.captured(2).toInt());
        if (date.isValid()) {
            return exactDate(date, match.captured(0));
        }
    }
    return {};
}

void removeTerms(QString *text, QStringList terms, QStringList *removedTerms = nullptr)
{
    std::sort(terms.begin(), terms.end(), [](const auto &left, const auto &right) {
        return left.size() > right.size();
    });
    for (const auto &term : uniqueTerms(terms)) {
        if (term.isEmpty() || !text->contains(term, Qt::CaseInsensitive)) {
            continue;
        }
        text->replace(term, QStringLiteral(" "), Qt::CaseInsensitive);
        if (removedTerms) {
            removedTerms->append(term);
        }
    }
}

SearchDateField preferredDateField(const QString &text, bool folderIntent)
{
    if (text.contains(QStringLiteral("修改时间"))
        || text.contains(QStringLiteral("更新时间"))
        || text.contains(QStringLiteral("文件时间"))) {
        return SearchDateField::FileModifiedTime;
    }
    if (folderIntent || text.contains(QStringLiteral("目录日期"))) {
        return SearchDateField::FolderDate;
    }
    const QStringList captureWords{
        QStringLiteral("拍摄"), QStringLiteral("拍的"), QStringLiteral("拍于"),
        QStringLiteral("摄于"), QStringLiteral("录制"), QStringLiteral("录的")
    };
    for (const auto &word : captureWords) {
        if (text.contains(word)) {
            return SearchDateField::CapturedTime;
        }
    }
    return SearchDateField::Any;
}

QString dateFieldLabel(SearchDateField field)
{
    switch (field) {
    case SearchDateField::CapturedTime: return QStringLiteral("拍摄日期");
    case SearchDateField::FolderDate: return QStringLiteral("目录日期");
    case SearchDateField::FileModifiedTime: return QStringLiteral("文件修改日期");
    case SearchDateField::Any: break;
    }
    return QStringLiteral("日期");
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
    default: break;
    }
    return {};
}

QVector<int> assetTypesFromText(const QString &text, QStringList *matchedWords)
{
    struct TypeWords {
        AssetType type;
        QStringList words;
    };
    const QVector<TypeWords> types{
        {AssetType::Video, {QStringLiteral("视频"), QStringLiteral("影片"), QStringLiteral("录像"), QStringLiteral("片段")}},
        {AssetType::Image, {QStringLiteral("图片"), QStringLiteral("照片"), QStringLiteral("图像"), QStringLiteral("海报")}},
        {AssetType::Audio, {QStringLiteral("音频"), QStringLiteral("录音"), QStringLiteral("声音")}},
        {AssetType::Document, {QStringLiteral("文档"), QStringLiteral("文本"), QStringLiteral("表格")}},
        {AssetType::Subtitle, {QStringLiteral("字幕")}},
        {AssetType::Archive, {QStringLiteral("压缩包"), QStringLiteral("归档")}},
        {AssetType::ProjectFile, {QStringLiteral("工程文件"), QStringLiteral("项目文件")}}
    };
    QVector<int> result;
    for (const auto &entry : types) {
        const auto wordMatches = matchingTerms(text, entry.words);
        if (!wordMatches.isEmpty()) {
            if (matchedWords) matchedWords->append(wordMatches);
            const auto type = static_cast<int>(entry.type);
            if (!result.contains(type)) {
                result.append(type);
            }
        }
    }
    return result;
}

QStringList colorDictionary()
{
    return {
        QStringLiteral("黑色"), QStringLiteral("白色"), QStringLiteral("灰色"), QStringLiteral("红色"),
        QStringLiteral("橙色"), QStringLiteral("黄色"), QStringLiteral("绿色"), QStringLiteral("青色"),
        QStringLiteral("蓝色"), QStringLiteral("紫色"), QStringLiteral("粉色"), QStringLiteral("棕色"),
        QStringLiteral("金色"), QStringLiteral("银色"), QStringLiteral("米色"), QStringLiteral("卡其色"),
        QStringLiteral("彩色"), QStringLiteral("透明")
    };
}

QStringList materialDictionary()
{
    return {
        QStringLiteral("牛仔"), QStringLiteral("棉质"), QStringLiteral("纯棉"), QStringLiteral("棉"),
        QStringLiteral("皮革"), QStringLiteral("真皮"), QStringLiteral("羊毛"), QStringLiteral("针织"),
        QStringLiteral("丝绸"), QStringLiteral("亚麻"), QStringLiteral("涤纶"), QStringLiteral("金属"),
        QStringLiteral("木质"), QStringLiteral("木制"), QStringLiteral("塑料"), QStringLiteral("玻璃"),
        QStringLiteral("陶瓷"), QStringLiteral("纸质")
    };
}

QStringList attributeDictionary()
{
    return {
        QStringLiteral("破洞"), QStringLiteral("条纹"), QStringLiteral("格纹"), QStringLiteral("印花"),
        QStringLiteral("纯色"), QStringLiteral("长袖"), QStringLiteral("短袖"), QStringLiteral("无袖"),
        QStringLiteral("宽松"), QStringLiteral("修身"), QStringLiteral("高腰"), QStringLiteral("低腰"),
        QStringLiteral("透明"), QStringLiteral("反光"), QStringLiteral("磨损"), QStringLiteral("复古")
    };
}

QStringList entityLabelDictionary()
{
    // Longest and more specific labels come first.
    return {
        QStringLiteral("连衣裙"), QStringLiteral("羽绒服"), QStringLiteral("牛仔裤"),
        QStringLiteral("夹克衫"), QStringLiteral("太阳镜"), QStringLiteral("高跟鞋"), QStringLiteral("运动鞋"),
        QStringLiteral("短裤"), QStringLiteral("长裤"), QStringLiteral("裤子"), QStringLiteral("衬衫"),
        QStringLiteral("上衣"), QStringLiteral("外套"), QStringLiteral("夹克"), QStringLiteral("裙子"),
        QStringLiteral("帽子"), QStringLiteral("鞋子"), QStringLiteral("背包"), QStringLiteral("手提包"),
        QStringLiteral("汽车"), QStringLiteral("自行车"), QStringLiteral("摩托车"), QStringLiteral("手机"),
        QStringLiteral("电脑"), QStringLiteral("相机"), QStringLiteral("摄影机"), QStringLiteral("显示器"),
        QStringLiteral("桌子"), QStringLiteral("椅子"), QStringLiteral("杯子"), QStringLiteral("瓶子"),
        QStringLiteral("广告牌"), QStringLiteral("雨伞"), QStringLiteral("伞"),
        QStringLiteral("人物"), QStringLiteral("男人"), QStringLiteral("女人"),
        QStringLiteral("儿童"), QStringLiteral("动物"), QStringLiteral("猫"), QStringLiteral("狗")
    };
}

QStringList visualLexicalDictionary()
{
    return {
        QStringLiteral("雨夜"), QStringLiteral("夜景"), QStringLiteral("夜晚"), QStringLiteral("白天"),
        QStringLiteral("日出"), QStringLiteral("日落"), QStringLiteral("海边"), QStringLiteral("雪山"),
        QStringLiteral("城市"), QStringLiteral("街道"), QStringLiteral("室内"), QStringLiteral("室外"),
        QStringLiteral("棚内"), QStringLiteral("逆光"), QStringLiteral("航拍"), QStringLiteral("特写"),
        QStringLiteral("撑伞"), QStringLiteral("跑步"), QStringLiteral("行走"), QStringLiteral("驾驶"),
        QStringLiteral("采访"), QStringLiteral("演讲"), QStringLiteral("跳舞"), QStringLiteral("做饭")
    };
}

QString quotedOcrText(const QString &text)
{
    const bool hasOcrIntent = text.contains(QStringLiteral("字幕"), Qt::CaseInsensitive)
        || text.contains(QStringLiteral("文字"), Qt::CaseInsensitive)
        || text.contains(QStringLiteral("OCR"), Qt::CaseInsensitive)
        || text.contains(QStringLiteral("写着"), Qt::CaseInsensitive)
        || text.contains(QStringLiteral("显示"), Qt::CaseInsensitive);
    if (!hasOcrIntent) {
        return {};
    }
    static const QRegularExpression quoted(
        QStringLiteral("[“\\\"']([^”\\\"']+)[”\\\"']"),
        QRegularExpression::CaseInsensitiveOption);
    const auto match = quoted.match(text);
    return match.hasMatch() ? match.captured(1).trimmed() : QString();
}
}

ParsedMaterialQuery NaturalLanguageQueryParser::parse(const QString &text,
                                                       const QDate &referenceDate) const
{
    ParsedMaterialQuery query;
    query.originalText = text.trimmed();
    query.semanticText = query.originalText;
    if (query.originalText.isEmpty()) {
        return query;
    }

    QString working = query.originalText;
    const auto parsedDate = normalizedDateFromText(working, referenceDate);
    query.dateConstraint.startDate = parsedDate.startDate;
    query.dateConstraint.endDate = parsedDate.endDate;
    query.dateConstraint.matchedText = parsedDate.matchedText;
    if (!parsedDate.matchedText.isEmpty()) {
        working.replace(parsedDate.matchedText, QStringLiteral(" "), Qt::CaseInsensitive);
    }
    if (query.dateConstraint.isExactDate()) {
        query.normalizedDate = query.dateConstraint.startDate;
    }

    query.folderIntent = working.contains(QStringLiteral("文件夹"))
        || working.contains(QStringLiteral("目录"));
    const bool hasExplicitAssetTarget = !assetTypesFromText(working, nullptr).isEmpty()
        || working.contains(QStringLiteral("素材"), Qt::CaseInsensitive)
        || working.contains(QStringLiteral("文件"), Qt::CaseInsensitive);
    const bool hasFrameWord = working.contains(QStringLiteral("帧"), Qt::CaseInsensitive);
    const bool hasStandalonePictureIntent = working.contains(QStringLiteral("画面"), Qt::CaseInsensitive)
        && !working.contains(QStringLiteral("画面文字"), Qt::CaseInsensitive)
        && !working.contains(QStringLiteral("画面内容"), Qt::CaseInsensitive)
        && !working.contains(QStringLiteral("画面描述"), Qt::CaseInsensitive)
        && !hasExplicitAssetTarget;
    query.frameIntent = !query.folderIntent && (hasFrameWord || hasStandalonePictureIntent);
    query.resultTarget = query.folderIntent
        ? SearchResultTarget::Folders
        : (query.frameIntent ? SearchResultTarget::Frames : SearchResultTarget::Assets);
    const QString compactOriginal = QString(query.originalText).remove(QRegularExpression(QStringLiteral("\\s+")));
    query.folderByAssetCriteria = query.folderIntent
        && (compactOriginal.contains(QStringLiteral("所在文件夹"))
            || compactOriginal.contains(QStringLiteral("所在的文件夹"))
            || compactOriginal.contains(QStringLiteral("所在目录"))
            || compactOriginal.contains(QStringLiteral("所在的目录"))
            || compactOriginal.contains(QStringLiteral("所属文件夹"))
            || compactOriginal.contains(QStringLiteral("所属的文件夹"))
            || compactOriginal.contains(QStringLiteral("所属目录"))
            || compactOriginal.contains(QStringLiteral("所属的目录"))
            || compactOriginal.contains(QStringLiteral("存放文件夹"))
            || compactOriginal.contains(QStringLiteral("存放的文件夹"))
            || compactOriginal.contains(QStringLiteral("包含")));
    query.dateConstraint.preferredField = preferredDateField(
        query.originalText,
        query.folderIntent && !query.folderByAssetCriteria);
    QStringList matchedTypeWords;
    const auto parsedAssetTypes = assetTypesFromText(working, &matchedTypeWords);
    if (query.resultTarget == SearchResultTarget::Assets
        || query.resultTarget == SearchResultTarget::Frames
        || query.folderByAssetCriteria) {
        query.assetTypeFilters = parsedAssetTypes;
        query.assetTypeFilter = query.assetTypeFilters.isEmpty()
            ? -1
            : query.assetTypeFilters.first();
    } else {
        matchedTypeWords.clear();
    }

    const auto colors = matchingTerms(working, colorDictionary());
    auto materials = matchingTerms(working, materialDictionary());
    const auto attributes = matchingTerms(working, attributeDictionary());
    const auto labels = matchingTerms(working, entityLabelDictionary());
    const auto specificLabels = mostSpecificTerms(labels);
    query.explicitEntityLabels = specificLabels;
    const auto visualTerms = matchingTerms(working, visualLexicalDictionary());
    query.ocrText = quotedOcrText(query.originalText);
    if (specificLabels.size() == 1) {
        removeTermsImpliedByLabel(&materials, specificLabels.first());
    }
    // When several different entities are present, assigning a modifier to the
    // first noun is ambiguous. The real-time text assistant resolves that
    // relation; the deterministic parser only creates a strict constraint for
    // one unambiguous entity.
    if (specificLabels.size() == 1
        && (!colors.isEmpty() || !materials.isEmpty() || !attributes.isEmpty())) {
        StrictEntityConstraint constraint;
        constraint.label = specificLabels.first();
        constraint.colors = colors;
        constraint.materials = materials;
        constraint.attributes = attributes;
        query.strictEntities.append(constraint);
    }

    QString contentWorking = working;
    QStringList intentTerms = matchedTypeWords;
    intentTerms.append({
        QStringLiteral("帮我找"), QStringLiteral("帮忙找"), QStringLiteral("查找"), QStringLiteral("搜索"),
        QStringLiteral("寻找"), QStringLiteral("找到"), QStringLiteral("看看"), QStringLiteral("找"),
        QStringLiteral("搜"), QStringLiteral("我想要"), QStringLiteral("给我"),
        QStringLiteral("文件夹"), QStringLiteral("目录"), QStringLiteral("素材"), QStringLiteral("文件"),
        QStringLiteral("请找"), QStringLiteral("请查"), QStringLiteral("相关的"),
        QStringLiteral("画面里面"), QStringLiteral("画面里"), QStringLiteral("画面中"),
        QStringLiteral("画面")
    });
    if (!query.dateConstraint.isEmpty()) {
        intentTerms.append({
            QStringLiteral("拍摄于"), QStringLiteral("拍摄的"), QStringLiteral("拍的"),
            QStringLiteral("拍于"), QStringLiteral("摄于"), QStringLiteral("录制的"),
            QStringLiteral("录制于"), QStringLiteral("拍摄"), QStringLiteral("录制")
        });
    }
    if (query.folderByAssetCriteria) {
        intentTerms.append({
            QStringLiteral("所在的"), QStringLiteral("所在"), QStringLiteral("所属的"),
            QStringLiteral("所属"), QStringLiteral("存放的"), QStringLiteral("存放"),
            QStringLiteral("包含的"), QStringLiteral("包含"), QStringLiteral("以及"),
            QStringLiteral("或者"), QStringLiteral("和"), QStringLiteral("或")
        });
    }
    if (query.frameIntent) {
        intentTerms.append({
            QStringLiteral("关键帧"), QStringLiteral("视频帧"), QStringLiteral("画面帧"),
            QStringLiteral("镜头帧"), QStringLiteral("帧画面"), QStringLiteral("帧"),
            QStringLiteral("包含了"), QStringLiteral("包含有"), QStringLiteral("包含"),
            QStringLiteral("包括了"), QStringLiteral("包括"), QStringLiteral("带有")
        });
    }
    removeTerms(&contentWorking, intentTerms, &query.ignoredIntentTerms);
    contentWorking.replace(QRegularExpression(QStringLiteral("(^|\\s)的(?=\\s|$)")),
                           QStringLiteral(" "));
    contentWorking = contentWorking.simplified();
    contentWorking.remove(QRegularExpression(QStringLiteral("^(的|有|在)+")));
    contentWorking.remove(QRegularExpression(QStringLiteral("(的|里面|中的)$")));
    contentWorking = contentWorking.trimmed();
    query.semanticText = contentWorking;

    QString lexicalWorking = contentWorking;
    QStringList structuredTerms;
    structuredTerms.append(colors);
    structuredTerms.append(materials);
    structuredTerms.append(attributes);
    structuredTerms.append(labels);
    removeTerms(&lexicalWorking, structuredTerms);

    QStringList lexicalTerms;
    static const QRegularExpression separator(QStringLiteral("[\\s,，。;；、|/\\\\]+"));
    for (auto part : lexicalWorking.split(separator, Qt::SkipEmptyParts)) {
        part = part.trimmed();
        part.remove(QRegularExpression(QStringLiteral("^(的|有|在|穿着|穿|是)+")));
        part.remove(QRegularExpression(QStringLiteral("(的|有|在|里面|中的)$")));
        if (!part.isEmpty()) {
            lexicalTerms.append(part);
        }
    }
    lexicalTerms.append(structuredTerms);
    lexicalTerms.append(visualTerms);
    if (!query.ocrText.isEmpty()) {
        lexicalTerms.append(query.ocrText);
    }
    if (query.hasStrictEntityConstraints()) {
        for (const auto &constraint : query.strictEntities) {
            lexicalTerms.append(constraint.allTerms());
        }
    }
    query.lexicalTerms = uniqueTerms(lexicalTerms);

    if (!query.dateConstraint.isEmpty()) {
        const auto value = query.dateConstraint.isExactDate()
            ? query.dateConstraint.startDate
            : QStringLiteral("%1 至 %2")
                  .arg(query.dateConstraint.startDate, query.dateConstraint.endDate);
        query.interpretationLabels.append(
            QStringLiteral("%1：%2")
                .arg(dateFieldLabel(query.dateConstraint.preferredField), value));
    }
    if (query.assetTypeFilter >= 0) {
        QStringList typeLabels;
        for (const auto type : std::as_const(query.assetTypeFilters)) {
            typeLabels.append(assetTypeLabel(type));
        }
        query.interpretationLabels.append(
            QStringLiteral("类型：%1").arg(typeLabels.join(QStringLiteral(" / "))));
    }
    if (query.folderIntent) {
        query.interpretationLabels.append(query.folderByAssetCriteria
            ? QStringLiteral("目标：匹配素材所在的文件夹")
            : QStringLiteral("目标：文件夹"));
    } else if (query.frameIntent) {
        query.interpretationLabels.append(QStringLiteral("目标：视觉帧"));
    } else {
        query.interpretationLabels.append(QStringLiteral("目标：素材"));
    }
    if (query.hasStrictEntityConstraints()) {
        query.interpretationLabels.append(
            QStringLiteral("同一对象：%1")
                .arg(query.strictEntities.first().allTerms().join(QLatin1Char(' '))));
    }
    if (!query.semanticText.isEmpty()) {
        query.interpretationLabels.append(
            QStringLiteral("内容：%1").arg(query.semanticText));
    }
    if (!query.ocrText.isEmpty()) {
        query.interpretationLabels.append(
            QStringLiteral("画面文字：%1").arg(query.ocrText));
    }
    query.ignoredIntentTerms = uniqueTerms(query.ignoredIntentTerms);
    return query;
}
