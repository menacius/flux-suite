#!/usr/bin/env python3
"""Startup progress and standalone Welcome workflow regression contract."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


main = read("Editor/main.cpp")
progress = read("Editor/startup-progress.h")
progress_impl = read("Editor/startup-progress.cpp")
splash = read("Editor/startup-progress-screen.cpp")
welcome = read("Editor/standalone-welcome-screen.cpp")
launch = read("Editor/startup-launch.h")
obs_host = read("../../plugins/obs/flux-motion/src/obs-editor-host.cpp")
cmake = read("CMakeLists.txt")

# The splash is a presentation-only observer of a module-extensible status
# channel, with both determinate and indeterminate progress support.
assert "class StartupProgress final" in progress
assert "registerStage" in progress
assert "reportIndeterminate" in progress
assert "setUpdateCallback" in progress
assert "QCoreApplication::processEvents" in progress_impl
assert "progress_bar_->setRange(0, 0)" in splash
assert "progress_bar_->setValue(state.percent)" in splash
for message in (
    "Initializing application...",
    "Loading configuration...",
    "Loading user preferences...",
    "Initializing rendering engine...",
    "Initializing media engine...",
    "Loading plugins...",
    "Loading fonts...",
    "Loading effects...",
    "Restoring workspace...",
    "Preparing UI...",
    "Ready.",
):
    assert message in main or message in progress

# Launch routing is decided before either landing window is instantiated.
assert "enum class LaunchMode" in launch
assert "StandaloneWelcome" in launch
assert "StandaloneProject" in launch
assert "StandaloneFile" in launch
assert "PluginHosted" in launch
assert main.index("resolveLaunchRequest") < main.index("make_unique<TitleEditor>")
assert main.index("resolveLaunchRequest") < main.index(
    "make_unique<fxm::editor::StandaloneWelcomeScreen>"
)
assert 'QStringLiteral("--hosted-by-obs")' in obs_host
assert 'QStringLiteral("hosted-by-obs")' in main

# The standalone landing page is a top-level workflow, not a modal dialog, and
# the Editor is created only from the selected/create/direct launch branches.
welcome_header = read("Editor/standalone-welcome-screen.h")
assert "class StandaloneWelcomeScreen final : public QWidget" in welcome_header
assert "class StandaloneApplicationWindow final : public QMainWindow" in welcome_header
assert "QStackedWidget" in welcome_header
for label in (
    "Recent Projects",
    "Create New Project",
    "Open Project",
    "Open Recent",
    "Import Project  (Coming Soon)",
    "Search projects",
):
    assert label in welcome
assert "QDialog" not in welcome
assert "application_window->showHome(welcome.get())" in main
assert "application_window->showEditor(" in main
assert "Qt::Widget" in main
assert "restoreWindowPlacement" in welcome
assert "saveWindowPlacement" in welcome
assert "normalGeometry()" in welcome
assert "current_screen->name()" in welcome
assert "QGuiApplication::screens()" in welcome
assert "Qt::WindowMaximized" in welcome
assert "recordProjectOpened" in welcome
assert "LaunchMode::StandaloneWelcome" in main
assert "launch.mode == fxm::editor::LaunchMode::PluginHosted" in main
assert 'QStringLiteral("project-file")' in main
assert "launch.project_path.toStdString()" in main
assert "LaunchMode::StandaloneFile" in main

# Startup-only source files belong exclusively to the standalone executable.
standalone_block = cmake[
    cmake.index("set(OBS_FXM_STANDALONE_EDITOR_SOURCES") :
    cmake.index("target_include_directories(flux-motion-editor")
]
assert "Editor/standalone-welcome-screen.cpp" in standalone_block
assert "Editor/startup-progress.cpp" in standalone_block

print("Startup experience contract passed")
