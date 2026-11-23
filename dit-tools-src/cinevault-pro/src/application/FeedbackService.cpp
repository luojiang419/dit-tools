#include "application/FeedbackService.h"

#include "application/ProjectService.h"
#include "infrastructure/config/AppSettings.h"
#include "shared/Paths.h"

#include <algorithm>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QScopeGuard>
#include <QSysInfo>
#include <QTimer>
#include <QUrlQuery>
#include <QWebSocket>

namespace {
constexpr auto kFeedbackServiceUrl = "http://115.231.35.105:3021/";
constexpr auto kTransferTimeoutMs = 120000;

QString jsonString(const QJsonObject &object, const QString &key)
{
    return object.value(key).toString().trimmed();
}

qint64 jsonInt64(const QJsonObject &object, const QString &key)
{
    return static_cast<qint64>(object.value(key).toDouble());
}

int jsonInt(const QJsonObject &object, const QString &key)
{
    return object.value(key).toInt();
}

QList<qint64> jsonInt64List(const QJsonArray &array)
{
    QList<qint64> values;
    values.reserve(array.size());
    for (const auto &value : array) {
        values.append(static_cast<qint64>(value.toDouble()));
    }
    return values;
}

QString uploadAttachmentMimeType(const QString &path)
{
    QMimeDatabase database;
    const auto mime = database.mimeTypeForFile(path, QMimeDatabase::MatchDefault);
    const auto normalized = mime.name().trimmed().toLower();
    return normalized.isEmpty()
        ? QStringLiteral("application/octet-stream")
        : normalized;
}

QString errorDetailFromPayload(const QByteArray &payload)
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return QString::fromUtf8(payload).trimmed();
    }

    const auto object = document.object();
    const auto detail = object.value(QStringLiteral("detail"));
    if (detail.isString()) {
        return detail.toString().trimmed();
    }
    if (detail.isArray()) {
        QStringList pieces;
        const auto items = detail.toArray();
        for (const auto &item : items) {
            if (item.isObject()) {
                const auto part = item.toObject().value(QStringLiteral("msg")).toString().trimmed();
                if (!part.isEmpty()) {
                    pieces.append(part);
                }
            } else if (item.isString()) {
                const auto part = item.toString().trimmed();
                if (!part.isEmpty()) {
                    pieces.append(part);
                }
            }
        }
        return pieces.join(QStringLiteral("；"));
    }
    return {};
}

QString replyErrorMessage(QNetworkReply *reply, const QString &fallbackMessage)
{
    const auto detail = errorDetailFromPayload(reply->readAll());
    const auto statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (!detail.isEmpty()) {
        if (statusCode > 0) {
            return QStringLiteral("%1（%2）").arg(detail).arg(statusCode);
        }
        return detail;
    }

    if (reply->error() != QNetworkReply::NoError) {
        return QStringLiteral("%1：%2").arg(fallbackMessage, reply->errorString());
    }
    if (statusCode > 0) {
        return QStringLiteral("%1（%2）").arg(fallbackMessage).arg(statusCode);
    }
    return fallbackMessage;
}

void addTextPart(QHttpMultiPart *multiPart, const QString &name, const QString &value)
{
    auto part = QHttpPart{};
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QStringLiteral("form-data; name=\"%1\"").arg(name));
    part.setBody(value.toUtf8());
    multiPart->append(part);
}

QUrl websocketUrlFromString(const QString &value)
{
    auto url = QUrl(value);
    if (!url.isValid()) {
        return {};
    }
    if (url.scheme() == QStringLiteral("http")) {
        url.setScheme(QStringLiteral("ws"));
    } else if (url.scheme() == QStringLiteral("https")) {
        url.setScheme(QStringLiteral("wss"));
    }
    return url;
}

qint64 latestAdminMessageId(const QVector<FeedbackMessage> &messages)
{
    qint64 latestId = 0;
    for (const auto &message : messages) {
        if (message.senderRole == QStringLiteral("admin")) {
            latestId = qMax(latestId, message.id);
        }
    }
    return latestId;
}

QString fallbackAttachmentName(const QString &displayName, const QUrl &sourceUrl)
{
    const auto normalizedName = QFileInfo(displayName.trimmed()).fileName();
    if (!normalizedName.isEmpty()) {
        return normalizedName;
    }

    const auto pathName = QFileInfo(sourceUrl.path()).fileName();
    return pathName.isEmpty() ? QStringLiteral("attachment") : pathName;
}

QString sanitizeAttachmentFileName(const QString &fileName)
{
    auto cleaned = QFileInfo(fileName.trimmed()).fileName();
    if (cleaned.isEmpty()) {
        cleaned = QStringLiteral("attachment");
    }
    cleaned.replace(QRegularExpression(QStringLiteral(R"([^A-Za-z0-9._-]+)")), QStringLiteral("_"));
    cleaned = cleaned.trimmed();
    cleaned.remove(QRegularExpression(QStringLiteral(R"(^[._-]+|[._-]+$)")));
    return cleaned.isEmpty() ? QStringLiteral("attachment") : cleaned;
}

QString feedbackAttachmentCacheRoot()
{
    return QDir(Paths::cacheRoot()).filePath(QStringLiteral("feedback-attachments"));
}

QString feedbackAttachmentCachePath(const QString &attachmentId, const QString &name)
{
    const auto normalizedId = attachmentId.trimmed().isEmpty()
        ? QStringLiteral("attachment")
        : sanitizeAttachmentFileName(attachmentId.trimmed());
    const auto normalizedName = sanitizeAttachmentFileName(name);
    return QDir(feedbackAttachmentCacheRoot())
        .filePath(QStringLiteral("%1__%2").arg(normalizedId, normalizedName));
}
}

FeedbackService::FeedbackService(AppSettings *settings, ProjectService *projectService, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_projectService(projectService)
    , m_network(new QNetworkAccessManager(this))
    , m_socket(new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this))
{
    connect(m_socket, &QWebSocket::connected, this, &FeedbackService::handleRealtimeConnected);
    connect(m_socket, &QWebSocket::disconnected, this, &FeedbackService::handleRealtimeDisconnected);
    connect(m_socket, &QWebSocket::textMessageReceived, this, &FeedbackService::handleRealtimeTextMessage);

    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, [this]() {
        if (m_conversation.clientWsUrl.isEmpty()) {
            activate();
        } else {
            openRealtime();
        }
    });

    loadStoredSession();
    if (hasProfile()) {
        QTimer::singleShot(0, this, &FeedbackService::activate);
    } else {
        setStatusMessage(QStringLiteral("请先填写昵称和联系方式，建立反馈会话。"));
    }
}

FeedbackService::~FeedbackService()
{
    disconnectRealtime();
}

bool FeedbackService::hasProfile() const
{
    return !m_session.nickname.isEmpty() && !m_session.contact.isEmpty();
}

bool FeedbackService::ready() const
{
    return !m_conversation.conversationId.isEmpty() && hasClientAuth();
}

bool FeedbackService::busy() const
{
    return m_busy;
}

bool FeedbackService::sending() const
{
    return m_sending;
}

bool FeedbackService::socketConnected() const
{
    return m_socketConnected;
}

QString FeedbackService::statusMessage() const
{
    return m_statusMessage;
}

int FeedbackService::unreadCount() const
{
    return m_unreadCount;
}

FeedbackConversation FeedbackService::conversation() const
{
    return m_conversation;
}

QVector<FeedbackMessage> FeedbackService::messages() const
{
    return m_messages;
}

void FeedbackService::setWorkspaceActive(bool active)
{
    if (m_workspaceActive == active) {
        return;
    }
    m_workspaceActive = active;
    if (m_workspaceActive) {
        markMessagesRead();
        refreshMessages();
    }
    emit stateChanged();
}

void FeedbackService::activate()
{
    if (!hasProfile()) {
        setStatusMessage(QStringLiteral("请先填写昵称和联系方式，建立反馈会话。"));
        return;
    }

    if (!m_restoreAttempted || !ready()) {
        createOrRestoreSession(m_session.nickname, m_session.contact);
        return;
    }

    ensureRealtimeConnected();
    if (m_workspaceActive) {
        refreshMessages();
    }
}

void FeedbackService::createOrRestoreSession(const QString &nickname, const QString &contact)
{
    const auto trimmedNickname = nickname.trimmed();
    const auto trimmedContact = contact.trimmed();
    if (trimmedNickname.isEmpty() || trimmedContact.isEmpty()) {
        setStatusMessage(QStringLiteral("昵称和联系方式不能为空。"));
        emit stateChanged();
        return;
    }
    if (m_busy) {
        return;
    }

    m_session.nickname = trimmedNickname;
    m_session.contact = trimmedContact;
    persistStoredSession();

    auto request = QNetworkRequest(serviceUrl(QStringLiteral("api/client/session")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(kTransferTimeoutMs);

    setBusy(true);
    setStatusMessage(QStringLiteral("正在连接反馈服务..."));

    const auto payload = QJsonDocument(buildSessionPayload(trimmedNickname, trimmedContact)).toJson(QJsonDocument::Compact);
    auto *reply = m_network->post(request, payload);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const auto cleanup = qScopeGuard([reply]() { reply->deleteLater(); });
        setBusy(false);

        if (reply->error() != QNetworkReply::NoError
            || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() >= 400) {
            handleNetworkFailure(reply, QStringLiteral("连接反馈服务失败"));
            return;
        }

        const auto document = QJsonDocument::fromJson(reply->readAll());
        if (!document.isObject()) {
            setStatusMessage(QStringLiteral("反馈服务返回格式无效。"));
            scheduleReconnect();
            return;
        }

        const auto object = document.object();
        applyConversationPayload(object.value(QStringLiteral("conversation")).toObject());
        applyMessagesPayload(object.value(QStringLiteral("messages")).toArray());
        m_restoreAttempted = true;
        if (m_workspaceActive) {
            markMessagesRead();
        } else {
            recomputeUnreadCount();
        }
        ensureRealtimeConnected();
        setStatusMessage(QStringLiteral("反馈会话已连接，可直接发送问题与附件。"));
        emit messagesChanged();
        emit stateChanged();
    });
}

void FeedbackService::refreshMessages()
{
    if (!hasClientAuth() || m_busy) {
        return;
    }

    auto url = serviceUrl(QStringLiteral("api/client/conversations/%1/messages").arg(m_conversation.conversationId));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("client_id"), m_session.clientId);
    query.addQueryItem(QStringLiteral("client_token"), m_session.clientToken);
    url.setQuery(query);

    auto request = QNetworkRequest(url);
    request.setTransferTimeout(kTransferTimeoutMs);

    setBusy(true);
    auto *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const auto cleanup = qScopeGuard([reply]() { reply->deleteLater(); });
        setBusy(false);

        if (reply->error() != QNetworkReply::NoError
            || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() >= 400) {
            handleNetworkFailure(reply, QStringLiteral("同步反馈消息失败"));
            return;
        }

        const auto document = QJsonDocument::fromJson(reply->readAll());
        if (!document.isObject()) {
            setStatusMessage(QStringLiteral("反馈消息返回格式无效。"));
            scheduleReconnect();
            return;
        }

        const auto object = document.object();
        applyConversationPayload(object.value(QStringLiteral("conversation")).toObject());
        applyMessagesPayload(object.value(QStringLiteral("messages")).toArray());
        if (m_workspaceActive) {
            markMessagesRead();
        } else {
            recomputeUnreadCount();
        }
        setStatusMessage(QStringLiteral("反馈消息已同步。"));
        emit messagesChanged();
        emit stateChanged();
    });
}

void FeedbackService::sendMessage(const QString &text, const QStringList &filePaths)
{
    if (!ready() || m_sending) {
        emit messageSubmitted(false);
        return;
    }

    const auto normalizedText = text.trimmed();
    if (normalizedText.isEmpty() && filePaths.isEmpty()) {
        setStatusMessage(QStringLiteral("请至少输入文字或添加一个附件。"));
        emit messageSubmitted(false);
        return;
    }

    auto *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    addTextPart(multiPart, QStringLiteral("conversation_id"), m_conversation.conversationId);
    addTextPart(multiPart, QStringLiteral("client_id"), m_session.clientId);
    addTextPart(multiPart, QStringLiteral("client_token"), m_session.clientToken);
    addTextPart(multiPart, QStringLiteral("text"), normalizedText);
    addTextPart(multiPart, QStringLiteral("app_version"), QCoreApplication::applicationVersion());
    addTextPart(multiPart, QStringLiteral("system_summary"), currentSystemSummary());
    addTextPart(multiPart, QStringLiteral("project_name"), currentProjectName());
    addTextPart(multiPart, QStringLiteral("project_path"), currentProjectPath());

    for (const auto &path : filePaths) {
        auto *file = new QFile(path, multiPart);
        if (!file->open(QIODevice::ReadOnly)) {
            setStatusMessage(QStringLiteral("无法读取附件：%1").arg(path));
            file->deleteLater();
            multiPart->deleteLater();
            emit messageSubmitted(false);
            return;
        }

        auto filePart = QHttpPart{};
        const auto fileName = QFileInfo(path).fileName();
        filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                           QStringLiteral("form-data; name=\"files\"; filename=\"%1\"").arg(fileName));
        filePart.setHeader(QNetworkRequest::ContentTypeHeader, uploadAttachmentMimeType(path));
        filePart.setBodyDevice(file);
        file->setParent(multiPart);
        multiPart->append(filePart);
    }

    auto request = QNetworkRequest(serviceUrl(QStringLiteral("api/client/messages")));
    request.setTransferTimeout(kTransferTimeoutMs);

    setSending(true);
    setStatusMessage(QStringLiteral("正在发送反馈..."));

    auto *reply = m_network->post(request, multiPart);
    multiPart->setParent(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const auto cleanup = qScopeGuard([reply]() { reply->deleteLater(); });
        setSending(false);

        if (reply->error() != QNetworkReply::NoError
            || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() >= 400) {
            handleNetworkFailure(reply, QStringLiteral("发送反馈失败"));
            emit messageSubmitted(false);
            return;
        }

        const auto document = QJsonDocument::fromJson(reply->readAll());
        if (!document.isObject()) {
            setStatusMessage(QStringLiteral("发送反馈成功，但返回内容无法解析。"));
            emit messageSubmitted(false);
            return;
        }

        const auto object = document.object();
        applyConversationPayload(object.value(QStringLiteral("conversation")).toObject());
        appendOrReplaceMessage(parseMessage(object.value(QStringLiteral("message")).toObject()));
        recomputeUnreadCount();
        setStatusMessage(QStringLiteral("反馈已发送，开发者收到后会即时回复。"));
        emit messagesChanged();
        emit stateChanged();
        emit messageSubmitted(true);
    });
}

void FeedbackService::downloadAttachment(const QString &url, const QString &targetPath, const QString &displayName)
{
    const auto normalizedTargetPath = QDir::cleanPath(targetPath.trimmed());
    const auto sourceUrl = QUrl::fromUserInput(url.trimmed());
    if (!sourceUrl.isValid() || normalizedTargetPath.isEmpty()) {
        setStatusMessage(QStringLiteral("附件保存失败：缺少可用的文件地址或保存路径。"));
        emit stateChanged();
        return;
    }

    const QFileInfo targetInfo(normalizedTargetPath);
    const auto targetDirectory = targetInfo.absolutePath();
    if (!QDir().mkpath(targetDirectory)) {
        setStatusMessage(QStringLiteral("附件保存失败：无法创建目录 %1").arg(targetDirectory));
        emit stateChanged();
        return;
    }

    const auto downloadName = fallbackAttachmentName(displayName, sourceUrl);
    setStatusMessage(QStringLiteral("正在保存附件：%1").arg(downloadName));
    emit stateChanged();

    transferAttachmentToPath(sourceUrl, normalizedTargetPath, downloadName, [this, normalizedTargetPath](bool success, const QString &errorMessage) {
        if (!success) {
            setStatusMessage(errorMessage);
            emit stateChanged();
            return;
        }
        setStatusMessage(QStringLiteral("附件已保存到：%1").arg(normalizedTargetPath));
        emit stateChanged();
    });
}

void FeedbackService::ensureAttachmentCached(const QString &attachmentId,
                                            const QString &url,
                                            const QString &name,
                                            AttachmentCacheCallback callback)
{
    const auto targetPath = feedbackAttachmentCachePath(attachmentId, name);
    const auto sourceUrl = QUrl::fromUserInput(url.trimmed());
    if (!sourceUrl.isValid()) {
        if (callback) {
            callback(false, {}, QStringLiteral("文档预览准备失败：附件地址无效。"));
        }
        setStatusMessage(QStringLiteral("文档预览准备失败：附件地址无效。"));
        emit stateChanged();
        return;
    }

    if (!QDir().mkpath(feedbackAttachmentCacheRoot())) {
        const auto errorMessage = QStringLiteral("文档预览准备失败：无法创建附件缓存目录。");
        if (callback) {
            callback(false, {}, errorMessage);
        }
        setStatusMessage(errorMessage);
        emit stateChanged();
        return;
    }

    QFileInfo targetInfo(targetPath);
    if (targetInfo.exists() && targetInfo.isFile() && targetInfo.size() > 0) {
        if (callback) {
            callback(true, targetInfo.absoluteFilePath(), {});
        }
        return;
    }

    if (targetInfo.exists() && targetInfo.size() <= 0) {
        QFile::remove(targetPath);
    }

    transferAttachmentToPath(sourceUrl, targetPath, name, [this, callback, targetPath](bool success, const QString &errorMessage) {
        if (!success) {
            QFile::remove(targetPath);
            if (callback) {
                callback(false, {}, errorMessage);
            }
            setStatusMessage(errorMessage);
            emit stateChanged();
            return;
        }

        if (callback) {
            callback(true, QFileInfo(targetPath).absoluteFilePath(), {});
        }
    });
}

void FeedbackService::deleteMessage(qint64 messageId)
{
    if (!ready() || m_sending || messageId <= 0 || !hasMessage(messageId)) {
        return;
    }

    auto url = serviceUrl(QStringLiteral("api/client/conversations/%1/messages/%2").arg(m_conversation.conversationId).arg(messageId));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("client_id"), m_session.clientId);
    query.addQueryItem(QStringLiteral("client_token"), m_session.clientToken);
    url.setQuery(query);

    auto request = QNetworkRequest(url);
    request.setTransferTimeout(kTransferTimeoutMs);

    setSending(true);
    setStatusMessage(QStringLiteral("正在删除消息..."));

    auto *reply = m_network->sendCustomRequest(request, QByteArrayLiteral("DELETE"));
    connect(reply, &QNetworkReply::finished, this, [this, reply, messageId]() {
        const auto cleanup = qScopeGuard([reply]() { reply->deleteLater(); });
        setSending(false);

        if (reply->error() != QNetworkReply::NoError
            || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() >= 400) {
            handleNetworkFailure(reply, QStringLiteral("删除消息失败"));
            return;
        }

        const auto document = QJsonDocument::fromJson(reply->readAll());
        if (!document.isObject()) {
            setStatusMessage(QStringLiteral("删除消息成功，但返回内容无法解析。"));
            return;
        }

        const auto object = document.object();
        applyConversationPayload(object.value(QStringLiteral("conversation")).toObject());
        removeMessages({object.value(QStringLiteral("message_id")).toVariant().toLongLong()});
        if (m_workspaceActive) {
            markMessagesRead();
        } else {
            recomputeUnreadCount();
        }
        setStatusMessage(QStringLiteral("消息已删除。"));
        emit messagesChanged();
        emit stateChanged();
    });
}

void FeedbackService::clearClientMessages()
{
    if (!ready() || m_sending) {
        return;
    }

    auto url = serviceUrl(QStringLiteral("api/client/conversations/%1/messages").arg(m_conversation.conversationId));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("client_id"), m_session.clientId);
    query.addQueryItem(QStringLiteral("client_token"), m_session.clientToken);
    url.setQuery(query);

    auto request = QNetworkRequest(url);
    request.setTransferTimeout(kTransferTimeoutMs);

    setSending(true);
    setStatusMessage(QStringLiteral("正在清空自己发送的消息..."));

    auto *reply = m_network->sendCustomRequest(request, QByteArrayLiteral("DELETE"));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const auto cleanup = qScopeGuard([reply]() { reply->deleteLater(); });
        setSending(false);

        if (reply->error() != QNetworkReply::NoError
            || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() >= 400) {
            handleNetworkFailure(reply, QStringLiteral("清空会话窗口失败"));
            return;
        }

        const auto document = QJsonDocument::fromJson(reply->readAll());
        if (!document.isObject()) {
            setStatusMessage(QStringLiteral("清空成功，但返回内容无法解析。"));
            return;
        }

        const auto object = document.object();
        applyConversationPayload(object.value(QStringLiteral("conversation")).toObject());
        removeMessages(jsonInt64List(object.value(QStringLiteral("message_ids")).toArray()));
        if (m_workspaceActive) {
            markMessagesRead();
        } else {
            recomputeUnreadCount();
        }
        setStatusMessage(QStringLiteral("已清空自己发送的消息。"));
        emit messagesChanged();
        emit stateChanged();
    });
}

void FeedbackService::openRealtime()
{
    if (!hasClientAuth()) {
        return;
    }

    const auto wsUrl = websocketUrlFromString(m_conversation.clientWsUrl);
    if (!wsUrl.isValid()) {
        return;
    }
    if (m_socket->state() == QAbstractSocket::ConnectedState || m_socket->state() == QAbstractSocket::ConnectingState) {
        return;
    }

    m_socket->open(wsUrl);
}

void FeedbackService::handleRealtimeConnected()
{
    m_socketConnected = true;
    if (m_reconnectTimer.isActive()) {
        m_reconnectTimer.stop();
    }
    setStatusMessage(QStringLiteral("反馈实时通道已连接。"));
    emit stateChanged();
    refreshMessages();
}

void FeedbackService::handleRealtimeDisconnected()
{
    const auto wasConnected = m_socketConnected;
    m_socketConnected = false;
    emit stateChanged();
    if (hasClientAuth()) {
        if (wasConnected) {
            setStatusMessage(QStringLiteral("反馈实时通道已断开，正在尝试重连..."));
        }
        scheduleReconnect();
    }
}

void FeedbackService::handleRealtimeTextMessage(const QString &payload)
{
    const auto document = QJsonDocument::fromJson(payload.toUtf8());
    if (!document.isObject()) {
        return;
    }

    const auto object = document.object();
    const auto type = object.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("conversation.deleted")) {
        if (jsonString(object, QStringLiteral("conversation_id")) == m_conversation.conversationId) {
            resetConversationState(QStringLiteral("当前反馈会话已被删除，正在重新建立会话..."), true);
        }
        return;
    }

    applyConversationPayload(object.value(QStringLiteral("conversation")).toObject());

    if (type == QStringLiteral("message.created")) {
        const auto message = parseMessage(object.value(QStringLiteral("message")).toObject());
        if (message.id > 0) {
            appendOrReplaceMessage(message);
            if (m_workspaceActive && message.senderRole == QStringLiteral("admin")) {
                markMessagesRead();
            } else {
                recomputeUnreadCount();
            }
            emit messagesChanged();
        }
    } else if (type == QStringLiteral("message.deleted")) {
        removeMessages({jsonInt64(object, QStringLiteral("message_id"))});
        if (m_workspaceActive) {
            markMessagesRead();
        } else {
            recomputeUnreadCount();
        }
        emit messagesChanged();
    } else if (type == QStringLiteral("messages.cleared")) {
        removeMessages(jsonInt64List(object.value(QStringLiteral("message_ids")).toArray()));
        if (m_workspaceActive) {
            markMessagesRead();
        } else {
            recomputeUnreadCount();
        }
        emit messagesChanged();
    }
    emit stateChanged();
}

QUrl FeedbackService::serviceBaseUrl() const
{
    return QUrl(QString::fromLatin1(kFeedbackServiceUrl));
}

QUrl FeedbackService::serviceUrl(const QString &relativePath) const
{
    return serviceBaseUrl().resolved(QUrl(relativePath));
}

QJsonObject FeedbackService::buildSessionPayload(const QString &nickname, const QString &contact) const
{
    QJsonObject payload{
        {QStringLiteral("nickname"), nickname},
        {QStringLiteral("contact"), contact},
        {QStringLiteral("app_version"), QCoreApplication::applicationVersion()},
        {QStringLiteral("system_summary"), currentSystemSummary()},
        {QStringLiteral("project_name"), currentProjectName()},
        {QStringLiteral("project_path"), currentProjectPath()}
    };

    if (!m_session.clientId.isEmpty() && !m_session.clientToken.isEmpty()) {
        payload.insert(QStringLiteral("client_id"), m_session.clientId);
        payload.insert(QStringLiteral("client_token"), m_session.clientToken);
    }
    return payload;
}

QString FeedbackService::currentSystemSummary() const
{
    return QStringLiteral("%1 · %2").arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture());
}

QString FeedbackService::currentProjectName() const
{
    return m_projectService && m_projectService->hasOpenProject()
        ? m_projectService->currentProject().name
        : QString();
}

QString FeedbackService::currentProjectPath() const
{
    return m_projectService && m_projectService->hasOpenProject()
        ? m_projectService->currentProject().rootPath
        : QString();
}

void FeedbackService::loadStoredSession()
{
    const auto json = m_settings ? m_settings->feedbackSessionJson().trimmed() : QString();
    if (json.isEmpty()) {
        return;
    }

    const auto document = QJsonDocument::fromJson(json.toUtf8());
    if (!document.isObject()) {
        return;
    }

    const auto object = document.object();
    m_session.nickname = jsonString(object, QStringLiteral("nickname"));
    m_session.contact = jsonString(object, QStringLiteral("contact"));
    m_session.conversationId = jsonString(object, QStringLiteral("conversation_id"));
    m_session.clientId = jsonString(object, QStringLiteral("client_id"));
    m_session.clientToken = jsonString(object, QStringLiteral("client_token"));
    m_session.lastReadMessageId = jsonInt64(object, QStringLiteral("last_read_message_id"));
}

void FeedbackService::persistStoredSession() const
{
    if (!m_settings) {
        return;
    }

    QJsonObject object{
        {QStringLiteral("nickname"), m_session.nickname},
        {QStringLiteral("contact"), m_session.contact},
        {QStringLiteral("conversation_id"), m_session.conversationId},
        {QStringLiteral("client_id"), m_session.clientId},
        {QStringLiteral("client_token"), m_session.clientToken},
        {QStringLiteral("last_read_message_id"), static_cast<double>(m_session.lastReadMessageId)}
    };
    m_settings->setFeedbackSessionJson(QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)));
}

void FeedbackService::setStatusMessage(const QString &message)
{
    if (m_statusMessage == message) {
        return;
    }
    m_statusMessage = message;
    emit stateChanged();
}

void FeedbackService::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    emit stateChanged();
}

void FeedbackService::setSending(bool sending)
{
    if (m_sending == sending) {
        return;
    }
    m_sending = sending;
    emit stateChanged();
}

void FeedbackService::setUnreadCount(int count)
{
    const auto normalized = qMax(0, count);
    if (m_unreadCount == normalized) {
        return;
    }
    m_unreadCount = normalized;
    emit unreadCountChanged();
    emit stateChanged();
}

void FeedbackService::applyConversationPayload(const QJsonObject &object)
{
    auto next = parseConversation(object);
    if (next.conversationId.isEmpty()) {
        return;
    }

    if (next.clientId.isEmpty()) {
        next.clientId = m_conversation.clientId.isEmpty() ? m_session.clientId : m_conversation.clientId;
    }
    if (next.clientToken.isEmpty()) {
        next.clientToken = m_conversation.clientToken.isEmpty() ? m_session.clientToken : m_conversation.clientToken;
    }
    if (next.clientWsUrl.isEmpty()) {
        next.clientWsUrl = m_conversation.clientWsUrl;
    }
    if (next.nickname.isEmpty()) {
        next.nickname = m_session.nickname;
    }
    if (next.contact.isEmpty()) {
        next.contact = m_session.contact;
    }

    m_conversation = next;

    m_session.nickname = m_conversation.nickname.isEmpty() ? m_session.nickname : m_conversation.nickname;
    m_session.contact = m_conversation.contact.isEmpty() ? m_session.contact : m_conversation.contact;
    m_session.conversationId = m_conversation.conversationId;
    m_session.clientId = m_conversation.clientId.isEmpty() ? m_session.clientId : m_conversation.clientId;
    m_session.clientToken = m_conversation.clientToken.isEmpty() ? m_session.clientToken : m_conversation.clientToken;
    persistStoredSession();
}

void FeedbackService::applyMessagesPayload(const QJsonArray &array)
{
    QVector<FeedbackMessage> nextMessages;
    nextMessages.reserve(array.size());
    for (const auto &value : array) {
        if (!value.isObject()) {
            continue;
        }
        nextMessages.append(parseMessage(value.toObject()));
    }
    std::sort(nextMessages.begin(), nextMessages.end(), [](const FeedbackMessage &left, const FeedbackMessage &right) {
        return left.id < right.id;
    });
    m_messages = nextMessages;
}

void FeedbackService::recomputeUnreadCount()
{
    int count = 0;
    for (const auto &message : m_messages) {
        if (message.senderRole == QStringLiteral("admin") && message.id > m_session.lastReadMessageId) {
            ++count;
        }
    }
    setUnreadCount(count);
}

void FeedbackService::markMessagesRead()
{
    const auto latestId = latestAdminMessageId(m_messages);
    if (latestId <= m_session.lastReadMessageId) {
        setUnreadCount(0);
        return;
    }
    m_session.lastReadMessageId = latestId;
    persistStoredSession();
    setUnreadCount(0);
}

void FeedbackService::ensureRealtimeConnected()
{
    if (!ready()) {
        return;
    }
    if (m_socketConnected) {
        return;
    }
    openRealtime();
}

void FeedbackService::disconnectRealtime()
{
    if (m_reconnectTimer.isActive()) {
        m_reconnectTimer.stop();
    }
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->close();
    }
}

void FeedbackService::scheduleReconnect()
{
    if (m_reconnectTimer.isActive()) {
        return;
    }
    m_reconnectTimer.start(4000);
}

void FeedbackService::handleNetworkFailure(QNetworkReply *reply, const QString &fallbackMessage)
{
    const auto statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (hasProfile() && hasClientAuth() && (statusCode == 401 || statusCode == 404)) {
        resetConversationState(QStringLiteral("当前反馈会话已失效，正在重新建立会话..."), true);
        return;
    }
    setStatusMessage(replyErrorMessage(reply, fallbackMessage));
    scheduleReconnect();
}

bool FeedbackService::hasClientAuth() const
{
    return !m_session.clientId.isEmpty()
        && !m_session.clientToken.isEmpty()
        && !m_conversation.conversationId.isEmpty();
}

bool FeedbackService::hasMessage(qint64 messageId) const
{
    for (const auto &message : m_messages) {
        if (message.id == messageId) {
            return true;
        }
    }
    return false;
}

void FeedbackService::transferAttachmentToPath(const QUrl &sourceUrl,
                                               const QString &targetPath,
                                               const QString &displayName,
                                               const std::function<void(bool, const QString &)> &callback)
{
    const auto normalizedTargetPath = QDir::cleanPath(targetPath.trimmed());
    if (!sourceUrl.isValid() || normalizedTargetPath.isEmpty()) {
        if (callback) {
            callback(false, QStringLiteral("附件保存失败：缺少可用的文件地址或保存路径。"));
        }
        return;
    }

    const QFileInfo targetInfo(normalizedTargetPath);
    const auto targetDirectory = targetInfo.absolutePath();
    if (!QDir().mkpath(targetDirectory)) {
        if (callback) {
            callback(false, QStringLiteral("附件保存失败：无法创建目录 %1").arg(targetDirectory));
        }
        return;
    }

    if (sourceUrl.isLocalFile()) {
        const auto sourcePath = QDir::cleanPath(sourceUrl.toLocalFile());
        const QFileInfo sourceInfo(sourcePath);
        if (sourcePath.isEmpty() || !sourceInfo.exists() || !sourceInfo.isFile()) {
            if (callback) {
                callback(false, QStringLiteral("附件保存失败：源文件不存在。"));
            }
            return;
        }

        if (QFileInfo(normalizedTargetPath).absoluteFilePath() == sourceInfo.absoluteFilePath()) {
            if (callback) {
                callback(true, {});
            }
            return;
        }

        QFile::remove(normalizedTargetPath);
        if (!QFile::copy(sourceInfo.absoluteFilePath(), normalizedTargetPath)) {
            if (callback) {
                callback(false, QStringLiteral("附件保存失败：无法写入 %1").arg(normalizedTargetPath));
            }
            return;
        }

        if (callback) {
            callback(true, {});
        }
        return;
    }

    auto request = QNetworkRequest(sourceUrl);
    request.setTransferTimeout(kTransferTimeoutMs);

    auto *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, callback, normalizedTargetPath, displayName]() {
        const auto cleanup = qScopeGuard([reply]() { reply->deleteLater(); });

        if (reply->error() != QNetworkReply::NoError
            || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() >= 400) {
            const auto fallbackName = fallbackAttachmentName(displayName, reply->url());
            const auto detail = replyErrorMessage(reply, QStringLiteral("附件下载失败：%1").arg(fallbackName));
            if (callback) {
                callback(false, detail);
            }
            return;
        }

        QFile::remove(normalizedTargetPath);
        QFile outputFile(normalizedTargetPath);
        if (!outputFile.open(QIODevice::WriteOnly)) {
            if (callback) {
                callback(false, QStringLiteral("附件保存失败：无法写入 %1").arg(normalizedTargetPath));
            }
            return;
        }

        outputFile.write(reply->readAll());
        outputFile.close();
        if (callback) {
            callback(true, {});
        }
    });
}

void FeedbackService::appendOrReplaceMessage(const FeedbackMessage &message)
{
    if (message.id <= 0) {
        return;
    }
    for (auto &existing : m_messages) {
        if (existing.id == message.id) {
            existing = message;
            return;
        }
    }
    m_messages.append(message);
    std::sort(m_messages.begin(), m_messages.end(), [](const FeedbackMessage &left, const FeedbackMessage &right) {
        return left.id < right.id;
    });
}

void FeedbackService::removeMessages(const QList<qint64> &messageIds)
{
    if (messageIds.isEmpty()) {
        return;
    }

    QSet<qint64> deletedIds(messageIds.begin(), messageIds.end());
    QVector<FeedbackMessage> nextMessages;
    nextMessages.reserve(m_messages.size());
    for (const auto &message : m_messages) {
        if (!deletedIds.contains(message.id)) {
            nextMessages.append(message);
        }
    }
    m_messages = nextMessages;
}

void FeedbackService::resetConversationState(const QString &statusMessage, bool recreateSession)
{
    disconnectRealtime();
    m_socketConnected = false;
    m_conversation = FeedbackConversation{};
    m_messages.clear();
    m_session.conversationId.clear();
    m_session.clientId.clear();
    m_session.clientToken.clear();
    m_session.lastReadMessageId = 0;
    m_restoreAttempted = false;
    persistStoredSession();
    setUnreadCount(0);
    setStatusMessage(statusMessage);
    emit messagesChanged();
    emit stateChanged();

    if (recreateSession && hasProfile()) {
        QTimer::singleShot(0, this, &FeedbackService::activate);
    }
}

FeedbackAttachment FeedbackService::parseAttachment(const QJsonObject &object) const
{
    FeedbackAttachment attachment;
    attachment.id = jsonString(object, QStringLiteral("id"));
    attachment.name = jsonString(object, QStringLiteral("name"));
    attachment.mimeType = jsonString(object, QStringLiteral("mime_type"));
    attachment.url = jsonString(object, QStringLiteral("url"));
    attachment.sizeBytes = jsonInt64(object, QStringLiteral("size_bytes"));
    return attachment;
}

FeedbackMessage FeedbackService::parseMessage(const QJsonObject &object) const
{
    FeedbackMessage message;
    message.id = jsonInt64(object, QStringLiteral("id"));
    message.conversationId = jsonString(object, QStringLiteral("conversation_id"));
    message.senderRole = jsonString(object, QStringLiteral("sender_role"));
    message.text = object.value(QStringLiteral("text")).toString();
    message.createdAt = jsonString(object, QStringLiteral("created_at"));
    const auto attachments = object.value(QStringLiteral("attachments")).toArray();
    for (const auto &value : attachments) {
        if (value.isObject()) {
            message.attachments.append(parseAttachment(value.toObject()));
        }
    }
    return message;
}

FeedbackConversation FeedbackService::parseConversation(const QJsonObject &object) const
{
    FeedbackConversation conversation;
    conversation.conversationId = jsonString(object, QStringLiteral("conversation_id"));
    conversation.clientId = jsonString(object, QStringLiteral("client_id"));
    conversation.clientToken = jsonString(object, QStringLiteral("client_token"));
    conversation.clientWsUrl = jsonString(object, QStringLiteral("client_ws_url"));
    conversation.nickname = jsonString(object, QStringLiteral("nickname"));
    conversation.contact = jsonString(object, QStringLiteral("contact"));
    conversation.status = jsonString(object, QStringLiteral("status"));
    conversation.appVersion = jsonString(object, QStringLiteral("app_version"));
    conversation.systemSummary = jsonString(object, QStringLiteral("system_summary"));
    conversation.projectName = jsonString(object, QStringLiteral("project_name"));
    conversation.projectPath = jsonString(object, QStringLiteral("project_path"));
    conversation.latestPreview = jsonString(object, QStringLiteral("latest_preview"));
    conversation.latestMessageAt = jsonString(object, QStringLiteral("latest_message_at"));
    conversation.createdAt = jsonString(object, QStringLiteral("created_at"));
    conversation.updatedAt = jsonString(object, QStringLiteral("updated_at"));
    conversation.unreadAdmin = jsonInt(object, QStringLiteral("unread_admin"));
    conversation.unreadClient = jsonInt(object, QStringLiteral("unread_client"));
    return conversation;
}
