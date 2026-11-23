#pragma once

#include <QQuickImageProvider>

class LocalImageProvider : public QQuickImageProvider {
public:
    LocalImageProvider();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
