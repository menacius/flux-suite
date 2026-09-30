"""The first Flux source must not synchronously scan the disk frame cache."""

from pathlib import Path

root = Path(__file__).resolve().parents[1]
header = (root / "src/cache/cache-manager.h").read_text(encoding="utf-8")
cache = (
    root / "src/cache/cache-manager/visual-hash-keying.inc"
).read_text(encoding="utf-8")
main = (root / "Editor/main.cpp").read_text(encoding="utf-8")
properties = (root / "src/editor/properties-panel.cpp").read_text(encoding="utf-8")
refresh = (
    root / "src/editor/properties-panel/selection-refresh.inc"
).read_text(encoding="utf-8")
hierarchy = (
    root / "src/editor/title-editor-internal/hierarchy-model.inc"
).read_text(encoding="utf-8")

constructor = cache[cache.index("DiskFrameCache::DiskFrameCache"):]
constructor = constructor[: constructor.index("DiskFrameCache::~DiskFrameCache")]
assert "rebuildIndex();" not in constructor
assert "writer_active_ = true;" in constructor
assert "writer_thread_ = std::thread(&DiskFrameCache::writerLoop, this);" in constructor

writer = cache[cache.index("void DiskFrameCache::writerLoop()") :]
writer = writer[: writer.index("QString DiskFrameCache::pathForKey")]
assert writer.index("rebuildIndex();") < writer.index("for (;;) {")
assert "index_ready_.store(true, std::memory_order_release);" in writer
assert "std::atomic<bool> index_ready_{false};" in header

for signature in (
    "bool DiskFrameCache::contains",
    "bool DiskFrameCache::get",
    "QVector<CacheFrameKey> DiskFrameCache::keysForTitle",
):
    body = cache[cache.index(signature) :]
    body = body[: body.index("\n}")]
    assert "index_ready_.load(std::memory_order_acquire)" in body

# Font discovery can take minutes on damaged or very large Windows font
# collections. It belongs to the first explicit font/style popup, not process
# or selection initialization.
assert "QFontDatabase().families()" not in main
assert "QFontDatabase().families()" not in refresh
assert "class LazyFontComboBox" in properties
assert "void showPopup() override" in properties
assert 'setProperty("fxmLazyFontStyles", true)' in properties
assert 'property("fxmLazyFontStyles").toBool()' in hierarchy

print("OBS startup asynchronous disk-cache index contract: ok")
