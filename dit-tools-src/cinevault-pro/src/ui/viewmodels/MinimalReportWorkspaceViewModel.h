#pragma once

#include <QObject>
#include <QUrl>

class MinimalReportWorkspaceViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString projectName READ projectName CONSTANT)
    Q_PROPERTY(QString projectPath READ projectPath CONSTANT)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(QString lastExportPath READ lastExportPath CONSTANT)
    Q_PROPERTY(QString defaultShootTime READ defaultShootTime CONSTANT)
    Q_PROPERTY(qint64 selectedSourceId READ selectedSourceId NOTIFY stateChanged)
    Q_PROPERTY(bool hasSelectedSource READ hasSelectedSource NOTIFY stateChanged)
    Q_PROPERTY(QString scopeText READ scopeText NOTIFY stateChanged)
    Q_PROPERTY(QUrl previewPageUrl READ previewPageUrl CONSTANT)
    Q_PROPERTY(int previewPageIndex READ previewPageIndex CONSTANT)
    Q_PROPERTY(int previewPageCount READ previewPageCount CONSTANT)
    Q_PROPERTY(qreal previewZoom READ previewZoom CONSTANT)
    Q_PROPERTY(bool hasPreview READ hasPreview CONSTANT)
    Q_PROPERTY(bool canPreview READ canPreview CONSTANT)
    Q_PROPERTY(bool canExport READ canExport CONSTANT)
    Q_PROPERTY(bool isExporting READ isExporting CONSTANT)
    Q_PROPERTY(bool isPreviewing READ isPreviewing CONSTANT)

public:
    explicit MinimalReportWorkspaceViewModel(QObject *parent = nullptr);

    QString projectName() const;
    QString projectPath() const;
    QString statusText() const;
    QString lastExportPath() const;
    QString defaultShootTime() const;
    qint64 selectedSourceId() const;
    bool hasSelectedSource() const;
    QString scopeText() const;
    QUrl previewPageUrl() const;
    int previewPageIndex() const;
    int previewPageCount() const;
    qreal previewZoom() const;
    bool hasPreview() const;
    bool canPreview() const;
    bool canExport() const;
    bool isExporting() const;
    bool isPreviewing() const;
    void setSelectedSource(qint64 sourceRootId);

    Q_INVOKABLE void exportPdf(const QString &shootTime,
                               const QString &location,
                               const QString &director,
                               const QString &cinematographer,
                               const QString &ditName);
    Q_INVOKABLE void refreshPreview(const QString &shootTime,
                                    const QString &location,
                                    const QString &director,
                                    const QString &cinematographer,
                                    const QString &ditName);
    Q_INVOKABLE void nextPreviewPage();
    Q_INVOKABLE void previousPreviewPage();
    Q_INVOKABLE void zoomPreviewIn();
    Q_INVOKABLE void zoomPreviewOut();
    Q_INVOKABLE void resetPreviewZoom();
    Q_INVOKABLE void openLastExportFolder();

signals:
    void stateChanged();

private:
    QString m_statusText;
    qint64 m_selectedSourceId = 0;
};
