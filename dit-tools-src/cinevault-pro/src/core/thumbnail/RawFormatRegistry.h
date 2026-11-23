#pragma once

#include <QByteArrayView>
#include <QString>
#include <QVector>

enum class RawFormatFamily : quint8 {
    Generic = 0,
    AdobeDng,
    AppleQuickTake,
    Arri,
    Canon,
    Casio,
    Epson,
    Fujifilm,
    GoPro,
    Hasselblad,
    Kodak,
    Leaf,
    Leica,
    Logitech,
    Mamiya,
    Minolta,
    Nikon,
    NuCore,
    Olympus,
    Panasonic,
    Pentax,
    PhaseOne,
    Ricoh,
    Samsung,
    Sigma,
    Sinar,
    Sony,
    VisionResearch
};

enum class RawPreviewProvider : quint8 {
    GoProGprSdk = 0,
    LibRawEmbeddedPreview,
    LibRawRenderedImage,
    ExifToolEmbeddedPreview,
    WindowsImagingComponent,
    FFmpeg,
    Placeholder
};

struct RawFormatDescriptor {
    QString extension;
    RawFormatFamily family = RawFormatFamily::Generic;
    bool preferEmbeddedPreview = true;
    QVector<RawPreviewProvider> providerCandidates;
};

class RawFormatRegistry final {
public:
    static const QVector<RawFormatDescriptor> &formats();
    static const RawFormatDescriptor *findByExtension(const QString &extension);
    static const RawFormatDescriptor *findByFileName(const QString &fileName);
    static const RawFormatDescriptor *findBySignature(QByteArrayView header);
    static const RawFormatDescriptor *find(const QString &fileName, QByteArrayView header = {});
    static bool isRawExtension(const QString &extension);
    static bool isRawFileName(const QString &fileName);
};
