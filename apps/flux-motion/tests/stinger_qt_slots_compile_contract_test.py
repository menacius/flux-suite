#!/usr/bin/env python3
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "src/core/title-data.cpp").read_text(encoding="utf-8")
match = re.search(
    r"void ensure_stinger_transition_input_layers\(Title &title\)\n\{(?P<body>.*?)\n\}\n\nStingerValidationResult",
    SOURCE,
    re.S,
)
assert match, "Could not locate ensure_stinger_transition_input_layers()"
body = match.group("body")
code_only = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
assert not re.search(r"\bslots\b", code_only)
assert "required_inputs[2]" in body
assert "first_inputs[2]" in body
assert "input_index" in body
assert "input_layer" in body

probe = r"""#define slots Q_SLOTS
#include <algorithm>
#include <iterator>
#include <memory>
#include <string>
#include <vector>
struct Layer {
    int type = 0;
    int transition_input_slot = -1;
    bool transition_input_required = false;
    std::string name;
};
struct Title { std::vector<std::shared_ptr<Layer>> layers; };
static std::shared_ptr<Layer> make_input(int index)
{
    auto layer = std::make_shared<Layer>();
    layer->type = 1;
    layer->transition_input_slot = index;
    layer->transition_input_required = true;
    return layer;
}
static void ensure_inputs(Title &title)
{
    std::shared_ptr<Layer> required_inputs[2];
    std::shared_ptr<Layer> first_inputs[2];
    for (const auto &layer : title.layers) {
        if (!layer || layer->type != 1) continue;
        const int input_index = layer->transition_input_slot;
        if (input_index < 0 || input_index > 1) continue;
        if (!first_inputs[input_index]) first_inputs[input_index] = layer;
        if (layer->transition_input_required) {
            if (!required_inputs[input_index]) required_inputs[input_index] = layer;
            else layer->transition_input_required = false;
        }
    }
    for (int input_index = 0; input_index < 2; ++input_index) {
        if (!required_inputs[input_index] && first_inputs[input_index]) {
            required_inputs[input_index] = first_inputs[input_index];
            required_inputs[input_index]->transition_input_required = true;
        }
        if (!required_inputs[input_index]) required_inputs[input_index] = make_input(input_index);
    }
    const auto contains = [&](const std::shared_ptr<Layer> &candidate) {
        return std::find(title.layers.begin(), title.layers.end(), candidate) != title.layers.end();
    };
    if (!contains(required_inputs[1])) title.layers.insert(title.layers.begin(), required_inputs[1]);
    if (!contains(required_inputs[0])) {
        auto scene_b = std::find(title.layers.begin(), title.layers.end(), required_inputs[1]);
        title.layers.insert(scene_b == title.layers.end() ? title.layers.begin() : std::next(scene_b), required_inputs[0]);
    }
    for (int input_index = 0; input_index < 2; ++input_index) {
        auto &input_layer = required_inputs[input_index];
        input_layer->transition_input_required = true;
    }
}
int main() { Title title; ensure_inputs(title); return title.layers.size() == 2 ? 0 : 1; }
"""
with tempfile.TemporaryDirectory() as td:
    src = Path(td) / "qt_slots_probe.cpp"
    exe = Path(td) / "qt_slots_probe"
    src.write_text(probe, encoding="utf-8")
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

print("Stinger Qt keyword compile contract passed")
