#include "packed-title-format.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include <iostream>

static bool require(bool condition, const char *message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

int main()
{
    QTemporaryDir temporary;
    if (!require(temporary.isValid(), "temporary directory"))
        return 1;

    const QString resource_path = QDir(temporary.path()).filePath("source.bin");
    const QByteArray resource(3 * 1024 * 1024 + 37, 'A');
    QFile resource_file(resource_path);
    if (!require(resource_file.open(QIODevice::WriteOnly), "open source") ||
        !require(resource_file.write(resource) == resource.size(), "write source"))
        return 1;
    resource_file.close();

    QList<fxm::packed_title::SourceEntry> sources;
    fxm::packed_title::SourceEntry title;
    title.archive_path = "title.json";
    title.kind = "title";
    title.data = R"({"format":"flux-motion-title-template","title":{"name":"Packed test"}})";
    sources.push_back(title);
    fxm::packed_title::SourceEntry binary;
    binary.archive_path = "assets/fonts/test.bin";
    binary.kind = "font";
    binary.source_path = resource_path;
    binary.metadata.insert("family", "Test Family");
    sources.push_back(binary);

    QJsonObject metadata;
    metadata.insert("package_id", "packed-format-test");
    metadata.insert("packing", QJsonObject{{"images", true}, {"media", false}, {"fonts", true}});
    const QString packed_path = QDir(temporary.path()).filePath("test.fxmp");
    QString error;
    if (!require(fxm::packed_title::write(packed_path, sources, metadata, &error),
                 error.toUtf8().constData()) ||
        !require(fxm::packed_title::has_packed_signature(packed_path), "packed signature"))
        return 1;

    fxm::packed_title::ReadResult result;
    const QString extraction = QDir(temporary.path()).filePath("extracted");
    if (!require(fxm::packed_title::read(packed_path, extraction, &result, &error),
                 error.toUtf8().constData()) ||
        !require(result.title_json == title.data, "title.json round trip") ||
        !require(result.extracted_font_paths.size() == 1, "font classification"))
        return 1;
    QFile extracted(result.extracted_font_paths.front());
    if (!require(extracted.open(QIODevice::ReadOnly), "open extracted resource") ||
        !require(extracted.readAll() == resource, "resource round trip"))
        return 1;

    const QJsonArray entries = result.manifest.value("entries").toArray();
    const quint64 offset = entries.at(0).toObject().value("offset").toVariant().toULongLong();
    QFile tampered(packed_path);
    if (!require(tampered.open(QIODevice::ReadWrite), "open packed title for tamper test") ||
        !require(tampered.seek(static_cast<qint64>(offset + 8)), "seek packed payload"))
        return 1;
    char byte = 0;
    if (!require(tampered.read(&byte, 1) == 1, "read packed payload byte") ||
        !require(tampered.seek(static_cast<qint64>(offset + 8)), "rewind packed payload"))
        return 1;
    byte ^= 0x01;
    if (!require(tampered.write(&byte, 1) == 1, "tamper packed payload"))
        return 1;
    tampered.close();
    fxm::packed_title::ReadResult rejected;
    if (!require(!fxm::packed_title::read(packed_path, extraction, &rejected, &error),
                 "tampered entry rejected"))
        return 1;

    std::cout << "Packed title format test passed\n";
    return 0;
}
