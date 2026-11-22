#include "core/scan/FileTypeService.h"

#include "core/thumbnail/RawFormatRegistry.h"

#include <QFileInfo>
#include <QSet>

AssetType FileTypeService::classify(const QString &fileName, QByteArrayView header)
{
    static const QSet<QString> videoExt = {QStringLiteral("mov"), QStringLiteral("mp4"), QStringLiteral("mxf"), QStringLiteral("avi"), QStringLiteral("mkv")};
    static const QSet<QString> audioExt = {QStringLiteral("wav"), QStringLiteral("mp3"), QStringLiteral("aac"), QStringLiteral("flac"), QStringLiteral("aif"), QStringLiteral("aiff")};
    static const QSet<QString> imageExt = {
        QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("jpe"),
        QStringLiteral("png"), QStringLiteral("apng"),
        QStringLiteral("webp"), QStringLiteral("gif"),
        QStringLiteral("tif"), QStringLiteral("tiff"),
        QStringLiteral("psd"), QStringLiteral("psb"),
        QStringLiteral("bmp"), QStringLiteral("dib"),
        QStringLiteral("heic"), QStringLiteral("heif"), QStringLiteral("avif"),
        QStringLiteral("jp2"), QStringLiteral("j2k"), QStringLiteral("jpf"), QStringLiteral("jpx"), QStringLiteral("jpm"),
        QStringLiteral("jxl"), QStringLiteral("exr"), QStringLiteral("hdr"), QStringLiteral("pic"),
        QStringLiteral("tga"), QStringLiteral("icb"), QStringLiteral("vda"), QStringLiteral("vst"),
        QStringLiteral("svg"), QStringLiteral("svgz"), QStringLiteral("ico")
    };
    static const QSet<QString> subtitleExt = {QStringLiteral("srt"), QStringLiteral("ass"), QStringLiteral("vtt")};
    static const QSet<QString> projectExt = {QStringLiteral("prproj"), QStringLiteral("drp"), QStringLiteral("aep"), QStringLiteral("fcpproj")};
    static const QSet<QString> documentExt = {
        QStringLiteral("pdf"), QStringLiteral("csv"), QStringLiteral("tsv"), QStringLiteral("json"),
        QStringLiteral("md"), QStringLiteral("txt"), QStringLiteral("doc"), QStringLiteral("docx"),
        QStringLiteral("xls"), QStringLiteral("xlsx"), QStringLiteral("ppt"), QStringLiteral("pptx")
    };
    static const QSet<QString> archiveExt = {QStringLiteral("zip"), QStringLiteral("rar"), QStringLiteral("7z"), QStringLiteral("tar"), QStringLiteral("gz")};

    if (RawFormatRegistry::find(fileName, header)) {
        return AssetType::Image;
    }

    const auto ext = QFileInfo(fileName).suffix().toLower();
    if (ext.isEmpty()) {
        return AssetType::Unknown;
    }
    if (videoExt.contains(ext)) return AssetType::Video;
    if (audioExt.contains(ext)) return AssetType::Audio;
    if (imageExt.contains(ext)) return AssetType::Image;
    if (subtitleExt.contains(ext)) return AssetType::Subtitle;
    if (projectExt.contains(ext)) return AssetType::ProjectFile;
    if (documentExt.contains(ext)) return AssetType::Document;
    if (archiveExt.contains(ext)) return AssetType::Archive;
    return AssetType::Other;
}
