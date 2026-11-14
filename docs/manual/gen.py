"""Dewitt user manual: python3 gen.py -> manual.html ; node render.js -> Dewitt_User_Manual.pdf
effects.json comes from dump.cpp (the effects, their knobs and defaults, straight from the plug-in's source);
the effect and move descriptions are read from plugin/YanceyUI.cpp, the built-in sets from plugin/YanceySets.hpp."""
import html, json, os, re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
VERSION = "1.2"
data = json.load(open(os.path.join(HERE, "effects.json")))
ui = open(os.path.join(ROOT, "plugin", "YanceyUI.cpp"), encoding="utf-8").read()

def c_strings(name):
    body = ui[ui.index(name):]
    body = body[body.index("{") + 1: body.index("};")]
    return [s.encode().decode("unicode_escape").encode("latin-1").decode("utf-8") for s in re.findall(r'"((?:[^"\\]|\\.)*)"', body)]

EFFECT_DESC = c_strings("kEffectDesc[kNumEffects] = {")
MOVE_DESC = c_strings("kMoveDesc[kNumMoves] = {")
SETS_SRC = open(os.path.join(ROOT, "plugin", "YanceySets.hpp"), encoding="utf-8").read()
SET_NAMES = re.findall(r'\{ "([A-Z-]+)", "([^"]+)", \{', SETS_SRC)

def e(s): return html.escape(s, quote=False)
def nice(s):   # "FILTER + DRIVE" -> as shown on the pads, with the alt dot
    return e(s)

pages = []
def page(rail, crumb, body, cls=""):
    pages.append((rail, crumb, body, cls))

# ------------------------------------------------------------------ cover
tiles = ["#FFB30F", "#01295F", "#437F97", "#437F97", "#849324", "#849324", "#849324", "#01295F",
         "#01295F", "#01295F", "#FFB30F", "#F7F1E3", "#3A404C", "#849324", "#FFB30F", "#3A404C"]
grid = "".join(f'<div style="background:{c}"></div>' for c in tiles)
page("performance effects", "user manual", f"""
<div class="covergrid">{grid}</div>
<div class="wordmark">Dewitt</div>
<div class="cover-sub">Performance effects for your pads. Hit a pad, bend the beat, let go.</div>
<div class="cover-meta">version {VERSION}<br>windows · vst3 · clap<br>mac · au · vst3 · clap · apple silicon + intel<br>by slapworx</div>
""", "cover")

# ------------------------------------------------------------------ contents
toc = [("01", "Install", "Windows and Mac, and finding Dewitt in your DAW"),
       ("02", "Quick start", "A sound in one minute"),
       ("03", "The screen", "Every control, numbered"),
       ("04", "Pads and banks", "Hold, latch, stacking, the + on empty pads, banks A B C"),
       ("05", "Your controller", "The setup, banks in one press, knobs, notes, bank buttons"),
       ("06", "Effects", f"All {len(data['effects'])}, with their knobs"),
       ("07", "Moves", "Kill, Throw, Freeze, Grab and friends"),
       ("08", "Samples and beats", "One-shots and loops on a pad"),
       ("09", "Sets", "The 16 built-in sets, your own sets, pad presets"),
       ("10", "Settings", "Pad feel, knobs, window size, dark mode"),
       ("11", "Help", "When something doesn't work, and where your files live")]
rows = "".join(f'<div class="row"><div class="a">{n}</div><div>{e(t)}<span class="d">{e(d)}</span></div></div>' for n, t, d in toc)
page("contents", "contents", f'<h1>Inside</h1><div class="toc">{rows}</div>')

# ------------------------------------------------------------------ 01 install
page("01 · install", "01 / install", """
<h1>Install</h1>
<h2>Windows <span class="tag">Windows 10 / 11 · 64-bit · VST3 + CLAP</span></h2>
<ol class="steps">
<li>Close your DAW.</li>
<li>Copy <b>Dewitt.vst3</b> into <code>C:\\Program Files\\Common Files\\VST3</code>.</li>
<li>Using CLAP (Bitwig, Reaper, FL Studio)? Copy <b>Dewitt.clap</b> into <code>C:\\Program Files\\Common Files\\CLAP</code> as well.</li>
<li>Updating? Replace the old Dewitt.vst3. Your projects, sets and controller setup stay as they were.</li>
<li>Open your DAW and let it scan for new plug-ins. In FL Studio: <b>Options › Manage plugins › Find more plugins</b>.</li>
</ol>
<h2>Mac <span class="tag">macOS 10.15 or newer · Apple Silicon + Intel · AU + VST3 + CLAP</span></h2>
<ol class="steps">
<li>Close your DAW.</li>
<li>Double-click <b>Dewitt-1.2-Mac.pkg</b> and click through the installer. Keep the formats you use ticked.</li>
<li>Open your DAW. Logic and GarageBand check new Audio Units on their own the first time.</li>
</ol>
<h2>Find it in your DAW</h2>
<p>Dewitt is an <b>effect</b>: it goes on the track (or mixer channel) your beat plays on. It's listed under <b>slapworx › Dewitt</b>.</p>
<div class="note"><b>Logic Pro</b> loads Dewitt as an <b>AU MIDI-controlled effect</b> in a Software Instrument track's instrument slot, so your controller can reach it. Section 05 and the <b>How to Set Up MIDI</b> pictures show the steps for every DAW.</div>
<h2>What's in the download</h2>
<table>
<tr><td>Dewitt.vst3 / .clap</td><td>The plug-in (Windows). On Mac, the installer.</td></tr>
<tr><td>Dewitt_User_Manual.pdf</td><td>This manual.</td></tr>
<tr><td>How to Set Up MIDI</td><td>One picture per DAW: FL Studio, Ableton Live, Logic Pro, Studio One, Bitwig, Reaper, Cubase.</td></tr>
</table>
""")

# ------------------------------------------------------------------ 02 quick start
page("02 · quick start", "02 / quick start", """
<h1>Quick start</h1>
<p class="lead">Dewitt is a box of performance effects you play like drum pads. Hold a pad and the beat runs through that effect; let go and it's back to normal.</p>
<ol class="steps big">
<li><b>Put Dewitt on your beat.</b> Load it as an effect on the track or mixer channel your beat plays on.</li>
<li><b>Press play in your DAW.</b> Dewitt follows your DAW's tempo.</li>
<li><b>Hold a pad with the mouse.</b> Try <b>01 LOOP GRAB</b>: it loops the last quarter note for as long as you hold it. Let go and the beat carries on.</li>
<li><b>Stack them.</b> Hold LOOP GRAB and FILTER + DRIVE together. Effects stack in a fixed order (section 04), so any combination sounds right.</li>
<li><b>Hook up your controller.</b> Click <b>SET UP CONTROLLER</b> at the top left and tap your pads when it asks. Your DAW needs to send the controller's MIDI to Dewitt first: see section 05 and the <b>How to Set Up MIDI</b> picture for your DAW.</li>
<li><b>Try a set.</b> Click the set name at the top (it says <b>Default</b>) and pick one of the 16 built-in sets.</li>
</ol>
<div class="note"><b>Panic button:</b> <b>ALL OFF</b> at the top turns off everything, latched pads and echo tails included.</div>
<div class="fig"><img src="img/main.png"><div class="cap">Dewitt with the Default set. Pad 03 (FILTER + DRIVE) is selected; its knobs are on the right.</div></div>
""")

# ------------------------------------------------------------------ 03 the screen
callouts = [  # number, x, y (design units of the 1080 x 640 window), name, text
    (1, 418, 34, "SET", "The set loaded now. Click it for the list; ‹ › step through the sets."),
    (2, 587, 34, "UNDO · REDO", "Undo and redo pad and knob changes."),
    (3, 717, 34, "TIMING", "RAW: an effect starts the instant you press. GRID: it waits for the beat. Shows your DAW's tempo."),
    (4, 848, 34, "ALL OFF", "Panic: turns off every pad, sample and tail."),
    (5, 946, 34, "LIMIT", "A limiter on Dewitt's output, so stacked effects can't clip. On by default."),
    (6, 1022, 34, "OUT", "Output level, -60 to +6 dB."),
    (7, 70, 80, "NOTES", "The note map: play the pads from notes in a row (section 05)."),
    (8, 122, 80, "Light / dark", "The small dot (a moon in dark mode) switches light and dark."),
    (9, 205, 80, "Controller", "SET UP CONTROLLER until you've set one up; then it shows your controller's status."),
    (10, 307, 80, "Settings", "The gear: controller, pad feel, banks, window and reset options (section 10)."),
    (11, 105, 115, "Banks", "A, B and C: three banks of 16 pads. Right-click a tab to learn a bank button."),
    (12, 258, 350, "Pads", "Hold to play. Drag to reorder. Drop a sample or beat on one."),
    (13, 124, 171, "Pad menu", "The ▾ on a pad picks what's on it: an effect, a move, a sample or a beat."),
    (14, 778, 300, "Pad card", "The selected pad's knobs and a line on what it does."),
    (15, 600, 520, "PAD MODE", "HOLD: on while you hold. LATCH: tap on, tap off."),
    (16, 974, 508, "PLAY", "Plays the selected pad with the mouse."),
    (17, 150, 600, "YOUR KNOBS", "Which pad controls your controller's knobs turn right now (section 05)."),
]
dots = "".join(f'<div class="co" style="left:{x / 1080 * 100:.2f}%;top:{y / 640 * 100:.2f}%">{n}</div>' for n, x, y, _, _ in callouts)
legend = "".join(f'<div class="li"><span class="n">{n}</span><div><b>{e(t)}</b> {e(d)}</div></div>' for n, _, _, t, d in callouts)
page("03 · the screen", "03 / the screen", f"""
<h1>The screen</h1>
<div class="shot"><img src="img/main.png">{dots}</div>
<div class="legend">{legend}</div>
""")

# ------------------------------------------------------------------ 04 pads and banks
page("04 · pads and banks", "04 / pads and banks", """
<h1>Pads and banks</h1>
<h2>Playing a pad</h2>
<ul class="dash">
<li><b>Mouse:</b> hold a pad, or select it and hold <b>PLAY</b>.</li>
<li><b>Controller:</b> once it's set up (section 05), your pads play Dewitt's pads.</li>
<li><b>HOLD or LATCH</b> (PAD MODE, on the pad's card): HOLD plays while you hold. LATCH turns on with one tap and off with the next, so you can leave it running with your hands free.</li>
<li><b>ON RELEASE</b> (echoes and reverbs): CUT stops the effect when you let go; RING OUT lets the tail fade on its own.</li>
<li><b>SNAP BACK</b> (DJ Looper): when you let go, the record snaps straight back into time.</li>
</ul>
<h2>Stacking</h2>
<p>Hold as many pads as you like. Whatever order you press them in, the effects run in one fixed order: <b>loops and chops first</b>, then <b>filters</b>, then <b>modulation</b> (chorus, flanger, phaser...), then <b>colour</b> (vinyl, lo-fi, drive), then <b>echoes and reverbs last</b>. So a reverb always catches the loop you grabbed, never the other way round.</p>
<h2>What's on a pad</h2>
<p>Click the <b>▾</b> on a pad (or the pad's name on its card). The menu lists every effect by family, the moves, and <b>Load sample</b> / <b>Load beat</b>. <b>Empty pads</b> (banks B and C start empty) show a <b>+</b>: click it and the same menu opens right there.</p>
<p><b>Right-click a pad</b> for MIDI learn on just that pad, samples and beats, and <b>pad presets</b>: save a pad with its knobs to reuse it in any project. <b>Drag a pad</b> onto another to swap them. <b>Drop a sample or beat</b> from your computer onto a pad to load it.</p>
<div class="cols">
<div class="fig tight"><img src="img/plusmenu-crop.png"><div class="cap">The + on an empty pad opens the pad menu.</div></div>
<div class="fig tight"><img src="img/padright-crop.png"><div class="cap">Right-click a pad.</div></div>
</div>
</div></div>
<h2>Banks A, B and C</h2>
<p>Dewitt has 48 pads in three banks of 16. Bank A starts with the Default layout; B and C start empty for your own effects, samples and beats. Click <b>A</b>, <b>B</b> or <b>C</b> to switch. Hitting a pad on your controller jumps the screen to that pad's bank.</p>
""")

# ------------------------------------------------------------------ 05 controller (two pages)
page("05 · your controller", "05 / your controller", """
<h1>Your controller</h1>
<p class="lead">Two steps: your DAW sends the controller's MIDI to Dewitt, then Dewitt learns your pads and knobs. You only do it once; it's saved on your computer and used in every project.</p>
<h2>Step 1: send MIDI to Dewitt (in your DAW)</h2>
<p>Every DAW does this a little differently. The <b>How to Set Up MIDI</b> folder has one picture per DAW, step by step. The short version:</p>
<table>
<tr><th>DAW</th><th>How</th></tr>
<tr><td>FL Studio</td><td>Options › MIDI settings: enable your controller and give it a <b>Port</b> (say 10). In Dewitt's wrapper (the gear at its top left): Settings › MIDI › <b>Input port 10</b>.</td></tr>
<tr><td>Ableton Live</td><td>A MIDI track: MIDI From your controller, Monitor In, MIDI To the audio track › <b>Dewitt</b>.</td></tr>
<tr><td>Logic Pro</td><td>Software Instrument track, instrument slot: <b>AU MIDI-controlled Effects › slapworx › Dewitt</b>; Side Chain: your audio track.</td></tr>
<tr><td>Studio One</td><td>An Instrument track with your controller as Input and <b>Dewitt</b> as Output; Monitor on.</td></tr>
<tr><td>Bitwig</td><td>On the audio track: <b>Note Receiver</b> (source: an Instrument track with your controller), then Dewitt.</td></tr>
<tr><td>Reaper</td><td>On Dewitt's track: Input: MIDI › your controller, Monitor Input, Record: disable (input monitoring only), arm.</td></tr>
<tr><td>Cubase</td><td>A MIDI track: Input Routing your controller, Output Routing <b>Dewitt</b>; Monitor on.</td></tr>
</table>
<h2>Step 2: the setup (in Dewitt)</h2>
<div class="cols"><div>
<ol class="steps">
<li>Click <b>SET UP CONTROLLER</b> (top left), or gear › Set up controller.</li>
<li>Pick <b>PAD CONTROLLER</b> or <b>KEYBOARD</b>.</li>
<li>Press any pad so Dewitt knows it hears you.</li>
<li>Tap your 16 pads one by one, in the order the screen shows. <b>SKIP</b> leaves one unmapped.</li>
<li>Add banks B and C if you want them (next page).</li>
<li>Turn your knobs one by one, or skip them.</li>
</ol>
<p>Done. To change one pad later, right-click it › <b>MIDI learn</b> and press the pad on your controller.</p>
</div><div>
<div class="fig tight"><img src="img/setup.png"><div class="cap">The setup's first question.</div></div>
</div></div>
""")
page("05 · your controller", "05 / your controller", """
<h2>Banks B and C in one press</h2>
<p>After bank A, choose <b>ADD BANK B</b>. Dewitt asks for just one pad: switch your controller to its bank B and press the top-left pad. Dewitt works out how your controller's banks differ (higher notes, another MIDI channel, or both) and fills in the other 15. Tap a few pads to check: they light up. Bank C works the same way. Prefer to map them yourself? Click <b>TAP EACH ONE MYSELF</b>.</p>
<div class="three">
<div class="fig tight"><img src="img/wiz-3.png"><div class="cap">One press for bank B.</div></div>
<div class="fig tight"><img src="img/wiz-4.png"><div class="cap">The other 15 filled in.</div></div>
<div class="fig tight"><img src="img/wiz-6.png"><div class="cap">A controller with no banks.</div></div>
</div>
<h2>Controllers without banks</h2>
<p>Some controllers send the same notes on every bank, or have no bank button. Dewitt notices and offers <b>FOLLOW THE BANK</b>: your 16 pads then play whichever bank is showing in Dewitt. Switch banks with the A / B / C tabs, or <b>right-click a tab › MIDI learn</b> to use a button on your controller. Turn it on or off any time: gear › Banks › <b>Bank A's pads play the bank on screen</b>.</p>
<h2>Your knobs</h2>
<p>Your controller's knobs turn the <b>selected pad's</b> knobs, in order. The <b>YOUR KNOBS</b> row at the bottom shows what each one does right now. If your controller has fewer knobs than an effect, the extra controls either follow one of your knobs or stay where you set them with the mouse. Gear › <b>Knobs: type, speed, pickup...</b> sets:</p>
<ul class="dash">
<li><b>Knob type:</b> AUTO tells endless knobs from normal ones. Pick ABSOLUTE or a REL mode by hand if your knobs jump.</li>
<li><b>Endless knob speed:</b> slow, normal or fast.</li>
<li><b>Normal knobs:</b> JUMP straight to where the knob is, or WAIT until the knob passes the current value (no jumps).</li>
<li><b>How many knobs your controller has</b>, and which row of the knob grid comes first.</li>
</ul>
<h2>Keyboards and the note map</h2>
<p>Playing from a piano-style keyboard? Pick <b>KEYBOARD</b> in the setup: Dewitt fills the pads from the key you press first. Or use the <b>NOTES</b> pill (top left): pick a first note and the 48 pads play 48 notes in a row, top row first or bottom row first. Scroll on the pill to move it by a half step.</p>
<h2>Pad feel</h2>
<p>Gear › Pad feel: <b>Velocity + pressure</b> (hit harder for more, press into the pad for more still), <b>Velocity only</b>, or <b>Full on any touch</b>.</p>
""")

# ------------------------------------------------------------------ 06 effects
fam_title = {"TIME": "Time: loops, chops and speed", "FILTER": "Filters", "MOD": "Modulation", "TEXTURE": "Colour", "SPACE": "Space: echoes and reverbs"}
fam_note = {"TIME": "These change when and how the beat plays.", "FILTER": "", "MOD": "", "TEXTURE": "", "SPACE": "Echoes and reverbs have ON RELEASE: CUT or RING OUT. SMEAR always rings out."}
cards = {f: [] for f in fam_title}
for fx in data["effects"]:
    knobs = " · ".join(f'<b>{e(k["label"])}</b> {e(k["def"])}' for k in fx["knobs"])
    cards[fx["family"]].append(f'<div class="fx"><h3>{nice(fx["name"])}</h3><p>{e(EFFECT_DESC[fx["id"]])}</p><div class="ctl">{knobs}</div></div>')
def fxpage(fams, first):
    body = "<h1>Effects</h1><p>Every effect, in the order they stack. The knobs are listed with where they start.</p>" if first else ""
    for f in fams:
        body += f'<h2>{e(fam_title[f])}</h2>' + (f'<p class="small">{e(fam_note[f])}</p>' if fam_note[f] else "") + f'<div class="fxgrid">{"".join(cards[f])}</div>'
    page("06 · effects", "06 / effects", body)
fxpage(["TIME"], True)
fxpage(["FILTER", "MOD"], False)
fxpage(["TEXTURE", "SPACE"], False)

# ------------------------------------------------------------------ 07 moves
mv = "".join(f'<tr><td>{e(m["name"])}</td><td>{"hold" if m["hold"] else "tap"}</td><td>{e(MOVE_DESC[m["id"]])}</td></tr>' for m in data["moves"])
page("07 · moves", "07 / moves", f"""
<h1>Moves</h1>
<p class="lead">Moves aren't effects: they act on everything at once. In every built-in set they sit on bank A's bottom row, so they're always under the same fingers.</p>
<table class="moves"><tr><th>Move</th><th>Play</th><th>What it does</th></tr>{mv}</table>
<div class="cols"><div>
<p><b>Throw</b> and <b>Freeze</b> have knobs: select the pad and set them on its card (Throw: TIME and FEEDBACK; Freeze: length and speed). <b>Dry Kill</b>'s knob sets how loud the tails ring.</p>
<p><b>Kill vs Dry Kill:</b> Kill mutes everything. Dry Kill mutes the beat but lets echo and reverb tails keep ringing: the classic dub drop.</p>
</div><div>
<div class="fig tight"><img src="img/move.png"><div class="cap">THROW selected: its TIME and FEEDBACK knobs.</div></div>
</div></div>
""")

# ------------------------------------------------------------------ 08 samples and beats
page("08 · samples and beats", "08 / samples and beats", """
<h1>Samples and beats</h1>
<h2>Samples (one-shots)</h2>
<ul class="dash">
<li>Drop a WAV, FLAC or MP3 on a pad, or pad menu › <b>Load sample...</b></li>
<li>A sample plays once, clean, on top of everything: horns, vocal tags, risers. Hit it again to restart it.</li>
<li>The pad card shows its name and length, with <b>REPLACE</b> and <b>REMOVE</b>.</li>
</ul>
<h2>Beats (loops)</h2>
<ul class="dash">
<li>Pad menu › <b>Load beat...</b>, or <b>LOAD BEAT</b> on an empty pad's card.</li>
<li>A beat plays <b>through the effects</b>, so every pad works on it: perfect for playing a beat live without a DAW track.</li>
<li>Dewitt detects the tempo. If it guessed half or double, click <b>1/2x</b> or <b>2x</b>, or click the BPM to type it.</li>
<li><b>NEXT BEAT STARTS:</b> INSTANT, on the NEXT BAR, or with a short XFADE, when you switch from one beat pad to another.</li>
<li>Beats are linked to their files. Moved a file? The pad says so: click <b>FIND FILE</b>. Set menu › <b>Collect beat files</b> copies a set's beats into Dewitt's folder so they travel with it.</li>
</ul>
<div class="fig"><img src="img/empty.png"><div class="cap">An empty pad's card: ADD EFFECT, LOAD SAMPLE, LOAD BEAT.</div></div>
""")

# ------------------------------------------------------------------ 09 sets
desc = {
 "Boom Bap Chops": "Loop Grab, Beat Repeat and Rev Stepper chops with dusty colour; bank B has more chop lengths.",
 "Dusty Sampler": "Vinyl, lo-fi and tape: the old-sampler sound. Bank B adds the Color modes and band cuts.",
 "Dub Siren": "Tape echo, delays and big reverbs, with filters and Throw for dub drops.",
 "Chop Shop": "Every chop length in rows, over all three banks: Loop Grab, Beat Repeat, Scatter, DJ Looper.",
 "Tape Deck": "Cassette, flutter, tape echo and gentle chorus. Warm and wobbly.",
 "Late Night": "Soft reverbs, slow modulation and long loops for chilled sets.",
 "Broken Radio": "Radio, ring mod, combs and crushers. Bank B goes further.",
 "Warm Wash": "Granular clouds, washes and big rooms for breakdowns.",
 "DJ Mixer": "Isolator kills, high and low pass, echoes and loops: a DJ mixer's FX section.",
 "Build & Drop": "Filter risers, rolls from 1/8 to 1/32, big tails, stops and spins.",
 "Peak Time": "Flanger, phaser, delays and loops for the main room. Bank B is latched.",
 "Garage Shuffle": "Triplet and shuffled chops, swung echoes and filters.",
 "Scratch & Spin": "The tape looper forwards, backwards, half speed; spins and stops.",
 "Latch Jam": "The Default effects, latched: tap them on, stack them, play over the top. Bank B adds more.",
 "Transitions": "Stops, spins, filter sweeps and washes to get from one beat to the next.",
 "FX Lab": "Sound design: everything turned up. Granular, ring mod, extreme combs and crushers.",
}
groups = {}
for g, n in SET_NAMES: groups.setdefault(g, []).append(n)
gt = {"HIP-HOP": "Hip-hop", "LO-FI": "Lo-fi", "CLUB": "Club", "LIVE": "Live"}
sl = "".join(f'<div class="setg"><div class="g">{e(gt.get(g, g))}</div>' + "".join(f'<div class="set"><b>{e(n)}</b> {e(desc.get(n, ""))}</div>' for n in ns) + "</div>" for g, ns in groups.items())
page("09 · sets", "09 / sets", f"""
<h1>Sets</h1>
<p class="lead">A set is all 48 pads at once: their effects, knobs, samples and beats. Click the set name at the top for the list, or ‹ › to step through.</p>
<div class="cols"><div>
<div class="setg"><div class="g">Built in</div><div class="set"><b>Default</b> The 12 core effects and four moves on bank A; B and C empty for your own.</div></div>
{sl}
</div><div>
<div class="fig tight"><img src="img/sets.png"><div class="cap">The set menu.</div></div>
<h3>Your own sets</h3>
<p>Set menu › <b>Save as a new set...</b> Your sets are listed under YOUR SETS, with <b>Save changes</b>, <b>Rename</b> and <b>Delete</b>. A set saves samples inside it, so it works on any computer.</p>
<h3>Pad presets</h3>
<p>One pad, saved with its knobs: right-click a pad › <b>Save pad as preset...</b>, and <b>Load pad preset...</b> on any pad, in any set.</p>
<h3>Projects</h3>
<p>Your DAW project keeps everything as you left it, edits and all, even if you never save a set.</p>
<p class="small">When the same effect is on several pads of a bank, the small line under each name shows what's different: "1/16", "LOW KILL", "30%".</p>
</div></div>
""")

# ------------------------------------------------------------------ 10 settings
page("10 · settings", "10 / settings", """
<h1>Settings</h1>
<div class="cols"><div>
<p>The <b>gear</b> next to the controller pill:</p>
<table>
<tr><td>Controller</td><td>Run the setup again, the knob settings (section 05), the note map.</td></tr>
<tr><td>Pad feel</td><td>Velocity + pressure, velocity only, or full on any touch.</td></tr>
<tr><td>Banks</td><td>Bank A's pads play the bank on screen.</td></tr>
<tr><td>Window</td><td>Reset window size.</td></tr>
<tr><td>Reset</td><td><b>Reset this project's pads</b> back to Default. <b>Restore factory defaults</b> forgets your controller setup and settings on this computer (your sets stay).</td></tr>
</table>
<h2>Window size</h2>
<p>Drag the window's corner (or use your DAW's size buttons) to make Dewitt bigger or smaller: from three-quarters to twice its normal size. It keeps its shape and remembers your size. On Retina and other high-resolution screens it opens sharp, at the same size as everywhere else.</p>
<h2>Light and dark</h2>
<p>Click the small dot right of the NOTES pill. It turns into a moon in dark mode.</p>
<h2>Knobs with the mouse</h2>
<ul class="dash"><li>Drag up and down. Hold <b>Shift</b> for fine moves.</li><li>Scroll over a knob to nudge it.</li><li>Right-click a knob › <b>Reset to default</b>.</li></ul>
</div><div>
<div class="fig tight"><img src="img/settings.png"><div class="cap">The gear menu.</div></div>
<div class="fig tight"><img src="img/dark.png"><div class="cap">Dark mode.</div></div>
</div></div>
""")

# ------------------------------------------------------------------ 11 help
page("11 · help", "11 / help", """
<h1>Help</h1>
<table class="help">
<tr><th>What happens</th><th>Try this</th></tr>
<tr><td>My pads do nothing in Dewitt</td><td>Your DAW isn't sending the controller to Dewitt. Follow the How to Set Up MIDI picture for your DAW. The dot in Dewitt's controller pill flashes when MIDI arrives.</td></tr>
<tr><td>MIDI arrives but the wrong pads play</td><td>Run the setup again (gear › Set up controller), or right-click the pad › MIDI learn.</td></tr>
<tr><td>Bank B or C plays bank A</td><td>Your controller sends the same notes on every bank. Turn on gear › Banks › Bank A's pads play the bank on screen.</td></tr>
<tr><td>Holding a pad does nothing</td><td>Is your DAW playing? Loops, echoes and chops need audio coming in (or a beat on a pad).</td></tr>
<tr><td>Effects start late</td><td>TIMING is on GRID: it waits for the next beat. Switch to RAW.</td></tr>
<tr><td>Something's stuck on</td><td>ALL OFF.</td></tr>
<tr><td>Typing a set name goes to my DAW</td><td>Turn off your DAW's computer-keyboard piano while typing.</td></tr>
<tr><td>Logic: my controller doesn't reach Dewitt</td><td>Load Dewitt in a Software Instrument track's instrument slot (AU MIDI-controlled Effects), not in an Audio FX slot.</td></tr>
<tr><td>A beat pad says it can't find its file</td><td>Click FIND FILE and point to it, or use Collect beat files next time.</td></tr>
</table>
<h2>Where your files live</h2>
<table>
<tr><td>Windows</td><td><code>%APPDATA%\\slapworx\\Dewitt</code></td></tr>
<tr><td>Mac</td><td><code>~/Library/Application Support/slapworx/Dewitt</code></td></tr>
</table>
<p>Inside: <b>Sets</b> (your sets), <b>Pads</b> (pad presets), <b>Beats</b> (collected beat files) and <b>settings.txt</b> (your controller setup and preferences). Copy the folder to bring everything to another computer.</p>
<div class="end"><div class="wordmark small">Dewitt</div><p>Made by slapworx: plug-ins + samples. that slap. <b>slapworx.net</b></p></div>
""")

# ------------------------------------------------------------------ html
CSS = open(os.path.join(HERE, "manual.css")).read()
out = []
for i, (rail, crumb, body, cls) in enumerate(pages):
    num = f"{i + 1:02d}"
    top = "" if cls == "cover" else f'<div class="top"><span><b>Dewitt</b> / {e(crumb)}</span><span>v{VERSION}</span></div>'
    out.append(f'<section class="page {cls}"><div class="rail"><span>{e(rail)}</span></div>{top}{body}'
               f'<div class="foot"><span>slapworx · plug-ins + samples. that slap.</span><span>{num}</span></div></section>')
open(os.path.join(HERE, "manual.html"), "w", encoding="utf-8").write(
    f'<!doctype html><html lang="en"><head><meta charset="utf-8"><title>Dewitt user manual</title><style>{CSS}</style></head><body>{"".join(out)}</body></html>')
print(len(pages), "pages")
