#pragma once

#include "domain/Enums.h"

#include <QByteArrayView>
#include <QString>

class FileTypeService {
public:
    static AssetType classify(const QString &fileName, QByteArrayView header = {});
};
