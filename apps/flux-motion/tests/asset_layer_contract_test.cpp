#include "source_bundle_reader.h"

#include <iostream>
#include <string>

namespace {
bool require(const std::string &text, const std::string &needle, const char *label)
{
    if (text.find(needle) != std::string::npos)
        return true;
    std::cerr << "Missing Asset Layer contract: " << label
              << " (" << needle << ")\n";
    return false;
}
}

int main(int argc, char **argv)
{
    if (argc != 13) {
        std::cerr << "usage: asset_layer_contract_test <model> <data> <editor> "
                     "<library> <layers> <canvas> <source> <dock> <properties> "
                     "<runtime> <locale> <cmake>\n";
        return 2;
    }
    const std::string model = read_file(argv[1]);
    const std::string data = read_file(argv[2]);
    const std::string editor = read_file(argv[3]);
    const std::string library = read_file(argv[4]);
    const std::string layers = read_file(argv[5]);
    const std::string canvas = read_file(argv[6]);
    const std::string source = read_file(argv[7]);
    const std::string dock = read_file(argv[8]);
    const std::string properties = read_file(argv[9]);
    const std::string runtime = read_file(argv[10]);
    const std::string locale = read_file(argv[11]);
    const std::string cmake = read_file(argv[12]);

    bool ok = true;
    ok &= require(model, "Asset = 9", "dedicated persisted layer type");
    ok &= require(model, "asset_playback_mode", "instance playback mode");
    ok &= require(model, "asset_source_playback_mode", "source playback snapshot");
    ok &= require(model, "asset_pause_duration", "independent pause duration");
    ok &= require(model, "asset_loop_count", "finite independent loop count");
    ok &= require(model, "asset_owner_id", "instance child ownership");
    ok &= require(data, "jt[\"is_asset\"]", "asset title serialization");
    ok &= require(data, "j[\"asset_title_id\"]", "asset layer serialization");
    ok &= require(data, "j[\"asset_pause_duration\"]", "pause duration serialization");
    ok &= require(data, "j[\"asset_loop_count\"]", "loop count serialization");
    ok &= require(editor, "void TitleEditor::save_title_as_asset()", "File Save as Asset command");
    ok &= require(editor, "title_has_timeline_animation(*stored)", "automatic animated-asset detection");
    ok &= require(editor, "void TitleEditor::insert_asset_layer", "Asset Layer insertion");
    ok &= require(editor, "asset_source_playback_mode", "source playback metadata capture");
    ok &= require(editor, "editor_title_animation_bounds", "static full-animation source envelope");
    ok &= require(editor, "kMaximumEnvelopeSamples", "bounded frame-by-frame envelope scan");
    ok &= require(editor, "void TitleEditor::edit_asset", "asset editor switching flow");
    ok &= require(editor, "SaveBeforeEditAssetPrompt", "unsaved-title save modal before asset editing");
    ok &= require(editor, "asset_overrides_requested", "exposed override editor");
    ok &= require(library, "kAssetLayerMimeType", "asset drag MIME type");
    ok &= require(library, "setEditCallback", "asset library Edit Asset callback");
    ok &= require(library, "setDragEnabled(true)", "library drag support");
    ok &= require(layers, "OBSTitles.AssetLayerTooltip", "Asset Layer row identity");
    ok &= require(canvas, "asset_layer_requested", "canvas drop insertion signal");
    ok &= require(canvas, "edit_asset_requested", "canvas Edit Asset action");
    ok &= require(canvas, "fixed selection envelope", "non-animated asset bounding box");
    ok &= require(canvas, "parent->type == LayerType::Asset", "opaque Asset canvas hierarchy");
    ok &= require(source, "layer_asset_resolved_time_impl", "nested asset time traversal");
    ok &= require(source, "asset_runtime::resolve_local_time", "shared independent monotonic clock");
    ok &= require(source, "gpu_layer_chain_visible", "asset-aware nested visibility clock");
    ok &= require(source, "gpu_layer_compositor_opacity", "asset-aware container opacity clock");
    ok &= require(source, "title_has_independent_asset_layer", "independent playback refresh");
    ok &= require(source, "layer_type_is_container", "Asset GPU container compositing");
    ok &= require(dock, "title->is_asset", "asset titles hidden from Titles dock");
    ok &= require(properties, "update_asset_playback_controls_visibility", "static asset playback controls hidden");
    ok &= require(properties, "asset_source_playback_mode == 2", "pause-only controls");
    ok &= require(properties, "asset_source_playback_mode == 1", "loop-only controls");
    ok &= require(runtime, "independent_elapsed", "per-instance monotonic runtime state");
    ok &= require(runtime, "map_pause_mode", "finite pause behavior");
    ok &= require(runtime, "map_restart_loop_mode", "finite loop-count behavior");
    ok &= require(locale, "OBSTitles.EditAsset=", "Edit Asset label");
    ok &= require(locale, "OBSTitles.SaveBeforeEditAssetPrompt=", "asset switch save prompt");
    ok &= require(locale, "OBSTitles.AssetPauseFor=", "Pause for label");
    ok &= require(locale, "OBSTitles.AssetLoopTimesSuffix=", "Loop times suffix");
    ok &= require(cmake, "VERSION 0.8.20", "current project version");
    ok &= require(cmake, "OBS_FXM_DEVELOPMENT_VERSION", "development version variable");
    return ok ? 0 : 1;
}
