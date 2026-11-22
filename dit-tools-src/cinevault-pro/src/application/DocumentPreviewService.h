#pragma once

#include <QObject>
#include <QUrl>

class DocumentPreviewService : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString title READ title NOTIFY stateChanged)
    Q_PROPERTY(QUrl sourceUrl READ sourceUrl NOTIFY stateChanged)
    Q_PROPERTY(QString content READ content NOTIFY stateChanged)
    Q_PROPERTY(bool isPdf READ isPdf NOTIFY stateChanged)
    Q_PROPERTY(bool isMarkdown READ isMarkdown NOTIFY stateChanged)
    Q_PROPERTY(bool isRichText READ isRichText NOTIFY stateChanged)
    Q_PROPERTY(bool truncated READ truncated NOTIFY stateChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged)
    Q_PROPERTY(bool hasError READ hasError NOTIFY stateChanged)
    Q_PROPERTY(bool hasContent READ hasContent NOTIFY stateChanged)

public:
    explicit DocumentPreviewService(QObject *parent = nullptr);

    QString title() const;
    QUrl sourceUrl() const;
    QString content() const;
    bool isPdf() const;
    bool isMarkdown() const;
    bool isRichText() const;
    bool truncated() const;
    QString errorMessage() const;
    bool hasError() const;
    bool hasContent() const;

    static QString extractTextForSummary(const QString &path, bool *truncated = nullptr, QString *errorMessage = nullptr);

    Q_INVOKABLE void loadFromFile(const QUrl &sourceUrl, const QString &title = QString());
    Q_INVOKABLE void clear();

signals:
    void stateChanged();

private:
    QString m_title;
    QUrl m_sourceUrl;
    QString m_content;
    bool m_isPdf = false;
    bool m_isMarkdown = false;
    bool m_isRichText = false;
    bool m_truncated = false;
    QString m_errorMessage;
};
