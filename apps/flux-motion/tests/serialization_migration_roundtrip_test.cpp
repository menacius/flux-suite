#include "title-serialization-schema.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>

using fxm::serialization::MigrationReport;
using fxm::serialization::json;

namespace {

json make_legacy_title()
{
    return {
        {"id", "title-fixed-id"},
        {"name", "Serialization Audit"},
        {"duration", 4.0},
        {"frame_rate", 50.0},
        {"graphic_type", 3},
        {"stinger_transition_point_percent", 37.5},
        {"stinger_editor_background", 1},
        {"external_data_enabled", true},
        {"external_data_sources", json::array({
            {
                {"id", "source-1"},
                {"name", "Unavailable future provider"},
                {"enabled", true},
                {"provider", {{"type", 999}, {"endpoint", "missing://provider"}}},
                {"future_provider_field", "preserve-me"}
            }
        })},
        {"live_text_external_bindings", json::array({
            {
                {"row_id", "row-1"},
                {"layer_id", "text-layer-stable"},
                {"binding", {
                    {"enabled", true},
                    {"property_path", "text"},
                    {"source_id", "source-1"},
                    {"field_path", "headline"},
                    {"formatter_config", {{"prefix", "["}, {"suffix", "]"}}}
                }}
            }
        })},
        {"layers", json::array({
            {
                {"id", "text-layer-stable"},
                {"type", 0},
                {"name", "Formatted text"},
                {"group_collapsed", true},
                {"rich_text", {
                    {"text", "HELLO"},
                    {"auto_style_rules", json::array({
                        {
                            {"id", "rule-1"},
                            {"style_preset_id", "preset-1"},
                            {"excludes_rule_ids", json::array({"rule-2"})},
                            {"future_pattern_field", 42}
                        }
                    })}
                }},
                {"position", {
                    {"static_value", {{"x", 100.0}, {"y", 200.0}}},
                    {"keyframes", json::array({
                        {
                            {"time", 0.0},
                            {"value", {{"x", 100.0}, {"y", 200.0}}},
                            {"temporal_in_influence", 25.0},
                            {"temporal_out_influence", 75.0},
                            {"temporal_in_speed", -12.5},
                            {"temporal_out_speed", 34.5},
                            {"spatial_in_tangent", {{"x", -20.0}, {"y", 5.0}}},
                            {"spatial_out_tangent", {{"x", 40.0}, {"y", -10.0}}},
                            {"spatial_tangents_linked", false},
                            {"future_keyframe_field", {{"preserve", true}}}
                        }
                    })}
                }},
                {"effects", json::array({
                    {{"type", 0}, {"future_effect_field", "preserve-me"}}
                })},
                {"transitions", json::array({
                    {{"id", "transition-1"}, {"edge", 0}, {"future_transition_field", 91}}
                })},
                {"future_layer_field", {"nested", true}}
            },
            {
                {"id", "audio-layer-stable"},
                {"type", 10},
                {"name", "Missing audio is allowed"},
                {"audio_source", "/definitely/missing/audio.wav"},
                {"audio_volume", 0.75},
                {"audio_pan", -0.25},
                {"audio_effects", json::array()},
                {"audio_waveform", json::array()}
            },
            {
                {"id", "image-layer-stable"},
                {"type", 2},
                {"name", "Missing image is allowed"},
                {"image_path", "/definitely/missing/image.png"}
            }
        })},
        {"proxy_metadata", {
            {"content_hash", "stale-hash"},
            {"proxy_path", "/definitely/missing/proxy.ogsf"},
            {"complete", true},
            {"future_proxy_field", "preserve-me"}
        }},
        {"future_title_field", {{"version", 999}, {"payload", "preserve-me"}}}
    };
}

void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::abort();
    }
}

} // namespace

int main()
{
    const json legacy = make_legacy_title();
    MigrationReport first_report;
    const json migrated = fxm::serialization::migrate_title_json(legacy, &first_report);

    require(migrated.at("schema_version") == fxm::serialization::kCurrentTitleSchemaVersion,
            "current title schema version was not written");
    require(migrated.at("development_version") == fxm::serialization::kCurrentDevelopmentVersion,
            "current development version was not written");
    require(migrated.at("id") == "title-fixed-id", "title ID changed during migration");
    require(migrated.at("layers").at(0).at("id") == "text-layer-stable",
            "text layer ID changed during migration");
    require(migrated.at("layers").at(1).at("id") == "audio-layer-stable",
            "audio layer ID changed during migration");
    require(migrated.at("layers").at(2).at("id") == "image-layer-stable",
            "image layer ID changed during migration");
    require(migrated.at("live_text_external_bindings").at(0).at("layer_id") ==
                "text-layer-stable",
            "external binding layer ID changed during migration");

    require(migrated.at("future_title_field") == legacy.at("future_title_field"),
            "unknown title field was not preserved");
    require(migrated.at("layers").at(0).at("future_layer_field") ==
                legacy.at("layers").at(0).at("future_layer_field"),
            "unknown layer field was not preserved");
    require(migrated.at("proxy_metadata").at("future_proxy_field") == "preserve-me",
            "unknown proxy field was not preserved");
    require(migrated.at("external_data_sources").at(0).at("future_provider_field") ==
                "preserve-me",
            "unknown provider field was not preserved");
    require(migrated.at("layers").at(0).at("position").at("keyframes").at(0)
                .at("future_keyframe_field").at("preserve") == true,
            "unknown keyframe field was not preserved");
    require(migrated.at("layers").at(0).at("effects").at(0)
                .at("future_effect_field") == "preserve-me",
            "unknown effect field was not preserved");
    require(migrated.at("layers").at(0).at("transitions").at(0)
                .at("future_transition_field") == 91,
            "unknown transition field was not preserved");

    const auto &document = migrated.at("layers").at(0).at("rich_text");
    require(document.at("formatting_schema_version") ==
                fxm::serialization::kCurrentFormattingSchemaVersion,
            "formatting schema version was not migrated");
    require(document.at("auto_style_rules").at(0).at("pattern_schema_version") ==
                fxm::serialization::kCurrentPatternSchemaVersion,
            "pattern schema version was not migrated");

    const auto &audio = migrated.at("layers").at(1);
    require(audio.contains("audio_volume_prop") && audio.contains("audio_pan_prop"),
            "legacy audio values were not migrated to animatable properties");
    require(audio.at("audio_source") == "/definitely/missing/audio.wav",
            "missing audio path was discarded instead of loading safely");
    require(migrated.at("layers").at(2).at("image_path") ==
                "/definitely/missing/image.png",
            "missing image path was discarded instead of loading safely");

    require(migrated.at("stinger_transition_point") == 1.5,
            "Stinger percentage transition point was not migrated");
    require(migrated.at("stinger_editor_background") == 3,
            "legacy Stinger editor background was not migrated");
    require(migrated.at("proxy_metadata").at("schema_version") ==
                fxm::serialization::kCurrentProxyManifestSchemaVersion,
            "proxy schema version was not migrated");

    const auto &keyframe = migrated.at("layers").at(0).at("position").at("keyframes").at(0);
    require(keyframe.at("spatial_in_tangent").at("x") == -20.0,
            "spatial incoming tangent changed");
    require(keyframe.at("spatial_out_tangent").at("x") == 40.0,
            "spatial outgoing tangent changed");
    require(keyframe.at("temporal_in_speed") == -12.5,
            "temporal incoming speed changed");
    require(keyframe.at("temporal_out_influence") == 75.0,
            "temporal outgoing influence changed");
    require(migrated.at("layers").at(0).at("group_collapsed") == true,
            "collapsed layer/group state changed");

    const std::string serialized = migrated.dump();
    const json reparsed = json::parse(serialized);
    MigrationReport second_report;
    const json round_trip = fxm::serialization::migrate_title_json(reparsed, &second_report);
    require(round_trip == migrated, "canonical title changed after JSON round trip");

    const auto ledger = fxm::serialization::development_migration_ledger();
    require(!ledger.empty(), "development migration ledger is empty");
    require(ledger.front() == fxm::serialization::kFirstAuditedDevelopmentVersion,
            "migration ledger does not start at development 144");
    require(ledger.back() == fxm::serialization::kCurrentDevelopmentVersion,
            "migration ledger does not reach the current development version");
    for (std::size_t index = 1; index < ledger.size(); ++index)
        require(ledger[index] == ledger[index - 1] + 1,
                "development migration ledger contains a gap");

    json malformed = {
        {"id", "malformed-but-recoverable"},
        {"layers", json::array({
            false,
            {
                {"id", "layer-ok"},
                {"effects", json::array({false, {{"type", 0}}})},
                {"transitions", "not-an-array"},
                {"audio_effects", json::array({nullptr, {{"type", 0}}})},
                {"external_bindings", 42},
                {"parent_bind_enabled", true},
                {"parent_bind_matrix", json::array({1.0, 2.0})}
            }
        })},
        {"cameras", json::array({false, {{"id", "camera-ok"}}})},
        {"external_data_sources", json::array({
            false,
            {{"id", "source-ok"}, {"fields", json::array({false, {{"path", "headline"}}})},
             {"provider", "not-an-object"}}
        })},
        {"live_text_external_bindings", json::array({false, {{"layer_id", "ok"}}})},
        {"proxy_metadata", "not-an-object"}
    };
    MigrationReport malformed_report;
    const json recovered = fxm::serialization::migrate_title_json(malformed, &malformed_report);
    require(recovered.at("layers").size() == 1,
            "malformed layer entries were not isolated");
    require(recovered.at("cameras").size() == 1,
            "malformed camera entries were not isolated");
    require(recovered.at("layers").at(0).at("effects").size() == 1,
            "malformed effect entries were not isolated");
    require(recovered.at("layers").at(0).at("transitions").is_array(),
            "malformed transition array was not recovered");
    require(recovered.at("layers").at(0).at("audio_effects").size() == 1,
            "malformed audio-effect entries were not isolated");
    require(recovered.at("layers").at(0).at("external_bindings").is_array(),
            "malformed external bindings were not recovered");
    require(recovered.at("layers").at(0).at("parent_bind_enabled") == false,
            "malformed parent bind matrix was not disabled");
    require(recovered.at("external_data_sources").size() == 1,
            "malformed external data source entries were not isolated");
    require(recovered.at("external_data_sources").at(0).at("fields").size() == 1,
            "malformed external field entries were not isolated");
    require(recovered.at("external_data_sources").at(0).at("provider").is_object(),
            "malformed external provider was not recovered");
    require(recovered.at("live_text_external_bindings").size() == 1,
            "only malformed binding entries should be discarded");
    require(recovered.at("proxy_metadata").is_object(),
            "malformed proxy metadata was not recovered");
    require(!malformed_report.recoveries.empty(), "malformed input produced no recovery report");

    json future = migrated;
    future["schema_version"] = fxm::serialization::kCurrentTitleSchemaVersion + 10;
    future["development_version"] = fxm::serialization::kCurrentDevelopmentVersion + 10;
    future["unknown_future_contract"] = {"safe", "value"};
    MigrationReport future_report;
    const json future_loaded = fxm::serialization::migrate_title_json(future, &future_report);
    require(future_loaded.at("unknown_future_contract") == future.at("unknown_future_contract"),
            "future unknown field was not preserved");
    require(!future_report.warnings.empty(), "future schema did not produce a warning");

    std::cout << "serialization migration round-trip tests passed\n";
    return 0;
}
