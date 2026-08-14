#pragma once

#include <functional>
#include <string>

enum class TitleProgramHotkeyCommand {
    CueToProgram = 0,
    Uncue,
    CueLast,
    NextCue,
    PreviousCue,
};

using TitleProgramHotkeyHandler =
    std::function<void(TitleProgramHotkeyCommand, const std::string &)>;

void title_hotkeys_register();
void title_hotkeys_unregister();
void title_hotkeys_set_program_command_handler(TitleProgramHotkeyHandler handler);
