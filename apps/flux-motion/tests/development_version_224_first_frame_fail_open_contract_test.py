from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RASTERS = (ROOT / "../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc").read_text(encoding="utf-8")
LIFECYCLE = (ROOT / "../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc").read_text(encoding="utf-8")
UPDATE = (ROOT / "../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc").read_text(encoding="utf-8")
TEXT = (ROOT / "src/rendering/title-gpu-text-renderer.cpp").read_text(encoding="utf-8")


def test_optional_gpu_raster_failure_cannot_block_first_frame():
    assert "optional_gpu_raster = entry.gpu_text || entry.gpu_primitive" in LIFECYCLE
    assert "stage=optional-raster-fail-open" in LIFECYCLE
    optional_block = LIFECYCLE.split("stage=optional-raster-fail-open", 1)[1]
    assert "all_required_rasters_ready = false" in optional_block
    assert "} else {" in optional_block


def test_failed_gpu_text_schedules_compatibility_rebuild():
    assert "bool force_compatibility_raster_rebuild = false;" in RASTERS
    assert "session->text_backend_unavailable = true;" in RASTERS
    assert "session->force_compatibility_raster_rebuild = true;" in RASTERS
    assert "entry.gpu_text = false;" in RASTERS
    assert "entry.pending_upload = false;" in RASTERS
    assert "return entry.texture != nullptr;" in RASTERS


def test_next_update_forces_cpu_raster_path():
    assert "const bool forced_compatibility_rebuild" in UPDATE
    assert "const bool model_changed = forced_compatibility_rebuild ||" in UPDATE
    assert "!forced_compatibility_rebuild && transform_only_update" in UPDATE
    assert "session->force_compatibility_raster_rebuild = false;" in UPDATE


def test_gpu_text_parameter_lookup_is_null_safe():
    assert "gpu_text_effect_param" in TEXT
    assert "return effect && name ? gs_effect_get_param_by_name(effect, name) : nullptr;" in TEXT
    assert 'gs_effect_get_param_by_name(impl_->effect, "glyphAtlas")' not in TEXT
