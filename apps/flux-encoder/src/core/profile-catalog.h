#pragma once
#include "core/media-types.h"

namespace flux {

class ProfileCatalog final {
public:
    static QList<ProfileCategory> builtInCategories();
    static QList<RenderProfile> builtInProfiles(const FfmpegCapabilities &capabilities);
    static void seedMissing(class Database &database, const FfmpegCapabilities &capabilities);
};

} // namespace flux
