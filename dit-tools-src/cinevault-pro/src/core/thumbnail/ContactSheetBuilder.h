#pragma once

#include <QString>
#include <QStringList>

class ContactSheetBuilder {
public:
    static bool build(const QStringList &imagePaths, int frameCount, const QString &outputPath, QString *errorMessage = nullptr);
};
