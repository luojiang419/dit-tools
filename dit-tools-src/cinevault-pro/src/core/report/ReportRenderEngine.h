#pragma once

#include "core/report/ReportModels.h"

#include <QString>
#include <QStringList>

class ReportRenderEngine {
public:
    bool renderPdf(const ReportDocument &document, const QString &outputPath, QString *errorMessage) const;
    bool renderPreviewImages(const ReportDocument &document, const QString &outputDirectory, QStringList *pagePaths, QString *errorMessage) const;
};
