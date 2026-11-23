#pragma once

#include <QObject>
#include <QVariantList>

class StorageVolumeService : public QObject {
    Q_OBJECT

public:
    explicit StorageVolumeService(QObject *parent = nullptr);

    QVariantList volumes() const;

public slots:
    void refresh();

signals:
    void volumesChanged();

private:
    QVariantList m_volumes;
};
