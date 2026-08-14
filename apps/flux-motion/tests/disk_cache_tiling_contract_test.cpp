#include <cassert>
#include <iostream>
#include <string>

#include "source_bundle_reader.h"

namespace {
void require(const std::string &source, const char *needle)
{
    if (source.find(needle) == std::string::npos) {
        std::cerr << "missing: " << needle << '\n';
        assert(false);
    }
}
}

int main(int argc, char **argv)
{
    assert(argc == 4);
    const std::string source = read_file(argv[1]);
    const std::string header = read_file(argv[2]);
    const std::string tile_header = read_file(argv[3]);

    require(source, "constexpr quint16 kRawFrameVersion = 5");
    require(source, "gpu-renderer-v31-lens-flare-dx11-keyword-fix");
    require(source, "QStringLiteral(\".ogst\")");
    require(source, "pathForTileDigest");
    require(source, "extract_nonempty_tiles");
    require(source, "frame_tile_digests_");
    require(source, "tile_ref_counts_");
    require(source, "addTileReferencesLocked");
    require(source, "releaseTileReferencesLocked");
    require(source, "rewriteManifestLocked");
    require(source, "cleanup_queue_.push_back(tombstone)");
    require(source, "QDir(cleanup_dir).removeRecursively()");

    require(header, "struct TileRef");
    require(header, "QHash<QString, QVector<QByteArray>> frame_tile_digests_");
    require(header, "QHash<QByteArray, qsizetype> tile_ref_counts_");

    require(tile_header, "inline constexpr int kTileSize = 256");
    require(tile_header, "digest_for_tile");
    require(tile_header, "extract_nonempty_tiles");
    require(tile_header, "compose_sparse_tiles");
    require(tile_header, "if (!has_visible_alpha(tile))");

    std::cout << "disk cache tiled payload and dedup contract passed\n";
    return 0;
}
