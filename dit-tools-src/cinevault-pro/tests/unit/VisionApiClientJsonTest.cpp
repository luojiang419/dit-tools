#include <QtTest>

#include "infrastructure/network/VisionResponseParser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

using VisionResponseParser::parseAssistantJson;
using VisionResponseParser::normalizeFrameAnalysis;
using VisionResponseParser::normalizeVideoSummary;
using VisionResponseParser::extractAssistantEnvelope;
using VisionResponseParser::extractAssistantContent;
using VisionResponseParser::fallbackFrameAnalysisFromContent;
using VisionResponseParser::fallbackVideoSummaryFromContent;

namespace {
QByteArray chatResponse(const QJsonValue &content)
{
    const QJsonObject root{
        {QStringLiteral("choices"),
         QJsonArray{
             QJsonObject{
                 {QStringLiteral("message"),
                  QJsonObject{
                      {QStringLiteral("content"), content}
                  }}
             }
         }}
    };
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}
}

class VisionApiClientJsonTest : public QObject {
    Q_OBJECT

private slots:
    void parseAssistantJson_acceptsPureJson()
    {
        QString error;
        const auto payload = parseAssistantJson(chatResponse(QStringLiteral("{\"status\":\"ok\"}")), &error);

        QVERIFY2(payload.has_value(), qPrintable(error));
        QCOMPARE(payload->value(QStringLiteral("status")).toString(), QStringLiteral("ok"));
    }

    void parseAssistantJson_acceptsMarkdownJson()
    {
        QString error;
        const auto payload = parseAssistantJson(
            chatResponse(QStringLiteral("```json\n{\"caption\":\"frame\",\"tags\":[\"a\"]}\n```")),
            &error);

        QVERIFY2(payload.has_value(), qPrintable(error));
        QCOMPARE(payload->value(QStringLiteral("caption")).toString(), QStringLiteral("frame"));
    }

    void parseAssistantJson_acceptsJsonWithTextAroundIt()
    {
        QString error;
        const auto payload = parseAssistantJson(
            chatResponse(QStringLiteral("Result:\n{\"caption\":\"frame\",\"objects\":[\"camera\"]}\nDone.")),
            &error);

        QVERIFY2(payload.has_value(), qPrintable(error));
        QCOMPARE(payload->value(QStringLiteral("caption")).toString(), QStringLiteral("frame"));
    }

    void parseAssistantJson_prefersLastValidObject()
    {
        QString error;
        const auto payload = parseAssistantJson(
            chatResponse(QStringLiteral("Schema: {\"caption\":\"\"}\nActual: {\"caption\":\"real frame\"}")),
            &error);

        QVERIFY2(payload.has_value(), qPrintable(error));
        QCOMPARE(payload->value(QStringLiteral("caption")).toString(), QStringLiteral("real frame"));
    }

    void parseAssistantJson_acceptsJsonAfterReasoningTag()
    {
        QString error;
        const auto payload = parseAssistantJson(
            chatResponse(QStringLiteral("<think>reasoning with {braces}</think>\n{\"summary\":\"video\"}")),
            &error);

        QVERIFY2(payload.has_value(), qPrintable(error));
        QCOMPARE(payload->value(QStringLiteral("summary")).toString(), QStringLiteral("video"));
    }

    void parseAssistantJson_rejectsContentWithoutJsonObject()
    {
        QString error;
        const auto payload = parseAssistantJson(chatResponse(QStringLiteral("no json here")), &error);

        QVERIFY(!payload.has_value());
        QVERIFY(!error.isEmpty());
    }

    void parseAssistantJson_classifiesReasoningOnlyTokenLimit()
    {
        const QJsonObject root{
            {QStringLiteral("choices"), QJsonArray{QJsonObject{
                {QStringLiteral("finish_reason"), QStringLiteral("length")},
                {QStringLiteral("message"), QJsonObject{
                    {QStringLiteral("content"), QString()},
                    {QStringLiteral("reasoning_content"), QStringLiteral("内部推理内容")}
                }}
            }}},
            {QStringLiteral("usage"), QJsonObject{
                {QStringLiteral("prompt_tokens"), 1200},
                {QStringLiteral("completion_tokens"), 640},
                {QStringLiteral("total_tokens"), 1840}
            }}
        };
        const auto response = QJsonDocument(root).toJson(QJsonDocument::Compact);

        QString envelopeError;
        const auto envelope = extractAssistantEnvelope(response, &envelopeError);
        QVERIFY2(envelope.has_value(), qPrintable(envelopeError));
        QVERIFY(envelope->content.isEmpty());
        QCOMPARE(envelope->reasoningLength, qsizetype{6});
        QCOMPARE(envelope->finishReason, QStringLiteral("length"));
        QCOMPARE(envelope->promptTokens, qint64{1200});
        QCOMPARE(envelope->completionTokens, qint64{640});

        QString parseError;
        const auto payload = parseAssistantJson(response, &parseError);
        QVERIFY(!payload.has_value());
        QVERIFY(parseError.contains(QStringLiteral("token 上限")));
        QVERIFY(parseError.contains(QStringLiteral("仅返回推理内容")));
    }

    void parseAssistantJson_classifiesEmptyFinalContent()
    {
        const QJsonObject root{
            {QStringLiteral("choices"), QJsonArray{QJsonObject{
                {QStringLiteral("finish_reason"), QStringLiteral("stop")},
                {QStringLiteral("message"), QJsonObject{
                    {QStringLiteral("content"), QString()}
                }}
            }}}
        };

        QString error;
        const auto payload = parseAssistantJson(
            QJsonDocument(root).toJson(QJsonDocument::Compact), &error);

        QVERIFY(!payload.has_value());
        QCOMPARE(error, QStringLiteral("视觉服务返回 HTTP 200，但最终正文为空"));
    }

    void parseAssistantJson_acceptsTextContentArray()
    {
        const QJsonArray content{
            QJsonObject{
                {QStringLiteral("type"), QStringLiteral("text")},
                {QStringLiteral("text"), QStringLiteral("Here: {\"status\":\"ok\"}")}
            }
        };

        QString error;
        const auto payload = parseAssistantJson(chatResponse(content), &error);

        QVERIFY2(payload.has_value(), qPrintable(error));
        QCOMPARE(payload->value(QStringLiteral("status")).toString(), QStringLiteral("ok"));
    }

    void extractAssistantContent_returnsPlainTextForRepair()
    {
        QString error;
        const auto content = extractAssistantContent(
            chatResponse(QStringLiteral("画面描述：人物在街道上行走。标签：人物、街道。")),
            &error);

        QVERIFY2(content.has_value(), qPrintable(error));
        QCOMPARE(*content, QStringLiteral("画面描述：人物在街道上行走。标签：人物、街道。"));
    }

    void extractAssistantContent_returnsCsvForRepair()
    {
        QString error;
        const auto content = extractAssistantContent(
            chatResponse(QStringLiteral("caption,tags,objects\n人物在室内,人物|室内,摄影机")),
            &error);

        QVERIFY2(content.has_value(), qPrintable(error));
        QVERIFY(content->contains(QStringLiteral("caption,tags,objects")));
        QVERIFY(content->contains(QStringLiteral("摄影机")));
    }

    void extractAssistantContent_returnsMarkdownTableForRepair()
    {
        QString error;
        const auto content = extractAssistantContent(
            chatResponse(QStringLiteral("| caption | tags |\n| --- | --- |\n| 棚内拍摄 | 摄影,灯光 |")),
            &error);

        QVERIFY2(content.has_value(), qPrintable(error));
        QVERIFY(content->contains(QStringLiteral("| caption | tags |")));
        QVERIFY(content->contains(QStringLiteral("棚内拍摄")));
    }

    void normalizeFrameAnalysis_acceptsAliasesAndStringLists()
    {
        const QJsonObject payload{
            {QStringLiteral("description"), QStringLiteral("人物在室内检查摄影机")},
            {QStringLiteral("keywords"), QStringLiteral("人物, 室内；摄影机")},
            {QStringLiteral("subjects"), QStringLiteral("摄影师、监视器")},
            {QStringLiteral("action"), QStringLiteral("检查设备")},
            {QStringLiteral("scene"), QStringLiteral("工作室")}
        };

        QString error;
        const auto analysis = normalizeFrameAnalysis(payload, &error);

        QVERIFY2(analysis.has_value(), qPrintable(error));
        QCOMPARE(analysis->caption, QStringLiteral("人物在室内检查摄影机"));
        QCOMPARE(analysis->tags, QStringList({QStringLiteral("人物"), QStringLiteral("室内"), QStringLiteral("摄影机")}));
        QCOMPARE(analysis->objects, QStringList({QStringLiteral("摄影师"), QStringLiteral("监视器")}));
        QCOMPARE(analysis->actions, QStringLiteral("检查设备"));
        QCOMPARE(analysis->setting, QStringLiteral("工作室"));
    }

    void normalizeFrameAnalysis_acceptsNestedTextObjects()
    {
        const QJsonObject payload{
            {QStringLiteral("caption"), QJsonObject{{QStringLiteral("text"), QStringLiteral("街道上的车辆")}}},
            {QStringLiteral("objects"), QJsonArray{
                 QStringLiteral("车辆"),
                 QJsonObject{{QStringLiteral("name"), QStringLiteral("行人")}}
             }},
            {QStringLiteral("setting"), QJsonArray{QStringLiteral("街道"), QStringLiteral("白天")}}
        };

        QString error;
        const auto analysis = normalizeFrameAnalysis(payload, &error);

        QVERIFY2(analysis.has_value(), qPrintable(error));
        QCOMPARE(analysis->caption, QStringLiteral("街道上的车辆"));
        QCOMPARE(analysis->objects, QStringList({QStringLiteral("车辆"), QStringLiteral("行人")}));
        QCOMPARE(analysis->setting, QStringLiteral("街道；白天"));
    }

    void normalizeFrameAnalysis_preservesEntityBindingsAndOcrCompleteness()
    {
        const QJsonObject payload{
            {QStringLiteral("caption"), QStringLiteral("人物站在广告牌旁")},
            {QStringLiteral("tags"), QJsonArray{QStringLiteral("人物"), QStringLiteral("广告牌")}},
            {QStringLiteral("objects"), QJsonArray{}},
            {QStringLiteral("actions"), QStringLiteral("站立")},
            {QStringLiteral("setting"), QStringLiteral("街道")},
            {QStringLiteral("entities"), QJsonArray{
                 QJsonObject{
                     {QStringLiteral("category"), QStringLiteral("clothing")},
                     {QStringLiteral("label"), QStringLiteral("短裤")},
                     {QStringLiteral("colors"), QJsonArray{QStringLiteral("红色")}},
                     {QStringLiteral("materials"), QJsonArray{QStringLiteral("牛仔")}},
                     {QStringLiteral("attributes"), QJsonArray{QStringLiteral("破洞")}}
                 }
             }},
            {QStringLiteral("ocr_text"), QStringLiteral("SALE, 50% / OFF")},
            {QStringLiteral("ocr_blocks"), QJsonArray{QStringLiteral("SALE, 50% / OFF")}}
        };

        QString error;
        const auto analysis = normalizeFrameAnalysis(payload, &error);

        QVERIFY2(analysis.has_value(), qPrintable(error));
        QVERIFY(analysis->factsComplete);
        QCOMPARE(analysis->structuredProfileVersion, 2);
        QCOMPARE(analysis->entities.size(), 1);
        QCOMPARE(analysis->entities.first().label, QStringLiteral("短裤"));
        QCOMPARE(analysis->entities.first().colors, QStringList{QStringLiteral("红色")});
        QCOMPARE(analysis->entities.first().materials, QStringList{QStringLiteral("牛仔")});
        QCOMPARE(analysis->ocrText, QStringLiteral("SALE, 50% / OFF"));
        QCOMPARE(analysis->ocrBlocks, QStringList{QStringLiteral("SALE, 50% / OFF")});
        QCOMPARE(analysis->objects, QStringList{QStringLiteral("短裤")});
    }

    void normalizeFrameAnalysis_rejectsEmptyPayload()
    {
        QString error;
        const auto analysis = normalizeFrameAnalysis(QJsonObject{}, &error);

        QVERIFY(!analysis.has_value());
        QCOMPARE(error, QStringLiteral("视觉接口返回帧解析字段为空"));
    }

    void normalizeFrameAnalysis_doesNotMarkMalformedStructuredFieldsComplete()
    {
        const QJsonObject payload{
            {QStringLiteral("caption"), QStringLiteral("测试画面")},
            {QStringLiteral("entities"), QStringLiteral("短裤")},
            {QStringLiteral("ocr_text"), QStringLiteral("SALE")},
            {QStringLiteral("ocr_blocks"), QStringLiteral("SALE")}
        };

        QString error;
        const auto analysis = normalizeFrameAnalysis(payload, &error);

        QVERIFY2(analysis.has_value(), qPrintable(error));
        QVERIFY(!analysis->factsComplete);
        QCOMPARE(analysis->structuredProfileVersion, 1);
    }

    void normalizeFrameAnalysis_normalizesMissingOcrTextToNonNullEmptyString()
    {
        const QJsonObject payload{
            {QStringLiteral("caption"), QStringLiteral("没有可辨文字的测试画面")},
            {QStringLiteral("entities"), QJsonArray{}},
            {QStringLiteral("ocr_blocks"), QJsonArray{}}
        };

        QString error;
        const auto analysis = normalizeFrameAnalysis(payload, &error);

        QVERIFY2(analysis.has_value(), qPrintable(error));
        QVERIFY(!analysis->ocrText.isNull());
        QVERIFY(analysis->ocrText.isEmpty());
        QVERIFY(!analysis->factsComplete);
    }

    void normalizeFrameAnalysis_normalizesNullOcrTextToNonNullEmptyString()
    {
        const QJsonObject payload{
            {QStringLiteral("caption"), QStringLiteral("没有可辨文字的测试画面")},
            {QStringLiteral("entities"), QJsonArray{}},
            {QStringLiteral("ocr_text"), QJsonValue::Null},
            {QStringLiteral("ocr_blocks"), QJsonArray{}}
        };

        QString error;
        const auto analysis = normalizeFrameAnalysis(payload, &error);

        QVERIFY2(analysis.has_value(), qPrintable(error));
        QVERIFY(!analysis->ocrText.isNull());
        QVERIFY(analysis->ocrText.isEmpty());
        QVERIFY(!analysis->factsComplete);
    }

    void normalizeVideoSummary_acceptsAliasesAndStringLists()
    {
        const QJsonObject payload{
            {QStringLiteral("overview"), QStringLiteral("视频展示工作人员在棚内调试拍摄设备。")},
            {QStringLiteral("tags"), QJsonArray{QStringLiteral("拍摄"), QStringLiteral("工作室, 设备")}},
            {QStringLiteral("locations"), QStringLiteral("[\"棚内\",\"工作区\"]")}
        };

        QString error;
        const auto summary = normalizeVideoSummary(payload, &error);

        QVERIFY2(summary.has_value(), qPrintable(error));
        QCOMPARE(summary->summary, QStringLiteral("视频展示工作人员在棚内调试拍摄设备。"));
        QCOMPARE(summary->keywords, QStringList({QStringLiteral("拍摄"), QStringLiteral("工作室"), QStringLiteral("设备")}));
        QCOMPARE(summary->scenes, QStringList({QStringLiteral("棚内"), QStringLiteral("工作区")}));
    }

    void normalizeVideoSummary_rejectsEmptyPayload()
    {
        QString error;
        const auto summary = normalizeVideoSummary(QJsonObject{}, &error);

        QVERIFY(!summary.has_value());
        QCOMPARE(error, QStringLiteral("视觉接口返回视频汇总字段为空"));
    }

    void fallbackFrameAnalysisFromContent_preservesPlainText()
    {
        QString error;
        const auto analysis = fallbackFrameAnalysisFromContent(
            QStringLiteral("画面描述：人物在街道上行走。标签：人物、街道。"),
            &error);

        QVERIFY2(analysis.has_value(), qPrintable(error));
        QCOMPARE(analysis->caption, QStringLiteral("画面描述：人物在街道上行走。标签：人物、街道。"));
        QVERIFY(analysis->tags.isEmpty());
        QVERIFY(analysis->objects.isEmpty());
        QVERIFY(analysis->actions.isEmpty());
        QVERIFY(analysis->setting.isEmpty());
    }

    void fallbackVideoSummaryFromContent_preservesMarkdownTable()
    {
        const auto table = QStringLiteral("| summary | keywords |\n| --- | --- |\n| 棚内拍摄和灯光调试 | 摄影,灯光 |");

        QString error;
        const auto summary = fallbackVideoSummaryFromContent(table, &error);

        QVERIFY2(summary.has_value(), qPrintable(error));
        QCOMPARE(summary->summary, table);
        QVERIFY(summary->keywords.isEmpty());
        QVERIFY(summary->scenes.isEmpty());
    }

    void fallbackFrameAnalysisFromContent_rejectsEmptyText()
    {
        QString error;
        const auto analysis = fallbackFrameAnalysisFromContent(QStringLiteral("   "), &error);

        QVERIFY(!analysis.has_value());
        QCOMPARE(error, QStringLiteral("视觉接口原始返回内容为空，无法生成帧解析兜底文本"));
    }
};

QTEST_MAIN(VisionApiClientJsonTest)

#include "VisionApiClientJsonTest.moc"
