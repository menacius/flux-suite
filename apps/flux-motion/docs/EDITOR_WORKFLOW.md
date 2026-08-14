# Editor workflow

## Window and panel model

The standalone Project panel is the central navigation point for multi-title
`.fxmproj` files. Each item opens an independent canvas and timeline; project
operations and nested title/graphic insertion are described in
[MULTI-TITLE-PROJECTS.md](MULTI-TITLE-PROJECTS.md).

The editor uses dockable Qt panels with a shared compact visual contract. Collapsible panels have a drag handle, title, optional header controls, an overflow action button where applicable, and a right-aligned caret. Sibling panels can be reordered by drag-and-drop, and their collapse/order state persists through `QSettings`.

Properties and Effects use the same margins, body insets, header sizing, dividers, switch geometry, and theme-derived colors. Explicit user colors and effect colors are not overridden by the OBS palette.

## Canvas and tools

The canvas supports selection, direct selection, shape drawing, pen/path editing, text creation, image placement, eyedropper sampling, gradient editing, ruler guides, snapping, safe areas, and zoom controls. Transform behavior is shared across single layers and groups, including center resize, constrained resize, rotated bounds, animated origins, and free-transform corners.

External drag-and-drop and clipboard input can create text and image layers. Canvas context menus expose the same layer actions available in the layer list, including grouping, parenting, matte assignment, asset editing, duplication, and deletion.

The horizontal and vertical rulers track the current canvas pointer in real time. Pointer movement invalidates only the cached GPU overlay, so ruler indicators update immediately without forcing the title artwork or its visual cache to rerender.

## Layer hierarchy

Grouping and parenting are separate systems.

- **Group:** composites child layers into one result. Group transforms and effects apply after children are composited. Collapsed groups are manipulated as a single object; expanded groups allow direct child selection.
- **Parenting:** applies inherited transform motion without changing layer ownership or list hierarchy.
- **Snapping:** moving a parent or group does not snap against its own descendants.
- **Ordering:** children remain inside their group ordering scope. Ungrouping and reparenting preserve visual placement.

## Mattes, masks, and visibility

A layer or group can act as a matte target or matte source. Alpha, Luma, inverted, and Clipping Matte modes are supported. Clipping uses shape coverage rather than source opacity. Visibility controls distinguish ordinary artwork visibility from active-mask participation.

Effects can respect the matte result through the Effects Settings toolbar. Canvas and OBS source compositing share the same intended matte/effect ordering.

## Timeline and animation

The timeline is frame-based and project-rate aware. It supports:

- property keyframes and easing;
- multi-keyframe selection and movement;
- group and child strips;
- layer ordering by drag-and-drop;
- playhead scrubbing and transport controls;
- keyframe navigation;
- negative values and keyframeable scale/size/origin properties;
- monitor-rate editor presentation with project-rate playback.

Position keyframes separate **temporal interpolation** (how progress advances over time) from **spatial interpolation** (the geometric motion path). Spatial modes are Linear, Auto Bezier, Continuous Bezier, and Manual Bezier. Incoming and outgoing handles are stored in layer-local coordinates, so parent, group, and nested-composition transforms are applied after path evaluation rather than baked into or destructively altering the curve.

Enable **Graph Editor** from the timeline footer to edit temporal interpolation directly. **Value Graph** displays the final animated property value; **Speed Graph** displays the derivative used by the final animation. Each keyframe supports independent incoming/outgoing influence and speed with Linear, Hold, Auto Bezier, Continuous Bezier, and Manual Bezier modes. Drag square velocity handles to shape the curve, Alt-drag to break only that temporal pair, use marquee or Shift selection for relative multi-edit, and open **Keyframe Velocity…** for exact numbers. Easy Ease, Easy Ease In, and Easy Ease Out are available from both the graph and ordinary keyframe context menus. Fit Graphs and Fit Selection frame both the value range and keyframe time range; Ctrl-wheel zooms time, Alt-wheel zooms values, Shift-wheel pans horizontally, and middle-drag pans freely.

When a layer with animated Position is selected with the Selection tool, the canvas shows its final transformed motion path, keyframe vertices, the selected vertex's incoming/outgoing handles, the current evaluated position, and a direction arrow. Drag a diamond vertex to edit its Position keyframe, drag a round handle to edit the tangent, hold **Shift** for 45-degree angle steps, or **Alt-drag** a handle to break only that tangent pair. Double-click a path segment to insert a keyframe on the existing curve. Right-click a vertex or path for Linear, Auto Bezier, Continuous Bezier, Manual Bezier, Rove Across Time, Break Tangents, and Join Tangents. Motion vertices snap to guides and other keyframe positions; holding **Ctrl** temporarily disables snapping. Locked layers and inline text-edit mode keep the editable handles hidden.

## 3D layers, cameras, and views

The 2D/3D layer toggle is opt-in. Pure 2D layers keep the legacy affine path, while promoted layers use Position, Scale and Anchor XYZ plus Rotation and Orientation XYZ. Orientation establishes the base local axes and Rotation XYZ is applied inside that basis. All authored Transform values are stored and keyframed in layer-local coordinates relative to the effective group or transform parent. Local, Parent and World affect gizmo-axis orientation only.

Title cameras support perspective and orthographic projection, Position, Point of Interest, Rotation, Orientation, focal length, field of view, zoom, near/far clipping and active-camera selection. Every animated camera property has a diamond in the 3D Camera inspector; Position, Target, Orientation and Rotation use one grouped XYZ diamond, while Projection creates discrete Hold keys. The editor provides Active Camera, Front, Back, Left, Right, Top, Bottom and Custom Perspective views. Editor view navigation is never serialized or included in title cache identity.

Move, Rotate and Scale gizmos support axis and plane handles, snapping, multi-selection and Local/Parent/World orientation. Rotate rings use the layer's current scale-free local basis, the effective parent's scale-free basis, or canonical world XYZ respectively. After a ring is hit, the first three pixels lock horizontal or vertical screen-space scrubbing; right/up increases and left/down decreases the angle at 0.5° per pixel. Because drag magnitude no longer comes from projected ring geometry, fully edge-on rings remain interactive. Hover highlights the exact axis, plane or ring that will receive the click. Projected gizmo geometry is shared by hit-testing and painting to avoid duplicate work.

## Camera Timeline and Vector3 channels

Cameras appear as first-class Timeline rows. The implicit default camera is hidden until its first camera-property keyframe is authored and is hidden again after its final keyframe is removed. Camera switching, Camera Switches rows, camera assignment and projection switching are discrete Hold tracks; continuous camera properties use ordinary temporal interpolation. Every property track—layer, material, effect, camera, light, assignment or switch—appears in the Layer List/Timeline only while it owns keyframes. Static cameras and unkeyed assignments retain the same render/cache behavior as the pre-animation 3D pipeline.

Layer List, Timeline and Graph Editor consume the same flattened row model. Expanding a Vector3 or four-channel property reveals X/Y/Z/W or A/R/G/B child rows on both sides. Aggregate rows target all channels, child rows target one component, and checked channel toggles use the same color as the rendered curve.

Graph keyframes move at sub-frame precision by default. Ctrl/Command enables explicit project-frame snapping. Double-clicking an empty property row creates a keyframe; double-clicking an existing temporal key opens Keyframe Velocity. Copy, cut, paste and delete are shared by Timeline and Graph Editor, and paste at an occupied time replaces the existing key instead of creating a duplicate.

Undo/Redo restores the authored title atomically and rebinds every Properties and Effects control to the restored layer objects in the same UI event, including current values and keyframe diamonds.

## Full XYZ spatial motion paths

Temporal and spatial interpolation are independent. A 3D Position keyframe stores one XYZ value plus incoming/outgoing XYZ tangents, interpolation mode and optional roving state. Paths are stored in parent space, evaluated through the complete parent hierarchy and projected through the selected camera for display.

Dragging a path point or handle reverses that process: the canvas point is unprojected to the working plane and transformed back into parent space. Segment insertion splits the cubic curve without changing its shape. Frame Selected includes layer geometry, keyframes and sampled spatial segments, including off-axis Z motion.

## Keyframe-safe parenting and grouping

Grouping, ungrouping, adding/removing from a group, changing Transform Parent and deleting a parent use one static parent-bind matrix evaluated at the edit playhead. The matrix preserves the visible world transform at that moment while leaving the authored Position, Scale, Rotation, Orientation, easing, tangents, roving metadata and keyframe count unchanged. It does not generate a keyframe for every project frame.

The bind is evaluated consistently by Canvas manipulation, 2D compatibility rendering, projected 3D rendering, masks, mattes, effects, motion blur and overlays. It is serialized, validated, included in visual/cache identity and disabled safely if malformed.

## Editor interaction and frame pacing

Canvas, Layer List, Timeline and Graph Editor use one guarded selection-synchronization path. Right-clicking a normal Timeline layer row opens the same multi-layer menu as the Canvas while keyframe, transition, property and camera rows keep specialized menus. A direct keyframe handle is required for spatial-keyframe actions; a motion-path segment otherwise falls through to the layer menu.

Playback cadence uses fractional millisecond accumulation for project rates such as 59.94 or 29.97 fps. Diagnostics publish one elapsed-time sample per second: FPS counts successfully presented frames, and average render time covers the frames rendered in the same interval. Monitor-rate interaction timers run only during active pointer gestures, while stopped clocks/tickers refresh at a bounded rate.

The playback hot path is split by cost. Canvas presentation remains project-frame driven; visible Timeline/timecode feedback is capped to the lower of monitor cadence and 30 Hz; heavyweight Layer, Graphic Properties, Layer Properties, Effects, Playback/Cache and sidebar evaluation is capped at 10 Hz while transport is running. Hidden docks are not reevaluated. Scrubbing, frame stepping and the final stopped frame remain exact rather than throttled.

Passive no-button pointer movement over ordinary inspector controls is coalesced to 30 Hz during playback so high-polling-rate mice cannot starve the GUI render queue. Canvas and Timeline input, button presses, drags, wheel events, Enter/Leave state and controls marked with `fxmContinuousPointerDuringPlayback` retain continuous delivery. New editor controls should not request continuous pointer traffic unless their interaction semantics require it.

Editor-side auxiliary work must remain state-driven: the private monitored-audio source exists only for titles with actual Audio layers (including nested Assets), title snapshots are republished only after a model/audio change or transport discontinuity, hidden meters suspend their timers, and setters/log statements on frame-adjacent paths must avoid work when their value or log level is unchanged. These are performance contracts, not optional micro-optimizations; adding a new per-frame panel update or precise timer requires profiling against playback plus high-rate pointer movement.

Editor tabs intentionally use text-only labels. Dock drop guides include upper, center, lower-left, and lower-right targets; the two lower targets allow common inspector/timeline arrangements without requiring manual split reconstruction.

## Assets and libraries

A saved title can become a reusable Asset Layer. Assets may be synchronized to the parent playhead or independent when their animation mode supports it. Animated assets use a stable bounds envelope covering the full authored motion rather than resizing the selection box every frame.

The Libraries dock contains assets, style presets, gradients, effects, and transitions. Editing an asset opens its source composition after offering to save or discard unsaved work in the current title.

## Undo, persistence, and selection

Editor operations should enter the shared undo history rather than modifying the model silently. Selection-only changes must not trigger expensive rerenders. Saved titles persist authored content; editor-only panel order, panel collapse state, and presentation preferences persist separately as UI settings.

## Stinger authoring

Stinger is a document playback mode and a native OBS transition type. Its timeline shows pre-roll, animation time, the switch point, and post-roll without changing the authored layer/keyframe time domain.

- **Switch at Point** renders the title animation and performs one Scene A → Scene B switch at the configured point. Editor-only Scene A/B backgrounds help preview the cut and never enter source output or cached frames.
- **Manual Scene Animation** adds persistent Scene A and Scene B input layers. They use the ordinary visual-layer contract for Size, transform, opacity, timing, transitions, hierarchy, masks, mattes, blend modes, and effects.
- Proxy readiness can be required or allowed to fall back to safe live rendering. Native OBS transition preview and on-air playback use the same transition callbacks.
