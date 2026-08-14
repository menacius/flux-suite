#include "source_bundle_reader.h"
#include <iostream>
#include <string>

int main(int argc, char **argv)
{
    if (argc != 4) {
        std::cerr << "usage: group_canvas_manipulation_contract_test <header> <press-inc> <drag-inc>\n";
        return 2;
    }
    const std::string header = read_file(argv[1]);
    const std::string press = read_file(argv[2]);
    const std::string drag = read_file(argv[3]);
    bool ok = true;
    ok &= header.find("local_left") != std::string::npos;
    ok &= header.find("local_top") != std::string::npos;
    ok &= press.find("selected_local_bounds.left()") != std::string::npos;
    ok &= press.find("selected_local_bounds.top()") != std::string::npos;
    ok &= drag.find("Use the exact same local bounds that produced the visible canvas") != std::string::npos;
    ok &= drag.find("start_state->local_left") != std::string::npos;
    ok &= drag.find("start_state->local_top") != std::string::npos;
    if (!ok) {
        std::cerr << "Group canvas manipulation contract failed\n";
        return 1;
    }
    return 0;
}
