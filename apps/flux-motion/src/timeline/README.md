# Timeline & Animation

The timeline/keyframe contract now lives in `Core/animation.h`. Its existing
animation implementation now lives in `Core/animation.cpp`.

`timeline-widget.cpp` provides the ordinary timeline, while `temporal-graph-editor.inc` implements the AE-style Value/Speed Graph Editor. Both views mutate `TimelinePropertyRef` and commit through the same title undo stack. Editor playback, cached/prerendered frames, and OBS output must always consume `AnimatedProperty::evaluate()` / `AnimatedVec2Property::evaluate()` rather than implementing separate curve math.
