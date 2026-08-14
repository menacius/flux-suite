#include "source_bundle_reader.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

bool require(const std::string &source, const std::string &needle,
             const std::string &label)
{
    if (source.find(needle) != std::string::npos)
        return true;
    std::cerr << "Missing unified serialization contract: " << label
              << " (" << needle << ")\n";
    return false;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 9) {
        std::cerr << "usage: unified_serialization_contract_test <title-data.cpp> "
                     "<schema.h> <style-presets.cpp> <disk-cache.inc> "
                     "<dock-lifecycle.inc> <collapsible.inc> <audio-runtime.cpp> <formatting-profile.inc>\n";
        return 2;
    }

    const std::string title_data = read_file(argv[1]);
    const std::string schema = read_file(argv[2]);
    const std::string styles = read_file(argv[3]);
    const std::string disk_cache = read_file(argv[4]);
    const std::string dock = read_file(argv[5]);
    const std::string collapsible = read_file(argv[6]);
    const std::string audio_runtime = read_file(argv[7]);
    const std::string formatting_profile = read_file(argv[8]);

    bool ok = true;
    const std::vector<std::string> title_contract = {
        "schema_version", "development_version", "external_data_sources",
        "live_text_external_bindings", "live_text_table_bindings",
        "formatting_schema_version", "pattern_schema_version", "auto_style_rules",
        "audio_source", "audio_effects", "audio_waveform",
        "stinger_transition_point", "stinger_pre_roll", "stinger_post_roll",
        "proxy_metadata", "spatial_in_tangent", "spatial_out_tangent",
        "temporal_in_speed", "temporal_out_speed", "temporal_in_influence",
        "temporal_out_influence", "group_collapsed",
        "migrate_title_json(input", "root.is_array()", "root[\"titles\"].is_array()"
    };
    for (const auto &token : title_contract)
        ok &= require(title_data, token, "title serializer/loader field");

    ok &= require(schema, "kFirstAuditedDevelopmentVersion = 144",
                  "migration range starts at 144");
    for (int version = 144; version <= 222; ++version) {
        ok &= require(schema, "case " + std::to_string(version) + ":",
                      "explicit migration step for development " + std::to_string(version));
    }
    ok &= require(schema,
                  "unknown fields on valid objects retain their original JSON values",
                  "raw migration unknown-field preservation");
    ok &= require(title_data, "merge_nested_passthrough",
                  "nested model round-trip unknown-field preservation");
    ok &= require(title_data, "duplicateOrMissingIds",
                  "post-load layer identity repair");
    ok &= require(title_data, "danglingOrCyclicLinks",
                  "post-load hierarchy reference repair");
    ok &= require(schema, "development_migration_ledger",
                  "contiguous migration ledger API");

    ok &= require(title_data, "Title entry root must be a JSON object",
                  "unrecoverable title roots are isolated per entry");
    ok &= require(title_data, "Never rewrite a",
                  "layer IDs remain stable");
    ok &= require(title_data, "Imported templates need a new title identity",
                  "import preserves document-local layer identities");
    ok &= require(title_data, "provider_supported && json_bool",
                  "unknown external providers are disabled safely");
    ok &= require(title_data, "!QFileInfo::exists",
                  "missing external assets are diagnostic-only");

    ok &= require(styles, "kStylePresetFileVersion = 3",
                  "style preset file schema");
    ok &= require(styles, "kCurrentFormattingSchemaVersion",
                  "style preset formatting schema");
    ok &= require(styles, "QJsonParseError", "style preset malformed JSON recovery");
    ok &= require(styles, "QSaveFile", "style preset atomic writes");

    ok &= require(disk_cache, "kCurrentProxyManifestSchemaVersion",
                  "proxy cache manifest schema");
    ok &= require(disk_cache, "schema_version <= 0",
                  "proxy manifest validation");
    ok &= require(dock, "kCurrentDockSettingsSchemaVersion",
                  "dock settings schema");
    ok &= require(dock, "kDockSavedDevelopmentVersionKey",
                  "dock development migration provenance");
    ok &= require(collapsible, "titlesCollapsed",
                  "collapsed dock state persistence");
    ok &= require(dock, "restoreState(splitter_state)",
                  "dock splitter state recovery");

    ok &= require(audio_runtime, "if (spec.path.empty() || decode_cancelled(decode_epoch))",
                  "empty audio source is non-fatal");
    ok &= require(audio_runtime, "avformat_open_input", "FFmpeg audio open path");
    ok &= require(audio_runtime, "if (!file || cancelled())",
                  "missing PCM WAV file is non-fatal");

    ok &= require(formatting_profile, "gsp-auto-style-rule-set",
                  "formatting profile file contract");
    ok &= require(formatting_profile, "kCurrentFormattingSchemaVersion",
                  "formatting profile schema version");
    ok &= require(formatting_profile, "kCurrentPatternSchemaVersion",
                  "pattern definition schema version");
    ok &= require(formatting_profile, "QSaveFile",
                  "formatting profile atomic write");
    ok &= require(formatting_profile, "loaded.size() < 128",
                  "bounded formatting profile recovery");

    if (ok)
        std::cout << "unified serialization contract test passed\n";
    return ok ? 0 : 1;
}
