"""The browser demo against the plugin's harness: the same clip, the same raster,
the same settings, the same audio, two renderers.

    python3 demo/tools/crosscheck.py --rutest build/rutest [--keep DIR]

Needs a GPU, Google Chrome, numpy, a built rutest, and stoatworks-backend's
`release/cdpshot.py` (CDPSHOT=path overrides ~/Projects/infrastructure/
stoatworks-backend/release/cdpshot.py). Not run by tools/verify.sh.
(woofer's crosscheck, with Rubens' two plugins.)

Each case is rendered twice:

  - by the PAGE: demo/ served from here on a free port and loaded in headless
    Chrome on the real GPU (ANGLE on Metal) at ?size=WxH&clip=C, with
    &plugin=over for SW Rubens Over. The page is paused, the case's settings
    applied, its audio chosen, and Restart pressed: a new instance at time 0,
    primed. Then Step is pressed N times, one frame of 1/60 s each, so the page
    has fed 800 N samples of the audio at 48 kHz and its clock reads N/60. For
    the Over, Mix is then set to 0 (the plugin hands the clip through
    untouched, and nothing is fed while paused, so the flames stand still), the
    canvas is read back as the input frame, Mix is put back and the page's
    output is read back. The source reads no clip; it is handed a black frame.
    preserveDrawingBuffer is forced on from outside so the canvas can be read
    after its frame; nothing in the page is changed for this.
  - by the PLUGIN: `rutest --pipe --fps 60 --size WxH [--over] --set ...` (the
    real RubensPlugin in a GL 4.1 context, its clock preset to seconds), fed
    that input frame for every frame, with the same host values: every control
    as the page's own hostValue() sends it to that plugin, the float32 printed
    to 9 digits. rutest feeds frame k the samples up to (k + 1) x 800 before
    drawing it, so its frame N-1 has had the same 800 N samples as the page;
    its last frame is compared. On the host-FFT case, where the fallback counts
    samples off the host clock instead, the page's frame at N/60 is compared
    with rutest's frame N (its clock at N/60, the same N steps of 1/60).

Two kinds of case are printed, not judged, and neither agrees, for one reason:
rutest presets the plugin's clock to seconds (SetClockScaleForTest); the page
lets the plugin vote, as a host would, and primes each new instance with five
frames about a millisecond apart so the vote settles before the first frame
shown. Those few milliseconds are frame steps the plugin takes and rutest's
instance never does. On the feed() path they count no samples (every fed
sample is integrated once, and nothing is fed during them), so the flames
agree; but the shimmer's clock is the sum of the frame steps, so at any
Shimmer above 0 the plumes' noise is a few milliseconds on from rutest's, and
the judged cases run at Shimmer 0. On "None (host FFT)" the fallback counts
its band noise off the same clock, so the piston takes a different path there.

The two outputs are compared byte for byte (RGB, and alpha separately), and
the flames read off the page (its status accessors; rutest's --pipe prints no
per-frame summary). There is no pass mark the plugin's own checks give for
this; the script prints the difference and fails only past max 8 codes or 1%
of values off. A negative control -- rutest with Gas one slider step (0.001)
from the page's on the first case -- must differ from the page by more than
that case does, so the comparison is shown to see a slip that small.

Exit 1 when a judged case is past that mark, the negative control passes, or
the page logs an error or a WebGL warning.
"""
import argparse
import base64
import functools
import http.server
import json
import os
import subprocess
import sys
import tempfile
import threading
import time

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
DEMO = os.path.abspath(os.path.join(HERE, ".."))
sys.dont_write_bytecode = True

CDPSHOT = os.environ.get("CDPSHOT", os.path.expanduser("~/Projects/infrastructure/stoatworks-backend/release/cdpshot.py"))
sys.path.insert(0, os.path.dirname(CDPSHOT))
import cdpshot  # noqa: E402

# The real GPU: SwiftShader rounds differently and is slow; --disable-gpu has no WebGL.
cdpshot.GL_FLAGS = ["--use-gl=angle", "--use-angle=metal", "--ignore-gpu-blocklist"]

SIZE = "640x360"

# name, plugin ('source' or 'over'), clip, audio (the host row's id), frames
# N, settings by the plugin's own names (the page's kitValues(): {'units': x}
# is in the plugin's units and goes through its compiled ParamFrom*, an option
# is its element's name, a bare number the host's 0..1), judged.
CASES = [
    ("source, no shimmer, 5.5 s into the track", "source", "scene", "track", 330, {"Shimmer": 0}, True),
    ("Over, no shimmer, 4 s in", "over", "scene", "track", 240, {"Shimmer": 0}, True),
    ("source, Gas 30 Pa, Dots, no shimmer", "source", "scene", "track", 300, {"Gas": {"units": 30}, "Backdrop": "Dots", "Shimmer": 0}, True),
    ("Over, 130 dB, 15 m behind, Mix 50%", "over", "bars", "track", 200,
     {"Volume": {"units": 130}, "Distance": {"units": 15}, "Mix": 0.5, "Shimmer": 0}, True),
    ("source, 3 m tube, 0.5 mm holes, Black, +1 stop", "source", "scene", "track", 260,
     {"Length": {"units": 3}, "Field Width": {"units": 3.4}, "Hole Size": {"units": 0.5e-3},
      "Backdrop": "Black", "Exposure": {"units": 1}, "Shimmer": 0}, True),
    ("Over, shape on transparency, close in", "over", "alpha", "track", 180,
     {"Field Width": {"units": 0.8}, "Tube Height": {"units": 0.3}, "Shimmer": 0}, True),
    ("source, silence", "source", "scene", "silence", 120, {"Shimmer": 0}, True),
    ("source, the defaults (Shimmer 0.5)", "source", "scene", "track", 330, {}, False),
    ("source, None (host FFT), no shimmer", "source", "scene", "hostfft", 120, {"Shimmer": 0}, False),
]

PRESERVE = """
(() => {
  const real = HTMLCanvasElement.prototype.getContext;
  HTMLCanvasElement.prototype.getContext = function (kind, attrs) {
    if (kind === 'webgl2') attrs = Object.assign({}, attrs || {}, { preserveDrawingBuffer: true });
    return real.call(this, kind, attrs);
  };
})();
"""

# Every control on the panel the running plugin declares, as the page sends it.
HOST = """import('./plugin.js').then(m => {
  const out = [];
  const variant = m.mounted.state.variant;
  for (const p of m.PARAMS) {
    const d = p.ff[variant];
    if (!d) continue;
    const h = m.hostValue(d, m.mounted.params.get(p.id));
    if (h !== null) out.push([d.name, h]);
  }
  return out;
})"""

# Restart with the audio chosen: the host row's own path (a new instance at time 0).
CHOOSE = """import('./plugin.js').then(m => { m.audio.id = %s; m.restartClock(); })"""

STEP = """(async () => {
  const step = [...document.querySelectorAll('.transport .btn')].find((b) => b.textContent === 'Step');
  for (let i = 0; i < %d; i += 1) {
    step.click();
    await new Promise((resolve) => requestAnimationFrame(() => resolve()));
  }
  const m = await import('./plugin.js');
  return { time: m.mounted.state.time, stats: m.renderer.stats() };
})()"""


class Page(cdpshot.Chrome):
    """cdpshot's Chrome, keeping the events it would discard."""

    def __init__(self):
        self.events = []
        super().__init__()
        self.call("Runtime.enable")
        self.call("Log.enable")
        self.call("Page.addScriptToEvaluateOnNewDocument", {"source": PRESERVE})

    def call(self, method, params=None):
        self.msg_id += 1
        want = self.msg_id
        self.ws.send(json.dumps({"id": want, "method": method, "params": params or {}}))
        while True:
            msg = json.loads(self.ws.recv())
            if msg.get("id") == want:
                if "error" in msg:
                    raise RuntimeError(f"{method}: {msg['error']}")
                return msg.get("result", {})
            if "method" in msg:
                self.events.append(msg)

    def run(self, expression):
        r = self.call("Runtime.evaluate", {"expression": expression, "returnByValue": True, "awaitPromise": True})
        if "exceptionDetails" in r:
            raise RuntimeError(f"the page threw: {json.dumps(r['exceptionDetails'])[:400]}")
        return r.get("result", {}).get("value")

    def frames(self):
        return int(self.run("Number(document.querySelector('canvas')?.dataset.frames || 0)"))

    def instances(self):
        return int(self.run("Number(document.querySelector('canvas')?.dataset.instances || 0)"))

    def load(self, url):
        self.call("Page.navigate", {"url": url})
        deadline = time.time() + 30
        while time.time() < deadline:
            try:
                if self.frames() > 2:
                    return
            except RuntimeError:
                pass
            time.sleep(0.25)
        raise RuntimeError(f"{url} drew no frames")

    def after(self, expression):
        """Run, then wait for the frame it causes (the page is paused)."""
        n0 = self.run("Number(document.querySelector('canvas')?.dataset.frames || 0) + ':' + (document.querySelector('canvas')?.dataset.instances || 0)")
        self.run(expression)
        deadline = time.time() + 20
        while time.time() < deadline:
            now = self.run("Number(document.querySelector('canvas')?.dataset.frames || 0) + ':' + (document.querySelector('canvas')?.dataset.instances || 0)")
            if now != n0:
                return
            time.sleep(0.03)
        raise RuntimeError("no frame after " + expression[:80])

    def canvas(self):
        """The canvas's bytes, rows top first."""
        r = self.run("""(() => {
          const c = document.querySelector('canvas'); const gl = c.getContext('webgl2');
          const w = c.width, h = c.height, px = new Uint8Array(w * h * 4);
          gl.bindFramebuffer(gl.FRAMEBUFFER, null);
          gl.readPixels(0, 0, w, h, gl.RGBA, gl.UNSIGNED_BYTE, px);
          const out = new Uint8Array(w * h * 4);
          for (let y = 0; y < h; y += 1) out.set(px.subarray((h - 1 - y) * w * 4, (h - y) * w * 4), y * w * 4);
          let s = '';
          for (let i = 0; i < out.length; i += 0x8000) s += String.fromCharCode.apply(null, out.subarray(i, i + 0x8000));
          return { w, h, b64: btoa(s) };
        })()""")
        return r["w"], r["h"], base64.b64decode(r["b64"])

    def problems(self):
        out = []
        for e in self.events:
            m, p = e["method"], e.get("params", {})
            if m == "Runtime.exceptionThrown":
                out.append(json.dumps(p.get("exceptionDetails", {}))[:300])
            elif m == "Runtime.consoleAPICalled" and p.get("type") in ("error", "warning", "assert"):
                out.append(" ".join(str(a.get("value", a.get("description", ""))) for a in p.get("args", []))[:300])
            elif m == "Log.entryAdded" and p.get("entry", {}).get("level") in ("error", "warning"):
                out.append(f"{p['entry'].get('source')}: {p['entry'].get('text')}"[:300])
        return out


class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


def serve():
    handler = functools.partial(Quiet, directory=DEMO)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server, f"http://127.0.0.1:{server.server_address[1]}/"


def rutest(binary, plugin, host, frame, count, audio, override=None):
    """The last of `count` frames, each fed the same input frame."""
    sets = []
    for name, value in host:
        value = (override or {}).get(name, value)
        sets += ["--set", f"{name}={value:.9g}" if isinstance(value, float) else f"{name}={value}"]
    extra = {"track": [], "silence": ["--silent"], "hostfft": ["--host-fft"]}[audio]
    if plugin == "over":
        extra = ["--over", *extra]
    env = dict(os.environ, RUBENS_NO_DEVICES="1")
    proc = subprocess.Popen([binary, "--pipe", "--fps", "60", "--size", SIZE, *extra, *sets],
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)

    def feed():
        try:
            for _ in range(count):
                proc.stdin.write(frame)
            proc.stdin.close()
        except BrokenPipeError:
            pass

    writer = threading.Thread(target=feed, daemon=True)
    writer.start()
    size = len(frame)
    last, got = None, 0
    while True:
        chunk = proc.stdout.read(size)
        if not chunk:
            break
        if len(chunk) < size:
            chunk += proc.stdout.read(size - len(chunk))
        if len(chunk) == size:
            last, got = chunk, got + 1
    writer.join()
    err = proc.stderr.read().decode()
    if proc.wait() != 0 or got != count:
        raise RuntimeError(f"rutest {' '.join(extra + sets)}: {got} of {count} frames; {err[:400]}")
    return last


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--rutest", required=True)
    ap.add_argument("--keep", help="write each case's two pictures and their difference (x40) here as PNG")
    ap.add_argument("--only", help="run only the cases whose name contains this")
    args = ap.parse_args()

    w, h = map(int, SIZE.split("x"))
    server, base = serve()
    page = Page()
    failures = 0
    first = True
    try:
        for name, plugin, clip, audio, count, settings, judged in CASES:
            if args.only and args.only not in name:
                continue
            page.load(f"{base}?size={SIZE}&clip={clip}" + ("&plugin=over" if plugin == "over" else ""))
            if page.run("import('./plugin.js').then(m => m.mounted.state.variant)") != plugin:
                raise RuntimeError(f"the page did not open on the {plugin}")
            page.after("document.querySelector('.btn--play').click()")
            if page.run("import('./plugin.js').then(m => m.mounted.state.playing)"):
                raise RuntimeError("the page did not pause")
            kit = page.run(f"import('./plugin.js').then(m => m.kitValues(m.mounted.state.variant, {json.dumps(settings)}))")
            if kit:
                page.after(f"import('./plugin.js').then(m => m.mounted.params.apply({json.dumps(kit)}))")
            before = page.instances()
            page.after(CHOOSE % json.dumps(audio))
            if page.instances() != before + 1:
                raise RuntimeError("choosing the audio did not start a new instance")
            stepped = page.run(STEP % count)
            if abs(stepped["time"] - count / 60) > 1e-9:
                raise RuntimeError(f"the page's clock reads {stepped['time']} after {count} steps")
            if plugin == "over":
                mix = page.run("import('./plugin.js').then(m => m.mounted.params.get('mix'))")
                page.after("import('./plugin.js').then(m => m.mounted.params.set('mix', 0))")
                cw, ch, frame = page.canvas()
                if (cw, ch) != (w, h):
                    raise RuntimeError(f"the canvas is {cw}x{ch}, not {SIZE}")
                page.after(f"import('./plugin.js').then(m => m.mounted.params.set('mix', {mix}))")
            else:
                frame = bytes(w * h * 4)
            cw, ch, ours = page.canvas()
            if (cw, ch) != (w, h):
                raise RuntimeError(f"the canvas is {cw}x{ch}, not {SIZE}")
            host = page.run(HOST)
            frames = count + 1 if audio == "hostfft" else count
            theirs = rutest(args.rutest, plugin, host, frame, frames, audio)

            a = np.frombuffer(ours, np.uint8).reshape(h, w, 4).astype(np.int16)
            b = np.frombuffer(theirs, np.uint8).reshape(h, w, 4).astype(np.int16)
            diff = np.abs(a - b)
            rgb = diff[..., :3]
            off = int((rgb > 0).sum())
            ok = rgb.max() <= 8 and off <= 0.01 * rgb.size and diff[..., 3].max() <= 1
            failures += not ok and judged
            s = stepped["stats"]
            mark = "info" if not judged else "ok  " if ok else "FAIL"
            print(f"{mark}  {name:<48} {clip:<6} max {rgb.max()} codes, {off} of {rgb.size} values off"
                  f" (mean {rgb.mean():.2e}), alpha max {diff[..., 3].max()}"
                  f" | page: {s['holes']} holes, {s['modes']} modes, flames {s['flameMin'] * 100:.2f}-{s['flameMax'] * 100:.2f} cm"
                  f" (quiet {s['quiet'] * 100:.1f}), {s['samples']} tap samples, earshot {s['state']}")
            if first and judged:
                first = False
                case_mean = rgb.mean()
                step = dict(host)["Gas"] + 0.001
                slip_bytes = rutest(args.rutest, plugin, host, frame, frames, audio, {"Gas": step})
                slip = np.abs(a - np.frombuffer(slip_bytes, np.uint8).reshape(h, w, 4).astype(np.int16))[..., :3]
                caught = slip.mean() > case_mean
                failures += not caught
                print(f"{'ok  ' if caught else 'FAIL'}  {'negative: Gas one step (0.001) off':<48} {'':<6} max {slip.max()} codes, mean {slip.mean():.2e}"
                      f" against the case's {case_mean:.2e}, {int((slip > 0).sum())} values off")
            if args.keep:
                from PIL import Image
                os.makedirs(args.keep, exist_ok=True)
                stem = os.path.join(args.keep, "".join(c if c.isalnum() else "-" for c in name))
                Image.fromarray(a[..., :3].astype(np.uint8)).save(f"{stem}-page.png")
                Image.fromarray(b[..., :3].astype(np.uint8)).save(f"{stem}-rutest.png")
                Image.fromarray(np.clip(rgb * 40, 0, 255).astype(np.uint8)).save(f"{stem}-diff-x40.png")
        problems = page.problems()
        for p in problems:
            print(f"FAIL  the page logged: {p}")
        failures += len(problems)
        renderer = page.run("(() => { const gl = document.createElement('canvas').getContext('webgl2'); const x = gl.getExtension('WEBGL_debug_renderer_info'); return x ? gl.getParameter(x.UNMASKED_RENDERER_WEBGL) : gl.getParameter(gl.RENDERER); })()")
        print(f"\nthe page rendered on: {renderer}")
    finally:
        page.close()
        server.shutdown()
    print(f"{'all judged cases within the mark' if failures == 0 else f'{failures} failure(s)'}")
    return 1 if failures else 0


if __name__ == "__main__":
    os.environ.setdefault("TMPDIR", tempfile.gettempdir())
    sys.exit(main())
