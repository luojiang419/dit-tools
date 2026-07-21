#include "infrastructure/raw/RawPreviewProtocol.h"
#include "infrastructure/raw/RawPreviewDecoder.h"

#include <QGuiApplication>
#include <QJsonObject>
#include <QtEndian>

#include <cstdio>
#include <exception>

#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif

namespace {

bool readExact(FILE *input, qsizetype size, QByteArray *data)
{
    data->resize(size);
    qsizetype offset = 0;
    while (offset < size) {
        const auto readSize = std::fread(data->data() + offset,
                                         1,
                                         static_cast<size_t>(size - offset),
                                         input);
        if (readSize == 0) {
            data->clear();
            return false;
        }
        offset += static_cast<qsizetype>(readSize);
    }
    return true;
}

bool readRequest(FILE *input, QJsonObject *request, QString *errorMessage)
{
    QByteArray prefix;
    if (!readExact(input, 4, &prefix)) {
        return false;
    }
    const auto payloadSize = qFromBigEndian<quint32>(
        reinterpret_cast<const uchar *>(prefix.constData()));
    if (payloadSize == 0
        || payloadSize > static_cast<quint32>(RawPreviewProtocol::MaximumPayloadBytes)) {
        *errorMessage = QStringLiteral("请求帧长度越界：%1").arg(payloadSize);
        return false;
    }

    QByteArray payload;
    if (!readExact(input, payloadSize, &payload)) {
        *errorMessage = QStringLiteral("请求帧在 JSON 载荷完成前中断");
        return false;
    }
    auto frame = prefix + payload;
    return RawPreviewProtocol::tryTakeMessage(&frame, request, errorMessage)
        == RawPreviewProtocol::ReadStatus::MessageReady;
}

QJsonObject handleRequest(const QJsonObject &request)
{
    const auto requestId = request.value(QStringLiteral("requestId")).toString();
    if (request.value(QStringLiteral("protocolVersion")).toInt()
            != RawPreviewProtocol::ProtocolVersion
        || requestId.isEmpty()) {
        return RawPreviewProtocol::errorResponse(
            requestId,
            QStringLiteral("protocol_error"),
            QStringLiteral("协议版本或请求 ID 无效"),
            false);
    }

    const auto command = request.value(QStringLiteral("command")).toString();
    if (command == QStringLiteral("ping")) {
        return RawPreviewProtocol::successResponse(requestId, {
            {QStringLiteral("workerVersion"), QStringLiteral("raw-worker-v1")},
            {QStringLiteral("protocolVersion"), RawPreviewProtocol::ProtocolVersion},
            {QStringLiteral("serial"), true},
        });
    }
    if (command == QStringLiteral("decode")) {
        if (!request.value(QStringLiteral("payload")).isObject()) {
            return RawPreviewProtocol::errorResponse(
                requestId,
                QStringLiteral("invalid_request"),
                QStringLiteral("RAW 解码请求缺少 payload"),
                false);
        }
        const auto decoded = RawPreviewDecoder::decode(
            request.value(QStringLiteral("payload")).toObject());
        if (!decoded.success) {
            return RawPreviewProtocol::errorResponse(
                requestId,
                decoded.errorCode.isEmpty()
                    ? QStringLiteral("decode_failed")
                    : decoded.errorCode,
                decoded.errorMessage.isEmpty()
                    ? QStringLiteral("RAW 预览生成失败")
                    : decoded.errorMessage,
                decoded.errorCode == QStringLiteral("cache_write_failed"));
        }
        return RawPreviewProtocol::successResponse(requestId, {
            {QStringLiteral("outputPath"), decoded.outputPath},
            {QStringLiteral("provider"), decoded.provider},
            {QStringLiteral("placeholder"), decoded.placeholder},
            {QStringLiteral("width"), decoded.width},
            {QStringLiteral("height"), decoded.height},
            {QStringLiteral("sourceSize"), decoded.sourceSize},
            {QStringLiteral("sourceModifiedMs"), decoded.sourceModifiedMs},
            {QStringLiteral("decoderPackageVersion"), decoded.decoderPackageVersion},
            {QStringLiteral("profileVersion"), decoded.profileVersion},
            {QStringLiteral("generatorProfile"), decoded.generatorProfile},
            {QStringLiteral("cacheKey"), decoded.cacheKey},
            {QStringLiteral("attempts"), decoded.attempts},
        });
    }
    return RawPreviewProtocol::errorResponse(
        requestId,
        QStringLiteral("unsupported_command"),
        QStringLiteral("不支持的 RAW worker 命令：%1").arg(command),
        false);
}

} // namespace

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    QGuiApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("CineVaultRawWorker"));

    while (true) {
        QJsonObject request;
        QString errorMessage;
        if (!readRequest(stdin, &request, &errorMessage)) {
            return std::feof(stdin) ? 0 : 3;
        }
        QJsonObject response;
        try {
            response = handleRequest(request);
        } catch (const std::exception &exception) {
            response = RawPreviewProtocol::errorResponse(
                request.value(QStringLiteral("requestId")).toString(),
                QStringLiteral("worker_exception"),
                QStringLiteral("RAW worker 异常：%1").arg(QString::fromLocal8Bit(exception.what())),
                true);
        } catch (...) {
            response = RawPreviewProtocol::errorResponse(
                request.value(QStringLiteral("requestId")).toString(),
                QStringLiteral("worker_exception"),
                QStringLiteral("RAW worker 发生未知异常"),
                true);
        }
        const auto frame = RawPreviewProtocol::encodeMessage(response, &errorMessage);
        if (frame.isEmpty()
            || std::fwrite(frame.constData(), 1, static_cast<size_t>(frame.size()), stdout)
                != static_cast<size_t>(frame.size())
            || std::fflush(stdout) != 0) {
            return 4;
        }
    }
}
