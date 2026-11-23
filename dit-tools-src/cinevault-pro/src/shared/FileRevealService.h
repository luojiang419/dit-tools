#pragma once

#include <QString>

namespace FileRevealService {

bool revealFile(const QString &filePath, QString *errorMessage = nullptr);
bool openDirectory(const QString &directoryPath, QString *errorMessage = nullptr);

}
