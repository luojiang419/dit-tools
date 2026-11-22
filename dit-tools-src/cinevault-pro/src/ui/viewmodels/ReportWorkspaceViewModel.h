#pragma once

#include <QObject>
#include <QStringList>
#include <QUrl>

class ProjectService;
class LibraryQueryService;
class ReportExportService;
class AppSettings;
struct ReportExportRequest;

class ReportWorkspaceViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString projectName READ projectName NOTIFY stateChanged)
    Q_PROPERTY(QString projectPath READ projectPath NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(QString lastExportPath READ lastExportPath NOTIFY stateChanged)
    Q_PROPERTY(QString defaultShootTime READ defaultShootTime NOTIFY stateChanged)
    Q_PROPERTY(qint64 selectedSourceId READ selectedSourceId NOTIFY stateChanged)
    Q_PROPERTY(bool hasSelectedSource READ hasSelectedSource NOTIFY stateChanged)
    Q_PROPERTY(QString scopeText READ scopeText NOTIFY stateChanged)
    Q_PROPERTY(QUrl previewPageUrl READ previewPageUrl NOTIFY stateChanged)
    Q_PROPERTY(int previewPageIndex READ previewPageIndex NOTIFY stateChanged)
    Q_PROPERTY(int previewPageCount READ previewPageCount NOTIFY stateChanged)
    Q_PROPERTY(qreal previewZoom READ previewZoom NOTIFY stateChanged)
    Q_PROPERTY(bool hasPreview READ hasPreview NOTIFY stateChanged)
    Q_PROPERTY(bool canPreview READ canPreview NOTIFY stateChanged)
    Q_PROPERTY(bool canExport READ canExport NOTIFY stateChanged)
    Q_PROPERTY(bool isExporting READ isExporting NOTIFY stateChanged)
    Q_PROPERTY(bool isPreviewing READ isPreviewing NOTIFY stateChanged)

public:
    explicit ReportWorkspaceViewModel(ProjectService *projectService,
                                      LibraryQueryService *libraryQueryService,
                                      ReportExportService *reportExportService,
                                      AppSettings *settings,
                                      QObject *parent = nullptr);

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

    void resetForProject();
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
    Q_INVOKABLE bool reportSectionEnabled(const QString &section) const;
    Q_INVOKABLE void setReportSectionEnabled(const QString &section, bool enabled);

signals:
    void stateChanged();

private:
    void clearPreview();
    void updateSelectedSourceDetails();
    ReportExportRequest buildRequest(const QString &shootTime,
                                     const QString &location,
                                     const QString &director,
                                     const QString &cinematographer,
                                     const QString &ditName) const;

    ProjectService *m_projectService = nullptr;
    LibraryQueryService *m_libraryQueryService = nullptr;
    ReportExportService *m_reportExportService = nullptr;
    AppSettings *m_settings = nullptr;
    QString m_statusText;
    QString m_lastExportPath;
    QStringList m_previewPagePaths;
    QString m_selectedSourceName;
    QString m_selectedSourcePath;
    qint64 m_selectedSourceId = 0;
    int m_previewPageIndex = 0;
    qreal m_previewZoom = 0.75;
    bool m_isExporting = false;
    bool m_isPreviewing = false;
};
