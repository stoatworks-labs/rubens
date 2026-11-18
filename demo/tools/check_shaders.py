"""The demo's copies of the plugin must be the plugin, character for character.

    python3 demo/tools/check_shaders.py           compare; exit 1 on any drift
    python3 demo/tools/check_shaders.py --write   regenerate demo/shaders.js

Called from `tools/verify.sh`. Exit code 1 means a copy has drifted.
(mirrorball's script, which is pinhole's and fountain's, with rubens' pieces,
its `uniform()` overloads and geometryUniforms(), and woofer's harness, device
and earshot checks.)

------------------------------------------------------------------- why

The page at rubens-demo.stoatworks-labs.com carries copies of this repository,
and copies drift quietly: flames that follow the pressure's square instead of
the holes' outflow, or a track whose lead sits an octave up, still look like a
Rubens tube.

1. **The GLSL.** `demo/shaders.js` holds the four GLSL pieces of
   `source/Shaders.cpp` (kQuadVertex, kCommon, kPlumeFragment,
   kCompositeFragment) plus kVersion, AND the order the plugin joins them in:
   Rubens.cpp's InitGL builds one vertex stage, `kVersion + kQuadVertex`, and
   compiles each pass of its stage table with it and `Assemble( pass )`'s
   fragment stage. This script reads that order out of the C++ and writes it
   into shaders.js as `ASSEMBLY`. The page builds its expected text for every
   stage from that table, and at run time REQUIRES the text the compiled plugin
   hands to glShaderSource to equal one of them before the kit's port() is
   applied (demo/wasm/gl_shim.cpp). The pieces are compared exactly -- no
   whitespace normalisation, no comment stripping: a comment updated on one
   side only is drift worth catching. Then:

     - every `const char* const k... = R"(` piece in Shaders.cpp must be one
       this script knows, so a new piece cannot be added to the plugin and
       silently left out of the page;
     - every value of `enum class Pass` in Shaders.h must be a case of
       Assemble()'s one switch, InitGL's stage table must compile every one of
       them, in the enum's order, and ASSEMBLY must list exactly those stages,
       piece for piece, in the plugin's order;
     - and shaders.js must be byte for byte what `--write` produces, so
       nothing else can be typed into it.

   The one transformation is a decode: a backtick cannot sit raw in a template
   literal, so shaders.js would escape it as \\`. Any other backslash, or a
   `${`, on the JS side is rejected; the C++ bodies hold neither.

2. **Every uniform's setter.** The page runs the plugin's own glUniform calls,
   and WebGL2 is stricter about them than a desktop driver is allowed to be
   lenient: a float sent to an `int` or a sampler is INVALID_OPERATION and the
   uniform stays 0, silently, and a sampler nobody sets sits on unit 0 with
   whatever is bound there. Rubens.cpp sets most of its uniforms through three
   `uniform()` overloads (int, float, two floats), so this reads which glUniform
   call each overload makes, works out which overload every
   `uniform( <pass>Shader, "Name", ... )` call picks from its arguments (an
   argument whose type it cannot tell is a failure, never a guess), adds the
   direct `glUniformNx( where( <pass>Shader, "Name" ), ... )` calls, and
   expands geometryUniforms() -- whose calls name its parameter `shader` -- into
   one set of calls for each pass it is called with. Each uniform a pass
   declares must be set for that pass by the setter its type takes, and nothing
   set that the pass does not declare.

3. **rutest's track.** The page feeds the plugin rutest's own music through
   earshot's feed(). demo/wasm/glue.cpp carries tools/rutest/main.cpp's
   `pcg`, `hash01`, `fadeIn`, `kickAt`, `noteAt` and `musicAt`, with the line
   they need (kFeedRate), and --host-fft's feedKicks() lines and the Rig's
   Bins() loop (with `instance->plugin` for rutest's `plugin`); each must be
   rutest's text character for character.

4. **No device.** No file the page serves may name getUserMedia,
   enumerateDevices or mediaDevices: the page never asks for a microphone or
   lists an audio device, and this keeps it so.

5. **The WebAssembly.** `demo/rubens-core.wasm` is the plugin's C++ and
   earshot's compiled by `demo/tools/build-wasm.sh`, and committed, because the
   deploy has no build step. Building it needs emscripten, which verify.sh
   cannot assume, so this checks the next best thing: `demo/wasm/inputs.sha256`,
   written by the build, records the hash of every file that went in (every
   source/*.h, the seven plugin .cpp files, earshot's sources and headers, the
   SDK files, the glue, the shim and the build script), of both outputs, and the
   SDK commit. A source file changed since the last build -- a regenerated
   StoatworksAbout.h or a new earshot snapshot included -- or an output that is
   not the one the build wrote fails here, and the fix is to rerun
   build-wasm.sh.

------------------------------------------------------------------- what it cannot

It cannot tell whether the .wasm really is what those inputs compile to (only
rebuilding can), and it says nothing about demo/plugin.js beyond rule 4: the
panel is built from the plugin's own declarations at run time, but its read-out
units, its tooltips, its presets and its layout are the page's.
"""
import collections
import hashlib
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.dont_write_bytecode = True

# JS constant, C++ symbol (all in source/Shaders.cpp), in the order they are
# declared there. kVersion is a plain string and is handled on its own.
SHADERS = [
    ("QUAD_VERTEX", "kQuadVertex"),
    ("COMMON", "kCommon"),
    ("PLUME_FRAGMENT", "kPlumeFragment"),
    ("COMPOSITE_FRAGMENT", "kCompositeFragment"),
]
JS_NAME = dict((symbol, name) for name, symbol in SHADERS)
JS_NAME["kVersion"] = "VERSION"

# The setter each GLSL uniform type takes, as Rubens.cpp spells it.
SETTER = {
    "sampler2D": "glUniform1i",
    "int": "glUniform1i",
    "bool": "glUniform1i",
    "uint": "glUniform1ui",
    "ivec2": "glUniform2i",
    "ivec3": "glUniform3i",
    "ivec4": "glUniform4i",
    "float": "glUniform1f",
    "vec2": "glUniform2f",
    "vec3": "glUniform3f",
    "vec4": "glUniform4f",
}

# Rubens.cpp's uniform() overloads, by the C++ type their value takes.
OVERLOADS = {"int v": "int", "float v": "float", "float a, float b": "float2"}

# The helper that sets the geometry on whichever pass it is handed.
GEOMETRY = "geometryUniforms"

# rutest's track, copied into the glue.
HARNESS = ["pcg", "hash01", "fadeIn", "kickAt", "noteAt", "musicAt"]
HARNESS_LINES = [
    "constexpr int kFeedRate = 48000;",
    # --host-fft: feedKicks(), and the Rig's Bins() it writes through.
    "const int since = ( frame - 20 ) % 30;",
    "const bool hard = ( ( frame - 20 ) / 30 ) % 2 == 1;",
    "double v = bin > 40 ? 0.05 : 0.02;",
    "if( frame >= 20 && since < 4 && bin < 10 )",
    "v += ( hard ? 0.9 : 0.25 ) * std::exp( -since * 0.8 );",
    "const unsigned int host = static_cast< unsigned int >( plugin.HostIndexOf( PT_AUDIO ) );",
    "for( int bin = 0; bin < 64; ++bin )",
]

# What a page that never touches a device must never name.
DEVICE_APIS = ("getUserMedia", "enumerateDevices", "mediaDevices")

HEADER = """// GENERATED from source/Shaders.cpp by demo/tools/check_shaders.py --write.
// Do not edit: tools/verify.sh fails if a character of this differs from the
// plugin's. The one escape is \\` for a backtick inside a comment.
"""

MANIFEST = os.path.join("demo", "wasm", "inputs.sha256")
OUTPUTS = ("demo/rubens-core.js", "demo/rubens-core.wasm")
REGISTRATION = ("SourcePlugin.cpp", "EffectPlugin.cpp")
# earshot's device backend, which build-wasm.sh leaves out (EARSHOT_CAPTURE=OFF).
NOT_COMPILED = ("Miniaudio.cpp", "MiniaudioImpl.cpp")


class Stale(Exception):
    """The C++ is not shaped the way this script reads it: fail, never guess."""


def read(*parts):
    with open(os.path.join(REPO, *parts)) as handle:
        return handle.read()


def cpp_version(source):
    match = re.search(r'const char\* const kVersion = "(.*?)";', source)
    return None if match is None else match.group(1)


def from_cpp(source, symbol):
    match = re.search(r'const char\* const ' + symbol + r' = R"\((.*?)\)";', source, re.S)
    return None if match is None else match.group(1)


def from_js(source, name):
    match = re.search(r'^export const ' + name + r' = `(.*?)`;$', source, re.S | re.M)
    if match is None:
        return None, None
    body = match.group(1)
    stray = re.search(r"\\(?!`)", body)
    if stray is not None:
        line = body[: stray.start()].count("\n") + 1
        return None, f"backslash that is not an escaped backtick, at line {line}"
    if "${" in body:
        return None, "template substitution"
    return body.replace("\\`", "`"), None


def unknown_pieces(cpp):
    """Raw-string pieces in Shaders.cpp that this script does not copy."""
    declared = re.findall(r'const char\* const (k\w+) = R"\(', cpp)
    return [symbol for symbol in declared if symbol not in JS_NAME]


def pieces_of(expression, where, prefix=None):
    """`std::string( kVersion ) + kA + kB` -> [kVersion, kA, kB]; a term that is
    `prefix` (Assemble()'s local) is replaced by the pieces it holds."""
    pieces = []
    for term in (t.strip() for t in expression.split("+")):
        if prefix is not None and term == prefix[0]:
            pieces.extend(prefix[1])
            continue
        m = re.fullmatch(r"(?:std::string\(\s*)?(?:shaders::)?(k\w+)(?:\s*\))?", term)
        if m is None:
            raise Stale(f"{where} joins `{term}`, which is not a piece")
        pieces.append(m.group(1))
    return pieces


def passes_of(shaders_h):
    enum = re.search(r"enum class Pass\s*\{(.*?)\};", shaders_h, re.S)
    if enum is None:
        raise Stale("no `enum class Pass` in source/Shaders.h")
    return [p.strip() for p in enum.group(1).split(",") if p.strip()]


def fragment_assembly(cpp, passes):
    """{pass: [pieces]} from Shaders.cpp's `std::string Assemble( Pass pass )`:
    one local `const std::string text = <pieces>;`, then one switch whose every
    case is `return text + kX...;`, then (unreachable, every case covered)
    `return text;`."""
    body = re.search(r"std::string Assemble\( Pass (\w+) \)\s*\{(.*?)\n\}", cpp, re.S)
    if body is None:
        raise Stale("no Assemble( Pass pass ) in source/Shaders.cpp")
    text = body.group(2)
    local = re.findall(r"const std::string (\w+) = (.*?);", text, re.S)
    if len(local) != 1:
        raise Stale("Assemble() does not start from one `const std::string text = ...;`")
    prefix = (local[0][0], pieces_of(local[0][1], "Assemble()'s prefix"))
    if text.count("switch") != 1:
        raise Stale("Assemble() is not one switch")
    after = text[text.rindex("}") + 1:] if "}" in text else ""
    if re.sub(r"\s+", " ", after).strip() != f"return {prefix[0]};":
        raise Stale(f"Assemble() ends in `{after.strip()}`, not `return {prefix[0]};` after its switch")
    out = {}
    for name, expression in re.findall(r"case Pass::(\w+):\s*return (.*?);", text, re.S):
        out[name] = pieces_of(expression, f"Assemble()'s case {name}", prefix)
    if sorted(out) != sorted(passes):
        raise Stale(f"Assemble()'s cases ({sorted(out)}) are not the Pass enum ({passes})")
    if len(re.findall(r"return ", text)) != len(out) + 1:
        raise Stale("Assemble() returns somewhere other than its cases and its last line")
    return out


def vertex_assembly(plugin_cpp):
    """InitGL's one vertex stage: `const std::string vertex = <pieces>;`."""
    found = re.findall(r"const std::string vertex = (.*?);", plugin_cpp, re.S)
    if len(found) != 1:
        raise Stale("Rubens.cpp's InitGL has no single `const std::string vertex = ...;`")
    return pieces_of(found[0], "InitGL's vertex stage")


def stage_table(plugin_cpp):
    """InitGL's `const Stage stages[]`: [(shader member, pass)] in its order."""
    for needed in ("const std::string fragment = shaders::Assemble( stage.pass );",
                   "stage.shader->Compile( vertex.c_str(), fragment.c_str() )"):
        if needed not in plugin_cpp:
            raise Stale(f"Rubens.cpp's InitGL no longer has `{needed}`")
    if len(re.findall(r"\.Compile\(|->Compile\(", plugin_cpp)) != 1:
        raise Stale("Rubens.cpp compiles a shader outside InitGL's stage table")
    stages = re.search(r"const Stage stages\[\] = \{(.*?)\};", plugin_cpp, re.S)
    if stages is None:
        raise Stale("no `const Stage stages[]` in Rubens.cpp's InitGL")
    rows = re.findall(r"\{\s*&(\w+),\s*shaders::Pass::(\w+),", stages.group(1))
    if not rows:
        raise Stale("InitGL's stage table has no `{ &shader, shaders::Pass::X, ... }` rows")
    return rows


def assembly(cpp, plugin_cpp, shaders_h):
    """The plugin's own assembly: every pass from the Pass enum, its vertex
    stage InitGL's one, its fragment stage from Assemble(), as InitGL compiles
    them. In JS names, keyed `<pass>_vertex` / `<pass>_fragment`."""
    passes = passes_of(shaders_h)
    vertex = vertex_assembly(plugin_cpp)
    fragment = fragment_assembly(cpp, passes)

    compiled = [p for _, p in stage_table(plugin_cpp)]
    if compiled != passes:
        raise Stale(f"InitGL compiles {compiled}, the Pass enum is {passes}")

    out = []
    for name in passes:
        for stage, symbols in (("vertex", vertex), ("fragment", fragment[name])):
            for symbol in symbols:
                if symbol not in JS_NAME:
                    raise Stale(f"{name} {stage} uses {symbol}, a piece this script does not copy")
            out.append((f"{name.lower()}_{stage}", [JS_NAME[s] for s in symbols]))
    return out


def render(cpp, plugin_cpp, shaders_h):
    """shaders.js as --write produces it."""
    version = cpp_version(cpp)
    if version is None:
        raise Stale("kVersion not found in source/Shaders.cpp")
    out = [HEADER]
    out.append(f"export const VERSION = '{version}';\n")
    for name, symbol in SHADERS:
        body = from_cpp(cpp, symbol)
        if body is None:
            raise Stale(f"{symbol} not found in source/Shaders.cpp")
        if "\\" in body or "${" in body:
            raise Stale(f"{symbol} holds a backslash or ${{; the escape scheme cannot carry it")
        out.append(f"\n// {symbol}, source/Shaders.cpp\nexport const {name} = `{body.replace('`', chr(92) + '`')}`;\n")
    out.append(
        "\n// Each stage's source, piece by piece, in the order the plugin joins them:\n"
        "// every pass as Rubens.cpp's InitGL compiles it, its vertex stage\n"
        "// InitGL's one and its fragment stage from Shaders.cpp's Assemble().\n"
        "export const ASSEMBLY = {\n"
    )
    for name, pieces in assembly(cpp, plugin_cpp, shaders_h):
        out.append(f"  {name}: [{', '.join(repr(p) for p in pieces)}],\n")
    out.append("};\n")
    return "".join(out)


def js_version(js):
    """kVersion's text as shaders.js carries it: the C literal's own escapes,
    which mean the same in a JS string."""
    match = re.search(r"^export const VERSION = '(.*?)';$", js, re.M)
    return None if match is None else match.group(1)


def first_difference(a, b):
    left, right = a.splitlines(), b.splitlines()
    for i in range(max(len(left), len(right))):
        x = left[i] if i < len(left) else "<missing>"
        y = right[i] if i < len(right) else "<missing>"
        if x != y:
            return i + 1, x, y
    return None


def check_shaders(cpp, plugin_cpp, shaders_h, js):
    problems = 0

    for symbol in unknown_pieces(cpp):
        print(f"FAIL  {symbol} is a piece of source/Shaders.cpp this script does not copy -- add it to SHADERS")
        problems += 1

    version_cpp = cpp_version(cpp)
    if version_cpp is None or js_version(js) != version_cpp:
        print("FAIL  VERSION does not match kVersion")
        problems += 1
    else:
        print(f"ok    {'VERSION':<24} matches kVersion")

    for name, symbol in SHADERS:
        cpp_text = from_cpp(cpp, symbol)
        js_text, complaint = from_js(js, name)
        if cpp_text is None:
            print(f"FAIL  {symbol} not found in source/Shaders.cpp")
            problems += 1
        elif complaint is not None:
            print(f"FAIL  {name} in demo/shaders.js has a {complaint}")
            problems += 1
        elif js_text is None:
            print(f"FAIL  {name} not found in demo/shaders.js")
            problems += 1
        elif cpp_text == js_text:
            print(f"ok    {name:<24} matches {symbol} ({len(cpp_text)} chars)")
        else:
            problems += 1
            print(f"FAIL  {name} has drifted from {symbol}")
            where = first_difference(cpp_text, js_text)
            if where:
                print(f"        first difference at line {where[0]}")
                print(f"          C++: {where[1]}")
                print(f"          js : {where[2]}")

    try:
        table = assembly(cpp, plugin_cpp, shaders_h)
    except Stale as stale:
        print(f"FAIL  the plugin's assembly could not be read: {stale}")
        return problems + 1
    block = re.search(r"^export const ASSEMBLY = \{\n(.*?)^\};$", js, re.S | re.M)
    rows = {} if block is None else dict(re.findall(r"^  (\w+): \[(.*)\],$", block.group(1), re.M))
    for name, pieces in table:
        want = ", ".join(repr(p) for p in pieces)
        if rows.get(name) == want:
            print(f"ok    {'ASSEMBLY.' + name:<24} {' + '.join(pieces)}")
        else:
            print(f"FAIL  ASSEMBLY.{name} is [{rows.get(name, '<missing>')}], the plugin's is [{want}]")
            problems += 1
    for name in sorted(set(rows) - set(n for n, _ in table)):
        print(f"FAIL  ASSEMBLY.{name} is no stage of the plugin's")
        problems += 1

    # Anything else typed into the file.
    try:
        expected = render(cpp, plugin_cpp, shaders_h)
    except Stale as stale:
        print(f"FAIL  {stale}")
        return problems + 1
    if expected != js:
        where = first_difference(expected, js)
        print("FAIL  demo/shaders.js is not exactly what --write produces")
        if where:
            print(f"        first difference at line {where[0]}")
            print(f"          want: {where[1]}")
            print(f"          have: {where[2]}")
        problems += 1
    return problems


#---------------------------------------------------------------------------
# The uniforms.
#---------------------------------------------------------------------------

def declared_uniforms(text):
    """{name: type} of every `uniform <type> <Name>;` in a stage's source."""
    out = {}
    for kind, name in re.findall(r"^\s*uniform\s+(\w+)\s+(\w+)\s*;", text, re.M):
        out[name] = kind
    return out


def split_top(text, sep):
    """Split at `sep` characters outside parentheses and braces."""
    parts, depth, start = [], 0, 0
    for i, c in enumerate(text):
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif c == sep and depth == 0:
            parts.append(text[start:i].strip())
            start = i + 1
    parts.append(text[start:].strip())
    return parts


def ternary(expression):
    """`c ? a : b` at the top level -> (a, b), else None. `::` is not a colon."""
    depth, question = 0, None
    for i, c in enumerate(expression):
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif depth == 0 and c == "?" and question is None:
            question = i
        elif depth == 0 and c == ":" and question is not None:
            if expression[i - 1] == ":" or (i + 1 < len(expression) and expression[i + 1] == ":"):
                continue
            return expression[question + 1:i].strip(), expression[i + 1:].strip()
    return None


class Types:
    """The declared C++ types of the names an argument can be built from: the
    members of Rubens.h's structs and class, Rubens.cpp's parameters and
    locals, and the functions Controls.h declares. Only the types an overload
    can take are kept; a name declared with two of them is ambiguous and fails
    when it is used."""

    KINDS = ("int", "double", "float", "bool", "uint32_t")

    def __init__(self, *sources):
        self.names = collections.defaultdict(set)
        self.calls = collections.defaultdict(set)
        kinds = "|".join(self.KINDS)
        for source in sources:
            # `int holes = 0;`, `double pitch = 0.0;`: every declarator.
            for kind, rest in re.findall(r"(?<![\w:])(?:const\s+)?(" + kinds + r")\s+(\w[^;()]*);", source):
                for declarator in split_top(rest, ","):
                    m = re.match(r"(\w+)", declarator)
                    if m:
                        self.names[m.group(1)].add(kind)
            # `int kind = OptionIndex( ... );` and a parameter `int kind )`.
            for kind, name in re.findall(r"(?<![\w:])(?:const\s+)?(" + kinds + r")&?\s+(\w+)\s*(?:=|,|\))", source):
                self.names[name].add(kind)
            for kind, name in re.findall(r"(?<![\w:])(" + "|".join(self.KINDS) + r")\s+(\w+)\(", source):
                self.calls[name].add(kind)

    def of_name(self, name):
        kinds = self.names.get(name, set())
        if len(kinds) != 1:
            raise Stale(f"`{name}` is declared as {sorted(kinds) or 'nothing this script can see'}")
        return next(iter(kinds))

    def of_call(self, name):
        kinds = self.calls.get(name, set())
        if len(kinds) != 1:
            raise Stale(f"`{name}()` returns {sorted(kinds) or 'nothing this script can see'}")
        return next(iter(kinds))


def type_of(expression, types):
    """The C++ type of a uniform() argument, as overload resolution sees it:
    'int' or 'float'. Anything else, or anything unclear, is Stale."""
    e = expression.strip()
    branches = ternary(e)
    if branches is not None:
        a, b = type_of(branches[0], types), type_of(branches[1], types)
        if a != b:
            raise Stale(f"`{e}` mixes {a} and {b}")
        return a
    if re.fullmatch(r"-?\d+", e):
        return "int"
    if re.fullmatch(r"-?\d+\.\d*f", e):
        return "float"
    if e.startswith("static_cast< float >"):
        # A float, or a float divided by one: every such argument here.
        if not all(t.strip().startswith("static_cast< float >") for t in split_top(e, "/")):
            raise Stale(f"`{e}` is not a static_cast< float > or a quotient of them")
        return "float"
    m = re.fullmatch(r"std::clamp\((.*)\)", e, re.S)
    if m is not None:
        kinds = {type_of(a, types) if re.fullmatch(r"-?[\d.]+f?", a.strip()) else None for a in split_top(m.group(1), ",")}
        kinds.discard(None)
        if len(kinds) != 1:
            raise Stale(f"`{e}`: std::clamp's bounds do not say its type")
        return kinds.pop()
    m = re.fullmatch(r"(\w+)\(.*\)", e, re.S)
    if m is not None:
        kind = types.of_call(m.group(1))
    else:
        m = re.fullmatch(r"(?:\w+(?:\.|->))*(\w+)(\(\s*\))?", e)
        if m is None:
            raise Stale(f"cannot tell the type of `{e}`")
        kind = types.of_call(m.group(1)) if m.group(2) else types.of_name(m.group(1))
    if kind not in ("int", "float"):
        raise Stale(f"`{e}` is {kind}, which no uniform() overload takes exactly")
    return kind


def overload_setters(plugin_cpp):
    """{C++ type: glUniform call} from Rubens.cpp's uniform() overloads."""
    found = re.findall(
        r"void RubensPlugin::uniform\( const FFGLShader& shader, const char\* name, ([^)]*) \)\s*\{\s*(glUniform\w+)\( where\( shader, name \),",
        plugin_cpp)
    out = {}
    for signature, setter in found:
        if signature not in OVERLOADS:
            raise Stale(f"a uniform() overload takes `{signature}`, which this script does not know")
        out[OVERLOADS[signature]] = setter
    if sorted(out) != sorted(OVERLOADS.values()):
        raise Stale(f"the uniform() overloads are {sorted(out)}, not {sorted(OVERLOADS.values())}")
    if len(re.findall(r"void RubensPlugin::uniform\(", plugin_cpp)) != len(found):
        raise Stale("a uniform() overload is not one glUniform call on where( shader, name )")
    return out


def function_body(source, signature):
    """The text between a definition's `{` and its closing `}` at the start of
    a line, for `signature` (the definition's first line, up to its `)`)."""
    match = re.search(re.escape(signature) + r"\s*\{(.*?)^\}$", source, re.S | re.M)
    if match is None:
        raise Stale(f"no `{signature}` in Rubens.cpp")
    return match.start(1), match.end(1)


def set_uniforms(plugin_cpp, types):
    """[(shader member, uniform, setter)] for every uniform Rubens.cpp sets:
    through a uniform() overload, or straight through where(), with
    geometryUniforms()'s calls on its parameter `shader` once for every pass
    it is called with. Anything else that names a uniform is a shape this
    script cannot read."""
    setters = overload_setters(plugin_cpp)
    start, end = function_body(plugin_cpp, f"void RubensPlugin::{GEOMETRY}( const FFGLShader& shader, int width, int height )")
    callers = re.findall(r"(?<![\w:])" + GEOMETRY + r"\(\s*(\w+),", plugin_cpp)
    if not callers:
        raise Stale(f"{GEOMETRY}() is defined and never called")

    raw = []
    for m in re.finditer(r"(?<![\w:])uniform\(\s*(\w+),\s*\"(\w+)\",(.*?)\);", plugin_cpp, re.S):
        args = split_top(m.group(3), ",")
        if len(args) == 2:
            kind = "float2"  # the one two-value overload: both converted to float
        elif len(args) == 1:
            kind = type_of(args[0], types)
        else:
            raise Stale(f"uniform( {m.group(1)}, \"{m.group(2)}\", ... ) passes {len(args)} values")
        raw.append((m.start(), m.group(1), m.group(2), setters[kind]))
    for m in re.finditer(r"(glUniform\w+)\(\s*where\(\s*(\w+),\s*\"(\w+)\"\s*\)", plugin_cpp):
        raw.append((m.start(), m.group(2), m.group(3), m.group(1)))
    named = len(re.findall(r"(?<![\w:])uniform\(\s*\w+,\s*\"", plugin_cpp)) + len(re.findall(r"\bwhere\(\s*\w+,\s*\"", plugin_cpp))
    if named != len(raw):
        raise Stale(f"Rubens.cpp names {named} uniforms and this script reads {len(raw)} of them")

    out = []
    inside = 0
    for at, shader, name, setter in raw:
        if start <= at < end:
            if shader != "shader":
                raise Stale(f"{GEOMETRY}() sets {name} on {shader}, not on its parameter")
            inside += 1
            out.extend((caller, name, setter) for caller in callers)
        elif shader == "shader":
            raise Stale(f"{name} is set on `shader` outside {GEOMETRY}()")
        else:
            out.append((shader, name, setter))
    if inside == 0:
        raise Stale(f"{GEOMETRY}() sets no uniform")
    return out


def check_uniforms(js_pieces, table, plugin_cpp, types):
    """Every declared uniform set for its pass, by its type's setter; nothing
    set that the pass does not declare."""
    problems = 0
    try:
        rows = stage_table(plugin_cpp)
        calls = set_uniforms(plugin_cpp, types)
    except Stale as stale:
        print(f"FAIL  the plugin's uniforms could not be read: {stale}")
        return 1
    stages = dict(table)
    by_shader = collections.defaultdict(list)
    for shader, name, setter in calls:
        by_shader[shader].append((name, setter))
    for shader, pass_name in rows:
        text = "".join(js_pieces[p] for p in stages[f"{pass_name.lower()}_vertex"] + stages[f"{pass_name.lower()}_fragment"])
        declared = declared_uniforms(text)
        done = by_shader.pop(shader, [])
        missing = sorted(set(declared) - {n for n, _ in done})
        for name in missing:
            print(f"FAIL  {pass_name}: uniform {declared[name]} {name} is never set (a sampler would sit on unit 0)")
            problems += 1
        for name, setter in done:
            if name not in declared:
                print(f"FAIL  {pass_name}: {setter} sets {name}, which the pass does not declare")
                problems += 1
            elif SETTER.get(declared[name]) != setter:
                print(f"FAIL  {pass_name}: {name} is {declared[name]} and is set with {setter}, not {SETTER.get(declared[name], '?')}")
                problems += 1
        if not missing and all(n in declared and SETTER.get(declared[n]) == s for n, s in done):
            print(f"ok    {'UNIFORMS.' + pass_name.lower():<24} all {len(declared)} set, each by its type's setter ({len(done)} calls)")
    for shader in sorted(by_shader):
        print(f"FAIL  {shader} has uniforms set but is no pass in InitGL's stage table")
        problems += 1
    return problems


#---------------------------------------------------------------------------
# rutest's track, and no device.
#---------------------------------------------------------------------------

def function_text(source, name):
    """A top-level C++ function: from the line that declares `name(` at the
    start of a line (its return type first) to the first `}` at the start of
    a line after it."""
    match = re.search(r"^[A-Za-z_][\w:<>&* ]*\b" + name + r"\(.*?^\}$", source, re.S | re.M)
    return None if match is None else match.group(0)


def check_harness(main_cpp, glue):
    problems = 0
    for name in HARNESS:
        theirs = function_text(main_cpp, name)
        ours = function_text(glue, name)
        if theirs is None:
            print(f"FAIL  {name} not found in tools/rutest/main.cpp")
            problems += 1
        elif ours is None:
            print(f"FAIL  {name} not found in demo/wasm/glue.cpp")
            problems += 1
        elif ours == theirs:
            print(f"ok    {name:<24} matches tools/rutest/main.cpp's ({len(ours)} chars)")
        else:
            print(f"FAIL  {name} in demo/wasm/glue.cpp has drifted from tools/rutest/main.cpp's")
            where = first_difference(theirs, ours)
            if where:
                print(f"        first difference at line {where[0]}")
                print(f"          rutest: {where[1]}")
                print(f"          glue  : {where[2]}")
            problems += 1
    rutest_lines = {line.strip() for line in main_cpp.splitlines()}
    # The glue's instance is rutest's rig's plugin: the one renaming the check allows.
    glue_lines = {line.strip().replace("instance->plugin.", "plugin.") for line in glue.splitlines()}
    for line in HARNESS_LINES:
        missing = [where for where, lines in (("tools/rutest/main.cpp", rutest_lines), ("demo/wasm/glue.cpp", glue_lines)) if line not in lines]
        if missing:
            print(f"FAIL  `{line}` is not in {' or '.join(missing)}")
            problems += 1
    if problems == 0:
        print(f"ok    {'HARNESS LINES':<24} all {len(HARNESS_LINES)} in both (kFeedRate, --host-fft's kicks and the Audio buffer's loop)")
    return problems


def check_no_devices():
    problems = 0
    demo = os.path.join(REPO, "demo")
    served = []
    for root, dirs, files in os.walk(demo):
        rel = os.path.relpath(root, demo)
        if rel.split(os.sep)[0] in ("tools", "wasm"):
            continue  # not served (.assetsignore), and this file names them
        for name in files:
            if name.endswith((".js", ".html")):
                served.append(os.path.join(root, name))
    for path in sorted(served):
        with open(path, encoding="utf-8", errors="replace") as handle:
            text = handle.read()
        for api in DEVICE_APIS:
            if api in text:
                print(f"FAIL  {os.path.relpath(path, REPO)} names {api}: the page must never ask for or list an audio device")
                problems += 1
    if problems == 0:
        print(f"ok    {'NO DEVICES':<24} none of the {len(served)} served scripts and pages names {', '.join(DEVICE_APIS)}")
    return problems


#---------------------------------------------------------------------------
# The WebAssembly.
#---------------------------------------------------------------------------

def sha256(path):
    digest = hashlib.sha256()
    with open(os.path.join(REPO, path), "rb") as handle:
        for block in iter(lambda: handle.read(1 << 16), b""):
            digest.update(block)
    return digest.hexdigest()


def check_wasm():
    try:
        lines = read(MANIFEST).splitlines()
    except FileNotFoundError:
        print(f"FAIL  {MANIFEST} is missing -- run demo/tools/build-wasm.sh")
        return 1

    problems = 0
    entries = 0
    recorded = set()
    for line in lines:
        pin = re.match(r"^# FFGL SDK ([0-9a-f]{40})$", line)
        if pin:
            try:
                head = subprocess.run(["git", "-C", os.path.join(REPO, "external", "ffgl"), "rev-parse", "HEAD"],
                                      capture_output=True, text=True, check=True).stdout.strip()
            except (OSError, subprocess.CalledProcessError):
                print("FAIL  cannot read the FFGL SDK's commit -- is the submodule checked out?")
                problems += 1
                continue
            if head != pin.group(1):
                print(f"FAIL  the .wasm was built against FFGL SDK {pin.group(1)[:7]}, the submodule is at {head[:7]}")
                problems += 1
            continue
        if not line or line.startswith("#"):
            continue
        want, path = line.split(None, 1)
        recorded.add(path)
        entries += 1
        if not os.path.exists(os.path.join(REPO, path)):
            print(f"FAIL  {path} went into the .wasm and no longer exists")
            problems += 1
        elif sha256(path) != want:
            print(f"FAIL  {path} has changed since demo/rubens-core.wasm was built -- rerun demo/tools/build-wasm.sh")
            problems += 1

    # A source file added since the build is an input the build never saw.
    trees = [("source", REGISTRATION), (os.path.join("external", "earshot", "src"), NOT_COMPILED),
             (os.path.join("external", "earshot", "include", "earshot"), ())]
    for tree, skipped in trees:
        for name in sorted(os.listdir(os.path.join(REPO, tree))):
            if name in skipped:
                continue
            rel = f"{tree}/{name}".replace(os.sep, "/")
            if (name.endswith(".h") or name.endswith(".cpp")) and rel not in recorded:
                print(f"FAIL  {rel} is newer than the .wasm's manifest -- rebuild")
                problems += 1

    for output in OUTPUTS:
        if output not in recorded:
            print(f"FAIL  {MANIFEST} does not record {output}")
            problems += 1
    if problems == 0:
        print(f"ok    {'WASM':<24} built from these sources ({entries} files hashed, SDK pin agrees)")
    return problems


def main(argv):
    cpp = read("source", "Shaders.cpp")
    plugin_cpp = read("source", "Rubens.cpp")
    shaders_h = read("source", "Shaders.h")
    if "--write" in argv:
        unknown = unknown_pieces(cpp)
        if unknown:
            print(f"FAIL  {', '.join(unknown)}: piece(s) this script does not copy -- add them to SHADERS")
            return 1
        try:
            text = render(cpp, plugin_cpp, shaders_h)
        except Stale as stale:
            print(f"FAIL  {stale}")
            return 1
        with open(os.path.join(REPO, "demo", "shaders.js"), "w") as handle:
            handle.write(text)
        print("wrote demo/shaders.js")
        return 0
    try:
        js = read("demo", "shaders.js")
    except FileNotFoundError:
        print("FAIL  demo/shaders.js is missing -- run demo/tools/check_shaders.py --write")
        return 1
    problems = check_shaders(cpp, plugin_cpp, shaders_h, js)
    try:
        table = assembly(cpp, plugin_cpp, shaders_h)
    except Stale:
        table = None  # already reported above
    if table is not None:
        pieces = {name: from_cpp(cpp, symbol) or "" for name, symbol in SHADERS}
        pieces["VERSION"] = cpp_version(cpp) or ""
        types = Types(read("source", "Rubens.h"), plugin_cpp, read("source", "Controls.h"))
        problems += check_uniforms(pieces, table, plugin_cpp, types)
    problems += check_harness(read("tools", "rutest", "main.cpp"), read("demo", "wasm", "glue.cpp"))
    problems += check_no_devices()
    problems += check_wasm()
    print()
    if problems:
        print(f"{problems} problem(s) -- rerun demo/tools/check_shaders.py --write for the GLSL and"
              " demo/tools/build-wasm.sh for the .wasm; never edit either output by hand")
        return 1
    stages = len(table)
    print(f"all {len(SHADERS) + 1} shader pieces and the order all {stages} stages join them in are the plugin's,"
          f" every uniform is set by its type's setter, the glue's {len(HARNESS)} track functions are rutest's,"
          " no served file names a device API, and the .wasm is built from the current sources")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
