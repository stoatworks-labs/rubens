/**
 * Rubens — browser demo.
 *
 * Rubens' flame tube as a pair of FFGL plugins (SW Rubens, RU01, a source; SW
 * Rubens Over, RU02, an effect that shows the clip through the heat above the
 * flames). Nothing is animated by a parameter: the music moves a piston at one
 * end of a propane-filled duct, the duct resonates (its modes discretised
 * exactly at 16 kHz), each hole's flow goes as the square root of the pressure
 * across it, outward only, into its flame, and the hot plumes above the flames
 * bend the light from what is behind them.
 *
 * ---------------------------------------------------------------------------
 * What runs here, and what does not
 * ---------------------------------------------------------------------------
 *
 * **This page does not port the plugin. It runs it.** `rubens-core.wasm` is the
 * plugin's own C++ compiled UNMODIFIED with emscripten
 * (demo/tools/build-wasm.sh): `source/Rubens.cpp` — the plugin class, with its
 * constructor, its clock and seconds-or-milliseconds vote, the R32F plume
 * buffer, the hole table it uploads every frame and every uniform it sets —
 * and everything it calls: the duct and its holes (Duct.cpp, in double), the
 * audio input (Audio.cpp) and, under it, earshot's snapshot, built with no
 * device backend at all (EARSHOT_CAPTURE=OFF), with the FFGL SDK's
 * CFFGLPlugin, its parameter bookkeeping, FFGLShader and FFGLFBO. Its GL calls
 * go to this page's WebGL2 context through emscripten's GL library.
 *
 * What is the page's and not the plugin's:
 *
 *   - **The host.** demo/wasm/glue.cpp constructs the plugin (what
 *     SourcePlugin.cpp and EffectPlugin.cpp do in a bundle), reads its
 *     parameter declarations back through the SDK's host getters — the panel
 *     below is built from them, not from a list typed here — and forwards
 *     SetFloatParameter, SetTime, the Audio buffer's elements and
 *     ProcessOpenGL, as tools/rutest's rig does.
 *   - **The audio.** No device: the page never asks for a microphone, never
 *     lists an input and makes no sound. It feeds the plugin as its harness
 *     does, through earshot's feed() (AudioInput::Feed, the path a device's
 *     samples take inside earshot): rutest's own track, generated in the wasm
 *     by rutest's own functions (copied into glue.cpp, held to
 *     tools/rutest/main.cpp by demo/tools/check_shaders.py), or a file the
 *     visitor chooses, decoded here. Or nothing fed, and rutest --host-fft's
 *     synthetic spectrum in the Audio buffer, which is the plugin's own
 *     fallback ("None (host FFT)") running on a made-up FFT.
 *   - **Five GL entry points** (demo/wasm/gl_shim.cpp). glShaderSource: the
 *     plugin hands GL desktop GLSL 4.10; `shaderSource()` below refuses any
 *     text that is not byte for byte one of this page's assemblies of its copy
 *     (shaders.js, held to source/Shaders.cpp by demo/tools/check_shaders.py),
 *     then applies the kit's `port()`. glEnable / glDisable / glIsEnabled:
 *     GL_PROGRAM_POINT_SIZE, which the plugin saves and restores every frame,
 *     does not exist in WebGL2. glTexImage2D: the FFGL SDK allocates the R32F
 *     plume with a GL_RGBA client format, which WebGL2 refuses; with no data
 *     the shim gives it GL_RED, the one format ES pairs with R32F.
 *   - **The panel's read-outs.** The number beside a slider is Controls.cpp's
 *     conversion, compiled in (glue.cpp's ru_convert); the units, the tooltips
 *     and the presets are this page's words. The presets are written in dB,
 *     metres, pascals and stops and go to the host's 0..1 through the plugin's
 *     own inverse functions (ru_param_from).
 *
 * ---------------------------------------------------------------------------
 * Decisions this page made, and why
 * ---------------------------------------------------------------------------
 *
 * **Every change of audio or of plugin is a new instance, from time 0.**
 * earshot's feed, once begun, is what its engine reads until a selectDevice,
 * and any select ends it; so the page never switches a running instance
 * between sources. Changing either restarts the clock, as Restart does, and
 * the new instance's feed starts at the track's beginning.
 *
 * **Each new instance is primed** (mirrorball's rule): five frames about a
 * millisecond apart, so the plugin's seconds-or-milliseconds vote settles
 * before the first frame shown and a paused page stands still. The feed is
 * begun before them, so they take no fallback samples. The shimmer's clock is
 * the sum of the plugin's frame steps, so it carries those few milliseconds.
 *
 * **The regime is the plugin's, said plainly.** At the defaults (Gas 3 Pa,
 * Volume 145 dB) loud music swings the holes far past the gas pressure, and
 * the flames flare where the pressure swings most: the reversal Ficken &
 * Stephenson reported (as Gee relates it), the opposite of the textbook's
 * dips. A line under the picture says so, two presets turn it the right way
 * up, and the status line reports which way round the last two seconds were.
 * No default is changed.
 *
 * **Photosensitivity.** The flames jump with the music's beats, and at the
 * defaults on this page's track they go over the flash line in a third of the
 * picture (8 rises and falls in a second; the Gas 30 Pa and Volume 130 dB
 * presets make none). index.html says so above the page, with the figures,
 * and links the user guide's table. No default is changed for it.
 *
 * **Left off the panel**: Audio Input and Rescan Inputs (no device exists
 * here), the Audio buffer (Resolume's FFT, which the page writes only in the
 * "None (host FFT)" choice) and the About block, as on every page in the suite.
 */

import { mountDemo } from './vendor/demo.js';
import { port, GLError } from './vendor/gl.js';
import * as S from './shaders.js';
import createRubens from './rubens-core.js';

//===========================================================================
// FFGL.h's parameter types, by number: what GetParamType returns.
//===========================================================================

const FF_TYPE_BOOLEAN = 0;
const FF_TYPE_EVENT = 1;
const FF_TYPE_STANDARD = 10;
const FF_TYPE_OPTION = 11;
const FF_TYPE_BUFFER = 12;
const FF_TYPE_TEXT = 100;

//===========================================================================
// The GLSL: the page's checked copy, assembled as the plugin assembles it
// (ASSEMBLY is read out of Rubens.cpp's InitGL and Shaders.cpp's Assemble()
// by check_shaders.py), one text per stage.
//===========================================================================

const PAGE_SHADERS = Object.entries(S.ASSEMBLY).map(([name, pieces]) => ({
  name,
  stage: name.endsWith('_vertex') ? 'vertex' : 'fragment',
  text: pieces.map((p) => S[p]).join(''),
}));

/** Which stages the compiled plugin handed over, and whether each was ours. */
const shaderCheck = { matched: new Set(), refused: 0 };

/**
 * Called by gl_shim.cpp's glShaderSource with the text the compiled plugin
 * handed to GL. Anything but one of this page's own assemblies is refused (an
 * empty source, so the plugin's InitGL fails and says so) rather than compiled.
 */
function shaderSource(stage, text) {
  const ours = PAGE_SHADERS.filter((s) => s.stage === stage && s.text === text);
  if (!ours.length) {
    shaderCheck.refused += 1;
    return '';
  }
  for (const s of ours) shaderCheck.matched.add(s.name);
  return port(text, { stage });
}

//===========================================================================
// The module.
//===========================================================================

const printed = [];
let core;
try {
  core = await createRubens({
    rubensShaderSource: shaderSource,
    print: (text) => { printed.push(text); console.log(text); },
    printErr: (text) => { printed.push(text); console.warn(text); },
  });
} catch (error) {
  const root = document.querySelector('#demo');
  root.textContent = `The demo could not start: this browser would not run its WebAssembly (${error.message}). The plugin, its source and its downloads are at https://github.com/stoatworks-labs/rubens.`;
  throw error;
}

const text = (pointer) => core.UTF8ToString(pointer);

// Before any instance (the declarations below construct two): the plugin's
// log in the page's memory, and verify.sh's switch that keeps
// AudioInput::Refresh from asking for a device list.
core._ru_prepare();

//===========================================================================
// The declarations, read from each plugin's constructor through the SDK.
//===========================================================================

function declarations(effect) {
  const h = core._ru_new(effect ? 1 : 0);
  const list = [];
  const n = core._ru_param_count(h);
  for (let i = 0; i < n; i += 1) {
    const type = core._ru_param_type(h, i);
    const d = { index: i, name: text(core._ru_param_name(h, i)), type, group: text(core._ru_param_group(h, i)) };
    d.default = type === FF_TYPE_TEXT ? null : core._ru_param_default(h, i);
    if (type === FF_TYPE_OPTION) {
      d.elements = [];
      d.values = [];
      for (let e = 0; e < core._ru_param_element_count(h, i); e += 1) {
        d.elements.push(text(core._ru_param_element_name(h, i, e)));
        d.values.push(core._ru_param_element_value(h, i, e));
      }
    }
    list.push(d);
  }
  const inputs = [core._ru_min_inputs(h), core._ru_max_inputs(h)];
  // Kept, never given GL: the read-outs and the presets ask it for
  // Controls.cpp's conversions, which need only its host-index table.
  return { list, inputs, converter: h };
}

const DECLARED = { source: declarations(false), over: declarations(true) };
if (DECLARED.source.inputs.join() !== '0,0' || DECLARED.over.inputs.join() !== '1,1') {
  throw new Error(`the plugins declare ${DECLARED.source.inputs.join('–')} and ${DECLARED.over.inputs.join('–')} inputs; this page hands the source none and the Over exactly one`);
}

/** Controls.cpp's conversion of a host value, for a declaration of a plugin. */
const convert = (variant, d, v) => core._ru_convert(DECLARED[variant].converter, d.index, Math.fround(v));
/** And its inverse: the host's 0..1 for a value in the plugin's units. */
const paramFrom = (variant, d, units) => core._ru_param_from(DECLARED[variant].converter, d.index, units);

//===========================================================================
// What the panel leaves out, by the plugin's own names: the device menu and
// its rescan (no device exists here), Resolume's FFT buffer (written only by
// the "None (host FFT)" audio choice) and the About block.
//===========================================================================

const LEFT_OUT = ['Audio Input', 'Rescan Inputs', 'Audio'];
const onPanel = (d) => d.group !== 'About' && !LEFT_OUT.includes(d.name);
for (const variant of ['source', 'over']) {
  for (const name of LEFT_OUT) {
    if (!DECLARED[variant].list.some((d) => d.name === name)) throw new Error(`the ${variant} no longer declares ${name}; this page's notes say it is left out`);
  }
}

//===========================================================================
// The read-outs: Controls.cpp's number, this page's unit.
//===========================================================================

const fixed = (n, d) => n.toFixed(d);
const signed = (n, d) => `${n > 0 ? '+' : n < 0 ? '−' : ''}${fixed(Math.abs(n), d)}`;
// Short: the read-out column is about ten characters wide. The tooltips say
// what each is.
const UNITS = {
  Volume: (x) => `${fixed(x, 1)} dB`,
  Length: (x) => `${fixed(x, 3)} m`,
  'Hole Pitch': (x) => `${fixed(x * 100, 2)} cm`,
  'Hole Size': (x) => `${fixed(x * 1000, 2)} mm`,
  Gas: (x) => `${fixed(x, x < 10 ? 2 : x < 100 ? 1 : 0)} Pa`,
  Flame: (x) => `${fixed(x * 100, 1)} cm`,
  Shimmer: (x) => fixed(x, 2),
  Distance: (x) => `${fixed(x, 1)} m`,
  'Field Width': (x) => `${fixed(x, 2)} m`,
  'Tube Height': (x) => `${fixed(x * 100, 0)}% up`,
  Exposure: (x) => `${signed(x, 2)} stops`,
};

function readout(d, variant) {
  return (v) => {
    const x = convert(variant, d, v);
    if (Number.isNaN(x)) return d.name === 'Mix' ? `${fixed(v * 100, 0)}%` : fixed(v, 2);
    return (UNITS[d.name] ?? ((y) => fixed(y, 3)))(x);
  };
}

/** Tooltips: this page's words, from the README's description of each control. */
const HINTS = {
  Channel: 'Which channel of the audio moves the piston: L+R (their average), Left or Right. rutest’s track is the same on both channels, so this acts only on a stereo file of yours whose channels differ.',
  'FFT Law': 'How the fallback reads Resolume’s FFT bins: linear or log layout, magnitude or power. It acts only on the “None (host FFT)” audio choice in the host row. Arena 7.29.0’s bins were measured: Linear Mag, the default, 375 Hz a bin.',
  Volume: '80–150 dB re 20 µPa: the level a full-scale sine makes as a travelling wave in the gas, which sets the piston’s speed. At the default 145 dB loud music swings the holes far past the default gas pressure, and the flames flare where the pressure swings most; turn it down (to about 130 dB) for the textbook dips.',
  Length: 'The tube, 0.5–3 m. Its modes are L/n apart along it; a longer tube has more of them below the first cross-mode.',
  'Hole Pitch': '1–5 cm between holes. The tube takes a whole number of holes (at most 160), so the pitch used is the length over the count.',
  'Hole Size': 'The hole radius, 0.3–2 mm (the slider is geometric). Each hole is an acoustic mass and a jet: smaller holes lower the holes’ Helmholtz frequency, which sets how far the low modes are pushed up.',
  Gas: 'The gas overpressure the regulator holds, 1–1000 Pa (the slider is geometric). It decides which way round the pattern sits. Well above the sound’s swing the flames dip where the pressure swings most, as the textbooks draw it; well below it, as at the default 3 Pa with loud music, those same places flare. More gas also damps the low modes.',
  Flame: 'The quiet flame’s height, 1–30 cm, whatever the Gas: so moving Gas changes the dip-or-flare balance without relighting the stage. (A real tube’s quiet flames grow with the gas pressure.)',
  Shimmer: 'The plumes’ turbulence, 0–1: a noise carried up with the hot air that modulates it and wiggles the columns. A look, not a measurement.',
  Distance: 'How far behind the plumes the backdrop, or the Over’s clip, sits: 0–30 m. The further, the more the heat bends it.',
  'Field Width': 'How wide the scene is across the frame, 0.5–6 m (the slider is geometric). The tube and the flames are drawn to that scale.',
  'Tube Height': 'The tube’s axis, up from the bottom of the frame: 0–60% of its height.',
  Exposure: '±4 stops on the whole picture, the flames’ light included.',
  Backdrop: 'SW Rubens only. What is behind the flames: Black, a Grid of thin lines every 10 cm (the refraction made visible) or Dots, a field as background-oriented schlieren uses.',
  Mix: 'SW Rubens Over only. 0 hands the clip straight back, bit for bit.',
};

//===========================================================================
// Kit parameters from the declarations. The id is the name as Arena
// addresses it (lower case, spaces removed). Shared names whose declarations
// agree but for the default are one row; a name only one plugin declares
// carries `only`.
//===========================================================================

const KIT_TYPES = {
  [FF_TYPE_STANDARD]: 'standard',
  [FF_TYPE_OPTION]: 'option',
  [FF_TYPE_BOOLEAN]: 'boolean',
};

/** The kit reads these from the query string itself, and this page reads `plugin` and `audio`. */
const RESERVED_QUERY = new Set(['embed', 'size', 'clip', 'bg', 'plugin', 'audio']);
const addressOf = (name) => {
  const address = name.toLowerCase().replace(/\s+/g, '');
  return RESERVED_QUERY.has(address) ? `param-${address}` : address;
};

function kitDefault(d) {
  switch (d.type) {
    case FF_TYPE_OPTION: {
      const i = d.values.findIndex((v) => v === d.default);
      return i >= 0 ? i : Math.round(d.default);
    }
    case FF_TYPE_BOOLEAN: return d.default > 0.5 ? 1 : 0;
    default: return d.default;
  }
}

/** The same control in both plugins: everything but its host index and default. */
const sameControl = (a, b) => JSON.stringify({ ...a, index: 0, default: 0 }) === JSON.stringify({ ...b, index: 0, default: 0 });

let mounted = null;
const query = new URLSearchParams(window.location.search);
const embed = query.has('embed') && query.get('embed') !== '0';
const initialVariant = query.get('plugin') === 'over' ? 'over' : 'source';
function currentVariant() { return mounted?.state.variant ?? initialVariant; }

const PARAMS = [];
{
  const over = DECLARED.over.list.filter(onPanel);
  const source = DECLARED.source.list.filter(onPanel);
  for (const d of [...source, ...over]) {
    if (KIT_TYPES[d.type] === undefined) throw new Error(`no kit control for FFGL type ${d.type} (${d.name})`);
  }
  // The source's order, then anything only the Over declares, after the last
  // row of its own group (Mix, in the View group, after Backdrop).
  for (const s of source) {
    const d = over.find((x) => x.name === s.name);
    if (d && !sameControl(s, d)) throw new Error(`${s.name}: the two plugins declare it differently, and this page has no row for that`);
    const p = { id: addressOf(s.name), name: s.name, type: KIT_TYPES[s.type], group: s.group, hint: HINTS[s.name], ff: { source: s, over: d ?? null } };
    if (!d) p.only = 'source';
    PARAMS.push(p);
  }
  for (const d of over) {
    if (source.some((x) => x.name === d.name)) continue;
    const p = { id: addressOf(d.name), name: d.name, type: KIT_TYPES[d.type], group: d.group, hint: HINTS[d.name], ff: { source: null, over: d }, only: 'over' };
    let at = PARAMS.length;
    for (let i = PARAMS.length - 1; i >= 0; i -= 1) if (PARAMS[i].group === d.group) { at = i + 1; break; }
    PARAMS.splice(at, 0, p);
  }
  for (const p of PARAMS) {
    const d = p.ff.source ?? p.ff.over;
    if (d.type === FF_TYPE_OPTION) p.elements = d.elements;
    if (d.type === FF_TYPE_STANDARD) p.display = (v) => readout(p.ff[currentVariant()] ?? d, p.ff[currentVariant()] ? currentVariant() : p.only)(v);
  }
}

/** Each constructor's defaults, by kit id; a row the plugin lacks keeps the other's. */
const DEFAULTS = { source: {}, over: {} };
for (const p of PARAMS) {
  for (const variant of ['source', 'over']) {
    DEFAULTS[variant][p.id] = kitDefault(p.ff[variant] ?? p.ff.source ?? p.ff.over);
  }
}
for (const p of PARAMS) p.default = DEFAULTS[initialVariant][p.id];

//===========================================================================
// The audio: the stand-in for what reaches the plugin in Resolume.
//===========================================================================

const FEED_RATE = 48000;
const AUDIO = [
  { id: 'track', name: 'rutest’s track (kicks, a bass line, a lead)', which: 0,
    hint: 'The plugin’s harness’s own music, generated in the WebAssembly by rutest’s own functions: a kick on every beat at 120 BPM, a hashed-noise hat on the off-beat, a bass line and a lead on eighth notes through A minor pentatonic from 220 to 880 Hz, peaking near full scale. Fed through earshot’s feed() as a device’s samples would arrive, on both channels.' },
  { id: 'silence', name: 'Silence (zeros fed)', which: 1,
    hint: 'rutest --silent: exact zeros through feed(). The flames burn at their quiet height; after three seconds earshot calls the input Silent and the lamp goes steady red, as it would for an input macOS is handing silence.' },
  { id: 'hostfft', name: 'None (host FFT): a synthetic kick spectrum', which: null,
    hint: 'The plugin’s own default, “None (host FFT)”: nothing is fed, and the plugin’s fallback drives the piston with band-limited noise whose level follows Resolume’s FFT buffer, four bands a frame. Here that buffer is rutest --host-fft’s made-up kick spectrum, written every frame, not a real FFT of anything. The lamp is amber.' },
  { id: 'file', name: 'Your audio file', which: null,
    hint: 'A file you choose, decoded in this page by the browser and fed through feed(), looped. It is not uploaded and not played aloud.' },
];
const audioById = (id) => AUDIO.find((a) => a.id === id);
const audio = {
  id: AUDIO.some((a) => a.id === query.get('audio') && a.id !== 'file') ? query.get('audio') : 'track',
  file: null, // { name, left: Float32Array, right: Float32Array, seconds, channels }
  epoch: 0, // bumped on every change of audio or plugin: a new instance
};

/**
 * Decode a file the visitor chose. An OfflineAudioContext renders to memory,
 * not to an output device; decodeAudioData resamples to its 48 kHz. Nothing
 * here opens a device or asks for one.
 */
async function decodeFile(file) {
  const bytes = await file.arrayBuffer();
  const context = new OfflineAudioContext(2, 1, FEED_RATE);
  const buffer = await context.decodeAudioData(bytes);
  const left = buffer.getChannelData(0);
  const right = buffer.numberOfChannels > 1 ? buffer.getChannelData(1) : left;
  if (!left.length) throw new Error('it holds no samples');
  return { name: file.name, left: new Float32Array(left), right: new Float32Array(right), seconds: left.length / FEED_RATE, channels: buffer.numberOfChannels };
}

//===========================================================================
// The presets: whole states on top of the constructor's defaults, by the
// plugin's own names. A number in { units } is in the plugin's units (dB,
// metres, pascals, stops) and is converted by the plugin's own inverse
// (ru_param_from); an option is its element's name; a bare number is the
// host's 0..1. They are the page's choices; the two "right way up" ones were
// chosen with rutest on its own track (Gas 30 Pa: every flame 16.6–19.5 cm
// against a quiet 20; Volume 130 dB: 16.2–19.8; the defaults: 13.5–28.1).
//===========================================================================

const u = (units) => ({ units });
const PRESET_UNITS = [
  ['The defaults: loud music flares the flames where the pressure swings most', {}],
  ['Gas 30 Pa: the right way up, the textbook dips', { Gas: u(30) }],
  ['Volume 130 dB: the right way up, the textbook dips', { Volume: u(130) }],
  ['Gas 100 Pa: a stiff jet, the low modes damped', { Gas: u(100) }],
  ['A 3 m tube: more modes below the cut-on', { Length: u(3), 'Field Width': u(3.4) }],
  ['Small holes: 0.5 mm, the holes’ Helmholtz frequency down', { 'Hole Size': u(0.5e-3) }],
  ['Dots, 15 m behind: the heat as background-oriented schlieren sees it', { Backdrop: 'Dots', Distance: u(15) }],
  ['No shimmer: the plumes’ columns alone', { Shimmer: 0 }],
  ['Close in: 0.8 m across, the middle of the tube', { 'Field Width': u(0.8), 'Tube Height': u(0.3) }],
];

/**
 * Settings by the plugin's own names as the kit's values: {units} goes
 * through the plugin's ParamFrom*, an option is its element's name, a bare
 * number the host's 0..1. (demo/tools/crosscheck.py uses it too.)
 */
function kitValues(variant, spec, what = 'settings') {
  const values = {};
  for (const [name, value] of Object.entries(spec)) {
    const p = PARAMS.find((x) => x.name === name);
    if (!p) throw new Error(`${what}: no control called ${name}`);
    const d = p.ff[variant] ?? p.ff.source ?? p.ff.over;
    if (p.type === 'option') {
      const slot = p.elements.indexOf(value);
      if (slot < 0) throw new Error(`${what}: ${name} has no element ${value}`);
      values[p.id] = slot;
    } else {
      const host = typeof value === 'object' ? paramFrom(p.ff[variant] ? variant : p.only, d, value.units) : value;
      if (!Number.isFinite(host) || host < -1e-6 || host > 1 + 1e-6) throw new Error(`${what}: ${name} ${JSON.stringify(value)} is outside the control`);
      values[p.id] = Math.min(1, Math.max(0, host));
    }
  }
  return values;
}

function kitPresets(variant) {
  const out = {};
  for (const [title, spec] of PRESET_UNITS) out[title] = kitValues(variant, spec, `preset “${title}”`);
  return out;
}

//===========================================================================
// The renderer: one plugin instance, driven as a host drives it.
//===========================================================================

let renderer = null;

/** The value the host would send for a kit control, or null for none. */
function hostValue(d, v) {
  switch (d.type) {
    case FF_TYPE_OPTION: return Math.fround(d.values[Math.round(v)] ?? 0);
    case FF_TYPE_BOOLEAN: return v > 0.5 ? 1 : 0;
    case FF_TYPE_EVENT:
    case FF_TYPE_TEXT:
    case FF_TYPE_BUFFER: return null;
    default: return Math.fround(v);
  }
}

const pluginName = (variant) => (variant === 'over' ? 'SW Rubens Over' : 'SW Rubens');

function createRenderer(gl) {
  // emscripten's GL library, pointed at the kit's context. Nothing is turned
  // on behind the kit's back: no extensions are enabled on its behalf. The
  // kit has enabled EXT_color_buffer_float (needFloat) for the plugin's R32F
  // plume buffer, a render target, or refused to start; the plume and the
  // RGBA32F hole table are only read with texelFetch, which needs no
  // float-filtering extension.
  const handle = core.GL.registerContext(gl, { majorVersion: 2, minorVersion: 0, enableExtensionsByDefault: false });
  core.GL.makeContextCurrent(handle);

  let instance = 0;
  let variant = null;
  let pushed = new Map();
  let clipName = 0;
  let clipTexture = null;
  let frames = 0;
  let instances = 0;
  let lastTime = null;
  let epoch = -1;
  let source = null;
  // The visitor's file, fed as rutest's Feeder feeds a WAV: the absolute
  // sample clock at 48 kHz, samples [0, fileSent) gone to feed(), looped.
  let fileSent = 0;
  let scratch = 0;
  let scratchFrames = 0;

  /** A kit texture under a GL name the plugin can bind, in emscripten's tables. */
  function glName(texture) {
    if (texture === clipTexture) return clipName;
    if (clipName) core.GL.textures[clipName] = null;
    clipName = core.GL.getNewId(core.GL.textures);
    core.GL.textures[clipName] = texture;
    texture.name = clipName;
    clipTexture = texture;
    return clipName;
  }

  function start(which, width, height) {
    if (instance) core._ru_delete(instance);
    instance = core._ru_new(which === 'over' ? 1 : 0);
    variant = which;
    pushed = new Map();
    frames = 0;
    instances += 1;
    fileSent = 0;
    source = audio.file || audio.id !== 'file' ? audio.id : 'track';
    if (!core._ru_init_gl(instance, width, height)) {
      const why = shaderCheck.refused > 0
        ? 'the GLSL the compiled plugin handed to GL is not this page’s checked copy of source/Shaders.cpp, so it was not compiled'
        : `the plugin’s InitGL failed${printed.length ? `: ${printed.slice(-3).join(' ')}` : ''}`;
      throw new GLError(`${pluginName(which)} could not start: ${why}.`);
    }
    // Everything but the host FFT goes through feed() from here on: earshot's
    // feedBegin(48000, 2), its state Running, as rutest's Feeder does it.
    if (source !== 'hostfft') core._ru_feed_begin(instance);
  }

  function push(params) {
    for (const p of PARAMS) {
      const d = p.ff[variant];
      if (!d) continue;
      const value = hostValue(d, params.get(p.id));
      if (value === null || pushed.get(p.id) === value) continue;
      pushed.set(p.id, value);
      core._ru_set_float(instance, d.index, value);
    }
  }

  function feedFile(end) {
    const f = audio.file;
    const n = Math.min(Math.max(0, Math.round(end) - fileSent), 4 * FEED_RATE);
    if (n <= 0) return;
    if (n > scratchFrames) {
      if (scratch) core._free(scratch);
      scratchFrames = Math.max(n, FEED_RATE);
      scratch = core._malloc(scratchFrames * 2 * 4);
    }
    const out = core.HEAPF32.subarray(scratch >> 2, (scratch >> 2) + 2 * n);
    const length = f.left.length;
    for (let k = 0; k < n; k += 1) {
      const at = (fileSent + k) % length;
      out[2 * k] = f.left[at];
      out[2 * k + 1] = f.right[at];
    }
    core._ru_feed(instance, scratch, n, FEED_RATE, 2);
    fileSent += n;
  }

  /** rutest's frame counter at 60 fps for a page time: what feedKicks is keyed on. */
  const kickFrame = (time) => Math.round(time * 60);

  /** The audio for a frame at page time `time`, before the plugin renders it. */
  function feed(time) {
    if (source === 'hostfft') {
      core._ru_host_fft(instance, kickFrame(time));
    } else if (source === 'file') {
      feedFile(time * FEED_RATE);
    } else {
      core._ru_feed_track(instance, time * FEED_RATE, audioById(source).which);
    }
  }

  function process(time, input) {
    const clip = variant === 'over' ? glName(input.texture) : 0;
    if (!core._ru_process(instance, time, clip, input.width, input.height)) {
      throw new GLError('The plugin’s ProcessOpenGL failed.');
    }
  }

  /**
   * Five host frames about a millisecond apart, before the first one shown:
   * enough for the plugin's seconds-or-milliseconds vote (four frames that
   * agree against the wall clock) to settle, so a paused page stands still.
   * Nothing is fed during them (the host FFT's spectrum is written for each).
   * The clock then steps back to the page's time, which the plugin takes as
   * no time passing.
   */
  function prime(time, input) {
    const t0 = performance.now();
    for (let k = 0; k < 5; k += 1) {
      if (k > 0) {
        // The plugin needs 0.5 ms of wall clock between votes. Date.now() caps
        // the wait where performance.now() stands still (a test harness's
        // stepped clock).
        const until = performance.now() + 1.05;
        const cap = Date.now() + 20;
        while (performance.now() < until && Date.now() < cap) { /* wait */ }
      }
      const at = time + (performance.now() - t0) / 1000;
      if (source === 'hostfft') core._ru_host_fft(instance, kickFrame(at));
      process(at, input);
    }
  }

  return {
    render({ input, params, width, height, time, variant: wanted }) {
      core.GL.makeContextCurrent(handle);
      // A new instance for the Plugin dropdown, a change of audio, and
      // Restart (the page's clock going back).
      const which = wanted ?? 'source';
      const fresh = !instance || which !== variant || epoch !== audio.epoch || (lastTime !== null && time < lastTime);
      lastTime = time;
      epoch = audio.epoch;
      if (fresh) start(which, width, height);
      push(params);
      if (fresh) prime(time, input);

      feed(time);
      process(time, input);

      // A setter WebGL2 refuses (a float sent to an int, say) fails silently
      // but for this: asked on an instance's first frames only, as a sync
      // point is not free. demo/tools/check_shaders.py checks the setters
      // statically.
      frames += 1;
      if (frames <= 3) {
        const error = gl.getError();
        if (error !== gl.NO_ERROR) {
          gl.canvas.dataset.glError = `0x${error.toString(16)}`;
          console.error(`WebGL reported error 0x${error.toString(16)} on frame ${frames} of ${pluginName(variant)}`);
        }
      }
      // For anything checking the page from outside.
      gl.canvas.dataset.frames = String(frames);
      gl.canvas.dataset.instances = String(instances);
      gl.canvas.dataset.audio = source;
      gl.canvas.dataset.variant = variant;
      gl.canvas.dataset.flameMax = String(core._ru_flame_max(instance));
      gl.canvas.dataset.flameMin = String(core._ru_flame_min(instance));
    },
    stats() {
      if (!instance) return null;
      return {
        variant,
        source,
        holes: core._ru_holes(instance),
        pitch: core._ru_pitch(instance),
        modes: core._ru_modes(instance),
        fH: core._ru_helmholtz_hz(instance),
        cutOn: core._ru_cut_on_hz(instance),
        quiet: core._ru_quiet_flame(instance),
        flameMin: core._ru_flame_min(instance),
        flameMax: core._ru_flame_max(instance),
        flameMean: core._ru_flame_mean(instance),
        v0: core._ru_v0(instance),
        samples: core._ru_tap_samples(instance),
        ductSamples: core._ru_duct_samples(instance),
        fromDevice: core._ru_from_device(instance) === 1,
        state: text(core._ru_capture_state_name(instance)),
        now: core._ru_host_now(instance),
        fed: source === 'file' ? fileSent : core._ru_fed(instance),
        frames,
      };
    },
    /** Hole i's flame the last frame drew, metres. */
    flame(i) {
      return instance ? core._ru_flame(instance, i) : 0;
    },
    log() {
      try {
        return core.FS.readFile(text(core._ru_log_path()), { encoding: 'utf8' });
      } catch {
        return '';
      }
    },
    shaders: shaderCheck,
  };
}

//===========================================================================
// What this page does not reproduce. The kit adds its four.
//===========================================================================

const DIFFERENCES = [
  'This page runs the plugin rather than a port of it. rubens-core.wasm is source/Rubens.cpp — the plugin class: its constructor, its clock and seconds-or-milliseconds vote, the R32F plume buffer and the hole table it uploads every frame, every uniform and the GL state it saves and puts back — and everything it calls (Duct, Audio, PassBuffer, Controls, Shaders, Diag), with earshot’s snapshot (02306da, unpatched) and the FFGL SDK’s CFFGLPlugin, parameter bookkeeping, FFGLShader and FFGLFBO, all compiled unmodified by emscripten (demo/tools/build-wasm.sh). The duct — its modes discretised exactly at 16 kHz in double, the left-out modes’ remainder, every hole’s Bernoulli jet by backward Euler, the flames following their fuel — is that C++. Only the two registrations (SourcePlugin.cpp and EffectPlugin.cpp) are left out; the page constructs the plugin itself, as its harness does. The panel is read back from the plugin’s own declarations through the SDK’s host getters, and the numbers beside the sliders are Controls.cpp’s conversions, compiled in; their units, every tooltip and the presets are this page’s.',
  'No audio device, and no microphone: the page never asks for one, never lists one and makes no sound. earshot is compiled here with no capture backend at all (EARSHOT_CAPTURE=OFF: its NoCapture.cpp, and miniaudio is not compiled), and the plugin is told not to ask for a device list (RUBENS_NO_DEVICES=1, its verification switch). So three of the plugin’s controls are not on this panel: Audio Input (the device menu: None (host FFT), System default, or a device by name), Rescan Inputs, and the Audio buffer (Resolume’s FFT, which a host writes and nobody sets). The live-device path — a device opened by earshot’s worker thread, its 16 kHz tap, Opening, Missing and Failed, the blinking lamp, the remembered device name — is not in the browser.',
  'The audio is the plugin’s harness’s, handed over as its harness hands it: through earshot’s feed() (AudioInput::Feed), the path a device’s samples take inside earshot, so the lamp is green and the piston follows the waveform, sample by sample. The track is rutest’s own (a kick on each beat at 120 BPM, a hat on the off-beat, a bass line and a pentatonic lead), generated in the WebAssembly by rutest’s functions, copied character for character and held to tools/rutest by the repository’s verify script; it is fed up to the page’s clock each frame, at 48 kHz, on both channels, or as exact zeros. Your own audio file is decoded by this browser (Web Audio’s decodeAudioData, in an OfflineAudioContext, resampled to 48 kHz; a mono file goes to both channels), never uploaded, and fed the same way, looped; rutest --wav plays a file once and then feeds silence. Nothing is played aloud.',
  'Channel acts only on a stereo file of yours whose two channels differ: the track is the same on both. FFT Law acts only on “None (host FFT)”. Both are on the panel because the plugin declares them; neither changes the picture otherwise.',
  '“None (host FFT)”, the plugin’s own default Audio Input, is offered in the host row: nothing is fed and the plugin’s fallback drives the piston with band-limited noise, one band each for 40–160, 160–640, 640–2560 and 2560–8000 Hz, its level following Resolume’s FFT buffer and stepping once a frame. Here the buffer holds rutest --host-fft’s synthetic kick spectrum, written every frame from the page’s clock — a made-up FFT, not an analysis of any sound. In Resolume the buffer is Resolume’s own FFT, measured in Arena 7.29.0: 64 linear bins of 375 Hz, magnitudes, which is FFT Law’s default, Linear Mag; the 40–160 Hz band takes 0.32 of the lowest bin (in v0.1.0 it took none). The fallback is uncalibrated. The melody, the transients inside a frame and frame-rate independence are lost on that path, here as in Resolume.',
  'Every change of audio or of plugin is a new instance from time 0, as Restart is: a feed, once begun, is what earshot reads until a device is selected, and the page never selects one. The page’s default audio is the track, as if a device were chosen; the plugin’s default, with no device, is “None (host FFT)”.',
  'At the defaults (Gas 3 Pa, Volume 145 dB) loud music swings the holes far past the gas pressure, so the flames flare where the pressure swings most: the reversal Ficken & Stephenson reported (as Gee relates it), the opposite of the textbook’s dips. Gas up (30 Pa) or Volume down (130 dB) turns it the right way up; two presets do. The defaults are the plugin’s, set so Resolume’s own demo clips move the flames visibly; nothing anchors Gas or Volume to a real tube, and this page changes neither.',
  'The GLSL is the plugin’s: demo/shaders.js is spliced from source/Shaders.cpp, with the order InitGL’s vertex line and Assemble() join the pieces in, and the repository’s verify script fails if a character differs, or if a uniform is set by a call its GLSL type does not take. At start-up the page also requires every stage the compiled plugin hands to glShaderSource to equal its copy, and refuses to compile anything else. The kit’s port() then changes the version line and adds the ES precision defaults. GL is emscripten’s WebGL2 library on this page’s context, not a GL 4.1 driver, and five entry points are the page’s (demo/wasm/gl_shim.cpp): glShaderSource, for that check and port; glEnable, glDisable and glIsEnabled, because GL_PROGRAM_POINT_SIZE — state the plugin saves and restores each frame, as a plugin in Resolume must — does not exist in WebGL2 (it reads as off here; the plugin draws no points); and glTexImage2D, because the FFGL SDK allocates the R32F plume buffer with an RGBA client format, which WebGL2 refuses, so with no data it is given GL_RED, the one format OpenGL ES pairs with R32F. What is allocated is the same.',
  'The clock is the page’s: seconds from frame deltas, capped at a tenth of a second a frame, handed to SetTime, and the audio is fed up to it. The plugin votes against the wall clock on whether those are seconds or milliseconds; so that a paused page stands still, each new instance — page load, a change of plugin or audio, and Restart — is handed five frames about a millisecond apart before the first one shown, which settles the vote, with nothing fed during them. The shimmer runs on the sum of the plugin’s own frame steps, and until the vote settles those steps are the wall clock’s, so it carries those few milliseconds, never quite the same twice: two Restarts at the same settings draw the same flames, but at any Shimmer above 0 the plumes’ noise differs between them. Paused, a moved control redraws the frame without feeding or moving the flames. Step advances the clock 1/60 s, which feeds 800 samples.',
  'A frame here is one of the display’s refreshes (60 or 120 a second, and whenever the browser gets round to it), not Resolume’s output frame. On the feed() path the flames do not depend on it: every sample is integrated once, and rutest measures the flames bit-identical at 24, 30, 60 and 240 fps. Which instants are drawn does; and on “None (host FFT)” the fallback steps once a frame, so there it does depend on the frame rate, as in Resolume.',
  'WebAssembly is not the Mac’s arm64. It has no fused multiply-add, and its sin, cos, exp and pow are musl’s, not Apple’s, so the duct’s doubles can differ from the plugin built for a Mac in the last digits. What reaches the GPU is the flames’ lengths as float, as in the plugin, rasterised by this browser’s GPU, not the Mac’s.',
  'The Over’s clip is the kit’s generated one, or your own image or video. The kit hands over premultiplied colour, and the Over treats its input as premultiplied, so the shape-on-transparency clip is offered and shows the Over’s alpha (1 − (1 − clip alpha)(1 − tube)(1 − flames)); the Behind menu says what sits under it. SW Rubens, the source, reads no clip and writes alpha 1. In Arena the effect gets the composition’s own frame; here its whole texture is used, unpadded.',
  'The photosensitivity note at the top is the page’s, not the plugin’s; in embed mode (?embed=1, the output as a video source) it is not shown. No default was changed for it. The About block (a credit line and its buttons) is declared by the plugin and left off this panel.',
  'Nothing here is measured. The plugin has run in Resolume only briefly, where Resolume’s own sound analysis moved the flames to a demo drum loop, and has never opened a live audio device (its README’s Status says what was seen). Its harness, rutest, drives the same class headlessly on a synthetic clock: the duct’s impulse response matches its own continuous model to 1.4e-6, its lossless resonances sit on the closed form to 4.4e-16, the flames’ period read off the rendered picture matches propane’s sound speed to 0.15%, a slow sine dips the flames to 0.838 of their height and crosses back at 4.43 times the gas pressure against 4.33 from the √Δp model, and 13 wrong models and 25 one-character mutants are caught. That harness, not this page, is the reason to believe the tube. What the flames and the hot air above them look like is chosen, not measured, and no source gives a working tube’s gas pressure or sound level.',
];

//===========================================================================
// The page.
//===========================================================================

mounted = mountDemo({
  name: 'Rubens',
  pluginId: 'RU01 · RU02',
  kind: ['source', 'effect'],
  tagline:
    'Rubens’ flame tube for Resolume: the music moves a piston at one end of a propane-filled pipe, the gas resonates, and a row of flames along the top draws the standing wave. Nothing is animated by a parameter. Each hole’s flow goes as the square root of the pressure across it, outward only, into its flame, so the peaks sit L/n apart at propane’s sound speed, off-resonance notes barely move it, and the hot air above the flames bends the backdrop behind them. SW Rubens is the tube in front of a backdrop; SW Rubens Over shows your clip through the heat.',
  repo: 'https://github.com/stoatworks-labs/rubens',

  blurb:
    'It is the plugin’s own C++ — the plugin class, the duct and its holes integrated in double at 16 kHz, its audio input and earshot under it — compiled unmodified to WebAssembly, drawing with its own GLSL in WebGL2. The host is this page: the audio is the plugin’s harness’s own track, or a file of yours, fed through earshot’s feed() as a device’s samples would be, never a microphone and never played aloud (the Audio Input menu, Rescan Inputs and the live-device path are not here), and its “None (host FFT)” fallback runs on a synthetic FFT, not Resolume’s; SW Rubens Over runs on a generated clip.',

  // The plume is an R32F render target, as in the plugin.
  needFloat: true,
  // The Over's output carries the clip's alpha, premultiplied; the source's is 1.
  showBackdrop: true,

  variants: {
    label: 'Plugin',
    default: initialVariant,
    options: [
      { id: 'source', name: 'SW Rubens (source)', hint: 'RU01, FF_SOURCE: the tube in front of a backdrop (Black, Grid or Dots). Reads no clip.' },
      { id: 'over', name: 'SW Rubens Over (effect)', hint: 'RU02, FF_EFFECT: the same tube and flames in front of your clip, seen through the heat above them.' },
    ],
  },

  // For SW Rubens Over only. 'alpha' is in: the Over treats its input as
  // premultiplied, which is what the kit hands over.
  sources: ['scene', 'bars', 'grid', 'detail', 'spot', 'ramp', 'alpha'],

  params: PARAMS,
  presets: kitPresets(initialVariant),
  differences: DIFFERENCES,

  createRenderer: (gl) => {
    renderer = createRenderer(gl);
    return renderer;
  },
});

//===========================================================================
// Which controls belong to which plugin, the host row and the lines under
// the picture.
//
// By inline style, not the `hidden` attribute: kit.css gives these elements a
// `display` of their own, which beats the attribute's user-agent rule.
//===========================================================================

if (mounted) {
  const params = mounted.params;
  let shownVariant = mounted.state.variant;

  // Copy link carries the plugin and the audio choice (a file cannot travel in a link).
  const toQuery = params.toQuery.bind(params);
  params.toQuery = () => {
    const q = toQuery();
    if (mounted.state.variant === 'over') q.set('plugin', 'over');
    if (audio.id !== 'track' && audio.id !== 'file') q.set('audio', audio.id);
    return q;
  };

  const show = (node, on) => { if (node) node.style.display = on ? '' : 'none'; };
  const rowOf = (p) => document.getElementById(`p-${p.id}`)?.closest('.prow')
    ?? [...document.querySelectorAll('.prow')].find((row) => row.querySelector('.prow__name')?.textContent === p.name);

  function showForVariant(which) {
    for (const p of PARAMS) if (p.only) show(rowOf(p), p.only === which);
    const effect = which === 'over';
    for (const field of document.querySelectorAll('.transport__field')) {
      const label = field.querySelector('.transport__label')?.textContent;
      if (label === 'Clip' || label === 'Behind') show(field, effect);
    }
    show(document.querySelector('.transport__file:not(#rubens-audio-file-label)'), effect);
  }

  document.addEventListener('demo:state', () => {
    const which = mounted.state.variant;
    if (which !== shownVariant) {
      shownVariant = which;
      // A new instance from time 0, at that constructor's defaults (for
      // Defaults too).
      params.defaults = { ...DEFAULTS[which] };
      params.reset();
      restartClock();
    }
    if (!embed) showForVariant(which);
  });

  if (!embed) {
    showForVariant(shownVariant);

    // The kit's note says every declared parameter is here; four are not.
    const note = document.querySelector('.inspector__note');
    if (note) {
      note.textContent += ' Left out here: Audio Input and Rescan Inputs (no audio device exists in the browser), the Audio buffer (Resolume’s FFT; the host row under the picture stands in for all three) and the About block. Channel acts only on a stereo file of yours, FFT Law only on “None (host FFT)”.';
    }
    buildHostRow();
    buildStatusLine();
  }
}

//---------------------------------------------------------------------------
// The host row: the stand-in for what Resolume would hand the plugin.
//---------------------------------------------------------------------------
function field(label, control, title) {
  const wrap = document.createElement('label');
  wrap.className = 'transport__field';
  if (title) wrap.title = title;
  const span = document.createElement('span');
  span.className = 'transport__label';
  span.textContent = label;
  // Narrow screens: the field may shrink, and the control with it.
  wrap.style.maxWidth = '100%';
  wrap.style.minWidth = '0';
  control.style.maxWidth = '100%';
  control.style.minWidth = '0';
  wrap.append(span, control);
  return wrap;
}

/** Restart the page's clock: a new instance from time 0, as Restart does. */
function restartClock() {
  audio.epoch += 1;
  mounted.state.time = 0;
  mounted.state.frameIndex = 0;
  mounted.redraw();
}

function buildHostRow() {
  const transport = document.querySelector('.transport');
  if (!transport) return;
  const row = document.createElement('div');
  row.className = 'transport';
  row.id = 'rubens-host';
  const tag = document.createElement('span');
  tag.className = 'transport__label';
  tag.textContent = 'The host:';
  tag.title = 'The stand-in for what Resolume would hand the plugin. Not a plugin control.';

  const select = document.createElement('select');
  select.className = 'transport__select';
  select.id = 'rubens-audio';
  for (const a of AUDIO) {
    const option = new Option(a.name, a.id);
    option.title = a.hint;
    if (a.id === 'file') option.disabled = true;
    select.append(option);
  }
  select.value = audio.id;
  select.addEventListener('change', () => {
    audio.id = select.value;
    restartClock();
  });

  const pick = document.createElement('label');
  pick.className = 'btn transport__file';
  pick.id = 'rubens-audio-file-label';
  pick.textContent = 'Audio file…';
  pick.title = 'Choose a sound file on this machine. The browser decodes it here and the page feeds it to the plugin through feed(); it is not uploaded and not played aloud.';
  const input = document.createElement('input');
  input.type = 'file';
  input.accept = 'audio/*';
  input.id = 'rubens-audio-file';
  input.hidden = true;
  pick.append(input);
  const note = document.createElement('span');
  note.className = 'transport__label';
  note.id = 'rubens-audio-note';
  note.setAttribute('role', 'status');
  input.addEventListener('change', async () => {
    const file = input.files?.[0];
    if (!file) return;
    note.textContent = `Decoding ${file.name}…`;
    try {
      audio.file = await decodeFile(file);
      const fileOption = [...select.options].find((o) => o.value === 'file');
      fileOption.disabled = false;
      fileOption.textContent = `Your file: ${file.name}`;
      audio.id = 'file';
      select.value = 'file';
      note.textContent = `${file.name}: ${audio.file.seconds.toFixed(1)} s, ${audio.file.channels === 1 ? 'mono, on both channels' : `${audio.file.channels} channels, the first two`}, at 48 kHz, looped. It stays in this page.`;
      restartClock();
    } catch (error) {
      note.textContent = `That file could not be decoded: ${error.message ?? error}`;
    }
  });

  row.append(
    tag,
    field('Audio', select, 'What reaches the plugin in place of an audio input. The plugin’s Audio Input menu, Rescan Inputs and the live-device path are not in the browser: the page feeds earshot’s feed() as the plugin’s harness does. Changing it restarts the clock (a new instance).'),
    pick,
    note,
  );
  transport.after(row);
}

//---------------------------------------------------------------------------
// The lines under the picture: which way round the defaults sit, said once;
// the plugin's own state, through the accessors its harness reads; and its
// log. They report; they measure nothing.
//---------------------------------------------------------------------------
function buildStatusLine() {
  const stage = document.querySelector('.stage');
  if (!stage) return;
  const regime = document.createElement('p');
  regime.className = 'stage__status rb-regime';
  regime.id = 'rubens-regime';
  regime.textContent = 'Which way round: at the plugin’s defaults (Gas 3 Pa, Volume 145 dB) loud music flares the flames where the pressure swings most — the reversal Ficken & Stephenson reported, as Gee relates it, and the opposite of the textbook’s dips. Gas up (30 Pa) or Volume down (130 dB) turns it the right way up: the presets “Gas 30 Pa” and “Volume 130 dB” do. The defaults are the plugin’s, and this page leaves them as they are.';
  const line = document.createElement('p');
  line.className = 'stage__status rb-status';
  line.id = 'rubens-status';
  line.setAttribute('aria-live', 'off');
  const log = document.createElement('p');
  log.className = 'stage__status rb-log';
  log.id = 'rubens-log';
  log.setAttribute('aria-live', 'off');
  stage.append(regime, line, log);

  const cm = (m) => `${fixed(m * 100, 1)} cm`;
  // The last two seconds of the tallest and shortest flame against the quiet
  // height, sampled with the line: which way round the pattern sat.
  const recent = [];

  let lastLine = '';
  let lastLog = '';
  setInterval(() => {
    const s = renderer?.stats();
    if (!s) return;
    recent.push({ min: s.flameMin, max: s.flameMax, quiet: s.quiet, frames: s.frames, playing: mounted.state.playing });
    while (recent.length > 8) recent.shift();
    const window = recent.filter((r) => r.quiet === s.quiet);
    const hi = Math.max(...window.map((r) => r.max));
    const lo = Math.min(...window.map((r) => r.min));
    let way;
    if (s.source === 'silence') {
      way = 'nothing to move them';
    } else if (hi > s.quiet * 1.02) {
      way = `the tallest at ${cm(hi)}, above the quiet ${cm(s.quiet)}: the loudest swings beat the gas pressure and flare the flames there (the reversal)`;
    } else if (lo < s.quiet * 0.98) {
      way = `between ${cm(lo)} and ${cm(hi)}, none above the quiet ${cm(s.quiet)}: dips where the pressure swings most (the textbook way up)`;
    } else {
      way = `all within 2% of the quiet ${cm(s.quiet)}`;
    }

    let feedText;
    if (s.source === 'hostfft') {
      feedText = `nothing fed: the fallback on rutest --host-fft’s synthetic spectrum (earshot ${s.state}, the lamp amber)`;
    } else {
      const lamp = s.state === 'Silent' ? 'steady red: silent' : s.fromDevice ? 'green' : 'amber';
      const what = s.source === 'file' ? 'your file' : s.source === 'silence' ? 'zeros' : 'the track';
      feedText = `${what} through feed(), ${(s.fed / FEED_RATE).toFixed(2)} s so far (earshot ${s.state}, the lamp ${lamp})`;
    }
    const next = `${pluginName(s.variant)}: ${s.holes} holes at ${fixed(s.pitch * 100, 2)} cm, ${s.modes} modes below the cross-mode at ${fixed(s.cutOn / 1000, 2)} kHz, the holes’ f_H ${fixed(s.fH, 1)} Hz`
      + ` · the flames over the last two seconds: ${way}; this frame ${cm(s.flameMin)} to ${cm(s.flameMax)}`
      + ` · ${feedText} · ${s.samples} samples at 16 kHz into the duct this frame`
      + ` · the plugin’s clock at ${s.now.toFixed(2)} s`;
    if (next !== lastLine) {
      lastLine = next;
      line.textContent = next;
    }

    const textLog = renderer.log();
    if (textLog === lastLog) return;
    lastLog = textLog;
    const lines = textLog.trim().split('\n').filter(Boolean).map((l) => l.replace(/^\S+ (INFO |WARN |ERROR) rubens: /, '$1 '));
    log.textContent = lines.length ? `The plugin’s own log (Diag.cpp, in this page’s memory):\n${lines.slice(-2).join('\n')}` : '';
  }, 250);
}

// For checking the page from outside (the crosscheck against rutest reads the
// declarations and the inverse conversions through these).
export { renderer, mounted, PARAMS, DECLARED, DEFAULTS, core, hostValue, kitValues, audio, restartClock };
