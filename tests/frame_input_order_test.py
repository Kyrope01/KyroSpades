"""Input is read before it is used, and the frame-cap wait happens before that.

The main loop used to run: physics/draw -> sound/network -> swap -> poll ->
frame-cap sleep.  Input collected right after the swap therefore sat through
the rest of the sleep and a whole update+draw before anything used it, which is
about one frame of avoidable latency - 4-7 ms at a 144/240 Hz cap, worst in
mouse look.

This test pins the fixed order down two ways:

  * structurally, on the shipped sources: the pacing wait is the first thing in
    the frame, window_poll() comes before physics/camera/display,
    window_present() comes after everything that draws, the swap and the event
    pump live in separate functions per backend, and window_update() still
    composes them for any caller that wants the old order;
  * behaviourally, by compiling the real frame_pace_wait() against a simulated
    clock (sleep advances it, spinning advances it in small ticks) and checking
    it waits exactly up to the frame deadline and never past it.

Run: python3 tests/frame_input_order_test.py
"""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
main_src = (root / 'src' / 'main.c').read_text()
window_src = (root / 'src' / 'window.c').read_text()
window_h = (root / 'src' / 'window.h').read_text()

failures = []


def check(what, ok, detail=''):
    print('  %-62s %s' % (what, 'ok' if ok else 'FAIL'))
    if not ok:
        failures.append(what)
        if detail:
            print('      ' + detail)


def body_after(src, signature, start=0):
    """Source of the function starting at `signature`, up to its closing brace at
    column 0."""
    i = src.index(signature, start)
    j = src.index('\n}\n', i)
    return src[i:j]


# ── 1. the loop, in the shipped order ────────────────────────────────────────
loop = main_src[main_src.index('        while(!window_closed()) {'):]
loop = loop[:loop.index('fps = 1.0F / dt;')]

order = [
    ('frame-cap wait', 'frame_pace_wait('),
    ('input poll', 'window_poll()'),
    ('network service', 'network_service()'),
    ('physics tick', 'player_update(PHYSICS_STEP_TIME, 1)'),
    ('camera update', 'camera_update(step)'),
    ('draw', 'display();'),
    ('sound', 'sound_update();'),
    ('network flush', 'network_update();'),
    ('present', 'window_present();'),
]
positions = [(name, loop.index(tok)) for name, tok in order]
monotonic = all(positions[i][1] < positions[i + 1][1] for i in range(len(positions) - 1))
check('frame order: pace, poll, update, draw, present',
      monotonic,
      ' -> '.join('%s@%d' % p for p in positions))
check('loop no longer uses the swap-then-poll window_update()',
      'window_update()' not in loop)
check('pacing wait runs before the poll (not after the swap)',
      loop.index('frame_pace_wait(') < loop.index('window_poll()'))
check('screenshot readback stays between display() and present',
      loop.index('display();') < loop.index('screenshot_capture();') < loop.index('window_present();'))
check('frame profiler sample ends at the swap, not the wait',
      loop.index('window_present();') < loop.index('prof_frame_end();'))

# ── 2. window_present()/window_poll() are split per backend ──────────────────
sdl_start = window_src.index('#ifdef USE_SDL')

for backend, swap, pump, start in (
    ('GLFW', 'glfwSwapBuffers(', 'glfwPollEvents(', 0),
    ('SDL', 'SDL_GL_SwapWindow(', 'SDL_PollEvent(', sdl_start),
):
    present = body_after(window_src, 'void window_present() {', start)
    poll = body_after(window_src, 'void window_poll() {', start)
    check('%s: present only swaps' % backend,
          swap in present and pump not in present)
    check('%s: poll only pumps events' % backend,
          pump in poll and swap not in poll)

check('GLFW poll re-applies the cursor mode after focus gain',
      'window_reapply_mousemode();' in body_after(window_src, 'void window_poll() {', 0))
check('window_update() still composes present+poll for old callers',
      window_src.count('void window_update() {\n\twindow_present();\n\twindow_poll();\n}') == 2)
check('window.h declares both halves',
      all(d in window_h for d in ('void window_present(void);', 'void window_poll(void);')))

# ── 3. the real pacing helper against a simulated clock ─────────────────────
start = main_src.index('static double frame_pace_remaining(')
end = main_src.index('int main(int argc, char** argv) {', start)
pacing = main_src[start:end]
assert 'frame_pace_wait' in pacing and 'nanosleep' in pacing, \
    'frame_pace_wait() is no longer the wait helper; update this test'

harness = r'''
#include <math.h>
#include <stdio.h>
#include <time.h>

/* The extracted code only touches these. */
static struct { int vsync; int precise_pacing; } settings;

static double clock_now = 0.0;
static int sleep_calls = 0;
static double slept_total = 0.0;

/* Every clock read during the spin costs a little time, like the real loop. */
static double spin_cost = 0.000010;
double window_time(void);
double window_time(void) { double t = clock_now; clock_now += spin_cost; return t; }

#define nanosleep sleep_stub
static int sleep_stub(const struct timespec* ts, struct timespec* rem);
static int sleep_stub(const struct timespec* ts, struct timespec* rem) {
    (void)rem;
    double s = (double)ts->tv_sec + (double)ts->tv_nsec / 1e9;
    sleep_calls++;
    slept_total += s;
    clock_now += s;   /* the clock advances while we sleep */
    return 0;
}

Pacing_here

static int failures = 0;
static void expect(const char* what, int ok) {
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if(!ok) failures++;
}

static void reset(double now) {
    clock_now = now;
    sleep_calls = 0;
    slept_total = 0.0;
    spin_cost = 0.000010;
}

int main(void) {
    /* The arithmetic has to be exactly "start + 1/hz - now", clamped at 0. */
    expect("no cap (vsync 0) never waits",
           frame_pace_remaining(100.0, 100.0, 0) == 0.0);
    expect("vsync 1 is handled by the display, not the cap",
           frame_pace_remaining(100.0, 100.0 - 0.5, 1) == 0.0);
    expect("144 Hz asks for the whole 1/144 s at frame start",
           fabs(frame_pace_remaining(10.0, 10.0, 144) - 1.0 / 144.0) < 1e-12);
    expect("half-way through the slot it asks for the other half",
           fabs(frame_pace_remaining(10.0 + 0.5 / 144.0, 10.0, 144) - 0.5 / 144.0) < 1e-12);
    expect("a late frame never asks for a negative wait",
           frame_pace_remaining(10.0 + 0.02, 10.0, 144) == 0.0);
    expect("420 Hz slot is 2.38 ms",
           fabs(frame_pace_remaining(0.0, 0.0, 420) - 1.0 / 420.0) < 1e-12);

    /* Precise pacing: sleep coarsely, spin the last millisecond, land on the
       deadline without sailing past it. */
    settings.vsync = 144;
    settings.precise_pacing = 1;
    reset(0.0);
    frame_pace_wait(0.0);
    double deadline = 1.0 / 144.0;
    printf("  144 Hz precise: slept %.4f ms in %d sleeps, finished %.3f ms after the deadline\n",
           slept_total * 1000.0, sleep_calls, (clock_now - deadline) * 1000.0);
    expect("144 Hz precise: waited the full slot", clock_now >= deadline - 1e-9);
    expect("144 Hz precise: never overshot by more than 0.1 ms", clock_now - deadline < 0.0001);
    expect("144 Hz precise: actually slept (not a busy wait)", sleep_calls > 0);

    /* Non-precise mode: one blocking sleep of the whole remainder. */
    settings.precise_pacing = 0;
    reset(0.0);
    frame_pace_wait(0.0);
    expect("144 Hz coarse: slept once for roughly the whole slot",
           sleep_calls == 1 && fabs(slept_total - deadline) < 1e-9);

    /* Late frame: the helper must not sleep at all. */
    settings.precise_pacing = 1;
    reset(0.02);
    frame_pace_wait(0.0);
    expect("late frame sleeps zero times", sleep_calls == 0);

    /* A 240 Hz slot: half of 144's, still exact. */
    settings.vsync = 240;
    reset(0.0);
    frame_pace_wait(0.0);
    expect("240 Hz precise: landed on the 1/240 s deadline",
           clock_now >= 1.0 / 240.0 - 1e-9 && clock_now - 1.0 / 240.0 < 0.0001);

    /* No cap means no waiting whatsoever. */
    settings.vsync = 0;
    reset(0.0);
    frame_pace_wait(0.0);
    expect("vsync 0 never sleeps", sleep_calls == 0);

    printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
    return failures != 0;
}
'''

with tempfile.TemporaryDirectory() as d:
    c = Path(d) / 'frame_pacing.c'
    exe = Path(d) / 'frame_pacing'
    c.write_text(harness.replace('Pacing_here', pacing))
    subprocess.run(['cc', '-std=gnu99', '-O2', '-Wall', str(c), '-lm', '-o', str(exe)], check=True)
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=300)
    print(result.stdout, end='')
    if result.stderr:
        print(result.stderr, end='')
    if result.returncode != 0 or 'all checks passed' not in result.stdout:
        failures.append('pacing helper behaviour')

print()
if failures:
    raise SystemExit('frame input order tests failed: %s' % ', '.join(failures))
print('Frame input order tests passed')
