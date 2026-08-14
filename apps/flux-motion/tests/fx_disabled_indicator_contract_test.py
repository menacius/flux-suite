from pathlib import Path

root = Path(__file__).resolve().parents[1]
layer_stack = (root / 'src/layers/layer-stack-widget.cpp').read_text(encoding='utf-8')
cmake = (root / 'CMakeLists.txt').read_text(encoding='utf-8')
build_info = (root / 'src/core/build-info.h').read_text(encoding='utf-8')
readme = (root / 'README.md').read_text(encoding='utf-8')
effects_doc = (root / 'docs/EFFECTS_AND_EXTENSIONS.md').read_text(encoding='utf-8')

required = [
    'class FxIndicatorButton final : public QToolButton',
    'void set_effect_stack_disabled(bool disabled)',
    'if (!effect_stack_disabled_) return;',
    'painter.drawLine(QPointF(4.0, height() - 3.0)',
    'fx_indicator->set_effect_stack_disabled(has_effect_stack && !has_enabled_effect_stack);',
    'fx_indicator->set_effect_stack_disabled(!enabled);',
]
for token in required:
    assert token in layer_stack, f'missing FX disabled-indicator contract: {token}'

assert 'project(flux-motion VERSION 0.8.11)' in cmake
assert 'set(OBS_FXM_DEVELOPMENT_VERSION "243")' in cmake
assert '#define PLUGIN_VERSION "0.8.11-alpha"' in build_info
assert '#define FXM_DEVELOPMENT_VERSION "243"' in build_info
assert 'v0.8.11-alpha` · `Development Version 243' in readme
assert 'diagonal strike-through' in effects_doc

print('Disabled FX stack indicator contract: OK')
