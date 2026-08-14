/* Shared GPU frame metadata aliasing.
 * Include only after source-registration.inc: gpu-session-lifecycle.inc,
 * source-lifecycle-playback.inc and source-registration.inc contain contiguous
 * fragments of split function bodies. */

static bool alias_global_gpu_frame_locked(
    const std::string &cache_key, const std::string &canonical_cache_key)
{
    if (cache_key.empty() || canonical_cache_key.empty())
        return false;

    auto canonical = g_gpu_frame_cache.find(canonical_cache_key);
    if (canonical == g_gpu_frame_cache.end())
        return false;
    if (cache_key == canonical_cache_key) {
        canonical->second.last_used = ++g_gpu_frame_cache_tick;
        return true;
    }

    /* Validate the complete canonical frame before changing any reference
     * counts. A frame alias owns only lightweight metadata and shares the
     * already uploaded content-addressed tile textures. */
    for (const GpuRamTileRef &reference : canonical->second.tiles) {
        if (g_gpu_ram_tiles.find(reference.digest) == g_gpu_ram_tiles.end())
            return false;
    }

    GpuRamFrameEntry alias;
    alias.width = canonical->second.width;
    alias.height = canonical->second.height;
    alias.tiles = canonical->second.tiles;
    alias.metadata_bytes = gpu_ram_frame_metadata_bytes(alias.tiles.size());
    alias.last_used = ++g_gpu_frame_cache_tick;
    canonical->second.last_used = alias.last_used;
    for (const GpuRamTileRef &reference : alias.tiles) {
        auto tile = g_gpu_ram_tiles.find(reference.digest);
        ++tile->second.references;
        tile->second.last_used = alias.last_used;
    }

    auto existing = g_gpu_frame_cache.find(cache_key);
    if (existing != g_gpu_frame_cache.end()) {
        release_gpu_ram_frame_locked(existing->second);
        g_gpu_frame_cache.erase(existing);
    }

    g_gpu_frame_cache_bytes += alias.metadata_bytes;
    g_gpu_frame_cache.emplace(cache_key, std::move(alias));
    evict_global_gpu_frame_cache_locked();
    return g_gpu_frame_cache.find(cache_key) != g_gpu_frame_cache.end();
}
