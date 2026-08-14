#!/usr/bin/env python3
"""Revision 251 MSVC hotfix contract.

The direct-video GPU cache key must use stream fields that exist on Layer.
A temporary implementation referenced layer.video_stream_id, which does not
exist in Core/layer-model.h and fails MSVC compilation.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
INC = ROOT / "src" / "obs" / "title-source" / "source-lifecycle-playback.inc"
MODEL = ROOT / "Core" / "layer-model.h"

inc = INC.read_text(encoding="utf-8")
model = MODEL.read_text(encoding="utf-8")

assert "video_stream_id" not in inc, "direct-video cache key must not reference missing Layer::video_stream_id"
assert "int         video_stream_index" in model, "Layer must expose video_stream_index"
assert "int         video_audio_stream_index" in model, "Layer must expose video_audio_stream_index"
assert "std::string video_selected_streams_json" in model, "Layer must expose video_selected_streams_json"

needle = '''"|stream=" + std::to_string(layer.video_stream_index) +\n                    "|astream=" + std::to_string(layer.video_audio_stream_index) +\n                    "|streams=" + layer.video_selected_streams_json'''
assert needle in inc, "direct-video cache key must use existing stream-index fields"

print("Revision 251 MSVC stream-index hotfix contract passed.")
