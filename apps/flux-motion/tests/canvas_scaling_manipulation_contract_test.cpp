#include <cmath>
#include <iostream>
#include <string>

#include "source_bundle_reader.h"

namespace {

bool require(const std::string &text, const std::string &needle, const char *label)
{
    if (text.find(needle) != std::string::npos)
        return true;
    std::cerr << "Missing canvas scaling contract: " << label
              << " (" << needle << ")\n";
    return false;
}

bool reject(const std::string &text, const std::string &needle, const char *label)
{
    if (text.find(needle) == std::string::npos)
        return true;
    std::cerr << "Obsolete canvas scaling path remains: " << label
              << " (" << needle << ")\n";
    return false;
}

bool near(double a, double b)
{
    return std::abs(a - b) < 1.0e-9;
}

bool verify_size_backed_anchor_math()
{
    // Shape with a 25% horizontal origin: initial local bounds [-25, 75].
    // Dragging the right handle to 175 should keep the left edge at -25.
    const double start_width = 100.0;
    const double start_left = -25.0;
    const double origin = -start_left / start_width;
    const double final_left = -25.0;
    const double final_right = 175.0;
    const double new_width = final_right - final_left;
    const double actual_left_before_translation = -origin * new_width;
    const double final_center = (final_left + final_right) * 0.5;
    const double actual_center = actual_left_before_translation + new_width * 0.5;
    const double translation = final_center - actual_center;
    return near(actual_left_before_translation + translation, final_left) &&
           near(actual_left_before_translation + new_width + translation, final_right);
}

bool verify_scale_backed_anchor_math()
{
    // Off-centre Group/Asset bounds [100, 200]. Doubling their width around
    // the layer origin would produce [200, 400]; the post-scale translation
    // must restore the fixed left handle and place the dragged edge at 300.
    const double start_left = 100.0;
    const double start_right = 200.0;
    const double final_left = 100.0;
    const double final_right = 300.0;
    const double scale_factor = (final_right - final_left) /
                                (start_right - start_left);
    const double scaled_left = start_left * scale_factor;
    const double scaled_right = start_right * scale_factor;
    const double desired_center = (final_left + final_right) * 0.5;
    const double current_center = (scaled_left + scaled_right) * 0.5;
    const double translation = desired_center - current_center;
    return near(scaled_left + translation, final_left) &&
           near(scaled_right + translation, final_right);
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::cerr << "usage: canvas_scaling_manipulation_contract_test <canvas-cpp>\n";
        return 2;
    }

    const std::string canvas = read_file(argv[1]);
    bool ok = true;
    ok &= require(canvas,
                  "const double left = start_state ? start_state->local_left",
                  "resize starts from the exact visible local bounds");
    ok &= require(canvas,
                  "const double origin_x = start_state->w > 0.0f",
                  "size-backed layers preserve their origin fraction");
    ok &= require(canvas,
                  "const QPointF desired_center_canvas = start_xf.map(final_rect.center())",
                  "all resize paths align to the pointer-defined target rectangle");
    ok &= require(canvas,
                  "const QPointF current_center_canvas = next_xf.map(start_rect.center())",
                  "scale-backed Group/Asset/text layers receive anchor correction");
    ok &= require(canvas,
                  "eval_origin_x(*layer, lt)",
                  "animated horizontal origin is captured at drag start");
    ok &= require(canvas,
                  "eval_origin_y(*layer, lt)",
                  "animated vertical origin is captured at drag start");
    ok &= reject(canvas,
                 "QRectF actual_rect(start_state ? start_state->local_left",
                 "074 absolute-local-offset resize regression");
    ok &= reject(canvas,
                 "if (alt_resize && start_state)",
                 "modifier-specific anchor correction that leaves normal resize inconsistent");
    ok &= reject(canvas,
                 "const QPointF fixed_center_canvas",
                 "second centre correction competing with the target rectangle");
    ok &= verify_size_backed_anchor_math();
    if (!verify_size_backed_anchor_math())
        std::cerr << "Size-backed anchor math failed\n";
    ok &= verify_scale_backed_anchor_math();
    if (!verify_scale_backed_anchor_math())
        std::cerr << "Scale-backed anchor math failed\n";
    return ok ? 0 : 1;
}
