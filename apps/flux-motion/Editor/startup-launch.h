#pragma once

#include <QString>

#include <utility>

namespace fxm::editor {

enum class LaunchMode {
    StandaloneWelcome,
    StandaloneProject,
    StandaloneFile,
    PluginHosted,
    Preferences,
    GraphicsSmokeTest,
};

struct LaunchRequest {
    LaunchMode mode = LaunchMode::StandaloneWelcome;
    QString project_id;
    QString project_path;
};

inline LaunchRequest resolveLaunchRequest(bool hosted_by_obs,
                                          bool preferences,
                                          bool graphics_smoke_test,
                                          QString project_id,
                                          QString project_path = {})
{
    project_id = project_id.trimmed();
    project_path = project_path.trimmed();
    if (graphics_smoke_test)
        return {LaunchMode::GraphicsSmokeTest, std::move(project_id), {}};
    if (preferences)
        return {LaunchMode::Preferences, std::move(project_id), {}};
    if (hosted_by_obs)
        return {LaunchMode::PluginHosted, std::move(project_id), {}};
    if (!project_path.isEmpty())
        return {LaunchMode::StandaloneFile, {}, std::move(project_path)};
    if (!project_id.isEmpty())
        return {LaunchMode::StandaloneProject, std::move(project_id), {}};
    return {LaunchMode::StandaloneWelcome, {}, {}};
}

} // namespace fxm::editor
