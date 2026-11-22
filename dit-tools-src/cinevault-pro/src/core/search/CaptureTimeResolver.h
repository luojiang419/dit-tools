#pragma once

#include <QString>

struct CaptureTimeInfo {
    QString captureTime;
    QString captureDate;
    QString source;
    double confidence = 0.0;
    bool fallback = false;

    [[nodiscard]] bool isValid() const { return !captureDate.trimmed().isEmpty(); }
};

class CaptureTimeResolver {
public:
    CaptureTimeInfo resolve(const QString &ffprobeJson,
                            const QString &folderDate,
                            const QString &modifiedAt) const;
};
