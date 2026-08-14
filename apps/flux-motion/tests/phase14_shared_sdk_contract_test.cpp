#include "command-transport.h"
#include "logger.h"
#include "playback-controller.h"
#include "project-loader.h"
#include "rendering-interfaces.h"
#include "title-preview-renderer.h"

#include <type_traits>

int main()
{
    static_assert(std::is_abstract_v<fxm::ILogger>);
    static_assert(std::is_abstract_v<fxm::communication::ICommandTransport>);
    static_assert(std::is_abstract_v<fxm::IProjectLoader>);
    static_assert(std::is_abstract_v<fxm::IPlaybackController>);
    static_assert(std::is_abstract_v<fxm::rendering::IRenderBackend>);
    static_assert(std::is_abstract_v<fxm::rendering::ITitlePreviewRenderer>);
    return 0;
}
