from pathlib import Path
root = Path(__file__).resolve().parents[1]
text = (root / "src/effects/effect-preset-catalog.cpp").read_text(encoding="utf-8")
assert '#include "effect-runtime.h"' in text
assert text.index('#include "effect-runtime.h"') < text.index('EffectDescriptor *descriptor')
print('effect preset catalog runtime include contract passed')
