"""How to Set Up MIDI: one 1080x1350 infographic per DAW, in Dewitt's look.
python3 gen.py  -> html/*.html ; node render.js -> "How to Set Up MIDI/*.png" """
import os, html

DAWS = [
 ("01 FL Studio", "FL Studio", "Windows + Mac · VST3 / CLAP",
  ["Your controller", "Port 10", "Dewitt in the Mixer"],
  [("Turn on your controller", "Options › MIDI settings (F10). Click your controller in the Input list and switch Enable on.", ["Options › MIDI settings", "Enable ●"]),
   ("Give it a port number", "Set Port to any number. We'll use 10. It just has to match in step 4.", ["Port  10"]),
   ("Put Dewitt on a mixer track", "Open the Mixer, pick the track your sound plays on, click an empty slot and choose Dewitt.", ["Mixer › slot › Dewitt"]),
   ("Use the same port in Dewitt", "Click the gear at the top left of Dewitt's window. Settings tab › MIDI › Input port: 10.", ["Settings › MIDI › Input port  10"])],
  "Several plug-ins on the same port all hear your controller. Give Dewitt its own number to play only Dewitt."),
 ("02 Ableton Live", "Ableton Live", "Windows + Mac · VST3 / AU",
  ["Your controller", "A MIDI track", "Dewitt on an audio track"],
  [("Turn on your controller", "Settings › Link, Tempo & MIDI. Next to your controller's Input, switch Track on.", ["Input  Track ●"]),
   ("Put Dewitt on your audio track", "From the browser: Plug-Ins › slapworx › Dewitt. Drag it onto the track with your sound.", ["Plug-Ins › slapworx › Dewitt"]),
   ("Make a MIDI track", "MIDI From: your controller, All Channels. Set Monitor to In.", ["MIDI From  your controller", "Monitor  In"]),
   ("Send it to Dewitt", "MIDI To: pick the audio track, then pick Dewitt in the box under it (not Track In).", ["MIDI To › Audio track › Dewitt"])],
  "Can't see MIDI From / MIDI To? Turn on the mixer's In/Out section: View › Mixer Controls › In/Out."),
 ("03 Logic Pro", "Logic Pro", "Mac · Audio Unit (AU)",
  ["Your controller", "A Software Instrument track", "Dewitt + your audio"],
  [("Plug in your controller", "Logic finds it by itself. Check Logic Pro › Settings › MIDI if the pads do nothing.", ["Logic Pro › Settings › MIDI"]),
   ("Load Dewitt as the instrument", "New Software Instrument track. Click its Instrument slot: AU MIDI-controlled Effects › slapworx › Dewitt.", ["AU MIDI-controlled Effects › Dewitt"]),
   ("Feed it your audio", "In the top bar of Dewitt's window, open Side Chain and pick the audio track with your sound.", ["Side Chain › your audio track"]),
   ("Hear it only through Dewitt", "On that audio track, set Output to No Output, so you don't hear it twice.", ["Output  No Output"]),
   ("Select Dewitt's track", "Click Dewitt's track (or record-arm it). Your controller now plays Dewitt.", ["Record arm ●"])],
  "Load Dewitt in the Instrument slot (as a MIDI-controlled effect), not an Audio FX slot, so it can hear your controller."),
 ("04 Studio One", "Studio One", "Windows + Mac · VST3",
  ["Your controller", "An Instrument track", "Dewitt on an audio track"],
  [("Add your controller", "Studio One › Options (Settings on Mac) › External Devices › Add… Choose your controller, Receive From: its MIDI port.", ["External Devices › Add…"]),
   ("Put Dewitt on your audio track", "Open the track's Inserts and add Dewitt (the VST3).", ["Inserts › Dewitt"]),
   ("Make an Instrument track", "Add an Instrument track with no instrument. Input: your controller.", ["Input  your controller"]),
   ("Send it to Dewitt", "Set that track's Output to Dewitt. It's in the list because Dewitt takes MIDI.", ["Output  Dewitt"]),
   ("Turn on Monitor", "Click the speaker (Monitor) button on the Instrument track.", ["Monitor ●"])],
  "Use the VST3 version. Studio One only sends MIDI to VST3 effects."),
 ("05 Bitwig Studio", "Bitwig Studio", "Windows + Mac + Linux · VST3 / CLAP",
  ["Your controller", "An Instrument track", "Note Receiver › Dewitt"],
  [("Add your controller", "Settings › Controllers › Add Controller. Not listed? Use Generic › MIDI Keyboard.", ["Settings › Controllers"]),
   ("Make an Instrument track", "Add an Instrument track with no instrument. Input: your controller. Arm it.", ["Input  your controller", "Arm ●"]),
   ("Add a Note Receiver", "On your audio track, add Note Receiver (Routing). Note Source: the Instrument track.", ["Note Receiver › Note Source"]),
   ("Put Dewitt after it", "Add Dewitt right after Note Receiver on the same audio track.", ["Note Receiver  ›  Dewitt"])],
  "Order matters: Note Receiver has to come before Dewitt in the chain."),
 ("06 Reaper", "Reaper", "Windows + Mac · VST3 / CLAP / AU",
  ["Your controller", "Track input", "Dewitt on the same track"],
  [("Turn on your controller", "Options › Preferences › MIDI Devices (MIDI Inputs on newer versions). Double-click it › Enable input.", ["MIDI Devices › Enable input"]),
   ("Put Dewitt on your audio track", "Click the track's FX button and add Dewitt.", ["FX › Dewitt"]),
   ("Listen to your controller", "Right-click the record-arm button: Input: MIDI › your controller › All channels. Then Monitor Input.", ["Input: MIDI › All channels", "Monitor Input ●"]),
   ("Never record over your audio", "Right-click arm again: Record: disable (input monitoring only). Then arm the track.", ["Record: disable (input monitoring only)"])],
  "The track stays armed while you play. With recording disabled, your audio is safe."),
 ("07 Cubase", "Cubase", "Windows + Mac · VST3",
  ["Your controller", "A MIDI track", "Dewitt on an audio track"],
  [("Check your controller", "Studio › Studio Setup › MIDI Port Setup. Your controller's input should be ticked In 'All MIDI Inputs'.", ["MIDI Port Setup"]),
   ("Put Dewitt on your audio track", "In the Inspector, open Inserts and add Dewitt.", ["Inserts › Dewitt"]),
   ("Make a MIDI track", "Input Routing: your controller (or All MIDI Inputs).", ["Input Routing  your controller"]),
   ("Send it to Dewitt", "Output Routing: Dewitt – your audio track's name.", ["Output Routing  Dewitt – Audio 01"]),
   ("Turn on Monitor", "Click Monitor on the MIDI track (or Record Enable).", ["Monitor ●"])],
  "Dewitt shows up in Output Routing as soon as it's on an audio track's inserts."),
]

CSS = """
@font-face { font-family: Shr; src: url(../fonts/shrikhand.ttf); }
@font-face { font-family: DXB; src: url(../fonts/dmxb.ttf); }
@font-face { font-family: DB; src: url(../fonts/dmb.ttf); }
@font-face { font-family: DM; src: url(../fonts/dmm.ttf); }
:root { --cream:#F7F1E3; --navy:#01295F; --amber:#FFB30F; --steel:#437F97; --olive:#849324; --tray:#EEE7D6; --edge:#D6CDBA; --red:#FD151B; }
* { box-sizing: border-box; margin: 0; padding: 0; }
html, body { width: 1080px; height: 1350px; background: var(--cream); color: var(--navy); font-family: DM; overflow: hidden; }
.wrap { position: absolute; inset: 0; padding: 54px 64px 0; }
.head { display: flex; align-items: center; justify-content: space-between; }
.logo { font-family: Shr; font-size: 52px; line-height: 1; letter-spacing: .5px; }
.tag { font-family: DXB; font-size: 17px; letter-spacing: .12em; background: var(--navy); color: var(--cream); padding: 11px 20px 10px; border-radius: 30px; }
h1 { font-family: DXB; font-size: 82px; letter-spacing: -1.5px; line-height: 1; margin-top: 34px; }
.sub { font-family: DB; font-size: 23px; opacity: .72; margin-top: 12px; }
.fmt { display: inline-block; font-family: DXB; font-size: 14px; letter-spacing: .1em; border: 2px solid var(--navy); border-radius: 20px; padding: 6px 14px 5px; margin-left: 14px; vertical-align: 3px; opacity: .8; }
.route { display: flex; align-items: center; gap: 12px; margin-top: 30px; background: var(--tray); border: 2px solid var(--edge); border-radius: 26px; padding: 16px; }
.blk { flex: 1; height: 74px; border-radius: 16px; display: flex; align-items: center; justify-content: center; text-align: center;
       font-family: DXB; font-size: 17px; letter-spacing: .05em; text-transform: uppercase; line-height: 1.15; padding: 0 12px; box-shadow: 0 5px 0 rgba(1,41,95,.22); }
.b1 { background: var(--amber); } .b2 { background: var(--steel); color: var(--cream); } .b3 { background: var(--navy); color: var(--cream); }
.arr { font-family: DXB; font-size: 30px; opacity: .55; }
.steps { margin-top: 30px; display: flex; flex-direction: column; gap: 16px; }
.st { display: flex; gap: 22px; align-items: flex-start; background: #FBF7EE; border: 2px solid var(--navy); border-radius: 22px; padding: 18px 22px 18px 18px; box-shadow: 0 5px 0 rgba(1,41,95,.16); }
.n { flex: 0 0 52px; height: 52px; border-radius: 50%; background: var(--navy); color: var(--cream); font-family: DXB; font-size: 25px; display: flex; align-items: center; justify-content: center; }
.t { font-family: DXB; font-size: 27px; letter-spacing: -.3px; line-height: 1.12; }
.d { font-family: DM; font-size: 19.5px; line-height: 1.35; margin-top: 5px; opacity: .88; }
.chips { display: flex; flex-wrap: wrap; gap: 8px; margin-top: 10px; }
.chip { font-family: DB; font-size: 16.5px; border: 2px solid var(--navy); border-radius: 18px; padding: 5px 13px 4px; background: var(--cream); white-space: nowrap; }
.chip.k { background: var(--amber); border-color: var(--amber); }
.note { margin-top: 18px; font-family: DB; font-size: 18px; line-height: 1.35; display: flex; gap: 12px; align-items: flex-start; }
.note i { flex: 0 0 26px; height: 26px; border-radius: 50%; background: var(--amber); font-style: normal; font-family: DXB; font-size: 16px; display: flex; align-items: center; justify-content: center; margin-top: 1px; }
.foot { position: absolute; left: 0; right: 0; bottom: 0; height: 136px; background: var(--navy); color: var(--cream); padding: 26px 64px 0; }
.last { display: flex; align-items: center; gap: 18px; }
.last .lab { font-family: DXB; font-size: 14px; letter-spacing: .14em; background: var(--amber); color: var(--navy); border-radius: 16px; padding: 7px 13px 6px; white-space: nowrap; }
.last .txt { font-family: DB; font-size: 22px; line-height: 1.3; }
.last .pill { display: inline-block; font-family: DXB; font-size: 15px; letter-spacing: .08em; border: 2px solid var(--cream); border-radius: 14px; padding: 2px 10px 1px; margin: 0 3px; vertical-align: 2px; }
.brand { position: absolute; left: 64px; right: 64px; bottom: 20px; display: flex; justify-content: space-between; font-family: DB; font-size: 15px; opacity: .6; letter-spacing: .02em; }
"""

def esc(s): return html.escape(s, quote=False)

def page(fname, name, fmt, route, steps, note):
    chips = lambda cs: "".join(f'<span class="chip{" k" if i == len(cs) - 1 and len(cs) > 0 else ""}">{esc(c)}</span>' for i, c in enumerate(cs))
    st = "".join(f'<div class="st"><div class="n">{i + 1}</div><div><div class="t">{esc(t)}</div><div class="d">{esc(d)}</div>'
                 f'<div class="chips">{chips(c)}</div></div></div>' for i, (t, d, c) in enumerate(steps))
    big = len(steps) >= 5
    extra = ("<style>.st{padding-top:12px;padding-bottom:12px}.steps{gap:11px;margin-top:22px}.d{font-size:18.5px;margin-top:3px}.t{font-size:25px}"
             ".chips{margin-top:7px}h1{margin-top:26px}.route{margin-top:24px}.note{margin-top:14px}</style>") if big else ""
    return f"""<!doctype html><html><head><meta charset="utf-8"><title>{esc(name)}: how to set up MIDI</title><style>{CSS}</style>{extra}</head><body><div class="wrap">
<div class="head"><div class="logo">Dewitt</div><div class="tag">HOW TO SET UP MIDI</div></div>
<h1>{esc(name)}</h1>
<div class="sub">Play Dewitt's pads and knobs from your controller.<span class="fmt">{esc(fmt).upper()}</span></div>
<div class="route"><div class="blk b1">{esc(route[0])}</div><div class="arr">›</div><div class="blk b2">{esc(route[1])}</div><div class="arr">›</div><div class="blk b3">{esc(route[2])}</div></div>
<div class="steps">{st}</div>
<div class="note"><i>!</i><div>{esc(note)}</div></div>
</div>
<div class="foot"><div class="last"><span class="lab">LAST STEP</span><span class="txt">In Dewitt, click <span class="pill">SET UP</span> and tap your pads one by one. Dewitt learns them, and they stay learned in every project.</span></div>
<div class="brand"><span>slapworx · plug-ins + samples. that slap.</span><span>Dewitt manual: Controller setup</span></div></div>
</body></html>"""

here = os.path.dirname(os.path.abspath(__file__))
os.makedirs(os.path.join(here, "html"), exist_ok=True)
for d in DAWS:
    open(os.path.join(here, "html", d[0] + ".html"), "w").write(page(*d))
print("\n".join(d[0] for d in DAWS))
