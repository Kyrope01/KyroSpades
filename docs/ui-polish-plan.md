# Menu UI polish
The first section covers the initial polish pass. [Phase 2](#phase-2--menu-motion-and-smooth-scrolling) records the animation and scrolling implementation.

The aim is to make KyroSpades look less boxy and feel more responsive without
moving controls, replacing the backdrop or fonts, changing the configured accent
hue, or redesigning the in-match HUD.

1. **Shared surfaces:** Use small, UI-scale-aware radii (larger on windows and
   panels, smaller on controls). Round the top corners of title bars only so
   they still meet the window body. Keep outlines thin and inside the bounds;
   keep raw rectangles available for selection, carets, overlays and meters.
2. **Interaction:** Separate resting, hovered and pressed colors using the
   existing accent. Add a small press/recovery movement to framed buttons.
   Drive it by elapsed time, scope it to the clicked control ID and menu
   context, and leave decorative/flat rows and drag controls stationary. Do not
   change submit timing, hit testing, focus, scrolling, or touch target sizes.
3. **Menu details:** Apply the same surface treatment to selected server rows
   and hovered settings rows without changing their full-row behavior. Keep
   existing special states such as pinning, status colors and selected skins.
4. **Rendering:** Keep each rounded surface to one UI command. Clip the
   original contour, not a newly rounded intersection, and send non-overlapping
   solid strips through the existing 2D batch. Flush before clip/blend changes.
   Avoid new shaders, platform-specific APIs or assets.
5. **Verification:** Exercise rounded clipping, tiny controls, borders, and
   button press/release timing in a headless C harness. Run existing HUD/input
   regression checks. On a graphics-capable build, inspect server list scroll
   edges, settings/sliders and text editing, titled dialogs, skins, and scaled
   touch menus. Compare desktop and mobile and confirm no gameplay HUD changes.

## Phase 2 — Menu motion and smooth scrolling

### What opening Settings should feel like

Clicking **Settings** still switches screens immediately. The background and
main navigation stay planted, so the new screen feels like part of the same
menu rather than a pop-up. The Settings panel enters first; its controls arrive
in small groups from top to bottom. No loading pause, zoom, bounce, or blank
frame. These are starting values to tune in the running game:

| Group | Start | Duration | Travel (UI-scaled px) | Starting opacity |
| --- | ---: | ---: | ---: | ---: |
| Main navigation and backdrop | — | — | 0 | 100% |
| Settings content panel | 0 ms | 160 ms | 4 px up | 75% |
| Search label, field and Clear button | 12 ms | 155 ms | 6 px up | 65% |
| Category list | 24 ms | 160 ms | 7 px up | 60% |
| Settings rows and section headers | 36 ms + 12 ms × group index (cap index at 4) | 155 ms | 8 px up | 55% |

“Up” means starting below the resting position. Group rows in pairs or by
section where appropriate. The last group settles by roughly **240 ms**; do
not wait for offscreen rows, and do not play a new entrance as rows scroll into
view. Move each label, field, value, and row highlight together. Keep the
category and row panel boundaries fixed as clipping surfaces so nothing slides
across the search bar or neighboring column. Use cubic ease-out for movement
and opacity; combine opacity with the existing color alpha without changing
accent RGB. Keep all groups readable from the first frame.

### Wiring and interaction rules

1. **Settings first.** Start a per-HUD entrance timestamp when navigation
   actually enters the screen (`hud_change`); use the same frame clock as the
   existing click animation. Use an explicit active flag so time zero works.
   Re-selecting the current tab, refreshing, resizing, or changing the accent
   must not restart it. A different HUD switch cancels the old entrance. Preserve
   the existing deferred navigation and settings-commit order.
2. **Animate drawing, not layout.** Add a scoped motion-group marker or compact
   group metadata to microui command generation. Tag the panel, search,
   categories, and row ranges in `hud_settings_render`; sample their animations
   once per frame. Replay alpha and y-offset consistently for text, icons,
   rectangular highlights, and rounded surfaces. Leave logical rectangles,
   IDs, tab/focus behavior, panel sizes, content size, and hitboxes unchanged.
   Keep parent viewport clipping stationary; translate widget-local clips with
   their widgets and intersect them with the fixed viewport. Verify rounded
   surfaces explicitly: their CPU clipping needs to use the transformed shape
   and the correct clip, not an already cropped contour shifted afterward.
3. **Don't make users wait.** On a pointer-down or touch-down in the entering
   screen, wheel/scroll gesture, focus-driving key, or text input, snap the
   entrance to its final pose *before* handling the event so rendered targets
   and hitboxes agree. Do not drop or replay that input. Keep textbox caret,
   selection, and IME ownership unchanged. Escape, Back, and gameplay transitions
   remain immediate; don't animate the outgoing screen over the game.
4. **Smaller transitions after entry.** Category changes get a local 120–140 ms
   reveal of the new rows; the category column and search stay still. If needed,
   preserve each category's current scroll policy first, then make any scroll
   restoration a separate decision. Tree expansion can reveal the new children
   locally; do not animate row heights or shift controls in use. Don't stagger
   search results on keystrokes: apply results immediately to avoid flicker.
   Modals can get a separate short fade later without delaying click capture.
5. **Roll out in order.** After Settings, use the same primitives for the server
   browser, Controls, and Macros. Do not replay on asynchronous list updates.
   Skins comes last: its 3D previews render after the microui pass and use their
   own scissors. Keep preview cells stationary initially or transform the
   models, labels and scissors together; don't move the tiles alone.
6. **Reduced motion.** Add a persistent HUD/UI setting covering entrances,
   press travel and eased scrolling. Reduced motion removes travel and stagger,
   snaps scroll to its destination and retains color-only interaction feedback.
   Toggling it mid-animation settles immediately. No menu effects in gameplay.

### Scrolling: smooth wheel, direct dragging

- **Separate current and target position per container and axis.** Keep
  fractional positions internally and use one rounded displayed position for
  layout, hit tests, thumb, and any overlays. At `mu_end`, route wheel steps
  to the same eligible hovered scroll target as today; only update its target.
  Advance that container's displayed scroll before building its content the
  next frame. Keep the other containers independent. No global scroll velocity.
- **Measure behavior by time, not by frames.** Start with exponential easing
  (`current += (target - current) * (1 - exp(-dt / 0.045))`); approximately 95%
  of travel is covered in 135 ms. Snap within half a pixel. Cap the distance
  pending from a wheel burst to about one viewport so reversing is responsive;
  opposite input drops the pending distance in the old direction. Clamp current
  and target to bounds whenever content or viewport size changes, including an
  empty search or shorter list. No overscroll, and settle after a long suspension
  instead of replaying old movement. Preserve the current wheel distance and
  direction initially; track fractional/high-resolution deltas where available.
- **Keep direct inputs direct.** Scrollbar drag cancels pending easing and
  updates both positions immediately. Do the same for touch pan so content
  follows the finger. SDL touch currently feeds the same microui scroll API as
  the wheel, so add an explicit direct-input path rather than easing both by
  accident. Ship touch without inertial fling first. If a user presses a row,
  slider or field while its panel is coasting, stop at the *currently drawn*
  position, not the unseen target, before hit-testing that press.
- **Audit existing writes.** Any programmatic scroll reset or jump should set
  both positions. Preserve nested panel routing, existing horizontal-axis
  behavior, and the skins preview's use of the displayed position. Don't
  silently change scroll rules for the in-game scoreboard or chat.

### Delivery and acceptance checks

| Milestone | Done when |
| --- | --- |
| A. Settings entrance | The shell stays still; panels and controls settle in under 250 ms. Quick navigation away and back never leaks commands or leaves stale transforms. |
| B. Wheel smoothing | One wheel notch travels the old distance and settles in about 150–200 ms; rapid notches, reversal, nested panels, end stops, resize and filtering behave predictably. Thumb, content and click targets stay aligned. |
| C. Input and accessibility | A click during entry acts once at the correct target; a click during coast acts on the visible row. Thumb drag and touch pan stay direct. Search, text selection, IME and Escape work at any point. Reduced motion removes movement immediately. |
| D. Rollout | Same motion language on the other menus; no animated refresh flicker; skins previews and scissors remain paired; gameplay HUD is unaffected. |

Add deterministic tests with explicit timestamps for easing and stagger caps,
input interruption, wheel reversal and clamping, direct-drag priority, and
state reset on panel reuse. Test identical sequences at 30, 60, and 144 Hz and
with a simulated long frame. Extend the headless command tests to check alpha,
fixed panel scissors, and transformed widget clips. Then visually check desktop
and Android at several UI scales: click Settings, scroll while it opens, drag a
slider, fling the mouse wheel and click a visible result, type a search, change
category, and quickly return to the browser. Verify no offscreen UI draws into
adjacent panels, no additional full-screen render pass is introduced, and menu
frame time stays comparable on a long server list.

### Implementation notes

- Settings has the full stagger. Category changes reveal the rows and reset
  the results pane to the top; search changes also reset the pane but do not
  replay the entrance. Expanding a section reveals only its children.
- Server browser, Controls, Macros, Demos, Recording and Replay use a shorter
  two-part entrance. Skins animates its surrounding controls only, keeping its
  separately rendered model grid and scissors stationary during entry. Chat
  Log retains its existing scroll behavior and programmatic jump semantics.
- **Settings → HUD/UI → Reduce menu motion** persists in the client config.
  There is no outgoing-screen transition, modal animation, or touch fling in
  this pass. Direct touch pans appear on the next layout pass without easing.
- `python3 tests/ui_motion_test.py` exercises the real microui command and
  scroll paths without requiring a graphical game build.
