#include <algorithm>
#include <cassert>
#include <cmath>
#include <string>

#include "source_bundle_reader.h"

namespace {

struct Rect {
    double x;
    double y;
    double width;
    double height;
};

struct Radii {
    double top_left;
    double top_right;
    double bottom_right;
    double bottom_left;
};

double smoothstep(double edge0, double edge1, double value)
{
    if (edge1 <= edge0)
        return value < edge0 ? 0.0 : 1.0;
    const double t = std::clamp((value - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double rounded_box_alpha(double x, double y, const Rect &rect, const Radii &input)
{
    const double rect_max_x = rect.x + rect.width;
    const double rect_max_y = rect.y + rect.height;
    if (x < rect.x || y < rect.y || x > rect_max_x || y > rect_max_y)
        return 0.0;

    const double local_x = x - rect.x;
    const double local_y = y - rect.y;
    const double width = std::max(rect.width, 0.0001);
    const double height = std::max(rect.height, 0.0001);
    const double max_radius = std::min(width, height) * 0.5;
    const Radii radii {
        std::clamp(input.top_left, 0.0, max_radius),
        std::clamp(input.top_right, 0.0, max_radius),
        std::clamp(input.bottom_right, 0.0, max_radius),
        std::clamp(input.bottom_left, 0.0, max_radius),
    };

    double radius = 0.0;
    double center_x = 0.0;
    double center_y = 0.0;
    if (local_x <= radii.top_left && local_y <= radii.top_left) {
        radius = radii.top_left;
        center_x = radius;
        center_y = radius;
    } else if (local_x >= width - radii.top_right &&
               local_y <= radii.top_right) {
        radius = radii.top_right;
        center_x = width - radius;
        center_y = radius;
    } else if (local_x >= width - radii.bottom_right &&
               local_y >= height - radii.bottom_right) {
        radius = radii.bottom_right;
        center_x = width - radius;
        center_y = height - radius;
    } else if (local_x <= radii.bottom_left &&
               local_y >= height - radii.bottom_left) {
        radius = radii.bottom_left;
        center_x = radius;
        center_y = height - radius;
    } else {
        return 1.0;
    }

    if (radius <= 0.0001)
        return 1.0;
    const double dx = local_x - center_x;
    const double dy = local_y - center_y;
    const double distance = std::sqrt(dx * dx + dy * dy);
    return 1.0 - smoothstep(std::max(0.0, radius - 1.0),
                            radius + 1.0, distance);
}

void test_corner_geometry()
{
    const Rect rect {0.0, 0.0, 100.0, 60.0};
    const Radii radii {20.0, 20.0, 20.0, 20.0};

    assert(rounded_box_alpha(-1.0, 30.0, rect, radii) == 0.0);
    assert(rounded_box_alpha(50.0, 30.0, rect, radii) == 1.0);

    /* These edge points are outside the corner's radius-by-radius square and
     * must remain opaque. The previous shader incorrectly tested a 2r-by-2r
     * square and cut them out, creating detached circular islands. */
    assert(rounded_box_alpha(35.0, 5.0, rect, radii) > 0.999);
    assert(rounded_box_alpha(65.0, 5.0, rect, radii) > 0.999);
    assert(rounded_box_alpha(35.0, 55.0, rect, radii) > 0.999);
    assert(rounded_box_alpha(65.0, 55.0, rect, radii) > 0.999);

    assert(rounded_box_alpha(3.0, 3.0, rect, radii) < 0.001);
    assert(rounded_box_alpha(97.0, 3.0, rect, radii) < 0.001);
    assert(rounded_box_alpha(97.0, 57.0, rect, radii) < 0.001);
    assert(rounded_box_alpha(3.0, 57.0, rect, radii) < 0.001);

    assert(rounded_box_alpha(10.0, 10.0, rect, radii) > 0.999);
    assert(rounded_box_alpha(90.0, 10.0, rect, radii) > 0.999);
}

void test_independent_corner_radii()
{
    const Rect rect {0.0, 0.0, 100.0, 60.0};
    const Radii radii {20.0, 0.0, 10.0, 0.0};

    assert(rounded_box_alpha(3.0, 3.0, rect, radii) < 0.001);
    assert(rounded_box_alpha(99.0, 1.0, rect, radii) > 0.999);
    assert(rounded_box_alpha(99.0, 59.0, rect, radii) < 0.001);
    assert(rounded_box_alpha(1.0, 59.0, rect, radii) > 0.999);
}

void test_shader_source_contract(const char *path)
{
    const std::string source = read_file(path);

    assert(source.find("float4 clampedRadii = clamp(radii, 0.0, maxRadius);") !=
           std::string::npos);
    assert(source.find("local.x <= clampedRadii.x && local.y <= clampedRadii.x") !=
           std::string::npos);
    assert(source.find("delta.x <= radius || delta.y <= radius") ==
           std::string::npos);
}

} // namespace

int main(int argc, char **argv)
{
    assert(argc == 2);
    test_corner_geometry();
    test_independent_corner_radii();
    test_shader_source_contract(argv[1]);
    return 0;
}
