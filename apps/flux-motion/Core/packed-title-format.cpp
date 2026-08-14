#include "packed-title-format.h"

#include <QCryptographicHash>
#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>

#include <lz4.h>

#include <algorithm>
#include <iterator>
#include <limits>

namespace fxm::packed_title {
namespace {

constexpr char kMagic[8] = {'F', 'X', 'M', 'P', 'A', 'C', 'K', '1'};
constexpr quint32 kContainerVersion = 1;
constexpr quint32 kHeaderSize = 64;
constexpr qint64 kBlockSize = 1024 * 1024;
constexpr qint64 kMaxManifestBytes = 16 * 1024 * 1024;
constexpr qint64 kMaxTitleJsonBytes = 512LL * 1024 * 1024;
constexpr qint64 kMaxEntryCount = 4096;
constexpr quint64 kMaxTotalExtractedBytes = 256ULL * 1024 * 1024 * 1024;
constexpr quint32 kStoredBlockFlag = 0x80000000u;

struct WrittenEntry {
    QString archive_path;
    QString kind;
    QJsonObject metadata;
    quint64 offset = 0;
    quint64 stored_size = 0;
    quint64 raw_size = 0;
    QByteArray sha256;
};

static void set_error(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

static bool safe_archive_path(const QString &path)
{
    if (path.isEmpty() || path.size() > 1024 || path.startsWith('/') ||
        path.startsWith('\\') || path.contains(QStringLiteral("\\")) ||
        path.contains(':') || path.contains(QChar::Null))
        return false;
    const QStringList parts = path.split('/', Qt::KeepEmptyParts);
    return std::all_of(parts.begin(), parts.end(), [](const QString &part) {
        return !part.isEmpty() && part != QStringLiteral(".") &&
               part != QStringLiteral("..");
    });
}

static bool write_block(QIODevice &destination, const char *data, int size,
                        quint64 *stored_total, QString *error)
{
    QByteArray compressed(LZ4_compressBound(size), Qt::Uninitialized);
    const int compressed_size = LZ4_compress_default(
        data, compressed.data(), size, compressed.size());
    const bool store_raw = compressed_size <= 0 || compressed_size >= size;
    const quint32 stored_size = static_cast<quint32>(
        store_raw ? size : compressed_size);
    const quint32 encoded_size = stored_size |
        (store_raw ? kStoredBlockFlag : 0u);

    QDataStream stream(&destination);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << static_cast<quint32>(size) << encoded_size;
    if (stream.status() != QDataStream::Ok) {
        set_error(error, QStringLiteral("Could not write a packed block header."));
        return false;
    }
    const char *payload = store_raw ? data : compressed.constData();
    if (destination.write(payload, stored_size) != stored_size) {
        set_error(error, QStringLiteral("Could not write packed resource data."));
        return false;
    }
    *stored_total += 8u + stored_size;
    return true;
}

static bool write_source(QSaveFile &destination, const SourceEntry &source,
                         WrittenEntry *written, QString *error)
{
    if (!safe_archive_path(source.archive_path)) {
        set_error(error, QStringLiteral("Unsafe packed resource path: %1")
                             .arg(source.archive_path));
        return false;
    }

    QFile input;
    QBuffer memory;
    QIODevice *device = nullptr;
    if (!source.source_path.isEmpty()) {
        input.setFileName(source.source_path);
        if (!input.open(QIODevice::ReadOnly)) {
            set_error(error, QStringLiteral("Could not open resource for packing: %1")
                                 .arg(source.source_path));
            return false;
        }
        device = &input;
    } else {
        memory.setData(source.data);
        if (!memory.open(QIODevice::ReadOnly)) {
            set_error(error, QStringLiteral("Could not read an in-memory packed entry."));
            return false;
        }
        device = &memory;
    }

    written->archive_path = source.archive_path;
    written->kind = source.kind;
    written->metadata = source.metadata;
    written->offset = static_cast<quint64>(destination.pos());
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray block(static_cast<int>(kBlockSize), Qt::Uninitialized);
    for (;;) {
        const qint64 count = device->read(block.data(), block.size());
        if (count < 0) {
            set_error(error, QStringLiteral("Failed while reading resource: %1")
                                 .arg(source.source_path));
            return false;
        }
        if (count == 0)
            break;
        hash.addData(block.constData(), count);
        written->raw_size += static_cast<quint64>(count);
        if (!write_block(destination, block.constData(), static_cast<int>(count),
                         &written->stored_size, error))
            return false;
    }
    written->sha256 = hash.result();
    return true;
}

static bool read_exact(QIODevice &device, char *data, qint64 size)
{
    qint64 done = 0;
    while (done < size) {
        const qint64 count = device.read(data + done, size - done);
        if (count <= 0)
            return false;
        done += count;
    }
    return true;
}

static bool json_u64(const QJsonObject &object, const char *key, quint64 *value)
{
    const QJsonValue item = object.value(QString::fromLatin1(key));
    if (!item.isString())
        return false;
    bool ok = false;
    const quint64 parsed = item.toString().toULongLong(&ok, 10);
    if (!ok)
        return false;
    *value = parsed;
    return true;
}

static bool read_entry(QFile &source, const QJsonObject &entry,
                       QIODevice &destination, qint64 maximum_size,
                       quint64 data_end,
                       QString *error)
{
    quint64 offset = 0;
    quint64 stored_size = 0;
    quint64 raw_size = 0;
    const QByteArray expected_hash = QByteArray::fromHex(
        entry.value(QStringLiteral("sha256")).toString().toLatin1());
    if (!json_u64(entry, "offset", &offset) ||
        !json_u64(entry, "stored_size", &stored_size) ||
        !json_u64(entry, "size", &raw_size) ||
        raw_size > static_cast<quint64>(maximum_size) ||
        data_end > static_cast<quint64>(source.size()) ||
        offset < kHeaderSize || stored_size > data_end ||
        offset > data_end - stored_size ||
        expected_hash.size() != 32 || !source.seek(static_cast<qint64>(offset))) {
        set_error(error, QStringLiteral("Invalid packed resource bounds."));
        return false;
    }

    quint64 raw_done = 0;
    quint64 stored_done = 0;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (raw_done < raw_size) {
        if (stored_done + 8 > stored_size) {
            set_error(error, QStringLiteral("Truncated packed resource block."));
            return false;
        }
        QDataStream stream(&source);
        stream.setByteOrder(QDataStream::LittleEndian);
        quint32 block_raw = 0;
        quint32 encoded_stored = 0;
        stream >> block_raw >> encoded_stored;
        if (stream.status() != QDataStream::Ok) {
            set_error(error, QStringLiteral("Could not read a packed block header."));
            return false;
        }
        stored_done += 8;
        const bool raw_block = (encoded_stored & kStoredBlockFlag) != 0;
        const quint32 block_stored = encoded_stored & ~kStoredBlockFlag;
        if (block_raw == 0 || block_raw > static_cast<quint32>(kBlockSize) ||
            block_stored == 0 || block_stored > static_cast<quint32>(LZ4_compressBound(kBlockSize)) ||
            stored_done + block_stored > stored_size ||
            raw_done + block_raw > raw_size ||
            (raw_block && block_stored != block_raw)) {
            set_error(error, QStringLiteral("Invalid packed resource block."));
            return false;
        }
        QByteArray stored(static_cast<int>(block_stored), Qt::Uninitialized);
        if (!read_exact(source, stored.data(), stored.size())) {
            set_error(error, QStringLiteral("Truncated packed resource data."));
            return false;
        }
        stored_done += block_stored;
        QByteArray raw;
        if (raw_block) {
            raw = std::move(stored);
        } else {
            raw.resize(static_cast<int>(block_raw));
            const int decoded = LZ4_decompress_safe(
                stored.constData(), raw.data(), stored.size(), raw.size());
            if (decoded != raw.size()) {
                set_error(error, QStringLiteral("Packed resource decompression failed."));
                return false;
            }
        }
        hash.addData(raw);
        if (destination.write(raw) != raw.size()) {
            set_error(error, QStringLiteral("Could not extract a packed resource."));
            return false;
        }
        raw_done += block_raw;
    }
    if (stored_done != stored_size || hash.result() != expected_hash) {
        set_error(error, QStringLiteral("Packed resource integrity check failed."));
        return false;
    }
    return true;
}

} // namespace

bool has_packed_signature(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    char magic[sizeof(kMagic)] = {};
    return file.read(magic, sizeof(magic)) == sizeof(magic) &&
           std::equal(std::begin(magic), std::end(magic), std::begin(kMagic));
}

bool write(const QString &path, const QList<SourceEntry> &entries,
           const QJsonObject &manifest_metadata, QString *error)
{
    if (error)
        error->clear();
    if (entries.isEmpty() || entries.size() > kMaxEntryCount) {
        set_error(error, QStringLiteral("Invalid number of packed entries."));
        return false;
    }

    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        set_error(error, QStringLiteral("Could not open the packed title for atomic writing."));
        return false;
    }

    QByteArray empty_header(kHeaderSize, '\0');
    if (file.write(empty_header) != empty_header.size()) {
        set_error(error, QStringLiteral("Could not reserve the packed title header."));
        file.cancelWriting();
        return false;
    }

    QList<WrittenEntry> written_entries;
    written_entries.reserve(entries.size());
    quint64 total_raw_size = 0;
    for (const SourceEntry &entry : entries) {
        WrittenEntry written;
        if (!write_source(file, entry, &written, error)) {
            file.cancelWriting();
            return false;
        }
        if (written.raw_size > kMaxTotalExtractedBytes - total_raw_size) {
            set_error(error, QStringLiteral("Packed title resource limit exceeded."));
            file.cancelWriting();
            return false;
        }
        total_raw_size += written.raw_size;
        written_entries.push_back(std::move(written));
    }

    QJsonObject manifest = manifest_metadata;
    manifest.insert(QStringLiteral("format"), QStringLiteral("flux-motion-packed-title"));
    manifest.insert(QStringLiteral("version"), static_cast<int>(kContainerVersion));
    QJsonArray manifest_entries;
    for (const WrittenEntry &entry : written_entries) {
        QJsonObject item = entry.metadata;
        item.insert(QStringLiteral("path"), entry.archive_path);
        item.insert(QStringLiteral("kind"), entry.kind);
        /* Decimal strings preserve the full 64-bit range; QJson numbers are
         * IEEE doubles and would lose exact offsets in very large media packs. */
        item.insert(QStringLiteral("offset"), QString::number(entry.offset));
        item.insert(QStringLiteral("stored_size"), QString::number(entry.stored_size));
        item.insert(QStringLiteral("size"), QString::number(entry.raw_size));
        item.insert(QStringLiteral("sha256"), QString::fromLatin1(entry.sha256.toHex()));
        item.insert(QStringLiteral("compression"), QStringLiteral("lz4-blocks"));
        manifest_entries.push_back(item);
    }
    manifest.insert(QStringLiteral("entries"), manifest_entries);
    const QByteArray manifest_bytes = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    if (manifest_bytes.size() <= 0 || manifest_bytes.size() > kMaxManifestBytes) {
        set_error(error, QStringLiteral("Packed title manifest is too large."));
        file.cancelWriting();
        return false;
    }
    const quint64 manifest_offset = static_cast<quint64>(file.pos());
    if (file.write(manifest_bytes) != manifest_bytes.size()) {
        set_error(error, QStringLiteral("Could not write the packed title manifest."));
        file.cancelWriting();
        return false;
    }
    const QByteArray manifest_hash = QCryptographicHash::hash(
        manifest_bytes, QCryptographicHash::Sha256);

    if (!file.seek(0)) {
        set_error(error, QStringLiteral("Could not finalize the packed title header."));
        file.cancelWriting();
        return false;
    }
    QDataStream header(&file);
    header.setByteOrder(QDataStream::LittleEndian);
    header.writeRawData(kMagic, sizeof(kMagic));
    header << kContainerVersion << kHeaderSize << manifest_offset
           << static_cast<quint64>(manifest_bytes.size());
    header.writeRawData(manifest_hash.constData(), manifest_hash.size());
    if (header.status() != QDataStream::Ok || file.pos() != kHeaderSize || !file.commit()) {
        set_error(error, QStringLiteral("Failed while atomically finalizing the packed title."));
        return false;
    }
    return true;
}

bool read(const QString &path, const QString &extraction_base,
          ReadResult *result, QString *error)
{
    if (error)
        error->clear();
    if (!result) {
        set_error(error, QStringLiteral("No packed-title result was supplied."));
        return false;
    }
    *result = ReadResult{};

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        set_error(error, QStringLiteral("Could not open the packed title."));
        return false;
    }
    QDataStream header(&file);
    header.setByteOrder(QDataStream::LittleEndian);
    char magic[sizeof(kMagic)] = {};
    quint32 version = 0;
    quint32 header_size = 0;
    quint64 manifest_offset = 0;
    quint64 manifest_size = 0;
    QByteArray expected_manifest_hash(32, Qt::Uninitialized);
    if (header.readRawData(magic, sizeof(magic)) != sizeof(magic)) {
        set_error(error, QStringLiteral("Truncated packed title header."));
        return false;
    }
    header >> version >> header_size >> manifest_offset >> manifest_size;
    if (header.readRawData(expected_manifest_hash.data(), expected_manifest_hash.size()) !=
        expected_manifest_hash.size() || header.status() != QDataStream::Ok ||
        !std::equal(std::begin(magic), std::end(magic), std::begin(kMagic)) ||
        version != kContainerVersion || header_size != kHeaderSize ||
        manifest_size == 0 || manifest_size > static_cast<quint64>(kMaxManifestBytes) ||
        manifest_offset < kHeaderSize ||
        manifest_size > static_cast<quint64>(file.size()) ||
        manifest_offset > static_cast<quint64>(file.size()) - manifest_size ||
        !file.seek(static_cast<qint64>(manifest_offset))) {
        set_error(error, QStringLiteral("Unsupported or invalid packed title header."));
        return false;
    }
    QByteArray manifest_bytes(static_cast<int>(manifest_size), Qt::Uninitialized);
    if (!read_exact(file, manifest_bytes.data(), manifest_bytes.size()) ||
        QCryptographicHash::hash(manifest_bytes, QCryptographicHash::Sha256) !=
            expected_manifest_hash) {
        set_error(error, QStringLiteral("Packed title manifest integrity check failed."));
        return false;
    }
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(manifest_bytes, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        set_error(error, QStringLiteral("Invalid packed title manifest."));
        return false;
    }
    const QJsonObject manifest = document.object();
    const QJsonArray entries = manifest.value(QStringLiteral("entries")).toArray();
    const QString package_id = manifest.value(QStringLiteral("package_id")).toString();
    if (manifest.value(QStringLiteral("format")).toString() !=
            QStringLiteral("flux-motion-packed-title") ||
        manifest.value(QStringLiteral("version")).toInt() != static_cast<int>(kContainerVersion) ||
        entries.isEmpty() || entries.size() > kMaxEntryCount ||
        package_id.isEmpty() || package_id.size() > 128 ||
        package_id.contains('/') || package_id.contains('\\') ||
        package_id.contains(':') || package_id.contains(QChar::Null) ||
        package_id.contains(QStringLiteral(".."))) {
        set_error(error, QStringLiteral("Unsupported packed title manifest."));
        return false;
    }

    QSet<QString> preflight_paths;
    quint64 total_raw_size = 0;
    int title_entries = 0;
    for (const QJsonValue &value : entries) {
        if (!value.isObject()) {
            set_error(error, QStringLiteral("Invalid packed title entry."));
            return false;
        }
        const QJsonObject entry = value.toObject();
        const QString archive_path = entry.value(QStringLiteral("path")).toString();
        quint64 offset = 0;
        quint64 stored_size = 0;
        quint64 raw_size = 0;
        const QByteArray hash = QByteArray::fromHex(
            entry.value(QStringLiteral("sha256")).toString().toLatin1());
        if (!safe_archive_path(archive_path) || preflight_paths.contains(archive_path) ||
            entry.value(QStringLiteral("compression")).toString() != QStringLiteral("lz4-blocks") ||
            !json_u64(entry, "offset", &offset) ||
            !json_u64(entry, "stored_size", &stored_size) ||
            !json_u64(entry, "size", &raw_size) || hash.size() != 32 ||
            offset < kHeaderSize || stored_size > manifest_offset ||
            offset > manifest_offset - stored_size ||
            raw_size > kMaxTotalExtractedBytes - total_raw_size) {
            set_error(error, QStringLiteral("Invalid packed title entry metadata."));
            return false;
        }
        total_raw_size += raw_size;
        preflight_paths.insert(archive_path);
        if (archive_path == QStringLiteral("title.json"))
            ++title_entries;
    }
    if (title_entries != 1 || total_raw_size > kMaxTotalExtractedBytes) {
        set_error(error, QStringLiteral("Packed title resource limits were exceeded."));
        return false;
    }

    /* Include the authenticated manifest fingerprint in the directory name.
     * A different archive cannot reuse a package_id to replace resources
     * already referenced by an imported title. */
    const QString extraction_id = package_id + QLatin1Char('-') +
        QString::fromLatin1(expected_manifest_hash.toHex().left(16));
    const QString extraction_root = QDir(extraction_base).filePath(extraction_id);
    if (!QDir().mkpath(extraction_root)) {
        set_error(error, QStringLiteral("Could not create the packed resource directory."));
        return false;
    }

    bool found_title = false;
    QSet<QString> seen_paths;
    for (const QJsonValue &value : entries) {
        if (!value.isObject()) {
            set_error(error, QStringLiteral("Invalid packed title entry."));
            return false;
        }
        const QJsonObject entry = value.toObject();
        const QString archive_path = entry.value(QStringLiteral("path")).toString();
        const QString kind = entry.value(QStringLiteral("kind")).toString();
        if (!safe_archive_path(archive_path) || seen_paths.contains(archive_path) ||
            entry.value(QStringLiteral("compression")).toString() != QStringLiteral("lz4-blocks")) {
            set_error(error, QStringLiteral("Invalid packed title entry metadata."));
            return false;
        }
        seen_paths.insert(archive_path);
        if (archive_path == QStringLiteral("title.json")) {
            if (found_title) {
                set_error(error, QStringLiteral("Packed title contains multiple title documents."));
                return false;
            }
            QBuffer output(&result->title_json);
            output.open(QIODevice::WriteOnly);
            if (!read_entry(file, entry, output, kMaxTitleJsonBytes,
                            manifest_offset, error))
                return false;
            found_title = true;
            continue;
        }

        const QString output_path = QDir(extraction_root).filePath(archive_path);
        const QFileInfo output_info(output_path);
        if (!QDir().mkpath(output_info.absolutePath())) {
            set_error(error, QStringLiteral("Could not create a packed resource subdirectory."));
            return false;
        }
        QSaveFile output(output_path);
        output.setDirectWriteFallback(false);
        if (!output.open(QIODevice::WriteOnly) ||
            !read_entry(file, entry, output, std::numeric_limits<qint64>::max(),
                        manifest_offset, error) ||
            !output.commit()) {
            if (error && error->isEmpty())
                *error = QStringLiteral("Could not atomically extract a packed resource.");
            return false;
        }
        if (kind == QStringLiteral("font"))
            result->extracted_font_paths.push_back(output_path);
    }
    if (!found_title || result->title_json.isEmpty()) {
        set_error(error, QStringLiteral("Packed title does not contain title.json."));
        return false;
    }
    result->extraction_root = extraction_root;
    result->manifest = manifest;
    return true;
}

} // namespace fxm::packed_title
