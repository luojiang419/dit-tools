#include "core/thumbnail/RawFormatRegistry.h"

#include <QFileInfo>

#include <cstring>
#include <utility>

namespace {

struct RawFormatSeed {
    const char *extension;
    RawFormatFamily family;
};

constexpr RawFormatSeed RawFormatSeeds[] = {
    {"3fr", RawFormatFamily::Hasselblad},
    {"ari", RawFormatFamily::Arri},
    {"arw", RawFormatFamily::Sony},
    {"bay", RawFormatFamily::Casio},
    {"bmq", RawFormatFamily::NuCore},
    {"cap", RawFormatFamily::PhaseOne},
    {"cine", RawFormatFamily::VisionResearch},
    {"cr2", RawFormatFamily::Canon},
    {"cr3", RawFormatFamily::Canon},
    {"crw", RawFormatFamily::Canon},
    {"cs1", RawFormatFamily::Sinar},
    {"dc2", RawFormatFamily::Kodak},
    {"dcr", RawFormatFamily::Kodak},
    {"dng", RawFormatFamily::AdobeDng},
    {"erf", RawFormatFamily::Epson},
    {"fff", RawFormatFamily::Hasselblad},
    {"gpr", RawFormatFamily::GoPro},
    {"ia", RawFormatFamily::Sinar},
    {"iiq", RawFormatFamily::PhaseOne},
    {"kc2", RawFormatFamily::Kodak},
    {"kdc", RawFormatFamily::Kodak},
    {"mdc", RawFormatFamily::Minolta},
    {"mef", RawFormatFamily::Mamiya},
    {"mos", RawFormatFamily::Leaf},
    {"mrw", RawFormatFamily::Minolta},
    {"nef", RawFormatFamily::Nikon},
    {"nrw", RawFormatFamily::Nikon},
    {"orf", RawFormatFamily::Olympus},
    {"pef", RawFormatFamily::Pentax},
    {"ptx", RawFormatFamily::Pentax},
    {"pxn", RawFormatFamily::Logitech},
    {"qtk", RawFormatFamily::AppleQuickTake},
    {"raf", RawFormatFamily::Fujifilm},
    {"raw", RawFormatFamily::Generic},
    {"rdc", RawFormatFamily::Ricoh},
    {"rw2", RawFormatFamily::Panasonic},
    {"rwl", RawFormatFamily::Leica},
    {"sr2", RawFormatFamily::Sony},
    {"srf", RawFormatFamily::Sony},
    {"srw", RawFormatFamily::Samsung},
    {"sti", RawFormatFamily::Sinar},
    {"x3f", RawFormatFamily::Sigma},
};

const QVector<RawPreviewProvider> &defaultProviderCandidates()
{
    static const QVector<RawPreviewProvider> candidates = {
        RawPreviewProvider::LibRawEmbeddedPreview,
        RawPreviewProvider::LibRawRenderedImage,
        RawPreviewProvider::ExifToolEmbeddedPreview,
        RawPreviewProvider::WindowsImagingComponent,
        RawPreviewProvider::FFmpeg,
        RawPreviewProvider::Placeholder,
    };
    return candidates;
}

bool containsBytes(QByteArrayView bytes, qsizetype offset, const char *expected, qsizetype expectedSize)
{
    return offset >= 0
        && expectedSize >= 0
        && offset <= bytes.size()
        && expectedSize <= bytes.size() - offset
        && std::memcmp(bytes.data() + offset, expected, static_cast<size_t>(expectedSize)) == 0;
}

quint16 readUInt16(QByteArrayView bytes, qsizetype offset, bool littleEndian, bool *ok)
{
    if (offset < 0 || offset > bytes.size() - 2) {
        *ok = false;
        return 0;
    }
    const auto first = static_cast<quint8>(bytes[offset]);
    const auto second = static_cast<quint8>(bytes[offset + 1]);
    return littleEndian
        ? static_cast<quint16>(first | (second << 8))
        : static_cast<quint16>((first << 8) | second);
}

quint32 readUInt32(QByteArrayView bytes, qsizetype offset, bool littleEndian, bool *ok)
{
    if (offset < 0 || offset > bytes.size() - 4) {
        *ok = false;
        return 0;
    }
    const auto first = static_cast<quint8>(bytes[offset]);
    const auto second = static_cast<quint8>(bytes[offset + 1]);
    const auto third = static_cast<quint8>(bytes[offset + 2]);
    const auto fourth = static_cast<quint8>(bytes[offset + 3]);
    return littleEndian
        ? static_cast<quint32>(first | (second << 8) | (third << 16) | (fourth << 24))
        : static_cast<quint32>((first << 24) | (second << 16) | (third << 8) | fourth);
}

bool hasDngVersionTag(QByteArrayView header)
{
    const bool littleEndian = containsBytes(header, 0, "II\x2a\x00", 4);
    const bool bigEndian = containsBytes(header, 0, "MM\x00\x2a", 4);
    if (!littleEndian && !bigEndian) {
        return false;
    }

    bool ok = true;
    const auto ifdOffset = readUInt32(header, 4, littleEndian, &ok);
    if (!ok || ifdOffset > static_cast<quint64>(header.size() - 2)) {
        return false;
    }
    const auto entryCount = readUInt16(header, static_cast<qsizetype>(ifdOffset), littleEndian, &ok);
    if (!ok) {
        return false;
    }

    constexpr quint16 DngVersionTag = 0xc612;
    constexpr qsizetype IfdEntrySize = 12;
    const auto entriesOffset = static_cast<qsizetype>(ifdOffset) + 2;
    const auto availableEntries = (header.size() - entriesOffset) / IfdEntrySize;
    const auto entriesToInspect = qMin<qsizetype>(entryCount, availableEntries);
    for (qsizetype index = 0; index < entriesToInspect; ++index) {
        if (readUInt16(header, entriesOffset + (index * IfdEntrySize), littleEndian, &ok) == DngVersionTag) {
            return ok;
        }
        if (!ok) {
            return false;
        }
    }
    return false;
}

bool isCr3Brand(const char *brand)
{
    return std::memcmp(brand, "crx ", 4) == 0;
}

bool hasCr3IsoBmffBrand(QByteArrayView header)
{
    qsizetype offset = 0;
    while (offset <= header.size() - 16) {
        bool ok = true;
        const auto boxSize = readUInt32(header, offset, false, &ok);
        if (!ok || boxSize < 8 || boxSize > static_cast<quint64>(header.size() - offset)) {
            return false;
        }
        if (containsBytes(header, offset + 4, "ftyp", 4)) {
            if (isCr3Brand(header.data() + offset + 8)) {
                return true;
            }
            for (qsizetype brandOffset = offset + 16;
                 brandOffset <= offset + static_cast<qsizetype>(boxSize) - 4;
                 brandOffset += 4) {
                if (isCr3Brand(header.data() + brandOffset)) {
                    return true;
                }
            }
            return false;
        }
        offset += static_cast<qsizetype>(boxSize);
    }
    return false;
}

} // namespace

const QVector<RawFormatDescriptor> &RawFormatRegistry::formats()
{
    static const QVector<RawFormatDescriptor> registry = [] {
        QVector<RawFormatDescriptor> result;
        result.reserve(std::size(RawFormatSeeds));
        for (const auto &seed : RawFormatSeeds) {
            auto providerCandidates = defaultProviderCandidates();
            if (seed.family == RawFormatFamily::GoPro) {
                providerCandidates.prepend(RawPreviewProvider::GoProGprSdk);
            }
            result.append({QString::fromLatin1(seed.extension),
                           seed.family,
                           true,
                           std::move(providerCandidates)});
        }
        return result;
    }();
    return registry;
}

const RawFormatDescriptor *RawFormatRegistry::findByExtension(const QString &extension)
{
    auto normalized = extension.trimmed();
    while (normalized.startsWith(QLatin1Char('.'))) {
        normalized.removeFirst();
    }
    normalized = normalized.toLower();
    for (const auto &format : formats()) {
        if (format.extension == normalized) {
            return &format;
        }
    }
    return nullptr;
}

const RawFormatDescriptor *RawFormatRegistry::findByFileName(const QString &fileName)
{
    return findByExtension(QFileInfo(fileName).suffix());
}

const RawFormatDescriptor *RawFormatRegistry::findBySignature(QByteArrayView header)
{
    if (containsBytes(header, 0, "FUJIFILMCCD-RAW", 16)) {
        return findByExtension(QStringLiteral("raf"));
    }
    if (containsBytes(header, 0, "FOVb", 4)) {
        return findByExtension(QStringLiteral("x3f"));
    }
    if (containsBytes(header, 0, "II\x2a\x00", 4)
        && containsBytes(header, 8, "CR\x02\x00", 4)) {
        return findByExtension(QStringLiteral("cr2"));
    }
    if (hasDngVersionTag(header)) {
        return findByExtension(QStringLiteral("dng"));
    }
    if (hasCr3IsoBmffBrand(header)) {
        return findByExtension(QStringLiteral("cr3"));
    }
    return nullptr;
}

const RawFormatDescriptor *RawFormatRegistry::find(const QString &fileName, QByteArrayView header)
{
    if (const auto *format = findByFileName(fileName)) {
        return format;
    }
    return header.isEmpty() ? nullptr : findBySignature(header);
}

bool RawFormatRegistry::isRawExtension(const QString &extension)
{
    return findByExtension(extension) != nullptr;
}

bool RawFormatRegistry::isRawFileName(const QString &fileName)
{
    return findByFileName(fileName) != nullptr;
}
