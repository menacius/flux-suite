from pathlib import Path
root=Path(__file__).resolve().parents[1]
required=[
 'src/ui/flux/flux-modern-controls.h','src/core/queue-runner.cpp','src/worker/worker-application.cpp',
 'src/app/profile-editor-dialog.cpp','resources/flux-encoder-icon.svg','LICENSE'
]
missing=[p for p in required if not (root/p).exists()]
text='\n'.join(p.read_text(errors='ignore') for p in root.glob('src/**/*.cpp'))
features=['h264_nvenc','h264_qsv','h264_amf','h264_vaapi','FluxSwitch','profileSnapshot','QSQLITE']
missing_features=[f for f in features if f not in text and f not in (root/'src/core/media-types.h').read_text()]
if missing or missing_features:
    raise SystemExit(f'Missing files={missing}, features={missing_features}')
print('Flux Encoder source contract OK')
