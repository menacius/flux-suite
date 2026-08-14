# Text and live data

## Rich-text model

Text layers use a run-based rich-text model as the single source of truth. Manual formatting, auto styling, the inline editor, property panels, serialization, and rendering read and write the same runs rather than converting between an independent HTML model and legacy overrides.

Character properties include font, size, fill, stroke, horizontal and vertical scale, tracking, baseline, capitalization, and style. Paragraph properties include alignment, justification, indentation, spacing before/after, vertical alignment, wrapping, and line breaks.

## Inline editing

Clicking a text layer can enter on-canvas editing. Cursor movement and character insertion preserve the active run style and caret position. Escape or switching tools leaves text-edit mode. Multi-style selections display mixed/blank property values when no single value is authoritative.

## Auto text styling

Auto-style rules apply formatting to ranges selected by conditions such as text start/end, paragraph start/end, whitespace, newline, custom characters, word counts, and character counts. Rules can combine conditions, exclude another rule, include or omit stop characters, and avoid applying when a required stop condition is absent.

Live text cues apply auto styling when values are committed, so dock-driven text uses the same formatting logic as editor-authored text.

## Clock and ticker layers

Clock and ticker content is generated at runtime but uses the same text layout and property model. Tickers support wrapping, vertical/horizontal modes, paragraph formatting, custom completion, and independent playback where applicable. Clock and non-cacheable ticker modes remain real-time and are excluded from ordinary frame prerendering.

## Exposed properties and cues

Text and image properties can be exposed to the Titles and Graphics dock. Cue rows store user-facing values and are applied as snapshots. The currently active cue keeps its applied snapshot even when the row is edited; updated values become active on the next cue.

Cue states include inactive, queued, active, and ending/outro. Runtime counters can show elapsed or remaining time according to playback mode. Uncue continues from the current frame and reaches the authored end before applying the configured end behavior.

Cue and uncue publication is runtime state, not an authoring edit. Cache eligibility is evaluated against the requested cue snapshot and playback state before a cached frame is accepted. The title store returns shared ownership without holding a lifetime mutex for the duration of editor access; source activation and OBS shutdown therefore cannot wait on an editor-held title lease. If the editor briefly owns the model lock, the real-time source path retains its last valid presentation rather than blocking the OBS graphics thread.

## External data

External data now has a provider-neutral core separate from the existing cue import/append workflow. A title can serialize named source definitions and typed fields, while `ExternalDataManager` owns runtime current values, source/field timestamps, and connection or error state. Current provider values are deliberately not written into title JSON.

Supported field types are string, integer, float, boolean, color, date/time, image/file path, and URL. A layer stores optional bindings by canonical property path. Each binding contains a source ID, field path, optional formatter, and optional binding fallback. The effective value is resolved in this order:

1. Current field value while the source is connected/updating, or the retained last-known value when that policy is enabled.
2. Binding fallback.
3. Field default.
4. The layer's authored property value.

This keeps authored content unchanged when live data arrives or a source disconnects. External updates increment only a runtime presentation revision, do not create undo commands, and do not schedule title persistence. Repeated values update receipt timestamps but do not enqueue render work, dirty the source, or change cache identity.

The initial renderer integration supports `text.content` and `image.path`. Text bindings update the editor and OBS output through a transient rich-text document, so the saved text and its authored formatting model remain intact. Image bindings use the same effective path in compatibility rendering, GPU upload paths, effects, and cache hashing. Generic string, number, boolean, and color resolvers are available for additional property paths.

### Providers

The provider layer implements a common `IExternalDataProvider` interface and supports:

- **JSON file** — nested object paths, array indexes such as `items[0].headline`, optional root paths, and periodic refresh.
- **CSV file** — selectable data row, optional first-row headers, and explicit `field.path=column` mapping.
- **HTTP/HTTPS JSON** — asynchronous requests, custom headers, optional bearer token, timeout, retries with backoff, and configurable polling.
- **WebSocket** — asynchronous JSON message parsing, automatic reconnect with bounded exponential delay, and last-known-value retention.
- **Local text file** — publishes the complete file contents to a configurable field path.
- **Manual/internal table** — typed values stored with the source definition and published through the same runtime path.

All provider objects and their timers live on a dedicated external-data worker thread. They never perform file or network work on the Qt UI thread or OBS render thread. Incoming values are rate-limited and coalesced by field before entering the manager's existing coalescing render queue. HTTP refreshes also coalesce overlapping requests.

Provider states are `Connected`, `Updating`, `Disconnected`, `Error`, and `Stale` (with `Connecting` retained for transition reporting). Provider responses automatically discover scalar JSON paths (including array indexes) and CSV columns. Discovered fields are immediately selectable in every binding popup; no manual schema entry is required. The settings dialog exposes source/provider options, an optional **Fields** override tab, manual values, and a **Bindings** tab for mapping fields to eligible text, clock, ticker, and image properties. It also displays the current state, last update time, and error message. Bindings are committed only when the dialog is saved, so Cancel never alters authored layer properties. Errors do not interrupt rendering. When **Keep last valid values** is enabled, the most recent valid field value remains effective through stale, error, reconnect, or temporary disconnect states; otherwise the binding fallback, field default, or authored value is used.

Informative state changes that cannot alter the effective value are recorded for the UI without incrementing the render revision. This prevents routine polling of unchanged data from dirtying previews or invalidating caches. `update_mock_value()` remains available as a provider-free test and internal-integration path.
### Populate Live Text Cues from a provider table

After refreshing a JSON, CSV, HTTP JSON, WebSocket, local-text, or manual source, open the **Data Sources** menu in the Live Text Cues toolbar and choose **Populate from external table…**. Select the discovered table or JSON array, then map each exposed cue column to a table field. No per-cell setup or manual field registration is required.

The mapper provides three update modes:

- **Replace rows** — the selected provider table becomes the cue list.
- **Append rows** — previously created cue rows remain and newly discovered source rows are added.
- **Synchronize rows** — source-managed rows are added, updated, reordered, or removed to match the provider snapshot; manually authored rows can be preserved.

Choose an optional stable **Row ID field** when the source contains an ID. Otherwise the provider row index is used. A start row, maximum row count, empty-row filter, formatter, fallback, and live result preview are available. Each generated cell is stored as a normal external binding, so editor preview, cue playback, OBS output, last-known-value handling, and visual binding indicators all use the same runtime path. An explicit cell binding can override a generated table cell without changing the table mapping.

Table-managed cue values use the explicit **ExternalTableManaged** cell state. They are shown in *italics* and are read-only inside the Live Text Cues table, while cue playback and OBS output remain unchanged. Unmapped cells in the same row remain normal authored cells. Right-click a managed cell and choose **Convert to editable value** to capture its current formatted value as an authored snapshot; later provider refreshes will preserve that detached cell. Choose **Restore table-managed value** to reconnect it to the row mapping. The table snapshot itself is authoritative: if a provider does not expose an identical row-specific scalar field key, the generated binding carries a transient runtime value so the cue still renders correctly while preserving authored storage and fallback rules.

### Binding UI and formatter pipeline

External Data Source Settings provides a complete source workflow: create, remove, or duplicate a source; choose the provider type; configure file/network/CSV/WebSocket options; test the connection; refresh manually; and inspect every discovered or pinned field's current value, type, timestamp, connection state, schema status, and error. Each source selects one refresh behavior: **Refresh on cue**, **Refresh continuously**, or **Refresh manually**. Polling is active only for continuous sources. The **Fields (optional)** tab is only for pinning offline schema, aliases, type overrides, CSV/custom mappings, and manual values. Selecting a discovered field in any binding automatically pins it. Unchecking or removing that override returns the field to provider-inferred discovery while retaining its last valid runtime value.

Bindable text and image properties display a chain-style data button. A highlighted button and a `D`/`FX•` layer-list badge identify externally bound content. The binding popup selects the source and field, accepts a typed fallback, and previews the raw value, formatted value, value origin, provider state, error, and update timestamp before Save. Live text and image cue cells expose the same popup from their context menu and show an orange bound-state border.

The structured formatter pipeline is shared by editor preview, live cue playback, cache identity, and OBS source rendering. Operations are applied deterministically: legacy formatter compatibility, empty-value policy, conditional replacement, date/time or numeric formatting, text case, then prefix/suffix. Supported controls are prefix, suffix, decimal places, thousands separators, uppercase/lowercase/title case, `strftime` date/time patterns, ordered exact conditional replacements with optional case sensitivity, and empty handling (keep empty, use fallback/default/authored, or replace).

Cue-cell bindings are saved by stable cue-row ID and layer ID. Cueing installs a runtime-only binding on the exposed layer, so subsequent external updates continue to reach the live output while the authored layer text, authored cue value, and undo history remain unchanged. A source configured for **Refresh on cue** is asked to refresh asynchronously immediately before the cue is applied; playback never waits for network or file I/O and uses the current last-known/fallback resolution until a new value arrives.

### External-data diagnostics logging

For provider or Live Text Cue troubleshooting, open **Flux Motion Preferences → Logging** and enable logging. Select **Debug** for lifecycle, refresh, state, mapping, and managed-cell population events; select **Trace** when field-level update suppression, render-queue coalescing, or every cue-cell resolution must be inspected. Ensure the **External data** category is enabled. The current session file path is shown in the Logging preferences page, and optional mirroring to the OBS log remains controlled by the global logging setting.

External-data messages include a `component=` tag such as `Provider`, `Http`, `WebSocket`, `Manager`, `TableMapping`, `Dock`, or `DockCell`. Provider locations are sanitized: URL user information, query strings, and fragments are omitted. Authentication tokens and request-header values are never logged. External values are represented by their type, set/empty state, byte length, and a deterministic fingerprint rather than their raw content. Matching fingerprints identify unchanged data across stages without exposing the value.

A useful debugging sequence is: open External Data Source Settings, press **Test connection** or **Refresh now**, apply the table mapping, and rebuild the Live Text Cues view. At Debug level the log then shows provider state transitions, parsed/published counts, table snapshot changes, mapping row counts, and each `ExternalTableManaged` cell as it is inserted into the dock. This makes it possible to distinguish provider parsing failures from mapping, normalization, binding-resolution, or final widget-population failures.
For cue switching, the `CueControl`, `CuePlayback`, and `CueApply` components show the requested/current/pending row, playback mode, transition phase, source-side commit, and a privacy-safe fingerprint of the resolved value. In Loop and Pause modes the next row is committed after the outgoing segment; the source resolves table-managed values at that exact commit rather than reading the empty authored placeholder.

---

## Unified Text Animators

Text Animators are the renderer-neutral text-animation model used by Flux Motion. A text layer owns an ordered stack of animators. Each animator combines editable animated properties with one or more selectors and is evaluated against the immutable shaped text layout shared by the editor and the OBS source renderer.

## Model and stack order

A text animator contains:

- a stable identifier, editable name, enable state, and expanded state;
- an Add, Replace, or Multiply composition mode;
- a default granularity;
- a text-change policy;
- animated properties;
- ordered selectors;
- an optional built-in/custom preset identifier and schema version.

Animators are evaluated from top to bottom. Multiple animators may affect the same shaped cluster. Selectors are evaluated in their visible order and combine with Add, Subtract, Intersect, Difference, Minimum, Maximum, or Multiply. The resulting influence is applied to every enabled property in the animator.

The Properties panel and timeline do not hold separate copies. Both expose the same `AnimatedProperty` objects, including static values, keyframes, interpolation modes, easing, and manual temporal handles.

## Text units and Unicode

Selection is resolved from the shaped `TextLayoutData`, not from UTF-8 bytes or UTF-16 code units. The unit map supports:

- grapheme clusters;
- visible characters;
- characters excluding spaces;
- words;
- shaped lines;
- paragraphs;
- rich-text paint runs;
- the complete text layer;
- legacy-compatible shaped sentences.

Cluster mappings retain canonical UTF-8 byte ranges for exact-text, regular-expression, external-data, newly-added, and changed-text selectors. Emoji ZWJ sequences, combining marks, Greek, RTL runs, and other complex shaped clusters remain indivisible when the shaping pipeline reports them as one cluster.

## Properties

The model stores transform, character-formatting, paragraph/layout, and visual properties. Transform-only properties are applied to batched glyph geometry without reshaping. Fill/stroke colour, opacity, reveal, SDF blur, and stroke-width changes are carried in the same glyph vertex batches.

Properties that can change line breaking or glyph placement are explicitly marked layout-affecting. Tracking currently uses a shaped-line post-pass when it can preserve the existing layout. Font-size and scale changes can use glyph-quad scaling when a full relayout is not required. Future layout-path work must continue to use the same animator model rather than introducing a second implementation.

## Selectors

### Range Selector

Range selectors support percentage/index units, Start, End, Offset, Amount, Square/Ramp/Triangle/Round/Smooth shapes, Ease High/Low, Smoothness, deterministic random order, seed, direction, and inversion. Meaningful numeric fields are ordinary timeline properties.

### Procedural Selector

Procedural modes include Random, Noise, Wave, Sine, Sawtooth, Pulse, Alternating, distances from start/end/centre, and distance from a custom index. Seeded modes are deterministic during playback, scrubbing, caching, and prerendering.

### Text-based Selector

Text-based selection supports character/word/line/paragraph ranges, exact text, regular expressions, whitespace, numbers, uppercase, lowercase, punctuation, rich-text runs, external-data byte ranges, newly-added text, and changed text.

### Wiggly Selector

The Wiggly selector uses amount, frequency, correlation, temporal/spatial phase, seed, minimum/maximum influence, dimension locking, and optional per-character values. A supplied seed produces repeatable output.

### Staggered Selector

The Staggered selector is the generic selector used by the historical FXM text transitions. It stores a keyframeable Completion track, stagger percentage, entrance/exit mode, unit easing, order/direction, deterministic random seed, and whitespace policy. Its evaluation reproduces the historical two-stage easing contract: the authored completion curve is evaluated first, followed by the same per-unit easing after the stagger delay. It is not a preset-specific renderer and can be edited or reused by custom animators.

## Timeline and keyframes

Animator and selector properties are published through `TimelinePropertyRef`. They therefore use the existing timeline implementation for:

- Linear, Hold, and Bezier interpolation;
- manual temporal velocity/influence handles;
- Value and Speed Graph views;
- copy/paste and multi-keyframe selection;
- Easy Ease commands;
- undo/redo and property reset;
- frame-accurate evaluation at project time.

Timeline labels use the form `Text › <Animator> › <Property or Selector> › <Component>`.

## Presets

A preset is serialized generic animator data, never a renderer command. Applying a preset creates editable properties, selectors, and keyframes.

Standalone presets use the `.fxmtextanim` extension. They contain metadata, schema version, the complete animator, selector configuration, seeds, and all temporal keyframe fields. Imported structures receive fresh stable IDs so they cannot collide with tracks already present on the layer.

### Legacy migration mapping

The legacy runtime presets present in development version 133 map as follows:

| Existing identifier | New properties | New selector |
| --- | --- | --- |
| `text.fade` | Opacity | Staggered Selector |
| `text.slide-in` | Position, Opacity | Staggered Selector |
| `text.scale` | Scale, Opacity | Staggered Selector + shared unit transform origin |
| `text.blur` | Blur, Opacity | Staggered Selector |
| `text.wipe` | Character Reveal, Opacity | Staggered Selector + directional shaped-unit clipping |
| `text.blur-slide-in` | Position, Blur, Opacity | Staggered Selector |

Legacy entrance/exit edge, duration, easing, direction, offset, scale-from, blur amount, character/word/sentence unit, order, and stagger are converted to layer-local animator data. The transition descriptor remains as authoring metadata so the existing timeline handles and Transition Editor keep working, but it is never executed by a renderer. A `transition_managed` animator bound by stable transition ID is the sole runtime implementation.

The binding stores a signature of the authored descriptor and its effective layer-local timing. Editing duration, direction, unit, easing, stagger, or edge timing rebuilds the bound animator; unrelated refresh, trim, save, or reload operations do not overwrite manual edits to its generated properties, selectors, or keyframes. Duplicating a managed animator detaches the copy as a normal custom animator. Deleting the managed animator also removes its authoring descriptor, preventing it from being recreated on reload.

Projects saved by intermediate Development Versions 134/135, which could contain generated animators without retained timeline descriptors, are detected and repaired deterministically. Converted projects save the current schema on the next save; legacy deserialization remains only as an import/conversion layer. General layer transitions remain untouched.

## Dynamic text, Live Text Cues, clocks, and external data

When source text changes, FXM compares shaped clusters, preserves stable prefix/suffix and LCS matches, and generates canonical byte ranges for additions and changes. Large strings use a bounded deterministic mapping instead of an unbounded quadratic diff.

Each animator can restart, continue local time, preserve completion, animate newly-added text, animate changed text, select removed text where retained geometry exists, or re-evaluate the complete text. Old cluster indices are never reused blindly after a new layout is created.

Auto Text Styling and rich-text shaping must finish before selector evaluation so style-run and line mappings match the rendered layout.

## Rendering, cache, and determinism

The shared evaluator produces one state per shaped cluster. The GPU renderer fans that state out to glyph quads and keeps batching by atlas/material page; it does not invoke one full text pass per character.

Wipe transitions clip glyph quads against shaped character/word/sentence bounds before applying the common transform. Word/sentence transforms can opt into a shared shaped-unit origin so Scale Text behaves like the historical grouped unit instead of shrinking every glyph around its own centre. Blur Text uses a bounded multi-sample SDF blur for fill and stroke, contracting to the sharp source as selector influence reaches zero.

When an exact glyph cannot use the SDF atlas (for example color-font emoji, failed alpha-map extraction, oversized glyphs, or atlas exhaustion), the Qt compatibility raster consumes the same immutable layout and `TextAnimatorEvaluation`. It isolates shaped clusters where practical and uses a bounded flattened fallback for very long content. No `LayerTransition`-specific compatibility renderer remains.

Text Animator signatures include animator order, properties, keyframes, selectors, seeds, preset schema, and migration version. Time-dependent animators participate in frame cache keys. Changes invalidate the affected text layer/title path rather than unrelated titles, and migrated legacy cache output is incompatible with the new signature.

Static text layers bypass animator evaluation and keep the existing shaping/rendering path.

## Diagnostics

Migration logs identify the layer and preset conversion. Any unsupported legacy field emits one descriptive fallback warning with the project/title, layer, preset, parameter, and deterministic replacement behavior. Dynamic text remapping and cache invalidation are logged at event boundaries, never once per glyph or frame.

## Validation

The source tree contains deterministic tests for shaped unit maps, Greek and combining text, emoji ZWJ clusters, RTL runs, selector composition, seeded procedural output, multiple animators, legacy mapping, local timing, content-length changes, cache signatures, timeline discovery, and a 1,200-cluster/10-animator stress case. Full pixel-equivalence validation still requires a matching OBS/libobs/Qt runtime on Windows and Linux.

## Development Version 138 glyph-envelope and blur parity correction

Development Version 138 fixes two visual regressions introduced when transition-managed layers were routed through the conservative flattened compositor in version 137. Unified text transitions again use the isolated shaped-unit compositor first. Unit images are derived from actual alpha/ink bounds, keep transparent interpolation gutters, and render inside a font-, rich-text-, stroke-, and antialias-aware surface envelope. The flattened compositor remains only as a guarded fallback.

Text Animator blur no longer owns a transition-specific kernel. Blur Text and Blur Slide call the same premultiplied-pixel backend, blur-pass mapping, and support-radius calculation as the standard FXM Blur effect. This preserves the established radius-driven behavior and keeps editor/source/cache output on one implementation. The renderer cache ABI is advanced so frames created before this correction are invalidated.

## Development Version 137 runtime correction

Development Version 137 activates the unified text-transition runtime in the real OBS/editor source path. It repairs the split `.inc` translation-unit boundary left by version 136, reconstructs managed animators from authoritative transition descriptors at render time, and sends transition-managed text through the generic raster compositor until the per-glyph GPU path has completed visual validation. Both render routes consume the same shaped layout and `TextAnimatorEvaluation`; no legacy descriptor-specific evaluator is used.

## Development Version 136 status

Development Version 136 completes the migration and runtime parity of the six historical FXM text-transition types: Fade Text, Slide In/Out, Scale Text, Blur Text, Wipe Text, and Blur Slide In/Out. Their existing authoring surfaces, edge handles, duration editing, units, ordering, timing, easing, preview, cache participation, and save/reload behavior are retained, while output is evaluated exclusively by `TextAnimatorStack`.

The following broader Text Animator specification work remains outside this legacy-transition milestone:

- true relayout for every layout-affecting animator property, including animated font size, word/line/paragraph spacing, and wrapping changes;
- complete generic effects-stack animation for glow, shadow, and motion-blur sampling beyond the transition properties used by the historical presets;
- complete typewriter cursor/audio events, removed-character retained geometry, character replacement, and scramble rendering;
- the expanded entrance, exit, continuous, ticker, clock, and broadcast preset library with generated thumbnails, category management, search, restore, and visual fixtures;
- pixel-comparison fixtures and full Windows/Linux OBS/libobs/Qt editor/source/cache validation;
- final profiling of high-DPI output, multiple animated layers, external-data churn, masks/mattes, group transforms, and motion blur.

All future work must extend the shared animator, selector, shaped-layout, and renderer paths rather than restoring descriptor-specific transition execution.


## Development Version 139 compile correction

The glyph-envelope height hint introduced in version 138 now reads font size and vertical scale from effective `RichTextCharFormat` values at rich-text range boundaries. `TextLayoutPaintStyle` remains paint-only, matching the immutable text-layout architecture and fixing the MSVC compile failure without weakening animation overscan or mixed-style glyph protection.

## Canonical text-property and geometry contract

`RichTextDocument` is the single authored source for static text, defaults, sparse character ranges, paragraph blocks and typing format. Animated tracks are the only separate authored time-varying overlay. Scalar `Layer` fields are compatibility mirrors, `QTextDocument` is an input/IME adapter, and immutable layout/GPU data are derived state.

Every mutation carries a property mask. Overlapping sparse masks are normalized without deleting unrelated font, H/V Scale, tracking, baseline, fill, stroke or paragraph properties, so one text box can contain multiple independent property combinations.

### H/V Scale, alignment and selection

The shaping font remains at neutral width. Effective H Scale is applied after shaping to clusters, glyph positions, advances, cursor boundaries, selection geometry and clips. Effective V Scale transforms each vector glyph around its baseline and expands the line ink envelope. Glyphs are mapped back to canonical UTF-8 clusters even when Qt coalesces runs. Horizontal Fit, center/right alignment and all justify modes use the final post-scale geometry.

### Stroke composition and clipping

Stroke order is **Behind → Fill → Front**. Fill and stroke use separate clip geometry: fill remains limited to the authored ligature/style slice, while stroke receives its real outside coverage and a sampling guard. Text-box dimensions control wrapping and alignment; they are not an ink mask. Surface bounds include stroke, scale and animator overhang.

### Undo/Redo, Text Styles and auto-size

Committed property edits create one title-level transaction and rebuild the inline adapter from the restored canonical snapshot. Native local Undo/Redo remains for ordinary typing. Text Style editing embeds the same Properties implementation as text layers. A manual width/height bounding-box edit disables only the corresponding `size to text` axis in the same geometry transaction.
