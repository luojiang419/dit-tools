#pragma once

#include "domain/Entities.h"

#include <functional>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QUrl>

class AppSettings;
class ProjectService;
class QNetworkAccessManager;
class QNetworkReply;
class QWebSocket;

class FeedbackService : public QObject {
    Q_OBJECT

public:
    using AttachmentCacheCallback = std::function<void(bool, const QString &, const QString &)>;

    explicit FeedbackService(AppSettings *settings, ProjectService *projectService, QObject *parent = nullptr);
    ~FeedbackService() override;

    bool hasProfile() const;
    bool ready() const;
    bool busy() const;
    bool sending() const;
    bool socketConnected() const;
    QString statusMessage() const;
    int unreadCount() const;
    FeedbackConversation conversation() const;
    QVector<FeedbackMessage> messages() const;
    void ensureAttachmentCached(const QString &attachmentId,
                                const QString &url,
                                const QString &name,
                                AttachmentCacheCallback callback);

    void setWorkspaceActive(bool active);

public slots:
    void activate();
    void createOrRestoreSession(const QString &nickname, const QString &contact);
    void refreshMessages();
    void sendMessage(const QString &text, const QStringList &filePaths);
    void downloadAttachment(const QString &url, const QString &targetPath, const QString &displayName = QString());
    void deleteMessage(qint64 messageId);
    void clearClientMessages();

signals:
    void stateChanged();
    void messagesChanged();
    void unreadCountChanged();
    void messageSubmitted(bool success);

private slots:
    void openRealtime();
    void handleRealtimeConnected();
    void handleRealtimeDisconnected();
    void handleRealtimeTextMessage(const QString &payload);

private:
    struct SessionCache {
        QString nickname;
        QString contact;
        QString conversationId;
        QString clientId;
        QString clientToken;
        qint64 lastReadMessageId = 0;
    };

    QUrl serviceBaseUrl() const;
    QUrl serviceUrl(const QString &relativePath) const;
    QJsonObject buildSessionPayload(const QString &nickname, const QString &contact) const;
    QString currentSystemSummary() const;
    QString currentProjectName() const;
    QString currentProjectPath() const;
    void loadStoredSession();
    void persistStoredSession() const;
    void setStatusMessage(const QString &message);
    void setBusy(bool busy);
    void setSending(bool sending);
    void setUnreadCount(int count);
    void applyConversationPayload(const QJsonObject &object);
    void applyMessagesPayload(const QJsonArray &array);
    void recomputeUnreadCount();
    void markMessagesRead();
    void ensureRealtimeConnected();
    void disconnectRealtime();
    void scheduleReconnect();
    void handleNetworkFailure(QNetworkReply *reply, const QString &fallbackMessage);
    bool hasClientAuth() const;
    bool hasMessage(qint64 messageId) const;
    void transferAttachmentToPath(const QUrl &sourceUrl,
                                  const QString &targetPath,
                                  const QString &displayName,
                                  const std::function<void(bool, const QString &)> &callback);
    void appendOrReplaceMessage(const FeedbackMessage &message);
    void removeMessages(const QList<qint64> &messageIds);
    void resetConversationState(const QString &statusMessage, bool recreateSession);
    FeedbackAttachment parseAttachment(const QJsonObject &object) const;
    FeedbackMessage parseMessage(const QJsonObject &object) const;
    FeedbackConversation parseConversation(const QJsonObject &object) const;

    AppSettings *m_settings = nullptr;
    ProjectService *m_projectService = nullptr;
    QNetworkAccessManager *m_network = nullptr;
    QWebSocket *m_socket = nullptr;
    QTimer m_reconnectTimer;
    SessionCache m_session;
    FeedbackConversation m_conversation;
    QVector<FeedbackMessage> m_messages;
    QString m_statusMessage;
    bool m_workspaceActive = false;
    bool m_busy = false;
    bool m_sending = false;
    bool m_socketConnected = false;
    bool m_restoreAttempted = false;
    int m_unreadCount = 0;
};
