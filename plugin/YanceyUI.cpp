// Yancey: user interface (look and feel carried over from Dewitt).
#include "DistrhoUI.hpp"
#include "YanceyParams.hpp"
#include "YanceySets.hpp"
#include "FontData.hpp"
#include "YanceyPlatform.hpp"
#include "YanceySamples.hpp"
#include "YanceyDecode.hpp"
#include "YanceyShared.hpp"
#include "YanceyBeats.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <cctype>
#include <memory>
#include <vector>
#include <algorithm>

START_NAMESPACE_DISTRHO

using namespace yancey;

static Color withAlpha(const Color& c, float a) { Color r(c); r.alpha = a; return r; }
static Color hex(int rgb) { return Color((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255); }

// ------------------------------------------------------------------------------------------------
// Themes

enum { kColSteel = 0, kColOlive, kColAmber, kColNavy, kColCream, kColRed, kNumCardCols };

struct Theme {
    Color bg, text, outline, face, faceInk, shadow, pillFill, frame, muted;
    Color menuBg, menuText, menuHover, menuHoverText, sig;
    Color card[kNumCardCols], ink[kNumCardCols];
    Color sel, selInk;               // latch/hold selected half on light cards
    Color tray, trayEdge, tile, tileEmpty, tileShadow, killTile, killShadow, tileInk, tileSub;
    float shadowA;
};

static Theme makeTheme(bool dark)
{
    Theme t;
    if (!dark) {
        t.bg = hex(0xF7F1E3); t.text = hex(0x01295F); t.outline = hex(0x01295F); t.face = hex(0xF7F1E3);
        t.faceInk = hex(0x01295F); t.shadow = hex(0x01295F); t.shadowA = 0.2f; t.pillFill = hex(0xF7F1E3);
        t.frame = hex(0x01295F); t.muted = withAlpha(hex(0x01295F), 0.55f);
        t.menuBg = hex(0xF7F1E3); t.menuText = hex(0x01295F); t.menuHover = hex(0x01295F); t.menuHoverText = hex(0xF7F1E3);
        t.sig = hex(0x808DA3);
        t.card[kColSteel] = hex(0x437F97); t.card[kColOlive] = hex(0x849324); t.card[kColAmber] = hex(0xFFB30F);
        t.card[kColNavy] = hex(0x01295F); t.card[kColCream] = hex(0xF7F1E3); t.card[kColRed] = hex(0xFD151B);
        for (int i = 0; i < kNumCardCols; ++i) t.ink[i] = hex(0x01295F);
        t.ink[kColNavy] = hex(0xF7F1E3);
        t.sel = hex(0x01295F); t.selInk = hex(0xF7F1E3);
        t.tray = hex(0xEEE7D6); t.trayEdge = hex(0xD6CDBA); t.tile = hex(0xD6D2C9); t.tileEmpty = hex(0xC4BFB5);
        t.tileShadow = hex(0xCDC7BA); t.killTile = hex(0x3A404C); t.killShadow = hex(0x1E222A);
        t.tileInk = hex(0x01295F); t.tileSub = hex(0x46597A);
    } else {
        t.bg = hex(0x0C1524); t.text = hex(0xEDE6D6); t.outline = hex(0x05090F); t.face = hex(0xE9E2D2);
        t.faceInk = hex(0x0A1A33); t.shadow = hex(0x000000); t.shadowA = 0.45f; t.pillFill = hex(0x1D3D6E);
        t.frame = withAlpha(hex(0xEDE6D6), 0.5f); t.muted = withAlpha(hex(0xEDE6D6), 0.55f);
        t.menuBg = hex(0x13223A); t.menuText = hex(0xEDE6D6); t.menuHover = hex(0xEDE6D6); t.menuHoverText = hex(0x0C1524);
        t.sig = hex(0x7C7C7C);
        t.card[kColSteel] = hex(0x3B7088); t.card[kColOlive] = hex(0x77852A); t.card[kColAmber] = hex(0xE5A417);
        t.card[kColNavy] = hex(0x1D3D6E); t.card[kColCream] = hex(0xD8D0BE); t.card[kColRed] = hex(0xD9262B);
        for (int i = 0; i < kNumCardCols; ++i) t.ink[i] = hex(0x0A1A33);
        t.ink[kColNavy] = hex(0xEDE6D6);
        t.sel = hex(0x0A1A33); t.selInk = hex(0xEDE6D6);
        t.tray = hex(0x13213A); t.trayEdge = hex(0x05090F); t.tile = hex(0xC9C4B8); t.tileEmpty = hex(0xA9A498);
        t.tileShadow = hex(0x03060B); t.killTile = hex(0x2A2F3A); t.killShadow = hex(0x000000);
        t.tileInk = hex(0x0A1A33); t.tileSub = hex(0x3E4A60);
    }
    return t;
}

// blend two themes for the light/dark crossfade
static Theme lerpTheme(const Theme& a, const Theme& b, float t)
{
    if (t <= 0.f) return a;
    if (t >= 1.f) return b;
    Theme r;
    #define L(f) r.f = Color(a.f, b.f, t)
    L(bg); L(text); L(outline); L(face); L(faceInk); L(shadow); L(pillFill); L(frame); L(muted);
    L(menuBg); L(menuText); L(menuHover); L(menuHoverText); L(sig); L(sel); L(selInk);
    L(tray); L(trayEdge); L(tile); L(tileEmpty); L(tileShadow); L(killTile); L(killShadow); L(tileInk); L(tileSub);
    for (int i = 0; i < kNumCardCols; ++i) { L(card[i]); L(ink[i]); }
    #undef L
    r.shadowA = a.shadowA + (b.shadowA - a.shadowA) * t;
    return r;
}

static int effectColor(int e)
{
    switch (kEffects[e].family) {
    case kFamFilter: return kColSteel;
    case kFamSpace: return kColOlive;
    case kFamTexture: return kColCream;
    case kFamMod: return kColRed;
    default: break;
    }
    return (e == kFxLoopGrab || e == kFxScatter) ? kColAmber : kColNavy;
}

// -1 = the dark kill tile
static int moveColor(int m)
{
    switch (m) {
    case kMvThrow: return kColOlive;
    case kMvFreeze: case kMvTiming: return kColNavy;
    case kMvGrab8: case kMvGrab4: case kMvGrab2: case kMvGrab1: return kColAmber;
    case kMvAllOff: return kColRed;
    default: return -1;          // Dry Kill and Kill: the dark tile
    }
}

static const Color kAmberAcc(255, 179, 15);
static const Color kRedAcc(253, 21, 27);
static const Color kOkGreen(132, 147, 36);
static Color accentFor(int col) { return (col == kColAmber || col == kColCream) ? kRedAcc : kAmberAcc; }
static Color accentInk(int col) { return (col == kColAmber || col == kColCream) ? hex(0xF7F1E3) : hex(0x0A1A33); }

static const char* const kEffectDesc[kNumEffects] = {
    "Grabs the last LENGTH of the beat and loops it, locked to the tempo.",
    "Hit the pad to grab a slice of the beat and loop it, let go to drop it. LENGTH sets the slice; SPEED below zero plays it backwards.",
    "Resonant filter with drive in front. Sweep CUTOFF for builds and drops.",
    "Three-band DJ kill. Pull LOW all the way down, then slam it back on the one.",
    "Synced echo with tape wobble and saturation. Push FEEDBACK past 90% for dub runaway.",
    "Echoes that melt into reverb, a little more on every repeat.",
    "Big reverb whose tail drifts in pitch like a wave. Great for washes and breakdowns.",
    "Pitch-shifted grains feeding back into a blurry cloud.",
    "Turntable power-off: the record slows to a stop over RATE.",
    "Yanks the record backwards and lets it ease to a stop, every LENGTH.",
    "Playback slows and catches back up every RATE while a filter breathes along.",
    "Chops the beat into steps and replaces some with stutters, reversals or shuffles.",
    "The old-record sound: COMP squashes, NOISE adds surface noise, WOW FLUT adds wow and flutter.",
    "Lowers the bit depth and sample rate, from subtle grit to mangled.",
    "Modulated filters for a sharp, sweeping sound locked to the tempo.",
    "A shimmering, watery doubling of the beat.",
    "A short sweeping delay for the classic jet whoosh.",
    "Metallic ringing at high settings, pulsing and ducking at slow ones.",
    "Notches that sweep through the sound in time with the beat.",
    "Plays the beat back slower, from half speed down to a quarter.",
    "Low-pass filter swept by an LFO. SPEED sets the sweep, RANGE how far it moves.",
    "High-pass filter swept by an LFO. SPEED sets the sweep, RANGE how far it moves.",
    "Band-pass filter swept by an LFO. SPEED sets the sweep, RANGE how far it moves.",
    "Vintage gear on one knob: cassette, flutter, tube amp, vinyl, saturation or radio.",
    "Turns the beat into small grains of sound to smear and scatter.",
    "Loops and repeats a slice of the beat for stutters. Turn on REVERSE to play it backwards.",
    "Slices the beat by DELAY TIME and plays each slice forwards and backwards, REPEATS times.",
    "Echo repeats. RANGE switches between normal, cross-feedback and ping-pong.",
    "Puts the beat in a space. PRE-DELAY, DECAY and DIFFUSION shape the room.",
    "Alternate chorus with every knob, MIX included.",
    "Alternate flanger with every knob, MIX included.",
    "Alternate phaser with every knob, MIX included.",
    "Alternate comb filter with RANGE, FEEDBACK and MIX.",
    "Alternate ring mod: 20 Hz to 2 kHz, with DEPTH and MIX.",
    "Alternate crusher with a MIX knob.",
    "Turns steps of the beat into quick 1/32 rolls. The knob sets how many steps roll; the same ones every bar.",
    "Plays steps of the beat backwards, swelling into the next one. The knob sets how many; the same ones every bar.",
    "Warm saturation, from light grit to heavy crunch. The level comes down as it gets heavier.",
    "Hold to cut the beat and smear it into a giant hanging reverb. Let go: the beat's back and the smear rings out.",
    "Shifts the beat up or down, in time. GLIDE bends into the pitch and back; GRAIN goes from smooth to gritty.",
};

static const char* const kMoveDesc[kNumMoves] = {
    "Hold to cut the beat while delay and reverb tails keep ringing. Knob 1 sets how loud the tails ring.",
    "Hold to throw the beat into a dub echo. Let go and the echoes ring out. Knobs 1 and 2: time and feedback.",
    "Hold to freeze the current moment into a tiny loop. Knobs 1 and 2: length and speed.",
    "Hold to loop the last 1/8 note of everything the effects put out.",
    "Hold to loop the last 1/4 note of everything the effects put out.",
    "Hold to loop the last 1/2 note of everything the effects put out.",
    "Hold to loop the last bar of everything the effects put out.",
    "Tap to switch between RAW (starts the instant you press) and GRID (locks to the beat).",
    "Tap to turn everything off: latched pads, samples and every tail.",
    "Hold to mute everything, tails included.",
};

// ------------------------------------------------------------------------------------------------
// Layout (design units; the window is 1080 x 640)

static constexpr float kW = 1080.f, kH = 640.f;
static constexpr float kTitleCY = 40.f, kTitleEm = 34.f;
static constexpr float kNotesX = 32.f, kNotesY = 72.f, kNotesH = 17.f;
static constexpr float kHdrCY = 34.f, kCapY = 66.f;
static constexpr float kPresetX = 300.f, kPresetY = 19.f, kPresetW = 236.f, kPresetH = 30.f;
static constexpr float kUndoCX = 568.f, kRedoCX = 606.f, kUndoR = 15.f;
static constexpr float kTimX = 660.f, kTimY = 20.f, kTimW = 114.f, kTimH = 28.f;
static constexpr float kAllX = 800.f, kAllY = 18.f, kAllW = 96.f, kAllH = 32.f;
static constexpr float kLimX = 920.f, kLimY = 21.f, kLimW = 52.f, kLimH = 26.f;
static constexpr float kOutCX = 1022.f, kOutCY = 34.f;
static constexpr float kKnobR = 17.f, kDotR = 24.f;

static constexpr float kTabY = 102.f, kTabH = 26.f;
static constexpr float kGearR = 9.f;
static constexpr float kTrayX = 32.f, kTrayY = 138.f, kTrayW = 452.f, kTrayH = 422.f;
static constexpr float kPadX0 = 46.f, kPadY0 = 152.f, kPadW = 97.f, kPadH = 88.f, kPadSX = 109.f, kPadSY = 102.f;

static constexpr float kDetX = 508.f, kDetY = 102.f, kDetW = 540.f, kDetH = 458.f;
static constexpr float kBigR = 28.f, kBigDot = 39.f, kBigCY = 128.f;
static constexpr float kAdvCY = 270.f, kAdvR = 22.f, kAdvDot = 31.f;
static constexpr float kSegY = 404.f, kSegH = 28.f, kSegW = 136.f, kRoundCX = 466.f, kRoundCY = 406.f, kRoundR = 32.f;

static constexpr float kFootCY = 600.f;
// the eight-knob grid in the right card
static const float kGridX[4] = { 82.f, 207.f, 332.f, 457.f };
static constexpr float kGridY0 = 116.f, kGridY1 = 246.f, kGridDot = 33.f;
static constexpr float kBeatRowY = 352.f;
static const float kEmptyBtnX[3] = { 30.f, 195.f, 360.f };
static constexpr float kEmptyBtnY = 250.f;

static constexpr float kPanX = 200.f, kPanY = 50.f, kPanW = 680.f, kPanH = 540.f;

enum { kActLearn = 1, kActCancelLearn, kActClear, kActReset, kActNotesOff, kActOrderTop, kActOrderBottom,
       kActLoadSmp, kActRemoveSmp, kActSetup, kActKnobTypeMenu, kActNotesMenu, kActResetSize, kActResetPads, kActFactory,
       kActSavePadPreset, kActLoadPadPresetMenu, kActSaveSet, kActSaveSetAs, kActDeleteSet, kActAddPage, kActRemovePage,
       kActNote0 = 20, kActKnob0 = 40, kActSpeed0 = 46, kActJump0 = 50, kActGrid0 = 53, kActRenameSet = 57, kActLoadBeat = 58, kActRemoveBeat = 59, kActCollect = 60, kActRelink = 61, kActFeel0 = 62, kActFollow = 65, kActSetPageNext = 66, kActSetPagePrev = 67, kActPadPageNext = 68, kActPadPagePrev = 69, kActNewPattern = 70, kActKCount0 = 80, kActType0 = 100, kActPadPreset0 = 200, kActSet0 = 1000 };
enum { kMenuPad = 0, kMenuKnob, kMenuType, kMenuLearn, kMenuNotes, kMenuSettings, kMenuKnobType, kMenuPadPresets, kMenuSets };

// the setup card's screens
enum { kScrHome = 0, kScrHear, kScrBackup, kScrPads, kScrMorePads, kScrKnobs, kScrDone, kScrConfirmPads, kScrConfirmFactory, kScrName,
       kScrChoose, kScrKeys, kScrBankFilled, kScrSameBank, kScrConfirmReplace };

// the built-in set: the default layout on bank A, banks B and C empty
// the built-in sets: Default, then the groups in YanceySets.hpp
#ifdef _WIN32
static const char* const kPathSep = "\\";
#else
static const char* const kPathSep = "/";
#endif
// motion (the fluid-motion rules): everything that moves, scales, fades or changes value runs on a damped spring,
// integrated at 240 Hz, redirected from where it is (never restarted) when the target changes. The four settings
// below are the approved "Balanced" ones: response = about how long it takes to settle, damping 1 = no overshoot.
static bool gReduceMotion = false;           // the system asks for less motion: springs jump to their targets
struct Spring {
    float x = 0.f, v = 0.f, target = 0.f, k = 300.f, zeta = 0.85f;
    void set(float t) { target = t; }
    void snap(float t) { target = x = t; v = 0.f; }
    Spring& tune(float response, float damping) {   // response in seconds
        const float w = 6.2831853f / response; k = w * w; zeta = damping; return *this;
    }
    Spring& general() { return tune(0.38f, 0.78f); }   // panels, menus, knobs, value changes
    Spring& press()   { return tune(0.10f, 1.f); }     // buttons, pads, hover
    Spring& fade()    { return tune(0.475f, 1.f); }    // fades and quiet transitions
    Spring& quick()   { return tune(0.22f, 1.f); }     // menus, the pad card, bank and screen changes: dialed back
                                                       // (Jeff, 2026-11-17: quicker, no overshoot, smaller moves)
    bool step(float dt) {
        if (gReduceMotion) { if (x == target && v == 0.f) return false; x = target; v = 0.f; return true; }
        if (std::fabs(x - target) < 0.0005f && std::fabs(v) < 0.0005f) {
            if (x == target && v == 0.f) return false;
            x = target; v = 0.f; return true;
        }
        const float c = 2.f * zeta * std::sqrt(k);
        float rem = dt;
        while (rem > 0.f) {
            const float h = rem > (1.f / 240.f) ? (1.f / 240.f) : rem;
            const float a = -k * (x - target) - c * v;
            v += a * h;
            x += v * h;
            rem -= h;
        }
        return true;
    }
};


class YanceyUI : public UI, public IdleCallback
{
public:
    YanceyUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT, false)   // false = no DPF auto-scale (see applyScaleFactor)
    {
        applyScaleFactor(getScaleFactor(), 1.0);

        fFontTitle = createFontFromMemory("yancey-title", kFontShrikhand, kFontShrikhandSize, false);
        fFontHeavy = createFontFromMemory("yancey-heavy", kFontDMSansXB, kFontDMSansXBSize, false);
        fFontBold  = createFontFromMemory("yancey-bold", kFontDMSansBold, kFontDMSansBoldSize, false);
        fFontMed   = createFontFromMemory("yancey-med", kFontDMSansMed, kFontDMSansMedSize, false);

        fTL = makeTheme(false);
        fTD = makeTheme(true);
        fT = fTL;
        for (int i = 0; i < kParamCount; ++i) {
            fParam[i] = isOutputParam(i) ? 0.f : paramDefault(i);
            fKnobDisp[i].k = 1400.f; fKnobDisp[i].zeta = 1.f;
            fPop[i].k = 700.f; fPop[i].zeta = 0.9f;
            if (!isOutputParam(i)) fKnobDisp[i].snap(normOfParam(i, fParam[i]));
        }
        for (int i = 0; i < kNumSlots; ++i) fParam[oSmpPos0 + i] = -1.f;
        fParam[oFocusPhase] = fParam[oGrabPhase] = -1.f;
        for (int s = 0; s < kNumSlots; ++s) {
            for (int e = 0; e < kNumEffects; ++e) fMemValid[s][e] = false;
            fActive[s] = false;
            fLift[s].general();
            fSmpLoaded[s] = false; fSmpDur[s] = 0.f; fSmpErrorUntil[s] = 0.0;
        }
        for (int q = 0; q < kPadsPerPage; ++q) {
            fPosSlot[q] = q;
            fPadPX[q].general(); fPadPY[q].general();
            fPlusA[q].press();
            fPadPX[q].snap(padX(q)); fPadPY[q].snap(padY(q));
        }
        for (int p = 0; p < kNumPads; ++p) {
            fPadScale[p].tune(0.16f, 0.82f).snap(1.f);           // a pad hit: a quick press, a hint of give
            fRing[p].fade();
        }
        for (int j = 0; j < kNumJobs; ++j) { fJobPop[j].general(); fGhost[j] = -1.f; fGhostAt[j] = 0.0; fJobLast[j] = -9.f; }
        for (int i = 0; i < kNumLearn; ++i) { fMap[i] = -1; fProjectMap[i] = -1; }
        fLimitT.general().snap(1.f);
        fTimingT.general();
        fNotesT.general();
        fSpin.k = 6.f; fSpin.zeta = 1.f;
        fBounce.general();
        fDarkT.fade();
        fPanelA.quick();
        fToastA.fade();
        fUndoPress.tune(0.16f, 0.82f).snap(1.f);
        fRedoPress.tune(0.16f, 0.82f).snap(1.f);
        fMenuA.quick();
        fHovX.press(); fHovY.press(); fHovW.press(); fHovH.press(); fHovA.press();
        fTabX.quick().snap(pageTabX(0));
        fPageSlide.quick();
        fCardA.quick().snap(1.f);
        fScrA.quick().snap(1.f);
        gReduceMotion = reduceMotion();
        std::snprintf(fPresetName, sizeof(fPresetName), "%s", "Default");

        Prefs pr;
        if (readPrefs(pr)) {
            if (pr.dark >= 0) { fDark = pr.dark != 0; fDarkT.snap(fDark ? 1.f : 0.f); fT = fDark ? fTD : fTL; }
            if (pr.uscale > 0.f || pr.scale > 0.f) applyScaleFactor(fScale, savedUserScale(pr));
        }
        readComputerSetup(pr);
        fNativeWin = getWindow().getNativeWindowHandle();
        installFileDrop(fNativeWin, &YanceyUI::onFileDropped, this);

        fOpenedAt = fLastTime = now();
        fLastW = (float)getWidth(); fLastH = (float)getHeight();
        addIdleCallback(this, 16);
    }

    ~YanceyUI() override
    {
        removeFileDrop(fNativeWin);
        removeIdleCallback(this);
    }

    void readComputerSetup(const Prefs& pr)
    {
        parseList(pr.map.c_str(), fMap, kNumLearn);
        fKnobMode = (pr.kmode >= kKnobAuto && pr.kmode < kNumKnobModes) ? pr.kmode : kKnobAuto;
        fNoteStart = (pr.notes >= 0 && pr.notes <= 112) ? pr.notes : -1;
        fNoteOrder = pr.norder == 1 ? 1 : 0;
        fKnobSpeed = pr.kspeed >= 0 && pr.kspeed <= 2 ? pr.kspeed : 1;
        fKnobJump = pr.kjump == 1;          // pickup unless switched to jump
        fKnobCountSet = pr.kcount >= 1 && pr.kcount <= 5 ? pr.kcount : 0;
        fKnobGridBottom = pr.kgrid == 1;
        fPadFeel = pr.padfeel >= 0 && pr.padfeel <= 2 ? pr.padfeel : 0;
        fFollow = pr.follow == 1;
        fNotesT.snap(fNoteStart >= 0 ? 1.f : 0.f);
        fWelcomed = pr.welcomed == 1;
    }
    bool computerHasSetup() const { return mappedCount() > 0 || fNoteStart >= 0; }

protected:
    // ------------------------------------------------------------------------------ what's where
    int effectAt(int s) const { return effectOf(fParam, s); }
    int moveAt(int s) const { return moveOf(fParam, s); }
    // 0 effect, 1 move, 2 one-shot sample, 3 beat, 4 empty
    int kindOf(int s) const { return fBeatHas[s] ? 3 : (fSmpLoaded[s] ? 2 : (isEmpty(fParam, s) ? 4 : (moveAt(s) >= 0 ? 1 : 0))); }
    bool hasAudio(int s) const { return s >= 0 && s < kNumSlots && (fSmpLoaded[s] || fBeatHas[s]); }
    bool beatReady(int s) const { return (fBeatReadyMask[s < 24 ? 0 : 1] >> (s < 24 ? s : s - 24)) & 1; }
    static int pageOfSlot(int s) { return padOfSlot(s) / kPadsPerPage; }
    static int posOfSlot(int s) { return padOfSlot(s) % kPadsPerPage; }
    // the slot shown at a grid position of the current page (-1 = the kill pad)
    int slotAtPos(int q) const { return fPage == 0 && q == kKillPad ? -1 : slotOfPad(fPage * kPadsPerPage + q); }
    int pagesShown() const { return kNumPages; }      // JAY always shows banks A, B and C
    int pagesShownOld() const
    {
        int n = std::max(1, std::min((int)kNumPages, fPagesShown));
        for (int p = kPadsPerPage; p < kNumPads; ++p) if (fMap[kLearnPad0 + p] >= 0) n = std::max(n, p / kPadsPerPage + 1);
        return n;
    }

    // ------------------------------------------------------------------------------ host -> UI

    void parameterChanged(uint32_t index, float value) override
    {
        if (index >= (uint32_t)kParamCount) return;
        const float prev = fParam[index];
        fParam[index] = value;
        const int i = (int)index;
        const double t = now();

        if (i >= oEff0 && i < oEff0 + kNumSlots) {
            const int s = i - oEff0;
            const bool on = value > 0.5f, was = fActive[s];
            fActive[s] = on;
            // DJ Looper SNAP BACK: the engine already jumped back; keep the saved knob in step
            if (was && !on && effectAt(s) == kFxDjLooper && fParam[slotParam(s, F_SNAP)] > 0.5f) {
                const int pid = slotParam(s, F_K2);
                setParam(pid, 1.f, false);
                fKnobDisp[pid].set(1.f);
            }
        } else if (i >= oSmpPos0 && i < oSmpPos0 + kNumSlots) {
        } else if (i == oFocus) {
            const int f = (int)std::lround(value);
            if (f >= 0 && f < kNumSlots && f != fSel) { fSel = f; if (!fPanelOpen) showPage(pageOfSlot(f)); }
        } else if (i == oMoves) {
            fMoves = (unsigned)std::lround(value);
            const bool grid = (fMoves & (1u << kMvTiming)) != 0;
            if (t - fTimingSetAt > 0.4 && (fParam[pTiming] > 0.5f) != grid) { fParam[pTiming] = grid ? 1.f : 0.f; setParameterValue(pTiming, fParam[pTiming]); fTimingT.set(grid ? 1.f : 0.f); }
        } else if (i == oBeatReadyLo || i == oBeatReadyHi) {
            fBeatReadyMask[i == oBeatReadyLo ? 0 : 1] = (int)std::lround(value);
        } else if (i == oBeatPlaying) {
            fBeatPlaying = (int)std::lround(value);
        } else if (i == oBank) {
            const int b = (int)std::lround(value);         // a bank button on the controller (or the project's bank)
            if (b >= 0 && b < kNumPages && b != fPage && !fPanelOpen) { setPagesShown(std::max(pagesShown(), b + 1)); showPage(b); }
        } else if (i == oPicked) {
            fPickedMask = (int)std::lround(value);
        } else if (i == oKnobTypes) {
            // knob types the engine has worked out: keep them on this computer
            const int packed = (int)std::lround(value);
            if (packed != fKnobTypesSaved && packed != 0) {
                fKnobTypesSaved = packed;
                std::string kt; char b[8];
                for (int j = 0; j < kNumJobs; ++j) { std::snprintf(b, sizeof(b), j ? ",%d" : "%d", (packed >> (3 * j)) & 7); kt += b; }
                Prefs up; up.ktypes = kt; writePrefs(up);
            }
        } else if (i == oJobSeq) {
            forwardJobs();
        } else if (i == oLearnSeq) {
            const int seq = (int)std::lround(value);
            if (seq != fLastLearnSeq) { const bool first = fLastLearnSeq < 0; fLastLearnSeq = seq; if (!first) learnedFromDsp(); }
        } else if (i == oLearnDupSeq) {
            const int seq = (int)std::lround(value);
            if (seq != fLearnDupSeq) {
                const bool first = fLearnDupSeq < 0;
                fLearnDupSeq = seq;
                if (!first && fPanelOpen && (fScr == kScrPads || fScr == kScrKnobs)) {
                    fWizDup = (int)std::lround(fParam[oLearnDup]); fWizDupAt = t;
                    if (fScr == kScrPads && fBankQuick && fWizDup >= kLearnPad0 && fWizDup < kLearnPad0 + kPadsPerPage) {
                        fWizDup = -1; cancelLearn(); fScr = kScrSameBank;          // same notes as bank A: no banks to fill
                    }
                }
            }
        } else if (i == oMidiLastSeq) {
            const int seq = (int)std::lround(value);
            if (seq != fMidiLastSeq) {
                const bool first = fMidiLastSeq < 0;
                fMidiLastSeq = seq;
                if (!first && !fPanelOpen && mappedCount() > 0) {
                    const int code = (int)std::lround(fParam[oMidiLast]);
                    showToast((code & 0x20000) ? "That knob isn't mapped. Is your controller on the right knob bank?"
                                               : "That pad isn't mapped. Is your controller on the right pad bank?");
                }
            }
        } else if (i == oRepeatSeq) {
            const int seq = (int)std::lround(value);
            if (seq != fRepeatSeq) { const bool first = fRepeatSeq < 0; fRepeatSeq = seq; if (!first) showToast("A pad is firing on its own. Turn off Note Repeat on your controller."); }
        } else if (i == oMidiSeq) {
            const int seq = (int)std::lround(value);
            if (seq != fMidiSeq) {
                if (fMidiSeq > 0 || seq > 0) fMidiAt = t;
                fMidiSeq = seq;
                if (fPanelOpen && fScr == kScrHear && fHeardAt <= 0.0 && seq != fHearSeq0) fHeardAt = t;
            }
        } else if (i == oMidiState) {
            fMidiState = (int)std::lround(value);
        } else if (i == oPadHitSeq) {
            const int seq = (int)std::lround(value);
            if (seq != fPadHitSeq) {
                const bool first = fPadHitSeq < 0;
                fPadHitSeq = seq;
                const int p = (int)std::lround(fParam[oPadHit]);
                if (!first && p >= 0 && p < kNumPads) {
                    fPadScale[p].x = 0.92f; fPadScale[p].set(1.f);
                    fWizHitAt[p] = t;
                    if (!fPanelOpen) showPage(p / kPadsPerPage);
                }
            }
        } else if (i == oKill) {
            fKill = value > 0.5f;
        } else if (i == oLevel) {
            if (value > fLastLevel + 0.08f && value > 0.15f) fBounce.v += 2.5f * value;
            fLastLevel = value;
        } else if (!isOutputParam(i)) {
            if (fDragKnob != i) fKnobDisp[i].set(normOfParam(i, value));
            if (i == pLimit) fLimitT.set(value > 0.5f ? 1.f : 0.f);
            if (i == pTiming) fTimingT.set(value > 0.5f ? 1.f : 0.f);
            (void)prev;
        }
        wake();
    }

    void stateChanged(const char* key, const char* value) override
    {
        if (value == nullptr) value = "";
        if (std::strncmp(key, "beat", 4) == 0 && std::isdigit((unsigned char)key[4])) {
            const int k = std::atoi(key + 4);
            if (k < 0 || k >= kNumSlots) return;
            applyBeatInfo(k, value[0] ? std::make_shared<const std::string>(value) : nullptr, false);
            wake();
            return;
        }
        if (std::strcmp(key, "beatmode") == 0) { fBeatMode = std::max(0, std::min(2, std::atoi(value))); wake(); return; }
        if (std::strncmp(key, "smp", 3) == 0) {
            const int k = std::atoi(key + 3);
            if (k < 0 || k >= kNumSlots) return;
            std::string name; double rate = 0.0; int ch = 0, frames = 0;
            fSmpLoaded[k] = peekSample(value, name, rate, ch, frames);
            fSmpName[k] = fSmpLoaded[k] ? name : std::string();
            fSmpDur[k] = fSmpLoaded[k] ? (float)(frames / rate) : 0.f;
            fSmpEnc[k] = fSmpLoaded[k] ? std::make_shared<const std::string>(value) : nullptr;
        } else if (std::strcmp(key, "midimap") == 0) {
            parseList(value, fProjectMap, kNumLearn);
            bool any = false;
            for (int i = 0; i < kNumLearn; ++i) any |= fProjectMap[i] >= 0;
            if (any && fPanelOpen && fScr == kScrHear && fHeardAt <= 0.0 && !computerHasSetup()) fScr = kScrBackup;
        } else if (std::strcmp(key, "dark") == 0) {
            if (value[0] != '\0') { fDark = value[0] == '1'; fDarkT.snap(fDark ? 1.f : 0.f); fT = fDark ? fTD : fTL; }
        } else if (std::strcmp(key, "mem") == 0) {
            parseMem(value);
        } else if (std::strcmp(key, "pages") == 0) {
            if (value[0]) fPagesShown = std::max(1, std::min((int)kNumPages, std::atoi(value)));
        } else if (std::strcmp(key, "preset") == 0) {
            if (value[0]) std::snprintf(fPresetName, sizeof(fPresetName), "%s", value);
        } else if (std::strcmp(key, "size") == 0) {
            const float sc = value[0] ? (float)std::atof(value[0] == 'u' ? value + 1 : value) : 0.f;
            if (sc > 0.1f) {
                const double u = clampUserScale(value[0] == 'u' ? sc : sc / fScale);
                const uint w = (uint)std::lround(kW * u * fScale);
                if (std::abs((int)w - (int)getWidth()) > 2) { applyScaleFactor(fScale, u); fLastW = (float)getWidth(); fLastH = (float)getHeight(); fSizeDirty = false; }
            }
        }
        wake();
    }

    // A knob on the controller moved something: mirror it into the real parameter so the screen
    // follows and it saves with the project. A turn counts as one undo step.
    void forwardJobs()
    {
        const double t = now();
        for (int j = 0; j < kNumJobs; ++j) {
            const float v = fParam[oJob0 + j];
            if (v == fJobLast[j]) continue;
            fJobLast[j] = v;
            fJobPop[j].x = 1.f; fJobPop[j].set(0.f);
            if (!(fPickedMask & (1 << j))) { fGhost[j] = v; fGhostAt[j] = t; continue; }
            fGhost[j] = -1.f;
            const int target = jobTarget(j, fSel, fParam, hasAudio(fSel));
            if (target < 0) continue;
            if (std::fabs(normOfParam(target, fParam[target]) - v) < 0.0005f) continue;
            if (t - fHwKnobAt > 1.0 && isSlotParam(target)) pushUndo();
            fHwKnobAt = t;
            const float before = normOfParam(target, fParam[target]);
            fParam[target] = valueOfNorm(target, v);
            setParameterValue(target, fParam[target]);
            forwardFollowers(target, normOfParam(target, fParam[target]) - before);
            if (fDragKnob != target) fKnobDisp[target].snap(v);     // hardware moves show instantly
            fPop[target].x = 1.f; fPop[target].set(0.f);
        }
    }

    // fewer knobs than controls: the controls following this knob move with it, here and in the host
    // (the engine does the same, so the sound follows straight away; these writes make the host agree)
    int knobsNow() const { return knobCount(fMap, fKnobCountSet); }
    void forwardFollowers(int target, float delta)
    {
        if (delta == 0.f || !isSlotKnob(target)) return;
        const int s = slotOf(target), e = effectAt(s);
        if (e < 0) return;
        const int n = knobsNow(), nc = numControls(e);
        if (n >= nc) return;
        const int h = fieldOf(target) - F_K1;
        for (int c = n; c < nc; ++c) {
            const Fold f = foldResolve(e, c, n);
            if (f.host != h) continue;
            const int p = slotParam(s, F_K1 + c);
            const float nv = clampf(normOfParam(p, fParam[p]) + f.amt * delta, 0.f, 1.f);
            fParam[p] = valueOfNorm(p, nv);
            setParameterValue(p, fParam[p]);
            if (fDragKnob != p) fKnobDisp[p].snap(nv);
        }
    }
    // the knobs your controller reaches on this pad: fewer than the effect has?
    bool folding(int s) const
    {
        const int e = effectAt(s);
        return e >= 0 && !hasAudio(s) && kindOf(s) == 0 && knobsNow() < numControls(e);
    }

    // the engine's knob values, read directly every frame (no waiting on the host)
    void pollEngine()
    {
        if (fLiveIn == nullptr) {
            if (fLiveTried) return;
            fLiveTried = true;
            fLiveIn = yLiveFind(getPluginInstancePointer());
            if (fLiveIn == nullptr) return;
        }
        bool amtMoved = false;
        for (int s = 0; s < kNumSlots; ++s) {
            const float a = fLiveIn->amt[s].load(std::memory_order_relaxed);
            if (std::fabs(a - fAmt[s]) > 0.002f) { fAmt[s] = a; amtMoved = true; }
        }
        if (amtMoved) repaint();
        const int seq = fLiveIn->seq.load(std::memory_order_acquire);
        if (seq == fLiveSeq) return;
        fLiveSeq = seq;
        fPickedMask = fLiveIn->picked.load(std::memory_order_relaxed);
        for (int j = 0; j < kNumJobs; ++j) fParam[oJob0 + j] = fLiveIn->job[j].load(std::memory_order_relaxed);
        forwardJobs();
        repaint();
    }

    // ------------------------------------------------------------------------------ animation clock

    void idleCallback() override
    {
        const double t = now();
        float dt = (float)(t - fLastTime);
        fLastTime = t;
        if (dt > 0.05f) dt = 0.05f;
        if (dt <= 0.f) return;

        pollEngine();
        bool moving = false;
        for (int i = 0; i < pPad0; ++i) { moving |= fKnobDisp[i].step(dt); moving |= fPop[i].step(dt); }
        for (int q = 0; q < kPadsPerPage; ++q) { moving |= fPadPX[q].step(dt); moving |= fPadPY[q].step(dt); }
        for (int s = 0; s < kNumSlots; ++s) moving |= fLift[s].step(dt);
        for (int p = 0; p < kNumPads; ++p) {
            const int s = slotOfPad(p);
            fRing[p].set((s < 0 ? fKill : padActive(s)) ? 1.f : 0.f);
            moving |= fPadScale[p].step(dt);
            moving |= fRing[p].step(dt);
        }
        for (int j = 0; j < kNumJobs; ++j) moving |= fJobPop[j].step(dt);
        moving |= fLimitT.step(dt); moving |= fTimingT.step(dt); moving |= fNotesT.step(dt); moving |= fAllPress.step(dt);
        moving |= fUndoPress.step(dt); moving |= fRedoPress.step(dt);
        fPanelA.set(fPanelOpen ? 1.f : 0.f); moving |= fPanelA.step(dt);
        fMenuA.set(fMenuOpen ? 1.f : 0.f); moving |= fMenuA.step(dt);
        moving |= fHovX.step(dt); moving |= fHovY.step(dt); moving |= fHovW.step(dt); moving |= fHovH.step(dt); moving |= fHovA.step(dt);
        fTabX.set(pageTabX(fPage)); moving |= fTabX.step(dt);
        moving |= fPageSlide.step(dt);
        if (fSel != fCardSel) { fCardSel = fSel; fCardA.x = 0.f; fCardA.v = 0.f; fCardA.set(1.f); }   // another pad's card eases in
        moving |= fCardA.step(dt);
        if (fScr != fLastScr) { fLastScr = fScr; if (fPanelA.x > 0.5f) { fScrA.x = 0.f; fScrA.v = 0.f; } fScrA.set(1.f); }
        moving |= fScrA.step(dt);
        for (int q = 0; q < kPadsPerPage; ++q) { fPlusA[q].set(fPlusHover == q ? 1.f : 0.f); moving |= fPlusA[q].step(dt); }
        fToastA.set(t < fToastUntil ? 1.f : 0.f); moving |= fToastA.step(dt);
        for (int k = 0; k < kNumSlots; ++k) {
            if (fSmpErrorUntil[k] > 0.0 && fSmpErrorUntil[k] < t) { fSmpErrorUntil[k] = 0.0; moving = true; }
            if (fParam[oSmpPos0 + k] >= 0.f) moving = true;
        }
        if (!fWelcomeChecked && t - fOpenedAt > 0.35) {
            fWelcomeChecked = true;
            if (!computerHasSetup() && !fWelcomed) {
                bool backup = false;
                for (int i = 0; i < kNumLearn; ++i) backup |= fProjectMap[i] >= 0;
                openSetup(backup ? kScrBackup : kScrHear);
            }
        }
        if (fPanelOpen && fScr == kScrHear && fHeardAt > 0.0 && t - fHeardAt > 1.3) startMapping();
        if (fPanelOpen) moving = true;
        if (fDarkT.step(dt)) { fT = lerpTheme(fTL, fTD, fDarkT.x); moving = true; }

        fSpin.set(fParam[oLevel] > 0.003f ? 1.f : 0.f);
        moving |= fSpin.step(dt);
        moving |= fBounce.step(dt);
        if (fSpin.x > 0.0005f) { fDonutAngle += fSpin.x * 0.42f * 6.2831853f * dt; if (fDonutAngle > 6.2831853f) fDonutAngle -= 6.2831853f; moving = true; }
        for (int j = 0; j < kNumJobs; ++j) if (fGhost[j] >= 0.f && t - fGhostAt[j] < 1.8) moving = true;

        {
            const float W = (float)getWidth(), H = (float)getHeight();
            if (W != fLastW || H != fLastH) { fLastW = W; fLastH = H; fSizeChangedAt = t; fSizeDirty = true; }
            else if (fSizeDirty && t - fSizeChangedAt > 0.6) {
                fSizeDirty = false;
                const float sc = (float)userScale();
                if (std::fabs(sc - fSavedScale) > 0.003f) {
                    fSavedScale = sc;
                    char b[24]; std::snprintf(b, sizeof(b), "u%.4f", sc);   // "u": the user's scale, not pixels
                    setState("size", b);
                    Prefs up; up.uscale = sc; up.scale = W / kW; writePrefs(up);
                }
            }
        }

        const bool looping = fParam[oFocusPhase] >= 0.f || fParam[oGrabPhase] >= 0.f;
        const bool blinking = fLearning >= 0 || (t - fMidiAt < 1.0) || fToastA.x > 0.001f || fNaming;
        if (moving || looping || blinking || fWake > 0) { if (fWake > 0) --fWake; repaint(); }
    }

    // ------------------------------------------------------------------------------ pad state

    bool padActive(int s) const
    {
        if (s < 0) return fKill;
        if (fBeatHas[s]) return fBeatPlaying == s;
        if (fSmpLoaded[s]) return fParam[oSmpPos0 + s] >= 0.f;
        if (isEmpty(fParam, s)) return false;
        const int m = moveAt(s);
        if (m == kMvTiming) return (fMoves & (1u << kMvTiming)) != 0;
        if (m == kMvAllOff) return false;
        if (m == kMvKill) return fKill;
        return fActive[s];
    }

    // ------------------------------------------------------------------------------ drawing

    void onNanoDisplay() override
    {
        const float s = (float)getWidth() / kW;
        save();
        scale(s, s);
        const float dh = (float)getHeight() / s;
        beginPath(); rect(0, 0, kW, std::max(dh, kH)); fillColor(fT.bg); fill();

        drawHeader();
        drawPadSection();
        drawDetail();
        drawFooter();

        if (fToastA.x > 0.003f) drawToast();
        if (fPanelA.x > 0.003f) drawPanel();
        if (fMenuOpen || fMenuA.x > 0.003f) drawMenu();
        restore();
    }

    void drawText(FontId f, float em, const Color& c, float x, float y, int align, const char* str, float spacing = 0.f)
    {
        if (str == nullptr || str[0] == '\0') return;
        const bool title = (f == fFontTitle);
        const float ratio = title ? 1.458f : 1.302f;
        const float cap = title ? 0.669f : 0.70f;
        fontFaceId(f);
        fontSize(em * ratio + 0.02f);
        textLetterSpacing(spacing);
        fillColor(c);
        if (align & NanoVG::ALIGN_MIDDLE) {
            align = (align & ~NanoVG::ALIGN_MIDDLE) | NanoVG::ALIGN_BASELINE;
            y += cap * em * 0.5f;
        }
        float xf[6];
        currentTransform(xf);
        if (std::fabs(xf[1]) < 1e-5f && std::fabs(xf[2]) < 1e-5f && xf[0] > 0.f && xf[3] > 0.f) {
            x = (std::round(xf[0] * x + xf[4]) - xf[4]) / xf[0];
            y = (std::round(xf[3] * y + xf[5]) - xf[5]) / xf[3];
        }
        textAlign(align);
        text(x, y, str, nullptr);
    }

    float measure(FontId f, float em, const char* str, float spacing)
    {
        fontFaceId(f);
        fontSize(em * (f == fFontTitle ? 1.458f : 1.302f) + 0.02f);
        textLetterSpacing(spacing);
        Rectangle<float> b;
        return textBounds(0.f, 0.f, str, nullptr, b);
    }

    void drawDonut(float cx, float cy)
    {
        static const Color kDough(232, 184, 114), kFrost(244, 128, 178);
        static const Color kSprBlue(48, 118, 214), kSprWhite(255, 255, 255), kSprYellow(255, 210, 63);
        const float sc = 1.f + 0.07f * fBounce.x;
        save();
        translate(cx, cy);
        scale(sc, sc);
        rotate(fDonutAngle);
        const float ox = -21.f, oy = -21.f;
        lineJoin(NanoVG::ROUND);
        lineCap(NanoVG::ROUND);
        beginPath(); circle(0.f, 0.f, 17.f);
        fillColor(kDough); fill();
        strokeColor(fT.outline); strokeWidth(2.5f); stroke();
        save();
        scale(1.17f, 1.17f);
        beginPath();
        moveTo(ox + 8.5f, oy + 17.f);
        quadTo(ox + 11.f, oy + 12.f, ox + 15.f, oy + 13.f);
        quadTo(ox + 18.f, oy + 9.f,  ox + 22.f, oy + 11.f);
        quadTo(ox + 26.f, oy + 8.f,  ox + 29.f, oy + 12.f);
        quadTo(ox + 34.f, oy + 12.f, ox + 34.f, oy + 17.f);
        quadTo(ox + 36.f, oy + 21.f, ox + 33.f, oy + 24.f);
        quadTo(ox + 34.f, oy + 29.f, ox + 29.f, oy + 30.f);
        quadTo(ox + 26.f, oy + 34.f, ox + 22.f, oy + 32.f);
        quadTo(ox + 18.f, oy + 35.f, ox + 15.f, oy + 31.f);
        quadTo(ox + 10.f, oy + 31.f, ox + 9.5f, oy + 26.f);
        quadTo(ox + 6.5f, oy + 22.f, ox + 8.5f, oy + 17.f);
        closePath();
        fillColor(kFrost); fill();
        strokeColor(fT.outline); strokeWidth(2.f / 1.17f); stroke();
        restore();
        beginPath(); circle(0.f, 0.f, 5.f);
        fillColor(fT.bg); fill();
        strokeColor(fT.outline); strokeWidth(2.f); stroke();
        struct Spr { float a, r, rot; int c; };
        static const Spr kSpr[] = {
            { 0.20f, 9.5f, 0.9f, 0 }, { 0.75f, 12.5f, -0.6f, 1 }, { 1.30f, 9.f, 0.3f, 2 },
            { 1.85f, 12.5f, 1.4f, 0 }, { 2.45f, 9.5f, -1.1f, 2 }, { 3.00f, 12.5f, 0.5f, 1 },
            { 3.55f, 9.f, -0.2f, 0 }, { 4.10f, 12.5f, 1.0f, 2 }, { 4.65f, 9.5f, -0.8f, 1 },
            { 5.20f, 12.5f, 0.2f, 0 }, { 5.75f, 9.f, 1.3f, 2 }, { 2.75f, 12.f, -0.3f, 0 },
        };
        for (const Spr& sp : kSpr) {
            const float px = sp.r * std::cos(sp.a), py = sp.r * std::sin(sp.a);
            const float dx = 1.6f * std::cos(sp.rot), dy = 1.6f * std::sin(sp.rot);
            beginPath();
            moveTo(px - dx, py - dy); lineTo(px + dx, py + dy);
            strokeColor(sp.c == 0 ? kSprBlue : (sp.c == 1 ? kSprWhite : kSprYellow)); strokeWidth(2.2f); stroke();
        }
        restore();
    }

    // moon morph: a soft disc becomes a crescent as a shadow slides across it (follows the theme crossfade)
    void drawMoon(float cx, float cy)
    {
        const float m = clamp01(fDarkT.x);
        const float R = 9.f;
        const Color btn = Color(withAlpha(fT.bg, 0.f), fT.pillFill, m);
        beginPath(); circle(cx, cy, R);
        fillColor(btn); fill();
        const Color body = Color(fT.muted, kAmberAcc, m);
        const float r = R * 0.58f;
        beginPath(); circle(cx - 0.05f * R, cy + 0.05f * R, r);
        fillColor(body); fill();
        if (m > 0.01f) {
            const float ang = (-40.f - 20.f * m) * 3.14159265f / 180.f;
            const float dist = R * (0.9f - 0.6f * m);
            const float sr = r * 0.86f * std::sqrt(m);
            beginPath(); circle(cx + dist * std::cos(ang), cy + dist * std::sin(ang), sr);
            fillColor(Color(fT.bg, fT.pillFill, m)); fill();
        }
        beginPath(); circle(cx, cy, R);
        strokeColor(Color(fT.muted, fT.outline, m)); strokeWidth(1.5f); stroke();
    }

    void pill(float x, float y, float w, float h, const Color& f, const Color& s, float sw)
    {
        beginPath();
        roundedRect(x, y, w, h, h * 0.5f);
        fillColor(f); fill();
        strokeColor(s); strokeWidth(sw); stroke();
    }

    void knobShape(float cx, float cy, float n, const Color& ink, bool bipolar, float r, float dotR, float pop)
    {
        n = clamp01(n);
        for (int i = 0; i < 11; ++i) {
            const float t = i / 10.f;
            const float a = (-135.f + 270.f * t) * 3.14159265f / 180.f;
            bool lit;
            if (bipolar) lit = t >= std::fmin(0.5f, n) - 0.001f && t <= std::fmax(0.5f, n) + 0.001f;
            else lit = t <= n + 0.001f;
            beginPath();
            circle(cx + dotR * std::sin(a), cy - dotR * std::cos(a), lit ? 2.1f + pop * 0.3f : 1.6f);
            fillColor(withAlpha(ink, lit ? 1.f : 0.4f));
            fill();
        }
        const float rr = r * (1.f + pop * 0.05f);
        beginPath(); circle(cx, cy, rr - 1.5f);
        fillColor(fT.face); fill();
        strokeColor(fT.outline); strokeWidth(3.f); stroke();
        const float ang = (-135.f + 270.f * n) * 3.14159265f / 180.f;
        const float sa = std::sin(ang), ca = std::cos(ang);
        beginPath();
        moveTo(cx + sa * 3.f, cy - ca * 3.f);
        lineTo(cx + sa * (rr - 6.f), cy - ca * (rr - 6.f));
        lineCap(NanoVG::ROUND);
        strokeColor(fT.outline); strokeWidth(3.f); stroke();
    }

    void drawSkull(float cx, float cy, const Color& c, const Color& bg)
    {
        lineCap(NanoVG::ROUND);
        for (int i = 0; i < 2; ++i) {
            const float a = (i ? -45.f : 45.f) * 3.14159265f / 180.f;
            const float dx = 13.f * std::cos(a), dy = 13.f * std::sin(a);
            beginPath(); moveTo(cx - dx, cy + 3.f - dy); lineTo(cx + dx, cy + 3.f + dy);
            strokeColor(c); strokeWidth(3.2f); stroke();
            for (int e = -1; e <= 1; e += 2) {
                const float ex = cx + e * dx, ey = cy + 3.f + e * dy;
                for (int o = -1; o <= 1; o += 2) {
                    beginPath(); circle(ex + o * 1.6f * std::sin(a), ey - o * 1.6f * std::cos(a), 2.1f);
                    fillColor(c); fill();
                }
            }
        }
        beginPath(); ellipse(cx, cy - 3.5f, 8.f, 7.5f); fillColor(c); fill();
        beginPath(); roundedRect(cx - 5.f, cy + 1.f, 10.f, 6.f, 2.f); fillColor(c); fill();
        beginPath(); circle(cx - 3.5f, cy - 3.3f, 2.1f); fillColor(bg); fill();
        beginPath(); circle(cx + 3.5f, cy - 3.3f, 2.1f); fillColor(bg); fill();
        beginPath(); moveTo(cx, cy - 0.5f); lineTo(cx - 1.3f, cy + 2.f); lineTo(cx + 1.3f, cy + 2.f); closePath();
        fillColor(bg); fill();
    }


    // shorten a name with "..." so it fits in the given width
    std::string fitText(FontId f, float em, const std::string& in, float maxW, float spacing)
    {
        if (measure(f, em, in.c_str(), spacing) <= maxW) return in;
        std::string t = in;
        while (!t.empty()) {
            t.pop_back();
            const std::string c = t + "...";
            if (measure(f, em, c.c_str(), spacing) <= maxW) return c;
        }
        return "...";
    }


    void caption(float cx, const char* str, const Color& c)
    {
        drawText(fFontHeavy, 10.f, c, cx, kCapY, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, str, 0.3f);
    }

    void segSwitch(float x, float y, float w, float h, int n, int sel, const char* const* labels,
                   const Color& base, const Color& edge, const Color& selFill, const Color& selInk, const Color& offInk, float em = 11.f)
    {
        beginPath(); roundedRect(x, y, w, h, h * 0.5f);
        fillColor(base); fill();
        if (sel >= 0 && sel < n) {
            save();
            intersectScissor(x + w * sel / n, y, w / n, h);
            beginPath(); roundedRect(x, y, w, h, h * 0.5f);
            fillColor(selFill); fill();
            restore();
        }
        beginPath(); roundedRect(x, y, w, h, h * 0.5f);
        strokeColor(edge); strokeWidth(2.5f); stroke();
        for (int i = 1; i < n; ++i) {
            const float lx = x + w * i / n;
            beginPath(); moveTo(lx, y + 4.f); lineTo(lx, y + h - 4.f);
            strokeColor(edge); strokeWidth(2.f); stroke();
        }
        for (int i = 0; i < n; ++i)
            drawText(fFontHeavy, em, i == sel ? selInk : offInk, x + w * (i + 0.5f) / n, y + h * 0.5f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, labels[i], 0.3f);
    }

    void chevron(float cx, float cy, const Color& c, float flip = 0.f)
    {
        save();
        translate(cx, cy);
        rotate(3.14159265f * flip);
        beginPath();
        moveTo(-3.5f, -1.75f); lineTo(0.f, 1.75f); lineTo(3.5f, -1.75f);
        lineCap(NanoVG::ROUND); lineJoin(NanoVG::ROUND);
        strokeColor(c); strokeWidth(2.f); stroke();
        restore();
    }

    float notesPillW()
    {
        char lb[64]; notesLabel(lb, sizeof(lb));
        return std::round(measure(fFontBold, 9.5f, lb, 0.3f) + 20.f);
    }
    void notesLabel(char* buf, size_t sz)
    {
        if (fNoteStart < 0) { std::snprintf(buf, sz, "NOTES OFF"); return; }
        char nb[8]; formatNote(fNoteStart, nb, sizeof(nb));
        std::snprintf(buf, sz, "NOTES  %s \xC2\xB7 %d%s", nb, fNoteStart, fNoteOrder ? " \xC2\xB7 BOTTOM UP" : "");
    }

    int mappedCount() const { int n = 0; for (int i = 0; i < kNumLearn; ++i) if (fMap[i] >= 0) ++n; return n; }
    int mappedPads() const { int n = 0; for (int i = 0; i < kNumPads; ++i) if (fMap[kLearnPad0 + i] >= 0) ++n; return n; }
    int mappedKnobs() const { int n = 0; for (int j = 0; j < kNumJobs; ++j) if (fMap[kLearnKnob0 + j] >= 0) ++n; return n; }

    int ctrlStatus() const
    {
        if (fMidiSeq <= 0) return 0;
        return (fMidiState == 1 || computerHasSetup()) ? 1 : 2;
    }
    const char* ctrlLabel() const
    {
        if (!computerHasSetup()) return "SET UP CONTROLLER";
        return ctrlStatus() == 1 ? "CONTROLLER OK" : "NO MIDI YET";
    }
    Color ctrlDot() const
    {
        switch (ctrlStatus()) {
        case 1: return kOkGreen;
        case 2: return kAmberAcc;
        default: return computerHasSetup() ? fT.muted : kAmberAcc;
        }
    }
    float ctrlPillX() { return kNotesX + notesPillW() + 38.f; }
    float ctrlPillW() { return std::round(measure(fFontBold, 9.5f, ctrlLabel(), 0.3f) + 34.f); }
    float gearCX() { return ctrlPillX() + ctrlPillW() + 18.f; }

    void drawGear(float cx, float cy, const Color& c, bool open)
    {
        if (open) { beginPath(); circle(cx, cy, kGearR + 4.f); fillColor(fT.text); fill(); }
        const Color ink = open ? fT.bg : c;
        save();
        translate(cx, cy);
        for (int i = 0; i < 8; ++i) {
            save(); rotate(i * 3.14159265f / 4.f);
            beginPath(); roundedRect(-2.2f, -kGearR, 4.4f, 4.f, 1.f); fillColor(ink); fill();
            restore();
        }
        beginPath(); circle(0.f, 0.f, kGearR - 2.6f); strokeColor(ink); strokeWidth(2.6f); stroke();
        beginPath(); circle(0.f, 0.f, 2.2f); fillColor(ink); fill();
        restore();
    }

    // undo / redo: a curved arrow
    void drawUndoIcon(float cx, float cy, bool redo, bool enabled, float press)
    {
        const Color ink = enabled ? fT.text : withAlpha(fT.text, 0.3f);
        save();
        translate(cx, cy); scale(press, press);
        if (redo) scale(-1.f, 1.f);
        beginPath(); circle(0.f, 0.f, kUndoR);
        fillColor(fT.pillFill); fill(); strokeColor(enabled ? fT.outline : withAlpha(fT.outline, 0.3f)); strokeWidth(2.5f); stroke();
        beginPath(); arc(0.5f, 1.5f, 6.f, -2.6f, 1.2f, NanoVG::CW);
        strokeColor(ink); strokeWidth(2.4f); lineCap(NanoVG::ROUND); stroke();
        beginPath(); moveTo(-8.f, -4.5f); lineTo(-5.2f, -0.6f); lineTo(-1.6f, -3.9f);
        lineJoin(NanoVG::ROUND); stroke();
        restore();
    }

    void drawHeader()
    {
        const float tw = measure(fFontTitle, kTitleEm, "Dewitt", 0.f);
        drawText(fFontTitle, kTitleEm, fT.text, 32.f, kTitleCY, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, "Dewitt", 0.f);
        drawDonut(32.f + tw + 12.f + 21.f, kTitleCY);

        // NOTES pill + moon
        {
            char lb[64]; notesLabel(lb, sizeof(lb));
            const float w = notesPillW();
            const float on = clamp01(fNotesT.x);
            const bool open = fMenuOpen && fMenuKind == kMenuNotes;
            beginPath(); roundedRect(kNotesX, kNotesY, w, kNotesH, kNotesH * 0.5f);
            fillColor(withAlpha(fDark ? fT.pillFill : fT.text, std::max(on, open ? 1.f : 0.f))); fill();
            strokeColor(on > 0.5f ? (fDark ? fT.outline : fT.text) : fT.muted); strokeWidth(1.5f); stroke();
            const Color tc = on > 0.5f ? (fDark ? fT.text : fT.bg) : fT.muted;
            drawText(fFontBold, 9.5f, tc, kNotesX + w * 0.5f, kNotesY + kNotesH * 0.5f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, lb, 0.3f);
            drawMoon(kNotesX + w + 15.f, kNotesY + kNotesH * 0.5f);
        }
        // controller pill + settings gear
        {
            const float x = ctrlPillX(), w = ctrlPillW();
            beginPath(); roundedRect(x, kNotesY, w, kNotesH, kNotesH * 0.5f);
            fillColor(withAlpha(fT.text, fPanelOpen ? 1.f : 0.f)); fill();
            strokeColor(fPanelOpen ? fT.text : fT.muted); strokeWidth(1.5f); stroke();
            const float blink = now() - fMidiAt < 0.25 ? 1.6f : 1.f;
            beginPath(); circle(x + 11.f, kNotesY + kNotesH * 0.5f, 3.6f * blink); fillColor(ctrlDot()); fill();
            drawText(fFontBold, 9.5f, fPanelOpen ? fT.bg : fT.muted, x + 20.f, kNotesY + kNotesH * 0.5f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, ctrlLabel(), 0.3f);
            drawGear(gearCX(), kNotesY + kNotesH * 0.5f, fT.muted, fMenuOpen && (fMenuKind == kMenuSettings || fMenuKind == kMenuKnobType));
        }

        // preset bar: < name >  (click the name for the list)
        {
            const bool open = fMenuOpen && fMenuKind == kMenuSets;
            pill(kPresetX, kPresetY, kPresetW, kPresetH, open ? fT.text : fT.pillFill, fT.outline, 2.5f);
            const Color ink = open ? fT.bg : fT.text;
            chevron(kPresetX + 16.f, kPresetY + kPresetH * 0.5f, ink, 0.5f);
            chevron(kPresetX + kPresetW - 16.f, kPresetY + kPresetH * 0.5f, ink, -0.5f);
            const std::string nm = fitText(fFontHeavy, 12.f, std::string(fPresetName) + (fPresetDirty ? " *" : ""), kPresetW - 64.f, 0.2f);
            drawText(fFontHeavy, 12.f, ink, kPresetX + kPresetW * 0.5f, kPresetY + kPresetH * 0.5f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, nm.c_str(), 0.2f);
            caption(kPresetX + kPresetW * 0.5f, "SET", fT.muted);
        }
        drawUndoIcon(kUndoCX, kHdrCY, false, !fUndo.empty(), fUndoPress.x);
        drawUndoIcon(kRedoCX, kHdrCY, true, !fRedo.empty(), fRedoPress.x);
        caption(0.5f * (kUndoCX + kRedoCX), "UNDO \xC2\xB7 REDO", fT.muted);

        // timing RAW | GRID, with the host tempo
        {
            static const char* const lb[2] = { "RAW", "GRID" };
            segSwitch(kTimX, kTimY, kTimW, kTimH, 2, fParam[pTiming] > 0.5f ? 1 : 0, lb, fT.face, fT.outline, fT.sel, fT.selInk, fT.faceInk);
            char cap[40]; std::snprintf(cap, sizeof(cap), "TIMING \xC2\xB7 %.0f BPM", fParam[oBpm] > 1.f ? fParam[oBpm] : 120.f);
            caption(kTimX + kTimW * 0.5f, cap, fT.muted);
        }
        // ALL OFF
        {
            const float ps = 1.f - 0.05f * fAllPress.x;
            save();
            translate(kAllX + kAllW * 0.5f, kAllY + kAllH * 0.5f); scale(ps, ps);
            pill(-kAllW * 0.5f, -kAllH * 0.5f, kAllW, kAllH, fT.pillFill, fT.outline, 3.f);
            drawText(fFontHeavy, 12.f, fT.text, 0.f, 0.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "ALL OFF", 0.3f);
            restore();
            caption(kAllX + kAllW * 0.5f, "PANIC", fT.muted);
        }
        // LIMIT
        {
            const float lt = fLimitT.x;
            pill(kLimX, kLimY, kLimW, kLimH, Color(fDark ? fT.pillFill : fT.text, kAmberAcc, clamp01(lt)), fT.outline, 3.f);
            const float thumbX = kLimX + 3.f + 10.f + (kLimW - 6.f - 20.f) * lt;
            beginPath(); circle(thumbX, kLimY + kLimH * 0.5f, 8.f); fillColor(fT.face); fill(); strokeColor(fT.outline); strokeWidth(2.f); stroke();
            caption(kLimX + kLimW * 0.5f, "LIMIT", fT.text);
        }
        // OUT
        {
            knobShape(kOutCX, kOutCY, fKnobDisp[pOut].x, fT.text, false, kKnobR, kDotR, fPop[pOut].x);
            drawGhost(pOut, kOutCX, kOutCY, kDotR);
            char vb[24], cap[40];
            const float db = fParam[pOut];
            if (db <= -59.9f) std::snprintf(vb, sizeof(vb), "-inf");
            else std::snprintf(vb, sizeof(vb), std::fabs(db) < 0.05f ? "0.0 dB" : "%+.1f dB", db);
            std::snprintf(cap, sizeof(cap), "OUT %s", vb);
            drawText(fPop[pOut].x > 0.3f ? fFontHeavy : fFontBold, 10.f, fT.text, kOutCX, kCapY, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, cap, 0.2f);
        }
    }

    // where a controller knob sits when it hasn't picked the value up yet
    void drawGhost(int param, float cx, float cy, float dotR)
    {
        const double t = now();
        for (int j = 0; j < kNumJobs; ++j) {
            if (fGhost[j] < 0.f || t - fGhostAt[j] > 1.8) continue;
            if (jobTarget(j, fSel, fParam, hasAudio(fSel)) != param) continue;
            const float a = (float)std::min(1.0, (1.8 - (t - fGhostAt[j])) / 0.5);
            const float ang = (-135.f + 270.f * clamp01(fGhost[j])) * 3.14159265f / 180.f;
            beginPath(); circle(cx + dotR * std::sin(ang), cy - dotR * std::cos(ang), 4.5f);
            strokeColor(withAlpha(kAmberAcc, a)); strokeWidth(2.5f); stroke();
        }
    }

    // ------------------------------------------------------------------------------ pads

    float pageTabX(int pg) const { return kTrayX + pg * 52.f; }

    void drawPadSection()
    {
        const int np = pagesShown();
        const float mx = fTabX.x;                             // the marker, sliding between the tabs
        beginPath(); roundedRect(mx, kTabY, 44.f, kTabH, kTabH * 0.5f);
        fillColor(fDark ? fT.pillFill : fT.text); fill();
        for (int pg = 0; pg < np; ++pg) {
            const float x = pageTabX(pg), w = 44.f;
            const float on = clamp01(1.f - std::fabs(mx - x) / w);   // how much of this tab the marker covers
            beginPath(); roundedRect(x, kTabY, w, kTabH, kTabH * 0.5f);
            strokeColor(fDark ? Color(fT.frame, fT.outline, on) : fT.text); strokeWidth(2.5f); stroke();
            if (fLearning == kLearnBank0 + pg) {                 // learning a button on the controller for this bank
                const float blink = 0.5f + 0.5f * std::sin((float)now() * 12.566f);
                beginPath(); roundedRect(x - 3.f, kTabY - 3.f, w + 6.f, kTabH + 6.f, kTabH * 0.5f + 3.f);
                strokeColor(withAlpha(kAmberAcc, blink)); strokeWidth(3.f); stroke();
            }
            char lb[4] = { (char)('A' + pg), 0, 0, 0 };
            drawText(fFontHeavy, 12.f, fDark ? fT.text : Color(fT.text, fT.bg, on), x + w * 0.5f, kTabY + kTabH * 0.5f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, lb, 0.3f);
        }
        if (false) {
            const float x = pageTabX(np);
            beginPath(); roundedRect(x, kTabY + 2.f, 70.f, kTabH - 4.f, (kTabH - 4.f) * 0.5f);
            strokeColor(fT.muted); strokeWidth(1.5f); stroke();
            drawText(fFontBold, 9.5f, fT.muted, x + 35.f, kTabY + kTabH * 0.5f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "+ PAGE", 0.3f);
        }
        drawText(fFontMed, 10.5f, fT.muted, kTrayX + kTrayW, kTabY + kTabH * 0.5f, NanoVG::ALIGN_RIGHT | NanoVG::ALIGN_MIDDLE, "Drag to reorder, drop a sample on a pad", 0.f);

        beginPath(); roundedRect(kTrayX, kTrayY, kTrayW, kTrayH, 24.f);
        fillColor(fT.tray); fill();
        strokeColor(fT.trayEdge); strokeWidth(1.5f); stroke();

        const float slide = fPageSlide.x;
        save();
        if (std::fabs(slide) > 0.001f) {
            scissor(kTrayX, kTrayY, kTrayW, kTrayH);
            translate(slide * 36.f, 0.f);
            globalAlpha(clamp01(1.f - 0.8f * std::fabs(slide)));
        }
        for (int pass = 0; pass < 2; ++pass)
            for (int q = 0; q < kPadsPerPage; ++q) {
                const int s = slotOfPad(fPage * kPadsPerPage + fPosSlot[q]);
                const bool top = (q == fDragPos && fPadDragging);
                if ((pass == 0) == top) continue;
                if (s >= 0) drawPad(s, fPadPX[q].x, fPadPY[q].x, posDisplay(q));
            }
        restore();
    }

    // a blank pad on bank B or C: waiting for an effect, sample or beat
    void emptyPad(float x, float y, int q)
    {
        save(); translate(x, y);
        beginPath(); roundedRect(1.5f, 1.5f, kPadW - 3.f, kPadH - 3.f, 14.5f);
        fillColor(withAlpha(fT.text, 0.03f)); fill();
        // dashed outline
        const float w = kPadW - 3.f, h = kPadH - 3.f;
        for (float d = 14.f; d < w - 14.f; d += 10.f) { beginPath(); moveTo(1.5f + d, 1.5f); lineTo(1.5f + d + 5.f, 1.5f); moveTo(1.5f + d, 1.5f + h); lineTo(1.5f + d + 5.f, 1.5f + h); strokeColor(withAlpha(fT.text, 0.35f)); strokeWidth(2.f); stroke(); }
        for (float d = 14.f; d < h - 14.f; d += 10.f) { beginPath(); moveTo(1.5f, 1.5f + d); lineTo(1.5f, 1.5f + d + 5.f); moveTo(1.5f + w, 1.5f + d); lineTo(1.5f + w, 1.5f + d + 5.f); stroke(); }
        char num[12]; std::snprintf(num, sizeof(num), "%02d", q + 1);
        drawText(fFontHeavy, 11.f, withAlpha(fT.text, 0.45f), 12.f, 19.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, num);
        // the +: under the mouse it lights up, and a click opens the pad menu right here
        const float hov = q >= 0 && q < kPadsPerPage ? clamp01(fPlusA[q].x) : 0.f;
        if (hov > 0.003f) { beginPath(); circle(kPadW * 0.5f, kPadH * 0.5f + 1.f, (kPlusR - 3.f) * (0.7f + 0.3f * hov)); fillColor(withAlpha(fT.text, 0.1f * hov)); fill(); }
        drawText(fFontHeavy, 22.f, withAlpha(fT.text, 0.35f + 0.5f * hov), kPadW * 0.5f, kPadH * 0.5f + 2.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "+");
        drawText(fFontBold, 8.5f, withAlpha(fT.text, 0.4f), kPadW * 0.5f, kPadH - 16.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "EMPTY", 0.4f);
        restore();
    }
    // the number shown on the pad at drag-position q (follows the slot while dragging)
    int posDisplay(int q) const { for (int i = 0; i < kPadsPerPage; ++i) if (fPosSlot[i] == fPosSlot[q]) return i; return q; }


    void wrap2(const char* name, float maxW, std::string& a, std::string& b)
    {
        a = name; b.clear();
        if (measure(fFontHeavy, 10.5f, name, 0.15f) <= maxW) return;
        const std::string s(name);
        size_t best = std::string::npos;
        for (size_t i = s.find(' '); i != std::string::npos; i = s.find(' ', i + 1))
            if (measure(fFontHeavy, 10.5f, s.substr(0, i).c_str(), 0.15f) <= maxW) best = i;
        if (best == std::string::npos) best = s.find(' ');
        if (best == std::string::npos) return;
        a = s.substr(0, best); b = s.substr(best + 1);
        if (b.size() > 2 && b[0] == '+' && b[1] == ' ') { a += " +"; b = b.substr(2); }
    }

    // colors for what a slot holds: -1 the dark kill tile, -2 a sample tile
    int padCol(int s) const
    {
        if (s < 0) return -1;
        switch (kindOf(s)) {
        case 4: return kColCream;    // an empty pad: a quiet cream card
        case 3: return -3;
        case 2: return -2;
        case 1: return moveColor(moveAt(s));
        default: return effectColor(effectAt(s));
        }
    }
    Color colBg(int col) const { return col == -1 ? fT.killTile : (col == -2 ? fT.tile : (col == -3 ? (fDark ? hex(0x4E3F78) : hex(0x5E4B8B)) : fT.card[col])); }
    Color colInk(int col) const { return col == -1 || col == -3 ? hex(0xF7F1E3) : (col == -2 ? fT.tileInk : fT.ink[col]); }
    Color colAccInk(int col) const { return col < 0 || col == kColNavy || col == kColOlive || col == kColSteel ? hex(0xF7F1E3) : hex(0x0A1A33); }

    // s: slot (-1 = kill); pos: number shown (page position)
    // how much of a pad's effect is playing: the pad's own colour a shade darker (lighter on the darkest pads),
    // filling from the left, with a thin line at its edge
    void pressureShadow(float w, float h, const Color& bg, float amt)
    {
        if (amt <= 0.005f) return;
        const bool darkPad = bg.red + bg.green + bg.blue < 0.9f;
        const Color white(1.f, 1.f, 1.f, 1.f), black(0.f, 0.f, 0.f, 1.f);
        const Color shade = darkPad ? Color(bg, white, 0.14f) : Color(bg, black, 0.15f);
        const Color edge = darkPad ? Color(bg, white, 0.32f) : Color(bg, black, 0.32f);
        const float right = 3.f + (w - 6.f) * clamp01(amt);
        save();
        intersectScissor(0.f, 0.f, right, h);
        beginPath(); roundedRect(3.f, 3.f, w - 6.f, h - 6.f, 13.f);
        fillColor(shade); fill();
        restore();
        if (amt < 0.98f) { beginPath(); moveTo(right, 6.f); lineTo(right, h - 6.f); strokeColor(edge); strokeWidth(1.5f); stroke(); }
    }

    void drawPad(int s, float x, float y, int pos)
    {
        if (s >= 0 && kindOf(s) == 4 && !(fPadDragging && fPosSlot[fDragPos] == s % kPadsPerPage)) {
            const bool sel = fSel == s;
            if (sel) { beginPath(); roundedRect(x - 9.f, y - 9.f, kPadW + 18.f, kPadH + 22.f, 24.f); strokeColor(kAmberAcc); strokeWidth(3.5f); stroke(); }
            emptyPad(x, y, pos);
            return;
        }
        const float w = kPadW, h = kPadH;
        const bool kill = s < 0;
        const int p = kill ? kKillPad : padOfSlot(s);
        const int kind = kill ? -1 : kindOf(s);
        const int col = padCol(s);
        const Color bg = colBg(col), ink = colInk(col);
        const float lift = kill ? 0.f : fLift[s].x;
        const float ring = clamp01(fRing[p].x);
        const bool selected = !kill && fSel == s;
        const bool learning = fLearning == kLearnPad0 + p;
        const bool wizHere = fPanelOpen && fScr == kScrPads && fWizStep == p;

        save();
        translate(x + w * 0.5f, y + h * 0.5f);
        const float sc = fPadScale[p].x * (1.f + 0.04f * lift);
        scale(sc, sc);
        rotate(0.02f * lift * fDragTilt);
        translate(-w * 0.5f, -h * 0.5f);

        // playing: a red glow; selected (the pad your knobs control): a yellow outline
        if (ring > 0.01f) {
            beginPath(); roundedRect(-4.5f, -4.5f, w + 9.f, h + 13.f, 19.f);
            strokeColor(withAlpha(kRedAcc, ring)); strokeWidth(4.f); stroke();
        }
        if (selected) {
            beginPath(); roundedRect(-9.f, -9.f, w + 18.f, h + 22.f, 24.f);
            strokeColor(kAmberAcc); strokeWidth(3.5f); stroke();
        }
        beginPath(); roundedRect(0.f, 4.f + 6.f * lift, w, h, 16.f);
        fillColor(col == -1 ? fT.killShadow : (col == -2 ? fT.tileShadow : (lift > 0.01f ? withAlpha(fT.shadow, fT.shadowA + 0.1f) : fT.outline))); fill();
        beginPath(); roundedRect(1.5f, 1.5f, w - 3.f, h - 3.f, 14.5f);
        fillColor(bg); fill();
        if (learning || wizHere) {
            const float blink = 0.5f + 0.5f * std::sin((float)now() * 12.566f);
            strokeColor(Color(fT.outline, kAmberAcc, blink));
        } else strokeColor(fT.outline);
        strokeWidth(3.f); stroke();
        if (!kill && kind == 0) pressureShadow(w, h, bg, fAmt[s]);

        char num[12]; std::snprintf(num, sizeof(num), "%02d", pos + 1);
        beginPath(); roundedRect(9.f, 9.f, 30.f, 20.f, 10.f);
        fillColor(selected ? kAmberAcc : fT.face); fill();
        strokeColor(fT.outline); strokeWidth(2.5f); stroke();
        drawText(fFontHeavy, 11.f, selected ? hex(0x0A1A33) : fT.faceInk, 24.f, 19.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, num);

        if (kill) {
            drawSkull(w - 26.f, 22.f, hex(0xF7F1E3), bg);
            drawText(fFontHeavy, 12.f, ink, 10.f, 58.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, learning ? "LEARN" : "KILL", 0.4f);
            drawText(fFontMed, 9.f, withAlpha(ink, 0.8f), 10.f, 73.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, "hold to mute all");
            restore();
            return;
        }

        if (kind == 1 && moveAt(s) == kMvKill) drawSkull(w - 46.f, 22.f, hex(0xF7F1E3), bg);
        // the caret: change what's on this pad right here
        {
            const bool open = fMenuOpen && fMenuKind == kMenuType && fMenuTarget == s;
            beginPath(); circle(w - 19.f, 19.f, 10.f);
            fillColor(open ? ink : withAlpha(ink, 0.12f)); fill();
            chevron(w - 19.f, 19.5f, open ? bg : ink);
        }
        // ON tag while it sounds
        if (ring > 0.5f) {           // sits between the number badge and the caret
            const float tw = measure(fFontHeavy, 8.f, "ON", 0.3f) + 10.f;
            pill(43.f, 11.f, tw, 16.f, kRedAcc, fT.outline, 1.5f);
            drawText(fFontHeavy, 8.f, hex(0xF7F1E3), 43.f + tw * 0.5f, 19.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "ON", 0.3f);
        }

        std::string a, b;
        char sub[40];
        if (kind == 3) {
            const BeatInfo& bi = fBeatInfo[s];
            std::string nm = learning ? std::string("LEARN") : bi.name;
            for (char& ch : nm) ch = (char)std::toupper((unsigned char)ch);
            wrap2(nm.c_str(), w - 18.f, a, b);
            a = fitText(fFontHeavy, 10.5f, a, w - 18.f, 0.15f);
            b = fitText(fFontHeavy, 10.5f, b, w - 18.f, 0.15f);
            if (b.empty()) drawText(fFontHeavy, 10.5f, ink, 10.f, 58.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, a.c_str(), 0.15f);
            else {
                drawText(fFontHeavy, 10.5f, ink, 10.f, 47.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, a.c_str(), 0.15f);
                drawText(fFontHeavy, 10.5f, ink, 10.f, 60.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, b.c_str(), 0.15f);
            }
            const float ph = fParam[oSmpPos0 + s];
            if (fBeatPlaying == s && ph >= 0.f) {
                beginPath(); roundedRect(10.f, 72.f, w - 20.f, 4.f, 2.f); fillColor(withAlpha(ink, 0.25f)); fill();
                beginPath(); roundedRect(10.f, 72.f, std::max(4.f, (w - 20.f) * clamp01(ph)), 4.f, 2.f); fillColor(kAmberAcc); fill();
            } else {
                char sub2[40];
                if (fBeatMissing[s]) std::snprintf(sub2, sizeof(sub2), "MISSING");
                else if (!beatReady(s)) std::snprintf(sub2, sizeof(sub2), "LOADING...");
                else std::snprintf(sub2, sizeof(sub2), "BEAT \xC2\xB7 %.1f BPM", bi.bpm);
                drawText(fFontBold, 8.5f, fBeatMissing[s] ? kAmberAcc : withAlpha(ink, 0.85f), 10.f, 75.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, sub2, 0.3f);
            }
            restore();
            return;
        }
        if (kind == 2) {
            if (fSmpErrorUntil[s] > 0.0) {
                drawText(fFontHeavy, 10.f, ink, 10.f, 50.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, "COULDN'T LOAD", 0.15f);
                const std::string e = fitText(fFontMed, 8.5f, fSmpError[s], w - 18.f, 0.f);
                drawText(fFontMed, 8.5f, fT.tileSub, 10.f, 66.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, e.c_str());
                restore();
                return;
            }
            std::string nm = learning ? std::string("LEARN") : fSmpName[s];
            for (char& ch : nm) ch = (char)std::toupper((unsigned char)ch);
            wrap2(nm.c_str(), w - 18.f, a, b);
            a = fitText(fFontHeavy, 10.5f, a, w - 18.f, 0.15f);
            b = fitText(fFontHeavy, 10.5f, b, w - 18.f, 0.15f);
            std::snprintf(sub, sizeof(sub), "ONE-SHOT \xC2\xB7 %.1f S", fSmpDur[s]);
        } else {
            const int m = kind == 1 ? moveAt(s) : -1;
            wrap2(learning ? "LEARN" : (kind == 0 ? kEffects[effectAt(s)].name : kMoveNames[m]), w - 18.f, a, b);
            const bool latch = latchOf(fParam, s);
            if (kind == 0) {
                const std::string v = padVariant(s);
                if (!v.empty()) std::snprintf(sub, sizeof(sub), "%s%s", latch ? "LATCH \xC2\xB7 " : "", v.c_str());
                else std::snprintf(sub, sizeof(sub), "%s%s", latch ? "LATCH" : "HOLD",
                                   kEffects[effectAt(s)].tail && fParam[slotParam(s, F_REL)] > 0.5f ? " \xC2\xB7 RING" : "");
            }
            else std::snprintf(sub, sizeof(sub), "MOVE \xC2\xB7 %s", !moveIsHold(m) ? "TAP" : (latch ? "LATCH" : "HOLD"));
        }
        if (b.empty()) drawText(fFontHeavy, 10.5f, ink, 10.f, 58.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, a.c_str(), 0.15f);
        else {
            drawText(fFontHeavy, 10.5f, ink, 10.f, 47.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, a.c_str(), 0.15f);
            drawText(fFontHeavy, 10.5f, ink, 10.f, 60.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, b.c_str(), 0.15f);
        }
        // progress bar for a playing sample, a looping effect or a grab
        float ph = -1.f;
        if (kind == 2) ph = fParam[oSmpPos0 + s];
        else if (kind == 0 && s == fSel) ph = fParam[oFocusPhase];
        else if (kind == 1 && moveAt(s) >= kMvGrab8 && moveAt(s) <= kMvGrab1 && ring > 0.5f) ph = fParam[oGrabPhase];
        if (kind == 2 && ph >= 0.f) {
            beginPath(); roundedRect(10.f, 72.f, w - 20.f, 4.f, 2.f); fillColor(withAlpha(ink, 0.18f)); fill();
            beginPath(); roundedRect(10.f, 72.f, std::max(4.f, (w - 20.f) * clamp01(ph)), 4.f, 2.f); fillColor(ink); fill();
        } else {
            drawText(fFontBold, 8.5f, kind == 2 ? fT.tileSub : withAlpha(ink, 0.85f), 10.f, 75.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE,
                     fitText(fFontBold, 8.5f, sub, w - 18.f, 0.3f).c_str(), 0.3f);
            if (ph >= 0.f) {
                beginPath(); roundedRect(10.f, h - 8.f, w - 20.f, 3.f, 1.5f); fillColor(withAlpha(ink, 0.2f)); fill();
                beginPath(); roundedRect(10.f, h - 8.f, std::max(3.f, (w - 20.f) * clamp01(ph)), 3.f, 1.5f); fillColor(ink); fill();
            }
        }
        restore();
    }

    // ------------------------------------------------------------------------------ detail card

    struct KnobRef { int param; const char* label; int kind; };   // kind: 0 slot knob, 1 pct, 2 throw time, 3 freeze ms, 4 freeze speed, 5 volume

    int moveKnobs(int m, KnobRef* out) const
    {
        int n = 0;
        switch (m) {
        case kMvDryKill: out[n++] = { pTail, "TAIL", 1 }; break;
        case kMvThrow: out[n++] = { pThrowTime, "TIME", 2 }; out[n++] = { pThrowFb, "FEEDBACK", 1 }; break;
        case kMvFreeze: out[n++] = { pFreezeLen, "LENGTH", 3 }; out[n++] = { pFreezeSpeed, "SPEED", 4 }; break;
        default: break;
        }
        return n;
    }

    void formatGlobal(const KnobRef& k, char* buf, size_t sz) const
    {
        const float v = fParam[k.param];
        switch (k.kind) {
        case 2: std::snprintf(buf, sz, "%s", kDelayTimeNames[(int)clampf(std::round(v), 0, 12)]); break;
        case 3: std::snprintf(buf, sz, "%d ms", (int)std::lround(freezeMs(v))); break;
        case 4: { const float sp = freezeSpeed(v); std::snprintf(buf, sz, "%+.2f", std::fabs(sp) < 0.005f ? 0.f : sp); break; }
        case 5: formatVol(v, buf, sz); break;
        default: std::snprintf(buf, sz, "%d%%", (int)std::lround(v * 100.f)); break;
        }
    }

    void formatVol(float v, char* buf, size_t sz) const
    {
        const float g = padVolGain(v);
        if (g < 0.0005f) std::snprintf(buf, sz, "-inf");
        else { const float db = 20.f * std::log10(g); std::snprintf(buf, sz, std::fabs(db) < 0.05f ? "0.0 dB" : "%+.1f dB", db); }
    }

    static float spreadX(int n, int i, float w, float gap) { return (w - (n - 1) * gap) * 0.5f + i * gap; }

    // a knob with its controller number: "1 CUTOFF"
    void bigKnob(float cx, float cy, int param, int knobNo, const char* label, const char* value, const Color& ink, bool bipolar, float r, float dotR)
    {
        knobShape(cx, cy, fKnobDisp[param].x, ink, bipolar, r, dotR, fPop[param].x);
        drawGhost(param, cx, cy, dotR);
        const float ly = cy + dotR + 15.f, em = r > 20.f ? 12.f : 11.f;
        const float lw = measure(fFontBold, em, label, 0.2f);
        if (knobNo > 0) {
            const float bx = cx - lw * 0.5f - 11.f;
            beginPath(); circle(bx, ly, 7.f); fillColor(ink); fill();
            char n[4]; std::snprintf(n, sizeof(n), "%d", knobNo);
            drawText(fFontHeavy, 8.5f, invertInk(ink), bx, ly, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, n);
        }
        drawText(fFontBold, em, ink, cx + (knobNo > 0 ? 5.f : 0.f), ly, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, label, 0.2f);
        drawText(fPop[param].x > 0.3f ? fFontBold : fFontMed, em, ink, cx, ly + 16.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, value);
    }
    static Color invertInk(const Color& ink) { return (ink.red + ink.green + ink.blue) > 1.5f ? hex(0x0A1A33) : hex(0xF7F1E3); }

    // the eight controller knobs as two rows of four, laid out like the controller
    void foldTag(float cx, float cy, const char* text, bool filled, const Color& ink, bool leftAlign = false)
    {
        const float tw = measure(fFontHeavy, 8.5f, text, 0.3f) + 18.f, h = 18.f;
        const float x = leftAlign ? cx : cx - tw * 0.5f;
        beginPath(); roundedRect(x, cy - h * 0.5f, tw, h, h * 0.5f);
        if (filled) { fillColor(kAmberAcc); fill(); }
        strokeColor(filled ? fT.outline : ink); strokeWidth(1.8f); stroke();
        drawText(fFontHeavy, 8.5f, filled ? hex(0x0A1A33) : ink, x + tw * 0.5f, cy + 0.5f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, text, 0.3f);
    }

    void knobGrid(int s, const Color& ink, const Color& bg)
    {
        const bool fold = folding(s);
        const int fe = fold ? effectAt(s) : -1, fn = knobsNow();
        const float* cxs = kGridX;
        for (int k = 0; k < kNumJobs; ++k) {
            int row = k / 4; if (fKnobGridBottom) row = 1 - row;
            const float cx = cxs[k % 4], cy = row == 0 ? kGridY0 : kGridY1;
            const int t = jobTarget(k, s, fParam, hasAudio(s));
            if (t < 0) continue;     // only the knobs an effect actually has
            if (t < 0) {
                for (int d = 0; d < 11; ++d) {
                    const float a = (-135.f + 27.f * d) * 3.14159265f / 180.f;
                    beginPath(); circle(cx + 33.f * std::sin(a), cy - 33.f * std::cos(a), 1.6f); fillColor(withAlpha(ink, 0.18f)); fill();
                }
                beginPath(); circle(cx, cy, 24.f); strokeColor(withAlpha(ink, 0.25f)); strokeWidth(2.f); stroke();
                const char* lb = "-";
                const float lw = measure(fFontBold, 11.f, lb, 0.2f), bx = cx - lw * 0.5f - 11.f, ly = cy + 33.f + 15.f;
                beginPath(); circle(bx, ly, 7.f); fillColor(withAlpha(ink, 0.3f)); fill();
                char n[4]; std::snprintf(n, sizeof(n), "%d", k + 1);
                drawText(fFontHeavy, 8.5f, invertInk(ink), bx, ly, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, n);
                drawText(fFontBold, 11.f, withAlpha(ink, 0.35f), cx + 5.f, ly, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, lb, 0.2f);
                continue;
            }
            char vb[24] = "-"; const char* lb = "OUT"; bool bip = false;
            if (t == pOut) { const float db = fParam[pOut]; if (db <= -59.9f) std::snprintf(vb, sizeof(vb), "-inf"); else std::snprintf(vb, sizeof(vb), std::fabs(db) < 0.05f ? "0.0 dB" : "%+.1f dB", db); }
            else if (isSlotKnob(t) && effectAt(slotOf(t)) >= 0) { const int e = effectAt(slotOf(t)), c = fieldOf(t) - F_K1; lb = kEffects[e].c[c].label; bip = ctlBipolar(e, c); formatFmt(kEffects[e].c[c].fmt, ctlFromNorm(kEffects[e].c[c], fParam[t]), vb, sizeof(vb)); }
            else if (isSlotParam(t)) { lb = "VOLUME"; formatVol(fParam[t], vb, sizeof(vb)); }
            else { KnobRef ks[3]; const int n = moveKnobs(moveAt(s), ks); for (int i = 0; i < n; ++i) if (ks[i].param == t) { lb = ks[i].label; bip = ks[i].kind == 4; formatGlobal(ks[i], vb, sizeof(vb)); } }
            if (!fold || k < fn) { bigKnob(cx, cy, t, k + 1, lb, vb, ink, bip, 24.f, 33.f); }
            else {
                // no knob of its own on this controller: dimmed, no number, and what moves it instead
                bigKnob(cx, cy, t, 0, lb, vb, ink, bip, 24.f, 33.f);
                beginPath(); rect(cx - 62.f, cy - 46.f, 124.f, 112.f); fillColor(withAlpha(bg, 0.58f)); fill();
            }
            if (!fold) continue;
            const bool lowerRow = cy >= std::max(kGridY0, kGridY1) - 1.f;
            const float tagY = cy + 33.f + 15.f + 38.f;
            char tb[40];
            if (k >= fn) {
                const Fold f = foldResolve(fe, k, fn);
                if (f.host >= 0) std::snprintf(tb, sizeof(tb), "FOLLOWS %s", kEffects[fe].c[f.host].label);
                else std::snprintf(tb, sizeof(tb), "FIXED");
                if (lowerRow) foldTag(cx + 48.f, cy + 33.f + 15.f, tb, false, ink, true);
                else foldTag(cx, tagY, tb, false, ink);
            } else {
                // a knob that carries others: "+ WOW + TONE"
                std::string carried;
                for (int c = fn; c < numControls(fe); ++c)
                    if (foldResolve(fe, c, fn).host == k) { carried += "+ "; carried += kEffects[fe].c[c].label; carried += " "; }
                if (!carried.empty()) {
                    carried.pop_back();
                    if (lowerRow) foldTag(cx + 48.f, cy + 33.f + 15.f, carried.c_str(), true, ink, true);
                    else foldTag(cx, tagY, carried.c_str(), true, ink);
                }
            }
        }
    }

    void drawDetail()
    {
        const int s = fSel;
        const int kind = kindOf(s);
        const int e = kind == 0 ? effectAt(s) : -1;
        const int m = kind == 1 ? moveAt(s) : -1;
        const int col = padCol(s);
        const Color bg = colBg(col), ink = colInk(col);
        const bool darkCard = col == -1 || col == kColNavy;
        const char* name = kind == 4 ? "EMPTY" : (kind == 3 ? "BEAT" : (kind == 2 ? "SAMPLE" : (kind == 1 ? kMoveNames[m] : kEffects[e].name)));

        save();
        translate(kDetX, kDetY + 4.f * (1.f - fCardA.x));
        if (fCardA.x < 0.999f) globalAlpha(0.6f + 0.4f * clamp01(fCardA.x));
        beginPath(); roundedRect(0.f, 5.f, kDetW, kDetH, 20.f); fillColor(withAlpha(fT.shadow, fT.shadowA)); fill();
        beginPath(); roundedRect(1.5f, 1.5f, kDetW - 3.f, kDetH - 3.f, 18.5f); fillColor(bg); fill();
        strokeColor(fT.outline); strokeWidth(3.f); stroke();

        char num[12]; std::snprintf(num, sizeof(num), "%c%d", 'A' + pageOfSlot(s), posOfSlot(s) + 1);
        beginPath(); roundedRect(18.f, 17.f, 44.f, 26.f, 13.f); fillColor(kAmberAcc); fill();
        strokeColor(fT.outline); strokeWidth(2.5f); stroke();
        drawText(fFontHeavy, 12.f, hex(0x0A1A33), 40.f, 30.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, num);
        {
            const bool open = fMenuOpen && fMenuKind == kMenuType && fMenuTarget == s && fMenuFromDetail;
            const float tw = measure(fFontHeavy, 12.f, name, 0.15f);
            const float cw = std::round(tw + 38.f);
            beginPath(); roundedRect(70.f, 17.f, cw, 26.f, 13.f);
            fillColor(open ? ink : withAlpha(ink, 0.f)); fill();
            strokeColor(ink); strokeWidth(2.f); stroke();
            const Color ci = open ? bg : ink;
            drawText(fFontHeavy, 12.f, ci, 83.f, 30.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, name, 0.15f);
            chevron(70.f + cw - 14.f, 30.f, ci);
            fTypeChipW = cw;
            if (kind == 0 && fAmt[s] > 0.005f) {
                const bool dk = bg.red + bg.green + bg.blue < 0.9f;
                const Color white(1.f, 1.f, 1.f, 1.f), black(0.f, 0.f, 0.f, 1.f);
                beginPath(); roundedRect(70.f, 50.f, cw, 4.f, 2.f); fillColor(dk ? Color(bg, white, 0.12f) : Color(bg, black, 0.10f)); fill();
                beginPath(); roundedRect(70.f, 50.f, std::max(4.f, cw * clamp01(fAmt[s])), 4.f, 2.f); fillColor(dk ? Color(bg, white, 0.35f) : Color(bg, black, 0.30f)); fill();
            }
        }
        {
            char tagBuf[24];
            const int kn = knobsNow();
            if (kn < kNumJobs) std::snprintf(tagBuf, sizeof(tagBuf), kn == 1 ? "YOUR KNOB" : "YOUR %d KNOBS", kn);
            else std::snprintf(tagBuf, sizeof(tagBuf), "YOUR KNOBS");
            const char* tag = tagBuf;
            const float tw = measure(fFontHeavy, 10.f, tag, 0.35f) + 22.f;
            pill(kDetW - 18.f - tw, 19.f, tw, 22.f, kAmberAcc, fT.outline, 2.f);
            drawText(fFontHeavy, 10.f, hex(0x0A1A33), kDetW - 18.f - tw * 0.5f, 30.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, tag, 0.35f);
        }

        const Color base = darkCard ? bg : fT.face, edge = darkCard ? ink : fT.outline;
        const Color sel = darkCard ? ink : fT.sel, selInk = darkCard ? bg : fT.selInk, offInk = darkCard ? ink : fT.faceInk;

        knobGrid(s, ink, bg);
        if (kind == 4) {
            drawText(fFontHeavy, 22.f, ink, kDetW * 0.5f, 170.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "Empty pad", 0.1f);
            drawText(fFontMed, 13.f, withAlpha(ink, 0.8f), kDetW * 0.5f, 204.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "Put an effect or a move on it, or drop a sample or a beat here.");
            panelPill(kEmptyBtnX[0], kEmptyBtnY, 150.f, 34.f, "ADD EFFECT", fT.face, fT.outline, fT.faceInk);
            panelPill(kEmptyBtnX[1], kEmptyBtnY, 150.f, 34.f, "LOAD SAMPLE", fT.face, fT.outline, fT.faceInk);
            panelPill(kEmptyBtnX[2], kEmptyBtnY, 150.f, 34.f, "LOAD BEAT", fT.face, fT.outline, fT.faceInk);
            restore();
            return;
        }
        if (kind == 0) {
            const EffectSpec& es = kEffects[e];
            drawParagraph(kEffectDesc[e], kDetW * 0.5f, 352.f, 470.f, 12.f, ink, fFontMed);
            static const char* const modes[2] = { "HOLD", "LATCH" };
            drawText(fFontHeavy, 10.f, ink, 22.f, kSegY - 11.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, "PAD MODE", 0.35f);
            segSwitch(20.f, kSegY, kSegW, kSegH, 2, latchOf(fParam, s) ? 1 : 0, modes, base, edge, sel, selInk, offInk);
            if (es.tail) {
                static const char* const rel[2] = { "CUT", "RING OUT" };
                drawText(fFontHeavy, 10.f, ink, 178.f, kSegY - 11.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, "ON RELEASE", 0.35f);
                segSwitch(176.f, kSegY, 150.f, kSegH, 2, fParam[slotParam(s, F_REL)] > 0.5f ? 1 : 0, rel, base, edge, sel, selInk, offInk, 10.f);
            } else if (e == kFxDjLooper) {
                static const char* const sn[2] = { "OFF", "ON" };
                drawText(fFontHeavy, 10.f, ink, 178.f, kSegY - 11.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, "SNAP BACK", 0.35f);
                segSwitch(176.f, kSegY, 110.f, kSegH, 2, fParam[slotParam(s, F_SNAP)] > 0.5f ? 1 : 0, sn, base, edge, sel, selInk, offInk);
            }
        } else if (kind == 1) {
            drawParagraph(kMoveDesc[m], kDetW * 0.5f, 352.f, 470.f, 12.f, ink, fFontMed);
            if (moveIsHold(m)) {
                static const char* const modes[2] = { "HOLD", "LATCH" };
                drawText(fFontHeavy, 10.f, ink, 22.f, kSegY - 11.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, "PAD MODE", 0.35f);
                segSwitch(20.f, kSegY, kSegW, kSegH, 2, latchOf(fParam, s) ? 1 : 0, modes, base, edge, sel, selInk, offInk);
            } else {
                drawText(fFontHeavy, 10.f, ink, 22.f, kSegY - 11.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, "PLAYS", 0.35f);
                pill(20.f, kSegY, 96.f, kSegH, base, edge, 2.5f);
                drawText(fFontHeavy, 11.f, offInk, 68.f, kSegY + kSegH * 0.5f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "ON TAP", 0.3f);
            }
        } else if (kind == 3) {
            const BeatInfo& bi = fBeatInfo[s];
            char d[200];
            const int mins = (int)(bi.len / 60.f), secs = (int)bi.len % 60;
            if (fBeatMissing[s]) std::snprintf(d, sizeof(d), "Can't find %s. Click FIND FILE to show where it is.", bi.name.c_str());
            else std::snprintf(d, sizeof(d), "%s \xC2\xB7 %d:%02d \xC2\xB7 plays through the effects", bi.name.c_str(), mins, secs);
            const std::string dd = fitText(fFontMed, 12.f, d, 490.f, 0.f);
            drawText(fFontMed, 12.f, ink, kDetW * 0.5f, 324.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, dd.c_str());
            // tempo: click to type it; half / double fix a detection that landed on 80 instead of 160
            char bp[24]; std::snprintf(bp, sizeof(bp), "%.1f BPM", bi.bpm);
            panelPill(20.f, kBeatRowY, 112.f, 26.f, bp, base, edge, offInk);
            panelPill(138.f, kBeatRowY, 44.f, 26.f, "1/2x", base, edge, offInk);
            panelPill(188.f, kBeatRowY, 44.f, 26.f, "2x", base, edge, offInk);
            static const char* const hm[3] = { "INSTANT", "NEXT BAR", "XFADE" };
            drawText(fFontHeavy, 9.f, withAlpha(ink, 0.8f), 244.f, kBeatRowY - 9.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, "NEXT BEAT STARTS", 0.35f);
            segSwitch(242.f, kBeatRowY, 188.f, 26.f, 3, fBeatMode, hm, base, edge, sel, selInk, offInk, 9.f);
            panelPill(20.f, kSegY, 120.f, kSegH, fBeatMissing[s] ? "FIND FILE" : "REPLACE", base, edge, offInk);
            panelPill(150.f, kSegY, 110.f, kSegH, "REMOVE", base, edge, offInk);
        } else {
            char d[160]; std::snprintf(d, sizeof(d), "%s \xC2\xB7 one-shot, %.1f seconds. Plays clean on top of everything; hit it again to restart it.", fSmpName[s].c_str(), fSmpDur[s]);
            drawParagraph(d, kDetW * 0.5f, 352.f, 470.f, 12.f, ink, fFontMed);
            panelPill(20.f, kSegY, 120.f, kSegH, "REPLACE", base, edge, offInk);
            panelPill(150.f, kSegY, 110.f, kSegH, "REMOVE", base, edge, offInk);
        }

        // round pad: play this pad from the mouse
        {
            const bool active = padActive(s);
            const float ps = fPadScale[padOfSlot(s)].x;
            const float r = kRoundR * ps, press = 1.f - ps;
            beginPath(); circle(kRoundCX, kRoundCY + 4.f - press * 40.f, r); fillColor(fT.outline); fill();
            const float cy = kRoundCY + press * 40.f;
            beginPath(); circle(kRoundCX, cy, r); fillColor(active ? kRedAcc : fT.face); fill();
            strokeColor(fT.outline); strokeWidth(3.f); stroke();
            drawText(fFontHeavy, 14.f, active ? hex(0xF7F1E3) : fT.faceInk, kRoundCX, cy, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, active ? (kind == 3 ? "STOP" : "ON") : "PLAY", 0.3f);
        }
        restore();
    }

    void panelPill(float x, float y, float w, float h, const char* label, const Color& fillC, const Color& edge, const Color& ink)
    {
        pill(x, y, w, h, fillC, edge, 2.5f);
        drawText(fFontHeavy, 11.f, ink, x + w * 0.5f, y + h * 0.5f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, label, 0.3f);
    }

    void drawParagraph(const char* str, float cx, float cy, float maxW, float em, const Color& c, FontId f)
    {
        std::string lines[4]; int n = 0;
        std::string cur, word;
        const std::string s(str);
        size_t i = 0;
        while (i <= s.size() && n < 4) {
            const size_t j = s.find(' ', i);
            word = s.substr(i, j == std::string::npos ? std::string::npos : j - i);
            const std::string trial = cur.empty() ? word : cur + " " + word;
            if (!cur.empty() && measure(f, em, trial.c_str(), 0.f) > maxW) { lines[n++] = cur; cur = word; }
            else cur = trial;
            if (j == std::string::npos) break;
            i = j + 1;
        }
        if (!cur.empty() && n < 4) lines[n++] = cur;
        const float lh = em * 1.55f;
        const float y0 = cy - (n - 1) * lh * 0.5f;
        for (int k = 0; k < n; ++k) drawText(f, em, c, cx, y0 + k * lh, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, lines[k].c_str());
    }

    // ------------------------------------------------------------------------------ footer

    const char* jobLabel(int j) const
    {
        const int t = jobTarget(j, fSel, fParam, hasAudio(fSel));
        if (t < 0) return "-";
        if (isSlotKnob(t) && effectAt(slotOf(t)) >= 0) return kEffects[effectAt(slotOf(t))].c[fieldOf(t) - F_K1].label;
        if (isSlotParam(t) && fieldOf(t) == F_VOL) return "VOLUME";
        switch (t) {
        case pTail: return "TAIL"; case pThrowTime: return "TIME"; case pThrowFb: return "FEEDBACK";
        case pFreezeLen: return "LENGTH"; case pFreezeSpeed: return "SPEED";
        }
        return "-";
    }

    void drawFooter()
    {
        const int kn = knobsNow();
        char head[24];
        if (kn < kNumJobs) std::snprintf(head, sizeof(head), kn == 1 ? "YOUR KNOB" : "YOUR %d KNOBS", kn);
        else std::snprintf(head, sizeof(head), "YOUR KNOBS");
        drawText(fFontHeavy, 10.f, fT.muted, 32.f, kFootCY, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, head, 0.3f);
        float x = 32.f + measure(fFontHeavy, 10.f, head, 0.3f) + 12.f;
        const bool fold = folding(fSel);
        const int fe = fold ? effectAt(fSel) : -1;
        for (int j = 0; j < kNumJobs; ++j) {
            if (fold && j >= kn) break;         // the controls past your knobs follow one of these, or stay put
            const char* lb0 = jobLabel(j);
            if (std::strcmp(lb0, "-") == 0) continue;
            std::string lbs = lb0;
            if (fold) for (int c = kn; c < numControls(fe); ++c) if (foldResolve(fe, c, kn).host == j) { lbs += " + "; lbs += kEffects[fe].c[c].label; }
            const char* lb = lbs.c_str();
            const float tw = measure(fFontHeavy, 9.5f, lb, 0.25f);
            const float w = std::max(tw + 34.f, 52.f), h = 24.f, y = kFootCY - h * 0.5f;
            const bool mapped = fMap[kLearnKnob0 + j] >= 0;
            const bool learning = fLearning == kLearnKnob0 + j;
            const bool live = std::strcmp(lb, "-") != 0;
            const float pop = clamp01(fJobPop[j].x);
            beginPath(); roundedRect(x, y, w, h, h * 0.5f);
            fillColor(withAlpha(kAmberAcc, pop * 0.9f)); fill();
            if (learning) { const float blink = 0.5f + 0.5f * std::sin((float)now() * 12.566f); strokeColor(Color(fT.frame, kAmberAcc, blink)); }
            else strokeColor(mapped && live ? fT.frame : withAlpha(fT.frame, 0.35f));
            strokeWidth(2.f); stroke();
            beginPath(); circle(x + 12.f, kFootCY, 8.f); fillColor(mapped ? fT.text : fT.muted); fill();
            char n[4]; std::snprintf(n, sizeof(n), "%d", j + 1);
            drawText(fFontHeavy, 9.f, fT.bg, x + 12.f, kFootCY, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, n);
            drawText(fFontHeavy, 9.5f, mapped && live ? fT.text : fT.muted, x + 25.f, kFootCY, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, learning ? "LEARN" : lb, 0.25f);
            fChipX[j] = x; fChipW[j] = w;
            x += w + 7.f;
        }
        drawText(fFontMed, 10.f, fT.sig, kW - 32.f, kFootCY, NanoVG::ALIGN_RIGHT | NanoVG::ALIGN_MIDDLE, "By Jehf Slaps", 0.2f);
    }

    // ---------------------------------------------------------------------------------- setup card

    enum { bNone = 0, bClose, bSetupAgain, bClearAll, bDone, bSkip, bBack, bOutPads, bOutKnobs, bMorePads, bNoMorePads,
           bStartPlaying, bUseBackup, bFresh, bMyself, bCancel, bConfirm, bSaveName, bWinDialog,
           bPadCtl, bKeyboard, bKeysManual, bKeysAuto, bKeysNext, bKeysRepick, bTapAll, bFollowYes };
    struct PanelBtn { float x, y, w, h; int id; const char* label; int style; };   // style 0 plain, 1 amber, 2 red, 3 link, 4 drawn by the screen

    int panelButtons(PanelBtn* out) const
    {
        const float w = kPanW, h = kPanH, by = h - 58.f;
        int n = 0;
        auto add = [&](float x, float y, float bw, float bh, int id, const char* lb, int st) { out[n++] = { x, y, bw, bh, id, lb, st }; };
        const bool firstRun = !computerHasSetup();
        switch (fScr) {
        case kScrHome:
            add(28.f, 196.f, 170.f, 36.f, bSetupAgain, computerHasSetup() ? "SET UP AGAIN" : "START SETUP", 1);
            add(210.f, 196.f, 190.f, 36.f, bClearAll, "CLEAR ALL MAPPINGS", 0);
            add(w - 148.f, by, 120.f, 36.f, bDone, "DONE", 1);
            break;
        case kScrHear:
            if (firstRun) add(28.f, by + 8.f, 150.f, 22.f, bMyself, "I'll map it myself", 3);
            if (fHeardAt > 0.0) add(w - 188.f, by, 160.f, 36.f, bSkip, "MAP THE PADS", 1);
            break;
        case kScrBackup:
            add(w * 0.5f - 210.f, 330.f, 200.f, 44.f, bUseBackup, "USE IT", 1);
            add(w * 0.5f + 10.f, 330.f, 200.f, 44.f, bFresh, "SET UP FRESH", 0);
            add(28.f, by + 8.f, 150.f, 22.f, bMyself, "I'll map it myself", 3);
            break;
        case kScrPads:
            if (fBankQuick) {
                add(28.f, by, 96.f, 34.f, bBack, "BACK", 0);
                add(w * 0.5f - 100.f, by, 200.f, 34.f, bTapAll, "TAP ALL 16 MYSELF", 0);
                add(w - 228.f, by, 200.f, 34.f, bNoMorePads, "DONE WITH PADS", 0);
                break;
            }
            add(28.f, by, 96.f, 34.f, bSkip, "SKIP", 0);
            add(134.f, by, 96.f, 34.f, bBack, "BACK", 0);
            add(w - 228.f, by, 200.f, 34.f, bOutPads, fWizStep < kPadsPerPage ? "THAT'S ALL MY PADS" : "DONE WITH PADS", 0);
            break;
        case kScrChoose:
            add(w * 0.5f - 262.f, 196.f, 250.f, 190.f, bPadCtl, "PAD CONTROLLER", 4);
            add(w * 0.5f + 12.f, 196.f, 250.f, 190.f, bKeyboard, "KEYBOARD", 4);
            if (firstRun) add(28.f, by + 8.f, 150.f, 22.f, bMyself, "I'll map it myself", 3);
            break;
        case kScrKeys:
            if (fKeyManual && fLearning >= 0) {
                add(28.f, by, 96.f, 34.f, bSkip, "SKIP", 0);
                add(134.f, by, 96.f, 34.f, bBack, "BACK", 0);
                add(240.f, by, 210.f, 34.f, bKeysAuto, "FILL IN AUTOMATICALLY", 0);
            } else {
                add(28.f, by, 200.f, 34.f, bKeysManual, "TAP EACH KEY MYSELF", 0);
                if (fLearning < 0) add(238.f, by, 200.f, 34.f, bKeysRepick, "CHOOSE ANOTHER KEY", 0);
            }
            if (fLearning < 0 || fKeyManual) add(w - 148.f, by, 120.f, 34.f, bKeysNext, "NEXT", fLearning < 0 ? 1 : 0);
            break;
        case kScrBankFilled:
            add(28.f, by, 200.f, 34.f, bTapAll, "TAP EACH ONE MYSELF", 0);
            if (fFillPage + 1 < kNumPages) add((228.f + w - 178.f) * 0.5f - 75.f, by, 150.f, 34.f, bMorePads, fFillPage + 1 == 1 ? "ADD BANK B" : "ADD BANK C", 0);
            add(w - 178.f, by, 150.f, 34.f, bNoMorePads, "ON TO KNOBS", 1);
            break;
        case kScrSameBank:
            add(28.f, by, 96.f, 34.f, bBack, "BACK", 0);
            add(134.f, by, 180.f, 34.f, bTapAll, "NO, I'LL MAP IT", 0);
            add(w - 248.f, by, 220.f, 34.f, bFollowYes, "YES, FOLLOW THE BANK", 1);
            break;
        case kScrMorePads:
            add(w * 0.5f - 210.f, 330.f, 200.f, 44.f, bMorePads, fMorePage == 1 ? "ADD BANK B" : "ADD BANK C", 0);
            add(w * 0.5f + 10.f, 330.f, 200.f, 44.f, bNoMorePads, "NO, ON TO KNOBS", 1);
            break;
        case kScrKnobs:
            add(28.f, by, 96.f, 34.f, bSkip, "SKIP", 0);
            add(134.f, by, 96.f, 34.f, bBack, "BACK", 0);
            add(w - 228.f, by, 200.f, 34.f, bOutKnobs, "I'M OUT OF KNOBS", 0);
            break;
        case kScrDone:
            add(w * 0.5f - 100.f, 400.f, 200.f, 46.f, bStartPlaying, "START PLAYING", 1);
            break;
        case kScrName:
            add(w * 0.5f - 210.f, 330.f, 200.f, 44.f, bCancel, "CANCEL", 0);
            add(w * 0.5f + 10.f, 330.f, 200.f, 44.f, bSaveName, "SAVE", 1);
#ifdef _WIN32
            if (fBpmEdit < 0) add(w * 0.5f - 70.f, 422.f, 140.f, 22.f, bWinDialog, "use the Windows dialog", 3);
#endif
            break;
        case kScrConfirmReplace:
            add(w * 0.5f - 210.f, 330.f, 200.f, 44.f, bCancel, "CANCEL", 0);
            add(w * 0.5f + 10.f, 330.f, 200.f, 44.f, bConfirm, "REPLACE", 2);
            break;
        case kScrConfirmPads: case kScrConfirmFactory:
            add(w * 0.5f - 210.f, 360.f, 200.f, 44.f, bCancel, "CANCEL", 0);
            add(w * 0.5f + 10.f, 360.f, 200.f, 44.f, bConfirm, fScr == kScrConfirmPads ? "RESET PADS" : "RESTORE DEFAULTS", 2);
            break;
        default: break;
        }
        return n;
    }

    void panelButton(const PanelBtn& b)
    {
        if (b.style == 4) return;
        if (b.style == 3) {
            const float tw = measure(fFontMed, 12.f, b.label, 0.f);
            drawText(fFontMed, 12.f, withAlpha(fT.menuText, 0.7f), b.x, b.y + b.h * 0.5f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, b.label);
            beginPath(); moveTo(b.x, b.y + b.h * 0.5f + 9.f); lineTo(b.x + tw, b.y + b.h * 0.5f + 9.f);
            strokeColor(withAlpha(fT.menuText, 0.5f)); strokeWidth(1.f); stroke();
            return;
        }
        beginPath(); roundedRect(b.x, b.y + 3.f, b.w, b.h, b.h * 0.5f);
        fillColor(withAlpha(fT.shadow, fT.shadowA)); fill();
        const Color f = b.style == 1 ? kAmberAcc : (b.style == 2 ? kRedAcc : fT.menuBg);
        pill(b.x, b.y, b.w, b.h, f, fDark ? fT.outline : fT.text, 2.5f);
        const Color ink = b.style == 1 ? hex(0x0A1A33) : (b.style == 2 ? hex(0xF7F1E3) : fT.menuText);
        drawText(fFontHeavy, 11.f, ink, b.x + b.w * 0.5f, b.y + b.h * 0.5f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, b.label, 0.3f);
    }

    void stepper(int cur)
    {
        const char* const labels[3] = { "HEAR IT", fKeyMode ? "KEYS" : "PADS", "KNOBS" };
        const float xs[3] = { 170.f, 340.f, 510.f }, y = 84.f;
        const Color ink = fT.menuText, edge = withAlpha(fT.menuText, 0.25f);
        for (int i = 0; i < 2; ++i) {
            beginPath(); moveTo(xs[i] + 18.f, y); lineTo(xs[i + 1] - 18.f, y);
            strokeColor(cur > i ? ink : edge); strokeWidth(3.f); stroke();
        }
        for (int i = 0; i < 3; ++i) {
            beginPath(); circle(xs[i], y, 14.f);
            if (i < cur) {
                fillColor(ink); fill();
                beginPath(); moveTo(xs[i] - 6.f, y); lineTo(xs[i] - 1.5f, y + 5.f); lineTo(xs[i] + 6.5f, y - 5.f);
                lineCap(NanoVG::ROUND); lineJoin(NanoVG::ROUND); strokeColor(fT.menuBg); strokeWidth(3.f); stroke();
            } else if (i == cur) {
                fillColor(kAmberAcc); fill(); strokeColor(fDark ? fT.outline : fT.text); strokeWidth(3.f); stroke();
                char n[4]; std::snprintf(n, sizeof(n), "%d", i + 1);
                drawText(fFontHeavy, 13.f, hex(0x0A1A33), xs[i], y, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, n);
            } else {
                strokeColor(edge); strokeWidth(3.f); stroke();
                char n[4]; std::snprintf(n, sizeof(n), "%d", i + 1);
                drawText(fFontHeavy, 13.f, withAlpha(ink, 0.5f), xs[i], y, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, n);
            }
            drawText(fFontHeavy, 9.5f, i <= cur ? ink : withAlpha(ink, 0.5f), xs[i], y + 28.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, labels[i], 0.4f);
        }
    }

    void statusLine(float y, bool ok, const char* msg, bool pulse)
    {
        const float a = pulse ? 0.45f + 0.55f * (0.5f + 0.5f * std::sin((float)now() * 6.f)) : 1.f;
        beginPath(); circle(34.f, y, 6.f);
        fillColor(ok ? kOkGreen : withAlpha(fT.menuText, 0.45f * a)); fill();
        drawText(fFontBold, 12.5f, fT.menuText, 48.f, y, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, msg);
    }

    void bigCheck(float cx, float cy, float r)
    {
        beginPath(); circle(cx, cy, r); fillColor(kOkGreen); fill();
        beginPath(); moveTo(cx - r * 0.42f, cy + r * 0.02f); lineTo(cx - r * 0.1f, cy + r * 0.34f); lineTo(cx + r * 0.45f, cy - r * 0.32f);
        lineCap(NanoVG::ROUND); lineJoin(NanoVG::ROUND); strokeColor(hex(0xF7F1E3)); strokeWidth(r * 0.2f); stroke();
    }

    static const char* padWhere(int i, char* buf, size_t sz)
    {
        static const char* const rows[4] = { "top row", "second row", "third row", "bottom row" };
        static const char* const cols[4] = { "1st", "2nd", "3rd", "4th" };
        const int q = i % kPadsPerPage;
        if (i == kKillPad) std::snprintf(buf, sz, "Press the pad you want for KILL");
        else if (q == 0) std::snprintf(buf, sz, i == 0 ? "Press the top-left pad" : "Page %c: press the top-left pad", 'A' + i / kPadsPerPage);
        else std::snprintf(buf, sz, "Press the %s pad, %s", cols[q % 4], rows[q / 4]);
        return buf;
    }

    // the 4 x 4 map of pad positions: one spot lit at a time
    void wizardGrid(float x0, float y0, int page = -1)
    {
        const float cw = 72.f, ch = 52.f, g = 8.f;
        const float blink = 0.5f + 0.5f * std::sin((float)now() * 7.f);
        const int pg = page >= 0 ? page : (fWizStep >= 0 && fWizStep < kNumPads ? fWizStep / kPadsPerPage : 0);
        for (int q = 0; q < kPadsPerPage; ++q) {
            const int i = pg * kPadsPerPage + q;
            const float x = x0 + (q % 4) * (cw + g), y = y0 + (q / 4) * (ch + g);
            const bool kill = i == kKillPad, done = fMap[kLearnPad0 + i] >= 0, cur = i == fWizStep;
            char n[8]; std::snprintf(n, sizeof(n), "%c%d", 'A' + pg, q + 1);
            if (cur) {
                beginPath(); roundedRect(x - 4.f, y - 4.f, cw + 8.f, ch + 8.f, 15.f);
                strokeColor(kAmberAcc); strokeWidth(3.f); stroke();
                beginPath(); roundedRect(x, y, cw, ch, 12.f);
                fillColor(Color(fT.menuBg, kAmberAcc, 0.35f + 0.65f * blink)); fill();
                strokeColor(fDark ? fT.outline : fT.text); strokeWidth(2.f); stroke();
                drawText(fFontHeavy, 11.f, hex(0x0A1A33), x + 10.f, y + 15.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, n);
                drawText(fFontHeavy, 10.f, hex(0x0A1A33), x + cw * 0.5f, y + 35.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, kill ? "KILL" : "PRESS", 0.4f);
            } else if (done) {
                const float hit = clamp01(1.f - (float)(now() - fWizHitAt[i]) / 0.6f);   // a tap on this pad
                beginPath(); roundedRect(x, y, cw, ch, 12.f);
                fillColor(Color(kill ? fT.killTile : fT.card[kColNavy], kAmberAcc, hit)); fill();
                const Color c = hex(0xF7F1E3);
                drawText(fFontHeavy, 11.f, c, x + 10.f, y + 15.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, n);
                if (kill) drawText(fFontHeavy, 8.f, kAmberAcc, x + cw - 9.f, y + 15.f, NanoVG::ALIGN_RIGHT | NanoVG::ALIGN_MIDDLE, "KILL", 0.4f);
                const int code = fMap[kLearnPad0 + i];
                char nb[12];
                if (code & 0x20000) std::snprintf(nb, sizeof(nb), "CC %d", code & 0x7f); else formatNote(code & 0x7f, nb, sizeof(nb));
                drawText(fFontBold, 12.f, c, x + cw * 0.5f, y + 35.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, nb);
            } else {
                beginPath(); roundedRect(x, y, cw, ch, 12.f);
                fillColor(fT.menuBg); fill();
                strokeColor(withAlpha(fT.menuText, 0.25f)); strokeWidth(2.f); stroke();
                drawText(fFontHeavy, 11.f, withAlpha(fT.menuText, 0.5f), x + 10.f, y + 15.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, n);
                if (kill) drawText(fFontHeavy, 9.f, withAlpha(fT.menuText, 0.5f), x + cw * 0.5f, y + 35.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "KILL", 0.4f);
            }
        }
    }

    // the two big choices on the first setup screen
    void chooseCard(float x, float y, bool keys)
    {
        const float w = 250.f, h = 190.f;
        const Color ink = fT.menuText, edge = fDark ? fT.outline : fT.text;
        beginPath(); roundedRect(x, y + 4.f, w, h, 20.f); fillColor(withAlpha(fT.shadow, fT.shadowA)); fill();
        beginPath(); roundedRect(x, y, w, h, 20.f); fillColor(fT.menuBg); fill();
        strokeColor(edge); strokeWidth(2.5f); stroke();
        const float icx = x + w * 0.5f, icy = y + 70.f;
        if (!keys) {
            for (int q = 0; q < 16; ++q) {
                const float px = icx - 46.f + (q % 4) * 24.f, py = icy - 46.f + (q / 4) * 24.f;
                beginPath(); roundedRect(px, py, 20.f, 20.f, 5.f);
                fillColor(q == 5 ? kAmberAcc : fT.card[kColNavy]); fill();
            }
        } else {
            const float kw = 16.f, kh = 70.f, x0 = icx - 7 * kw * 0.5f, y0 = icy - kh * 0.5f;
            for (int i = 0; i < 7; ++i) {
                beginPath(); rect(x0 + i * kw, y0, kw, kh);
                fillColor(i == 2 ? kAmberAcc : hex(0xF7F1E3)); fill();
                strokeColor(edge); strokeWidth(1.5f); stroke();
            }
            static const int blackAfter[5] = { 0, 1, 3, 4, 5 };
            for (int b : blackAfter) {
                beginPath(); rect(x0 + (b + 1) * kw - 5.f, y0, 10.f, kh * 0.6f);
                fillColor(hex(0x0A1A33)); fill();
            }
        }
        drawText(fFontHeavy, 15.f, ink, icx, y + 142.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, keys ? "KEYBOARD" : "PAD CONTROLLER", 0.3f);
        drawText(fFontMed, 12.f, withAlpha(ink, 0.7f), icx, y + 166.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE,
                 keys ? "Piano-style keys, with or without knobs" : "Pads and knobs");
    }

    // three octaves of keys: this bank's keys lit with their pad numbers, other banks' keys shaded
    void wizardKeyboard(float y0)
    {
        int padOf[128], bankOf[128];
        for (int n = 0; n < 128; ++n) padOf[n] = bankOf[n] = -1;
        for (int i = 0; i < kNumPads; ++i) {
            const int c = fMap[kLearnPad0 + i];
            if (c < 0 || !(c & 0x10000)) continue;
            const int n = c & 0x7f;
            if (i / kPadsPerPage == fKeyPage) padOf[n] = i % kPadsPerPage;
            else if (bankOf[n] < 0) bankOf[n] = i / kPadsPerPage;
        }
        int lo = 128, hi = -1;
        for (int n = 0; n < 128; ++n) if (padOf[n] >= 0) { lo = std::min(lo, n); hi = std::max(hi, n); }
        int start = lo <= hi ? (lo / 12) * 12 : 48;
        if (lo <= hi && hi > start + 35) start += 12;
        start = std::min(start, 84);
        auto isBlack = [](int n) { const int k = n % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; };
        const float ww = 28.f, wh = 120.f, bw = 18.f, bh = 74.f, x0 = (kPanW - 21.f * ww) * 0.5f;
        const Color edge = fDark ? fT.outline : fT.text, navy = hex(0x0A1A33), cream = hex(0xF7F1E3);
        int wi = 0;
        float wx[128];
        for (int n = start; n < start + 36; ++n) {
            if (isBlack(n)) { wx[n] = x0 + wi * ww; continue; }
            const float x = x0 + wi * ww;
            wx[n] = x;
            beginPath(); rect(x, y0, ww, wh);
            fillColor(padOf[n] >= 0 ? kAmberAcc : (bankOf[n] >= 0 ? hex(0xCFC6B4) : cream)); fill();
            strokeColor(edge); strokeWidth(1.5f); stroke();
            char lb[8];
            if (padOf[n] >= 0) { std::snprintf(lb, sizeof(lb), "%d", padOf[n] + 1); drawText(fFontHeavy, 11.f, navy, x + ww * 0.5f, y0 + wh - 14.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, lb); }
            else if (bankOf[n] >= 0) { std::snprintf(lb, sizeof(lb), "%c", 'A' + bankOf[n]); drawText(fFontHeavy, 10.f, withAlpha(navy, 0.6f), x + ww * 0.5f, y0 + wh - 14.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, lb); }
            if (n % 12 == 0) { char nb[8]; formatNote(n, nb, sizeof(nb)); drawText(fFontBold, 10.f, withAlpha(fT.menuText, 0.7f), x + ww * 0.5f, y0 + wh + 14.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, nb); }
            ++wi;
        }
        for (int n = start; n < start + 36; ++n) {
            if (!isBlack(n)) continue;
            const float x = wx[n] - bw * 0.5f;
            beginPath(); rect(x, y0, bw, bh);
            fillColor(padOf[n] >= 0 ? kAmberAcc : (bankOf[n] >= 0 ? hex(0x4A5568) : navy)); fill();
            strokeColor(edge); strokeWidth(1.5f); stroke();
            char lb[8];
            if (padOf[n] >= 0) { std::snprintf(lb, sizeof(lb), "%d", padOf[n] + 1); drawText(fFontHeavy, 10.f, navy, x + bw * 0.5f, y0 + bh - 12.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, lb); }
            else if (bankOf[n] >= 0) { std::snprintf(lb, sizeof(lb), "%c", 'A' + bankOf[n]); drawText(fFontHeavy, 9.f, cream, x + bw * 0.5f, y0 + bh - 12.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, lb); }
        }
    }

    void wizardKnobs(int first, float cy)
    {
        for (int k = 0; k < kNumJobs; ++k) {
            const int j = first + k;
            const float cx = 88.f + k * 72.f;
            const bool done = fMap[kLearnKnob0 + j] >= 0, cur = fWizStep == kLearnKnob0 + j;
            const Color ink = (done || cur) ? fT.menuText : withAlpha(fT.menuText, 0.45f);
            if (cur) { beginPath(); circle(cx, cy, 31.f); strokeColor(kAmberAcc); strokeWidth(3.f); stroke(); }
            const float spin = cur ? (float)std::fmod(now() * 0.8, 1.0) : (done ? 0.7f : 0.3f);
            knobShape(cx, cy, spin, ink, false, 16.f, 23.f, 0.f);
            char n[4]; std::snprintf(n, sizeof(n), "%d", k + 1);
            drawText(fFontHeavy, 12.f, ink, cx, cy + 42.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, n);
            static const char* const kn[kNumJobs] = { "KNOB 1", "KNOB 2", "KNOB 3", "KNOB 4", "KNOB 5", "KNOB 6", "KNOB 7", "KNOB 8" };
            drawText(fFontBold, 8.5f, ink, cx, cy + 58.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, kn[j], 0.3f);
            if (done) {
                beginPath(); circle(cx, cy + 76.f, 8.f); fillColor(fT.menuText); fill();
                beginPath(); moveTo(cx - 4.f, cy + 76.f); lineTo(cx - 1.f, cy + 79.f); lineTo(cx + 4.5f, cy + 73.f);
                lineCap(NanoVG::ROUND); strokeColor(fT.menuBg); strokeWidth(2.f); stroke();
            }
        }
    }

    void wizardFeedback(float y)
    {
        const double t = now();
        if (fWizDup >= 0 && t - fWizDupAt < 3.0) {
            char b[96];
            if (fWizDup < kLearnKnob0) std::snprintf(b, sizeof(b), "That one is already pad %d. Press a different one, or skip.", fWizDup + 1);
            else if (fWizDup >= kLearnBank0) std::snprintf(b, sizeof(b), "That one switches to bank %c. Press a different one, or skip.", 'A' + fWizDup - kLearnBank0);
            else std::snprintf(b, sizeof(b), "That knob is already knob %d. Turn a different one, or skip.", fWizDup - kLearnKnob0 + 1);
            drawText(fFontBold, 12.f, kRedAcc, kPanW * 0.5f, y, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, b);
        } else if (fWizLastCode >= 0) {
            char got[96], mc[64]; formatMidiCode(fWizLastCode, mc, sizeof(mc));
            std::snprintf(got, sizeof(got), "Got it: %s", mc);
            drawText(fFontBold, 12.f, fDark ? kAmberAcc : hex(0x56611A), kPanW * 0.5f, y, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, got);
        }
    }

    void drawPanel()
    {
        const float a = clamp01(fPanelA.x);
        beginPath(); rect(0.f, 0.f, kW, kH);
        fillColor(withAlpha(fT.shadow, 0.3f * a)); fill();
        save();
        globalAlpha(a);
        translate(kPanX, kPanY + 6.f * (1.f - a));
        const float w = kPanW, h = kPanH, cx = w * 0.5f;
        beginPath(); roundedRect(0.f, 7.f, w, h, 24.f);
        fillColor(withAlpha(fT.shadow, fT.shadowA + 0.15f)); fill();
        beginPath(); roundedRect(0.f, 0.f, w, h, 24.f);
        fillColor(fT.menuBg); fill();
        strokeColor(fDark ? fT.outline : fT.text); strokeWidth(3.f); stroke();

        const Color ink = fT.menuText, soft = withAlpha(ink, 0.7f);
        const char* title = "SET UP YOUR CONTROLLER";
        if (fScr == kScrHome) title = "CONTROLLER SETUP";
        if (fScr == kScrConfirmPads) title = "RESET THIS PROJECT'S PADS?";
        if (fScr == kScrName) title = fBpmEdit >= 0 ? "BEAT TEMPO" : "SAVE PRESET";
        if (fScr == kScrConfirmFactory) title = "START FRESH?";
        if (fScr == kScrConfirmReplace) title = "REPLACE IT?";
        drawText(fFontHeavy, 16.f, ink, 28.f, 38.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, title, 0.4f);
        beginPath(); circle(w - 36.f, 36.f, 14.f);
        strokeColor(ink); strokeWidth(2.f); stroke();
        beginPath(); moveTo(w - 41.f, 31.f); lineTo(w - 31.f, 41.f); moveTo(w - 31.f, 31.f); lineTo(w - 41.f, 41.f);
        lineCap(NanoVG::ROUND); strokeColor(ink); strokeWidth(2.2f); stroke();

        const float sa = clamp01(fScrA.x);
        save();
        if (sa < 0.999f) { scissor(0.f, 56.f, w, h - 56.f); translate(8.f * (1.f - fScrA.x), 0.f); globalAlpha(a * (0.4f + 0.6f * sa)); }
        switch (fScr) {
        case kScrHome: {
            const int st = ctrlStatus();
            statusLine(80.f, st == 1, st == 1 ? "Receiving from your controller." : (st == 2 ? "MIDI is arriving. Run the setup to map it."
                                                                                       : "No MIDI yet. Check that your DAW sends your controller's MIDI to this plug-in."), st != 1);
            char cnt[96]; std::snprintf(cnt, sizeof(cnt), "%d pads and %d of 8 knobs mapped on this computer", mappedPads(), mappedKnobs());
            drawText(fFontMed, 12.f, soft, 48.f, 104.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, cnt);
            drawText(fFontMed, 12.f, ink, 28.f, 150.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, "The setup walks you through your pads (or keyboard keys) and your knobs.");
            drawText(fFontMed, 12.f, ink, 28.f, 170.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, "It's saved on this computer and used by every project.");
            drawText(fFontHeavy, 10.f, ink, 28.f, 282.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, "KNOB TYPE", 0.4f);
            segSwitch(24.f, 296.f, w - 48.f, 30.f, kNumKnobModes, fKnobMode, kKnobModeNames, fT.face, fT.outline, fT.sel, fT.selInk, fT.faceInk, 10.f);
            drawText(fFontMed, 11.5f, soft, 28.f, 344.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, "AUTO tells endless knobs from normal ones. ENDLESS: endless knobs that send 0-127 like normal ones.");
            // how many knobs the controller has: with fewer than an effect's controls, the rest follow a knob or stay put
            drawText(fFontHeavy, 10.f, ink, 28.f, 372.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, "KNOBS ON MY CONTROLLER", 0.4f);
            {
                int m[kNumLearn]; for (int i = 0; i < kNumLearn; ++i) m[i] = fMap[i];
                const int counted = knobCount(m, 0);
                static char autoLb[16];
                if (counted < kNumJobs) std::snprintf(autoLb, sizeof(autoLb), "AUTO (%d)", counted); else std::snprintf(autoLb, sizeof(autoLb), "AUTO");
                const char* const names[6] = { autoLb, "1", "2", "3", "4", "5 OR MORE" };
                segSwitch(24.f, 384.f, w - 48.f, 28.f, 6, fKnobCountSet, names, fT.face, fT.outline, fT.sel, fT.selInk, fT.faceInk, 10.f);
            }
            drawText(fFontMed, 11.5f, soft, 28.f, 428.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, "AUTO counts the knobs you map. Controls past your knobs follow one of them or stay where you set them.");
            drawText(fFontMed, 11.5f, soft, 28.f, 452.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, "Right-click any pad or knob chip to learn just that one.");
            drawText(fFontMed, 11.5f, soft, 28.f, 470.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, "The NOTES pill maps notes in a row across the pages, top row first or bottom row first.");
            break;
        }
        case kScrHear: {
            stepper(0);
            const bool heard = fHeardAt > 0.0;
            statusLine(146.f, heard, heard ? "Got it. Your controller is coming through." : "Listening for your controller...", !heard);
            if (heard) {
                bigCheck(cx, 270.f, 34.f);
                drawText(fFontHeavy, 18.f, ink, cx, 336.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, fKeyMode ? "Next: set up your keys" : "Next: map your 16 pads", 0.1f);
            } else {
                drawText(fFontHeavy, 26.f, ink, cx, 250.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, fKeyMode ? "Press any key on your keyboard" : "Press any pad on your controller", 0.1f);
                const bool waited = now() - fHearStart > 5.0;
                const Color hc = waited ? ink : soft;
                drawText(fFontHeavy, 10.f, hc, 60.f, 320.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, "NOTHING HAPPENING?", 0.4f);
                drawText(fFontMed, 12.f, hc, 60.f, 344.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, "1.  In your DAW's MIDI settings, turn your controller on as an input.");
                drawText(fFontMed, 12.f, hc, 60.f, 366.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, "2.  Send it to the track this plug-in is on (in most DAWs: arm or monitor that track).");
                drawText(fFontMed, 12.f, hc, 60.f, 388.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, "3.  In FL Studio: give the controller a port number and set this plug-in's MIDI input port to match.");
                drawText(fFontMed, 12.f, hc, 60.f, 410.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, "4.  Plug the controller in by USB before you open your DAW.");
            }
            break;
        }
        case kScrChoose: {
            drawText(fFontHeavy, 22.f, ink, cx, 120.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "What are you playing with?", 0.1f);
            drawText(fFontMed, 13.f, soft, cx, 152.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "Pick one and the setup shows you how to map it. You can run it again any time.");
            chooseCard(cx - 262.f, 196.f, false);
            chooseCard(cx + 12.f, 196.f, true);
            break;
        }
        case kScrKeys: {
            stepper(1);
            statusLine(146.f, true, "Keyboard connected", false);
            const char bank = (char)('A' + fKeyPage);
            char head[64]; const char* sub;
            if (fKeyManual && fLearning >= 0) {
                std::snprintf(head, sizeof(head), "Press the key for pad %c%d", bank, fWizStep % kPadsPerPage + 1);
                sub = "Tap keys in the order you want, pad 1 to pad 16. SKIP leaves a pad empty.";
            } else if (fKeyManual) {
                std::snprintf(head, sizeof(head), "Bank %c is on these keys", bank);
                sub = "Play them to try it. Right-click any pad later to change just that one.";
            } else if (fLearning >= 0) {
                std::snprintf(head, sizeof(head), "Press the key you want for pad %c1", bank);
                sub = "The next 15 keys fill in for you, white and black. Or tap each key yourself.";
            } else {
                std::snprintf(head, sizeof(head), "Bank %c is on these 16 keys", bank);
                sub = "Play them to try it. Want it somewhere else? Choose another key.";
            }
            drawText(fFontHeavy, 20.f, ink, cx, 186.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, head, 0.1f);
            drawText(fFontMed, 12.f, soft, cx, 208.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, sub);
            wizardKeyboard(236.f);
            if (now() - fKeyNotKeyAt < 3.0)
                drawText(fFontBold, 12.f, kRedAcc, cx, 412.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "That was a knob or button. Press a key on the keyboard.");
            else wizardFeedback(412.f);
            break;
        }
        case kScrBackup:
            stepper(0);
            drawText(fFontHeavy, 22.f, ink, cx, 200.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "This project has a controller setup saved", 0.1f);
            drawText(fFontMed, 13.f, soft, cx, 236.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "This computer doesn't have one yet. Use the project's setup here,");
            drawText(fFontMed, 13.f, soft, cx, 256.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "or map your controller fresh?");
            break;
        case kScrPads: {
            stepper(1);
            statusLine(146.f, true, "Controller connected", false);
            char b[64];
            if (fBankQuick) {
                const int pg = fWizStep / kPadsPerPage;
                char hb[64], s1[96];
                if (fQuickRef == 0) std::snprintf(hb, sizeof(hb), "Bank %c: press the top-left pad", 'A' + pg);
                else { char w2[64]; padWhere(pg * kPadsPerPage + fQuickRef, w2, sizeof(w2)); std::snprintf(hb, sizeof(hb), "Bank %c: %c%s", 'A' + pg, std::tolower(w2[0]), w2 + 1); }
                std::snprintf(s1, sizeof(s1), "Switch your controller to bank %c and press that pad. Dewitt fills in the rest to match bank A.", 'A' + pg);
                drawText(fFontHeavy, 20.f, ink, cx, 186.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, hb, 0.1f);
                drawText(fFontMed, 12.f, soft, cx, 208.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, s1);
                wizardGrid(cx - 156.f, 226.f);
                wizardFeedback(474.f);
                break;
            }
            drawText(fFontHeavy, 20.f, ink, cx, 186.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, padWhere(fWizStep, b, sizeof(b)), 0.1f);
            const char* sub = fWizStep == kKillPad ? "Any pad or button you like. Holding it mutes everything."
                            : (fWizStep >= kPadsPerPage ? "Switch to your controller's next pad bank, or tap pads on another controller."
                            : (fWizStep >= 8 ? "Only 8 pads? Switch your controller to its second pad bank and keep going."
                                             : "Press the pad in that same spot on your controller."));
            drawText(fFontMed, 12.f, soft, cx, 208.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, sub);
            wizardGrid(cx - 156.f, 226.f);
            wizardFeedback(474.f);
            break;
        }
        case kScrBankFilled: {
            stepper(1);
            char b[64]; std::snprintf(b, sizeof(b), "%d pads mapped", mappedPads());
            statusLine(146.f, true, b, false);
            char hb[48]; std::snprintf(hb, sizeof(hb), "Bank %c is filled in", 'A' + fFillPage);
            drawText(fFontHeavy, 20.f, ink, cx, 186.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, hb, 0.1f);
            drawText(fFontMed, 12.f, soft, cx, 208.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, fFillText);
            wizardGrid(cx - 156.f, 226.f, fFillPage);
            drawText(fFontBold, 12.f, fDark ? kAmberAcc : hex(0x56611A), cx, 471.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "Tap a few pads on your controller to check: they light up here.");
            break;
        }
        case kScrSameBank: {
            stepper(1);
            statusLine(146.f, true, "Bank A is mapped", false);
            char hb[48]; std::snprintf(hb, sizeof(hb), "That's the same as pad A%d", fQuickRef + 1);
            drawText(fFontHeavy, 20.f, ink, cx, 196.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, hb, 0.1f);
            drawText(fFontMed, 13.f, soft, cx, 230.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "Your controller sends the same notes on every bank (or has no bank button).");
            drawText(fFontMed, 13.f, soft, cx, 250.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "Let bank A's pads play whichever bank is showing in Dewitt instead?");
            const float tw = 70.f, tx = cx - (3.f * tw + 2.f * 12.f) * 0.5f;
            for (int pg = 0; pg < kNumPages; ++pg) {
                const float x = tx + pg * (tw + 12.f), y = 290.f;
                beginPath(); roundedRect(x, y, tw, 46.f, 12.f);
                if (pg == 0) { fillColor(fT.card[kColNavy]); fill(); }
                strokeColor(pg == 0 ? fT.card[kColNavy] : withAlpha(ink, 0.5f)); strokeWidth(2.f); stroke();
                char lb[2] = { (char)('A' + pg), 0 };
                drawText(fFontHeavy, 14.f, pg == 0 ? hex(0xF7F1E3) : withAlpha(ink, 0.6f), x + tw * 0.5f, y + 23.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, lb);
            }
            drawText(fFontMed, 12.f, soft, cx, 372.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "Switch banks with Dewitt's A / B / C buttons,");
            drawText(fFontMed, 12.f, soft, cx, 390.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "or right-click them to learn a button on your controller.");
            break;
        }
        case kScrMorePads: {
            stepper(1);
            char b[64]; std::snprintf(b, sizeof(b), "%d pads mapped", mappedPads());
            statusLine(146.f, true, b, false);
            if (fKeyMode) {
                char q[48]; std::snprintf(q, sizeof(q), "Do you want bank %c on more keys?", 'A' + fMorePage);
                drawText(fFontHeavy, 22.f, ink, cx, 220.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, q, 0.1f);
                drawText(fFontMed, 13.f, soft, cx, 256.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "Another 16 keys, so you can play another bank of pads.");
                drawText(fFontMed, 13.f, soft, cx, 276.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "It goes on the keys right after the last bank, and you can move it.");
            } else {
                drawText(fFontHeavy, 22.f, ink, cx, 220.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "Do you want to add more pads?", 0.1f);
                drawText(fFontMed, 13.f, soft, cx, 256.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "Another bank of 16 pads: your controller's next pad bank,");
                drawText(fFontMed, 13.f, soft, cx, 276.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "or the pads of a second controller. Map as many as you like.");
            }
            break;
        }
        case kScrKnobs: {
            stepper(2);
            statusLine(146.f, true, "Pads done", false);
            const int j = fWizStep - kLearnKnob0;
            char b[64]; std::snprintf(b, sizeof(b), "Turn knob %d a few clicks", j + 1);
            drawText(fFontHeavy, 20.f, ink, cx, 200.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, b, 0.1f);
            const char* sub = j == 0 ? "A few clicks tells endless knobs from normal ones."
                            : (j >= 3 ? "Most effects use knobs 1-3; a few use up to 5. Map all of them." : "Turn the next knob along.");
            drawText(fFontMed, 12.f, soft, cx, 224.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, sub);
            wizardKnobs(0, 300.f);
            wizardFeedback(430.f);
            break;
        }
        case kScrName: {
            drawText(fFontHeavy, 18.f, ink, cx, 150.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE,
                     fBpmEdit >= 0 ? "Type the tempo" : (fNameForPad >= 0 ? "Name this pad preset" : (fRenaming ? "Rename this set" : "Name this set")), 0.1f);
            pill(cx - 220.f, 210.f, 440.f, 48.f, fT.face, fT.outline, 2.5f);
            std::string nm = fNameBuf;
            const bool blink = std::fmod(now(), 1.0) < 0.55;
            nm = fitText(fFontBold, 16.f, nm, 400.f, 0.f);
            drawText(fFontBold, 16.f, fT.faceInk, cx - 200.f, 234.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, nm.c_str());
            if (fNameFresh) {       // the suggested name is shown selected: typing replaces it
                beginPath(); rect(cx - 202.f, 222.f, measure(fFontBold, 16.f, nm.c_str(), 0.f) + 4.f, 24.f);
                fillColor(withAlpha(kAmberAcc, 0.45f)); fill();
                drawText(fFontBold, 16.f, fT.faceInk, cx - 200.f, 234.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, nm.c_str());
            } else if (blink) {
                const float tx = cx - 200.f + measure(fFontBold, 16.f, nm.c_str(), 0.f) + 2.f;
                beginPath(); rect(tx, 222.f, 2.f, 24.f); fillColor(fT.faceInk); fill();
            }
            drawText(fFontMed, 12.f, soft, cx, 286.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE,
                     fBpmEdit >= 0 ? "Type the beat's BPM (for example 92 or 92.5) and press Enter." : "Type a name and press Enter. Saved on this computer, ready in every project.");
#ifdef _WIN32
            if (fBpmEdit < 0) drawText(fFontMed, 11.5f, soft, cx, 412.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "Typing not showing up? Turn off your DAW's computer-keyboard piano, or");
#else
            if (fBpmEdit < 0) drawText(fFontMed, 11.5f, soft, cx, 412.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "Typing not showing up? Click the name box, or turn off your DAW's computer-keyboard piano.");
#endif
            break;
        }
        case kScrDone:
            stepper(3);
            statusLine(146.f, true, "Controller set up", false);
            bigCheck(cx, 250.f, 40.f);
            drawText(fFontHeavy, 28.f, ink, cx, 326.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "You're set.", 0.1f);
            drawText(fFontMed, 13.f, soft, cx, 362.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "This setup is saved on this computer and used in every project.");
            break;
        case kScrConfirmReplace: {
            char msg[160];
            std::snprintf(msg, sizeof(msg), fNameForPad >= 0 ? "You already have a pad preset called \"%s\". Replace it with this one?"
                                                              : "You already have a set called \"%s\". Replace it with this one?", fReplaceName.c_str());
            drawParagraph(msg, cx, 210.f, 520.f, 15.f, ink, fFontMed);
            drawText(fFontMed, 12.f, soft, cx, 280.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "Pick CANCEL to go back without saving.");
            break;
        }
        case kScrConfirmPads:
            drawParagraph("Puts every pad on every page back to the starting layout in this project, and removes samples from pads. "
                          "Your controller setup and saved presets stay as they are. You can undo this.", cx, 200.f, 520.f, 13.5f, ink, fFontMed);
            break;
        case kScrConfirmFactory:
            drawParagraph("Erases your controller setup, knob type, note map, dark mode and window size on this computer, "
                          "and resets this project's pads and samples. Projects you've already saved keep their own pad layouts. "
                          "Controller links you made in your DAW belong to the DAW and stay as they are.", cx, 210.f, 540.f, 13.5f, ink, fFontMed);
            drawText(fFontBold, 12.f, kRedAcc, cx, 310.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "This can't be undone.");
            break;
        default: break;
        }

        PanelBtn bt[8];
        const int n = panelButtons(bt);
        for (int i = 0; i < n; ++i) panelButton(bt[i]);
        restore();
        restore();
    }

    void drawToast()
    {
        const float a = clamp01(fToastA.x);
        const float tw = measure(fFontBold, 12.f, fToast, 0.f) + 48.f;
        const float x = (kW - tw) * 0.5f, y = kH - 70.f + 8.f * (1.f - a);
        save();
        globalAlpha(a);
        beginPath(); roundedRect(x, y + 3.f, tw, 30.f, 15.f);
        fillColor(withAlpha(fT.shadow, fT.shadowA)); fill();
        pill(x, y, tw, 30.f, fDark ? fT.face : fT.text, fT.outline, 2.f);
        beginPath(); circle(x + 18.f, y + 15.f, 4.5f); fillColor(kAmberAcc); fill();
        drawText(fFontBold, 12.f, fDark ? fT.faceInk : fT.bg, x + 30.f, y + 15.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, fToast);
        restore();
    }

    // --------------------------------------------------------------------------------------------
    // Menus

    struct MenuItem { char label[64]; int act; bool header; int col; };

    float menuItemH(int i) const { return fMenuItems[i].header ? 22.f : 26.f; }
    float menuTop() const { return fMenuRY + 6.f + (fMenuHeader[0] ? (fMenuSub[0] ? 40.f : 26.f) : 0.f); }
    float menuColW() const { return fMenuW / (float)fMenuCols; }
    float menuHeight() const
    {
        float cols[4] = { 0.f, 0.f, 0.f, 0.f };
        for (int i = 0; i < fMenuCount; ++i) cols[fMenuItems[i].col] += menuItemH(i);
        return 12.f + (fMenuHeader[0] ? (fMenuSub[0] ? 40.f : 26.f) : 0.f) + (fMenuFoot[0] ? 30.f : 0.f) + std::max(std::max(cols[0], cols[1]), std::max(cols[2], cols[3]));
    }

    void drawMenu()
    {
        const float w = fMenuW;
        const float h = menuHeight();
        float x = fMenuX, y = fMenuY;
        if (x + w > kW - 8.f) x = kW - 8.f - w;
        if (y + h > kH - 8.f) y = kH - 8.f - h;
        if (y < 8.f) y = 8.f;
        fMenuRX = x; fMenuRY = y;

        const float ma = clamp01(fMenuA.x);
        save();
        {   // grow from the corner nearest to where it was opened
            const float ax = fMenuX <= x + 1.f ? x : x + w, ay = fMenuY <= y + 1.f ? y : y + h;
            const float sc = 0.97f + 0.03f * fMenuA.x;
            translate(ax, ay - 3.f * (1.f - fMenuA.x)); scale(sc, sc); translate(-ax, -ay);
            globalAlpha(ma);
        }
        beginPath(); roundedRect(x, y + 4.f, w, h, 12.f);
        fillColor(withAlpha(fT.shadow, fT.shadowA)); fill();
        beginPath(); roundedRect(x, y, w, h, 12.f);
        fillColor(fT.menuBg); fill();
        strokeColor(fDark ? fT.outline : fT.text); strokeWidth(3.f); stroke();

        float iy = y + 6.f;
        if (fMenuHeader[0]) {
            if (fMenuSub[0]) {
                drawText(fFontHeavy, 9.5f, fT.menuText, x + 14.f, iy + 11.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, fMenuHeader, 0.3f);
                drawText(fFontMed, 10.5f, withAlpha(fT.menuText, 0.7f), x + 14.f, iy + 27.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, fMenuSub);
                iy += 40.f;
            } else {
                drawText(fFontMed, 12.f, withAlpha(fT.menuText, 0.75f), x + 14.f, iy + 13.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, fMenuHeader);
                iy += 26.f;
            }
        }
        const float top = iy, cw = menuColW();
        float colY[4] = { top, top, top, top };
        {   // the hover highlight: find the hovered item, then let the highlight glide there
            float hy[4] = { top, top, top, top };
            for (int i = 0; i < fMenuCount; ++i) {
                const MenuItem& it = fMenuItems[i];
                const float ih = menuItemH(i);
                if (i == fMenuHover && fMenuOpen && !it.header) {
                    const float tx = x + cw * it.col + 6.f, ty = hy[it.col];
                    if (fHovA.x < 0.02f) { fHovX.snap(tx); fHovY.snap(ty); fHovW.snap(cw - 12.f); fHovH.snap(ih); }   // appears in place
                    fHovX.set(tx); fHovY.set(ty); fHovW.set(cw - 12.f); fHovH.set(ih);
                }
                hy[it.col] += ih;
            }
            fHovA.set(fMenuHover >= 0 && fMenuOpen ? 1.f : 0.f);
        }
        const float ha = clamp01(fHovA.x);
        if (ha > 0.003f) {
            beginPath(); roundedRect(fHovX.x, fHovY.x, fHovW.x, fHovH.x, 8.f);
            fillColor(withAlpha(fT.menuHover, ha)); fill();
        }
        for (int c = 1; c < fMenuCols; ++c) {
            beginPath(); moveTo(x + cw * c, top + 4.f); lineTo(x + cw * c, y + h - 10.f - (fMenuFoot[0] ? 30.f : 0.f));
            strokeColor(withAlpha(fT.menuText, 0.15f)); strokeWidth(1.f); stroke();
        }
        for (int i = 0; i < fMenuCount; ++i) {
            const MenuItem& it = fMenuItems[i];
            const float ih = menuItemH(i);
            const float cx0 = x + cw * it.col;
            iy = colY[it.col];
            colY[it.col] += ih;
            if (it.header) {
                drawText(fFontHeavy, 9.f, withAlpha(fT.menuText, 0.5f), cx0 + 14.f, iy + ih * 0.5f + 2.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, it.label, 0.6f);
                continue;
            }
            // the text turns as the highlight covers it
            const float ox = std::max(0.f, std::min(cx0 + cw - 6.f, fHovX.x + fHovW.x) - std::max(cx0 + 6.f, fHovX.x));
            const float oy = std::max(0.f, std::min(iy + ih, fHovY.x + fHovH.x) - std::max(iy, fHovY.x));
            const float cover = ha * clamp01(ox / (cw - 12.f)) * clamp01(oy / ih);
            const bool hov = cover > 0.5f;
            const Color c = Color(fT.menuText, fT.menuHoverText, cover);
            drawText(fFontBold, 12.f, it.act == kActFactory && !hov ? kRedAcc : c, cx0 + 14.f, iy + ih * 0.5f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE,
                     fitText(fFontBold, 12.f, it.label, cw - 44.f, 0.f).c_str());
            if (fMenuChecked == i || fMenuChecked2 == i || fMenuChecked3 == i || fMenuChecked4 == i || fMenuChecked5 == i) {
                const float ccx = cx0 + cw - 22.f, ccy = iy + ih * 0.5f;
                beginPath();
                moveTo(ccx - 5.f, ccy); lineTo(ccx - 1.5f, ccy + 3.5f); lineTo(ccx + 5.f, ccy - 4.f);
                lineCap(NanoVG::ROUND); lineJoin(NanoVG::ROUND);
                strokeColor(c); strokeWidth(2.2f); stroke();
            }
        }
        iy = std::max(std::max(colY[0], colY[1]), std::max(colY[2], colY[3]));
        if (fMenuFoot[0]) {
            beginPath(); moveTo(x + 12.f, iy + 5.f); lineTo(x + w - 12.f, iy + 5.f);
            strokeColor(withAlpha(fT.menuText, 0.2f)); strokeWidth(1.f); stroke();
            drawText(fFontMed, 9.5f, withAlpha(fT.menuText, 0.7f), x + 14.f, iy + 19.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, fMenuFoot);
        }
        restore();
    }

    void menuBegin(int kind, int target, float x, float y, float w)
    {
        fMenuOpen = true; fMenuHover = -1; fMenuX = x; fMenuY = y; fMenuW = w;
        fMenuKind = kind; fMenuTarget = target; fMenuCount = 0; fMenuChecked = fMenuChecked2 = fMenuChecked3 = fMenuChecked4 = fMenuChecked5 = -1; fMenuCols = 1; fMenuCol = 0;
        fMenuHeader[0] = fMenuSub[0] = fMenuFoot[0] = '\0';
        // pre-compute the clamped position so hit-testing works before the first draw
        fMenuRX = x; fMenuRY = y;
    }
    void menuAdd(const char* label, int act, bool header = false)
    {
        if (fMenuCount >= kMaxMenuItems) return;
        MenuItem& it = fMenuItems[fMenuCount++];
        std::snprintf(it.label, sizeof(it.label), "%s", label);
        it.act = act; it.header = header; it.col = fMenuCol;
    }
    void menuNextColumn() { if (fMenuCol < 3) { ++fMenuCol; fMenuCols = fMenuCol + 1; } }
    void menuFinish()
    {
        const float h = menuHeight();
        float x = fMenuX, y = fMenuY;
        if (x + fMenuW > kW - 8.f) x = kW - 8.f - fMenuW;
        if (y + h > kH - 8.f) y = kH - 8.f - h;
        if (y < 8.f) y = 8.f;
        fMenuRX = x; fMenuRY = y;
        wake();
    }

    int menuItemAt(float x, float y) const
    {
        if (x < fMenuRX || x > fMenuRX + fMenuW) return -1;
        const int col = std::min(fMenuCols - 1, (int)((x - fMenuRX) / menuColW()));
        float iy = menuTop();
        for (int i = 0; i < fMenuCount; ++i) {
            if (fMenuItems[i].col != col) continue;
            const float ih = menuItemH(i);
            if (y >= iy && y < iy + ih) return fMenuItems[i].header ? -1 : i;
            iy += ih;
        }
        return -1;
    }


    // right-click on a pad (or a knob chip): MIDI learn, samples, pad presets
    void openLearnMenu(int learnSlot, int s, float x, float y)
    {
        menuBegin(kMenuLearn, learnSlot, x, y, 240.f);
        fMenuSlot = s;
        formatMidiCode(fMap[learnSlot], fMenuHeader, sizeof(fMenuHeader));
        if (learnSlot >= kLearnBank0) {                 // a bank tab: the button that shows this bank
            char m[64]; std::snprintf(m, sizeof(m), "%s", fMenuHeader);
            std::snprintf(fMenuHeader, sizeof(fMenuHeader), "Bank %c button: %s", 'A' + learnSlot - kLearnBank0, m);
        }
        menuAdd(fLearning == learnSlot ? "Cancel MIDI learn" : "MIDI learn", fLearning == learnSlot ? kActCancelLearn : kActLearn);
        if (fMap[learnSlot] >= 0) menuAdd("Clear MIDI mapping", kActClear);
        if (s >= 0) {
            menuAdd(fSmpLoaded[s] ? "Replace sample..." : "Load sample...", kActLoadSmp);
            menuAdd(fBeatHas[s] ? "Replace beat..." : "Load as beat...", kActLoadBeat);
            if (fSmpLoaded[s]) menuAdd("Remove sample", kActRemoveSmp);
            if (fBeatHas[s]) menuAdd("Remove beat", kActRemoveBeat);
            if (fBeatHas[s] && fBeatMissing[s]) menuAdd("Find missing file...", kActRelink);
            menuAdd("Save pad as preset...", kActSavePadPreset);
            menuAdd("Load pad preset...", kActLoadPadPresetMenu);
            if (!hasAudio(s) && (effectAt(s) == kFxStutterV || effectAt(s) == kFxReverseV)) menuAdd("New pattern", kActNewPattern);
        }
        menuFinish();
    }

    void openKnobMenu(int param, float x, float y)
    {
        menuBegin(kMenuKnob, param, x, y, 200.f);
        menuAdd("Reset to default", kActReset);
        menuFinish();
    }

    // what goes on a pad: an effect (left), a move or a sample (right)
    // what goes on a pad, in three columns by family: time and filters / modulation, color and space /
    // moves, audio and the pad itself
    void openTypeMenu(int s, float x, float y, bool fromDetail)
    {
        menuBegin(kMenuType, s, std::min(x, kW - 740.f), std::min(y, 60.f), 720.f);
        fMenuFromDetail = fromDetail;
        const int cur = hasAudio(s) ? -1 : typeOf(fParam, s);
        auto fam = [&](int f, const char* title) {
            menuAdd(title, 0, true);
            for (int i = 0; i < kNumEffects; ++i) {
                const int e = kChainOrder[i];
                if (kEffects[e].family != f) continue;
                if (typeOfEffect(e) == cur) fMenuChecked = fMenuCount;
                menuAdd(kEffects[e].name, kActType0 + typeOfEffect(e));
            }
        };
        fam(kFamTime, "TIME");
        fam(kFamFilter, "FILTER");
        menuNextColumn();
        fam(kFamMod, "MODULATION");
        fam(kFamTexture, "COLOR");
        menuNextColumn();
        fam(kFamSpace, "SPACE");
        menuAdd("MOVES", 0, true);
        static const int order[kNumMoves] = { kMvKill, kMvDryKill, kMvThrow, kMvFreeze, kMvGrab8, kMvGrab4, kMvGrab2, kMvGrab1, kMvTiming, kMvAllOff };
        for (int i = 0; i < kNumMoves; ++i) {
            if (typeOfMove(order[i]) == cur) fMenuChecked = fMenuCount;
            menuAdd(kMoveNames[order[i]], kActType0 + typeOfMove(order[i]));
        }
        menuNextColumn();
        menuAdd("AUDIO", 0, true);
        if (fSmpLoaded[s]) fMenuChecked = fMenuCount;
        menuAdd(fSmpLoaded[s] ? "Replace sample..." : "Load sample...", kActLoadSmp);
        if (fBeatHas[s]) fMenuChecked = fMenuCount;
        menuAdd(fBeatHas[s] ? "Replace beat..." : "Load beat...", kActLoadBeat);
        menuAdd("PAD", 0, true);
        if (cur == kTypeEmpty) fMenuChecked = fMenuCount;
        menuAdd("Empty", kActType0 + kTypeEmpty);
        menuAdd("Save as pad preset...", kActSavePadPreset);
        menuAdd("Load pad preset...", kActLoadPadPresetMenu);
        menuFinish();
    }

    void openPadPresetMenu(int s, float x, float y)
    {
        menuBegin(kMenuPadPresets, s, x, y, 240.f);
        listFiles(presetsDir("Pads").c_str(), ".ypad", fPadPresetNames);
        std::snprintf(fMenuHeader, sizeof(fMenuHeader), "PAD PRESETS");
        if (fPadPresetNames.empty()) std::snprintf(fMenuSub, sizeof(fMenuSub), "None saved yet");
        // long lists go on pages, so the menu always fits the window
        const int per = 16, n = (int)fPadPresetNames.size(), pages = std::max(1, (n + per - 1) / per);
        fPadPresetPage = std::max(0, std::min(fPadPresetPage, pages - 1));
        if (pages > 1) std::snprintf(fMenuSub, sizeof(fMenuSub), "Page %d of %d", fPadPresetPage + 1, pages);
        if (fPadPresetPage > 0) menuAdd("< Previous page", kActPadPagePrev);
        for (int i = fPadPresetPage * per; i < n && i < (fPadPresetPage + 1) * per; ++i) menuAdd(fPadPresetNames[(size_t)i].c_str(), kActPadPreset0 + i);
        if (fPadPresetPage < pages - 1) menuAdd("Next page >", kActPadPageNext);
        menuFinish();
    }

    void openSetsMenu()
    {
        refreshSetList();
        menuBegin(kMenuSets, 0, kPresetX - 120.f, kPresetY + kPresetH + 6.f, 3.f * 220.f);
        menuAdd("BUILT IN", 0, true);
        const char* group = "";
        for (int i = 0; i < numFactorySets(); ++i) {
            if (std::strcmp(factorySetGroup(i), group) != 0) {               // a new group: a heading, two groups a column
                group = factorySetGroup(i);
                if (std::strcmp(group, "CLUB") == 0) menuNextColumn();
                menuAdd(group, 0, true);
            }
            if (!fPresetUser && std::strcmp(fPresetName, factorySetName(i)) == 0) fMenuChecked = fMenuCount;
            menuAdd(factorySetName(i), kActSet0 + i);
        }
        menuNextColumn();
        if (!fUserSets.empty()) {
            // ten at a time, so the column always fits the window
            const int per = 10, n = (int)fUserSets.size(), pages = std::max(1, (n + per - 1) / per);
            fSetPage = std::max(0, std::min(fSetPage, pages - 1));
            char hd[32];
            if (pages > 1) std::snprintf(hd, sizeof(hd), "YOUR SETS  %d/%d", fSetPage + 1, pages); else std::snprintf(hd, sizeof(hd), "YOUR SETS");
            menuAdd(hd, 0, true);
            if (fSetPage > 0) menuAdd("< Previous sets", kActSetPagePrev);
            for (int i = fSetPage * per; i < n && i < (fSetPage + 1) * per; ++i) {
                if (fPresetUser && fUserSets[(size_t)i] == fPresetName) fMenuChecked = fMenuCount;
                menuAdd(fUserSets[(size_t)i].c_str(), kActSet0 + numFactorySets() + i);
            }
            if (fSetPage < pages - 1) menuAdd("More sets >", kActSetPageNext);
        }
        menuAdd("SAVE", 0, true);
        if (fPresetUser) menuAdd("Save changes", kActSaveSet);
        menuAdd("Save as a new set...", kActSaveSetAs);
        if (fPresetUser) menuAdd("Rename this set...", kActRenameSet);
        bool anyBeat = false; for (int k = 0; k < kNumSlots; ++k) anyBeat |= fBeatHas[k];
        if (anyBeat) { menuAdd("BEATS", 0, true); menuAdd("Collect beat files", kActCollect); }
        if (fPresetUser) menuAdd("Delete this set", kActDeleteSet);
        menuFinish();
    }

    void openNotesMenu(float x, float y)
    {
        menuBegin(kMenuNotes, 0, x, y, 250.f);
        std::snprintf(fMenuHeader, sizeof(fMenuHeader), "FIRST PAD NOTE");
        if (fNoteStart >= 0) {
            char a[8], b[8]; formatNote(fNoteStart, a, sizeof(a)); formatNote(std::min(127, fNoteStart + kNumPads - 1), b, sizeof(b));
            std::snprintf(fMenuSub, sizeof(fMenuSub), "Pages A to C play %s to %s", a, b);
        } else std::snprintf(fMenuSub, sizeof(fMenuSub), "Pick a note to turn it on");
        std::snprintf(fMenuFoot, sizeof(fMenuFoot), "Scroll the pill to move by a half step");
        menuAdd("Off", kActNotesOff);
        if (fNoteStart < 0) fMenuChecked = 0;
        for (int o = 0; o <= 7; ++o) {
            char lb[24]; std::snprintf(lb, sizeof(lb), "C%d \xC2\xB7 %d", o, o * 12);
            if (fNoteStart == o * 12) fMenuChecked = fMenuCount;
            menuAdd(lb, kActNote0 + o);
        }
        menuNextColumn();
        menuAdd("ROW ORDER", 0, true);
        if (fNoteOrder == 0) fMenuChecked = fMenuCount;
        menuAdd("Top row first", kActOrderTop);
        menuAdd("Bottom row first", kActOrderBottom);
        menuFinish();
    }

    void openSettingsMenu()
    {
        menuBegin(kMenuSettings, 0, gearCX() - 14.f, kNotesY + kNotesH + 8.f, 270.f);
        menuAdd("CONTROLLER", 0, true);
        menuAdd("Set up controller...", kActSetup);
        menuAdd("Knobs: type, speed, pickup...", kActKnobTypeMenu);
        menuAdd("Note map...", kActNotesMenu);
        menuAdd("PAD FEEL", 0, true);
        static const char* const feel[3] = { "Velocity + pressure", "Velocity only", "Full on any touch" };
        for (int f = 0; f < 3; ++f) { if (f == fPadFeel) fMenuChecked = fMenuCount; menuAdd(feel[f], kActFeel0 + f); }
        menuAdd("BANKS", 0, true);
        if (fFollow) fMenuChecked2 = fMenuCount;
        menuAdd("Bank A's pads play the bank on screen", kActFollow);
        menuAdd("WINDOW", 0, true);
        menuAdd("Reset window size", kActResetSize);
        menuAdd("RESET", 0, true);
        menuAdd("Reset this project's pads...", kActResetPads);
        menuAdd("Restore factory defaults...", kActFactory);
        menuFinish();
    }

    void openKnobTypeMenu()
    {
        menuBegin(kMenuKnobType, 0, gearCX() - 14.f, kNotesY + kNotesH + 8.f, 600.f);
        menuAdd("KNOB TYPE", 0, true);
        for (int m = 0; m < kNumKnobModes; ++m) { if (m == fKnobMode) fMenuChecked = fMenuCount; menuAdd(kKnobModeNames[m], kActKnob0 + m); }
        menuNextColumn();
        menuAdd("ENDLESS KNOB SPEED", 0, true);
        static const char* const sp[3] = { "Slow", "Normal", "Fast" };
        for (int k = 0; k < 3; ++k) { if (k == fKnobSpeed) fMenuChecked2 = fMenuCount; menuAdd(sp[k], kActSpeed0 + k); }
        menuAdd("NORMAL KNOBS", 0, true);
        if (fKnobJump) fMenuChecked3 = fMenuCount;
        menuAdd("Jump to the knob", kActJump0 + 1);
        if (!fKnobJump) fMenuChecked3 = fMenuCount;
        menuAdd("Wait for the knob", kActJump0);
        menuAdd("KNOBS ON SCREEN", 0, true);
        fMenuChecked4 = fMenuCount + (fKnobGridBottom ? 1 : 0);
        menuAdd("Top row 1-4, bottom 5-8", kActGrid0);
        menuAdd("Bottom row 1-4, top 5-8", kActGrid0 + 1);
        menuNextColumn();
        menuAdd("KNOBS ON MY CONTROLLER", 0, true);
        {
            char lb[32];
            int m[kNumLearn]; for (int i = 0; i < kNumLearn; ++i) m[i] = fMap[i];
            const int counted = knobCount(m, 0);
            if (counted < kNumJobs) std::snprintf(lb, sizeof(lb), "Count the ones I link (%d)", counted);
            else std::snprintf(lb, sizeof(lb), "Count the ones I link");
            fMenuChecked5 = fMenuCount + (fKnobCountSet >= 1 && fKnobCountSet <= 5 ? fKnobCountSet : 0);
            menuAdd(lb, kActKCount0);
            static const char* const kc[5] = { "1 knob", "2 knobs", "3 knobs", "4 knobs", "5 or more" };
            for (int k = 0; k < 5; ++k) menuAdd(kc[k], kActKCount0 + 1 + k);
        }
        menuFinish();
    }

    void runMenu(int item)
    {
        const int act = fMenuItems[item].act;
        const int target = fMenuTarget;
        const int kind = fMenuKind;
        if (act >= kActSet0) { loadSetByIndex(act - kActSet0); return; }
        if (act == kActSetPageNext || act == kActSetPagePrev) { fSetPage += act == kActSetPageNext ? 1 : -1; openSetsMenu(); return; }
        if (act == kActPadPageNext || act == kActPadPagePrev) { fPadPresetPage += act == kActPadPageNext ? 1 : -1; openPadPresetMenu(target, fMenuX, fMenuY); return; }
        if (act >= kActPadPreset0) { if ((size_t)(act - kActPadPreset0) < fPadPresetNames.size()) loadPadPreset(target, fPadPresetNames[act - kActPadPreset0]); return; }
        if (act >= kActType0) { setSlotType(target, act - kActType0); return; }
        if (act >= kActKnob0 && act < kActKnob0 + kNumKnobModes) { setKnobMode(act - kActKnob0); return; }
        if (act >= kActSpeed0 && act < kActSpeed0 + 3) { fKnobSpeed = act - kActSpeed0; Prefs up; up.kspeed = fKnobSpeed; writePrefs(up); bumpController(); return; }
        if (act == kActFollow) { setFollow(!fFollow); return; }
        if (act >= kActFeel0 && act < kActFeel0 + 3) { fPadFeel = act - kActFeel0; Prefs up; up.padfeel = fPadFeel; writePrefs(up); bumpController(); wake(); return; }
        if (act >= kActKCount0 && act < kActKCount0 + 6) { fKnobCountSet = act - kActKCount0; Prefs up; up.kcount = fKnobCountSet; writePrefs(up); bumpController(); wake(); return; }
        if (act >= kActGrid0 && act < kActGrid0 + 2) { fKnobGridBottom = act == kActGrid0 + 1; Prefs up; up.kgrid = fKnobGridBottom ? 1 : 0; writePrefs(up); wake(); return; }
        if (act >= kActJump0 && act < kActJump0 + 2) { fKnobJump = act == kActJump0 + 1; Prefs up; up.kjump = fKnobJump ? 1 : 0; writePrefs(up); bumpController(); return; }
        if (act >= kActNote0 && act < kActNote0 + 10) { setNoteStart((act - kActNote0) * 12); return; }
        const int slot = kind == kMenuLearn ? fMenuSlot : target;
        switch (act) {
        case kActReset: {
            pushUndo();
            const float def = knobDefault(target);
            setParam(target, def, true);
            fKnobDisp[target].set(normOfParam(target, def));
            break;
        }
        case kActLearn: startLearn(target); break;
        case kActCancelLearn: cancelLearn(); break;
        case kActClear: fMap[target] = -1; sendMidiMap(); break;
        case kActNewPattern: {
            // a new set of steps for Stutter / Reverse, like rolling the dice in Valens; kept with the pad
            const int s = fMenuSlot;
            if (s < 0) break;
            pushUndo();
            const float seed = (float)(1 + (int)(std::fmod(now() * 7919.0, 65534.0))) / 65535.f;
            setParam(slotParam(s, F_K5), seed, true);
            showToast("New pattern for this pad.");
            wake();
            break;
        }
        case kActLoadSmp: browseForPad(slot); break;
        case kActLoadBeat: browseForPad(slot, true); break;
        case kActRemoveBeat: pushUndo(); setBeatState(slot, nullptr); break;
        case kActRelink: fRelinkSlot = slot; browseForPad(slot, true); break;
        case kActCollect: collectBeats(); break;
        case kActRemoveSmp: pushUndo(); removeSample(slot); break;
        case kActSavePadPreset: startNaming(slot); break;
        case kActLoadPadPresetMenu: fPadPresetPage = 0; openPadPresetMenu(slot, fMouseX, fMouseY); break;
        case kActNotesOff: setNoteStart(-1); break;
        case kActOrderTop: setNoteOrder(0); break;
        case kActOrderBottom: setNoteOrder(1); break;
        case kActSetup: openSetup(computerHasSetup() ? kScrHome : kScrHear); break;
        case kActKnobTypeMenu: openKnobTypeMenu(); break;
        case kActNotesMenu: openNotesMenu(gearCX() - 14.f, kNotesY + kNotesH + 8.f); break;
        case kActAddPage: setPagesShown(pagesShown() + 1); showPage(pagesShown() - 1); break;
        case kActRemovePage: setPagesShown(pagesShown() - 1); if (fPage >= pagesShown()) showPage(pagesShown() - 1); break;
        case kActResetSize: resetWindowSize(); break;
        case kActResetPads: openSetup(kScrConfirmPads); break;
        case kActFactory: openSetup(kScrConfirmFactory); break;
        case kActSaveSet: saveSet(fPresetName); break;
        case kActSaveSetAs: fRenaming = false; startNaming(-1); break;
        case kActRenameSet: fRenaming = true; startNaming(-1); break;
        case kActDeleteSet: deleteCurrentSet(); break;
        default: break;
        }
    }

    // --------------------------------------------------------------------------------------------
    // Input

    bool onMouse(const MouseEvent& ev) override
    {
        const float sc = (float)getWidth() / kW;
        const float mx = (float)ev.pos.getX() / sc, my = (float)ev.pos.getY() / sc;
        fMouseX = mx; fMouseY = my;

        if (fMenuOpen) {
            if (ev.press) {
                const int item = menuItemAt(mx, my);
                fMenuOpen = false;
                if (item >= 0 && ev.button == kMouseButtonLeft) runMenu(item);
                wake();
            }
            return true;
        }
        if (fPanelOpen) {
            if (ev.press && ev.button == kMouseButtonLeft) panelClick(mx - kPanX, my - kPanY);
            return true;
        }

        if (ev.button == kMouseButtonRight && ev.press) {
            int q, param, k;
            if (hitGridPad(mx, my, q)) {
                const int s = slotAtGrid(q);
                if (s >= 0) selectSlot(s);
                openLearnMenu(kLearnPad0 + (s >= 0 ? padOfSlot(s) : kKillPad), s, mx, my);
                return true;
            }
            if (hitChip(mx, my, k)) { openLearnMenu(kLearnKnob0 + k, -1, mx, my); return true; }
            if (hitKnob(mx, my, param)) { openKnobMenu(param, mx, my); return true; }
            if (my >= kTabY && my <= kTabY + kTabH)
                for (int pg = 0; pg < pagesShown(); ++pg)
                    if (mx >= pageTabX(pg) && mx <= pageTabX(pg) + 44.f) { openLearnMenu(kLearnBank0 + pg, -1, mx, my); return true; }
            return false;
        }
        if (ev.button != kMouseButtonLeft) return false;

        if (ev.press) {
            if (mx >= kAllX && mx <= kAllX + kAllW && my >= kAllY && my <= kAllY + kAllH) { allOff(); return true; }
            if (mx >= kLimX - 6.f && mx <= kLimX + kLimW + 6.f && my >= kLimY - 4.f && my <= kCapY + 6.f) { toggleParam(pLimit); return true; }
            if (mx >= kTimX && mx <= kTimX + kTimW && my >= kTimY && my <= kTimY + kTimH) {
                const float v = mx < kTimX + kTimW * 0.5f ? 0.f : 1.f;
                setParam(pTiming, v, true); fTimingT.set(v); fTimingSetAt = now(); wake();
                return true;
            }
            if (inCircle(mx, my, kUndoCX, kHdrCY, kUndoR + 3.f)) { fUndoPress.x = 0.9f; fUndoPress.set(1.f); undo(); return true; }
            if (inCircle(mx, my, kRedoCX, kHdrCY, kUndoR + 3.f)) { fRedoPress.x = 0.9f; fRedoPress.set(1.f); redo(); return true; }
            if (my >= kPresetY && my <= kPresetY + kPresetH && mx >= kPresetX && mx <= kPresetX + kPresetW) {
                if (mx < kPresetX + 32.f) stepSet(-1);
                else if (mx > kPresetX + kPresetW - 32.f) stepSet(1);
                else {
                    refreshSetList();
                    fSetPage = 0;
                    if (fPresetUser) for (size_t i = 0; i < fUserSets.size(); ++i) if (fUserSets[i] == fPresetName) fSetPage = (int)i / 10;
                    openSetsMenu();
                }
                return true;
            }
            if (hitNotes(mx, my)) { if (fNoteStart < 0) setNoteStart(36); else openNotesMenu(kNotesX, kNotesY + kNotesH + 6.f); return true; }
            if (hitGear(mx, my)) { openSettingsMenu(); return true; }
            if (hitMoon(mx, my)) {
                fDark = !fDark; fDarkT.set(fDark ? 1.f : 0.f);
                setState("dark", fDark ? "1" : "0");
                Prefs up; up.dark = fDark ? 1 : 0; writePrefs(up);
                wake(); return true;
            }
            if (hitCtrlPill(mx, my)) { openSetup(computerHasSetup() ? kScrHome : kScrHear); return true; }
            if (my >= kTabY && my <= kTabY + kTabH) {
                const int np = pagesShown();
                for (int pg = 0; pg < np; ++pg) if (mx >= pageTabX(pg) && mx <= pageTabX(pg) + 44.f) { showPage(pg); return true; }
                if (np < kNumPages && mx >= pageTabX(np) && mx <= pageTabX(np) + 70.f) { setPagesShown(np + 1); showPage(np); return true; }
            }
            int param;
            if (hitKnob(mx, my, param)) { beginKnobDrag(param, my, (ev.mod & kModifierShift) != 0); return true; }

            int q;
            if (hitGridPad(mx, my, q)) {
                const int s = slotAtGrid(q);
                if (s < 0) { pressPadParam(kKillPad); fPadScale[kKillPad].set(0.95f); fPressedVisual = kKillPad; return true; }
                const float px = fPadPX[q].x, py = fPadPY[q].x;
                if (inCircle(mx - px, my - py, kPadW - 19.f, 19.f, 13.f)) { selectSlot(s); openTypeMenu(s, px + kPadW - 30.f, py + 32.f, false); return true; }
                if (onEmptyPlus(s, px, py, mx, my)) { fPlusHover = -1; selectSlot(s); openTypeMenu(s, px + kPadW * 0.5f, py + kPadH * 0.5f, false); return true; }
                selectSlot(s);
                fDragPos = q; fPadDragging = false;
                fDragStartX = mx; fDragStartY2 = my;
                fDragGrabX = mx - px; fDragGrabY = my - py;
                fPadScale[padOfSlot(s)].set(0.95f); fPressedVisual = padOfSlot(s);
                wake();
                return true;
            }
            if (hitDetail(mx, my)) { detailClick(mx - kDetX, my - kDetY); return true; }
            return false;
        }

        // release
        if (fHeldPadParam >= 0) releasePadParam();
        if (fPressedVisual >= 0) { fPadScale[fPressedVisual].set(1.f); fPadScale[fPressedVisual].v = 2.f; fPressedVisual = -1; }
        if (fDragKnob >= 0) { endKnobDrag(); return true; }
        if (fDragPos >= 0) {
            if (fPadDragging) {
                const int q = gridPosOf(fPosSlot[fDragPos]);
                fLift[slotAtPagePos(fPosSlot[fDragPos])].set(0.f);
                (void)q;
                commitOrder();
            }
            fDragPos = -1; fPadDragging = false;
            wake();
            return true;
        }
        return true;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        const float sc = (float)getWidth() / kW;
        const float mx = (float)ev.pos.getX() / sc, my = (float)ev.pos.getY() / sc;
        const float dxm = mx - fMouseX;
        fMouseX = mx; fMouseY = my;

        if (fMenuOpen) { const int h = menuItemAt(mx, my); if (h != fMenuHover) { fMenuHover = h; wake(); } return true; }

        if (fDragKnob >= 0) {
            const bool fine = (ev.mod & kModifierShift) != 0;
            if (fine != fDragFine) { fDragStartY = my; fDragStartNorm = fDragNorm; fDragFine = fine; }
            fDragNorm = clamp01(fDragStartNorm + (fDragStartY - my) / (fine ? 900.f : 220.f));
            const float n = jobSnap(fDragKnob, fDragNorm, fParam);
            const float v = valueOfNorm(fDragKnob, n);
            if (v != fParam[fDragKnob]) { fParam[fDragKnob] = v; setParameterValue(fDragKnob, v); }
            fKnobDisp[fDragKnob].set(n);
            wake();
            return true;
        }

        if (fDragPos >= 0) {
            const int dragged = fPosSlot[fDragPos];          // the page-local pad being carried
            const int s = slotAtPagePos(dragged);
            if (!fPadDragging && (std::fabs(mx - fDragStartX) > 6.f || std::fabs(my - fDragStartY2) > 6.f)) {
                fPadDragging = true; fLift[s].set(1.f);
                if (fPressedVisual >= 0) { fPadScale[fPressedVisual].set(1.f); fPressedVisual = -1; }
            }
            if (fPadDragging) {
                fDragTilt = dxm > 0.5f ? 1.f : (dxm < -0.5f ? -1.f : fDragTilt * 0.8f);
                const float x = clampf(mx - fDragGrabX, kTrayX - 20.f, kTrayX + kTrayW - kPadW + 20.f);
                const float y = clampf(my - fDragGrabY, kTrayY - 20.f, kTrayY + kTrayH - kPadH + 10.f);
                const int cur = fDragPos;
                fPadPX[cur].x = x; fPadPX[cur].v = 0.f; fPadPX[cur].target = x;
                fPadPY[cur].x = y; fPadPY[cur].v = 0.f; fPadPY[cur].target = y;
                const int col = clampi((int)std::lround((x - kPadX0) / kPadSX), 0, 3);
                const int row = clampi((int)std::lround((y - kPadY0) / kPadSY), 0, 3);
                int to = row * 4 + col;
                if (fPage == 0 && to == kKillPad) to = kKillPad - 1;
                if (to != cur) {
                    // carry the pad to its new spot; everything between shifts along
                    const int n = kPadsPerPage;
                    int order[kPadsPerPage], k = 0;
                    for (int i = 0; i < n; ++i) if (i != cur) order[k++] = fPosSlot[i];
                    Spring px[kPadsPerPage], py[kPadsPerPage];
                    int from[kPadsPerPage];
                    for (int i = 0; i < n; ++i) from[i] = (i == to) ? cur : -1;
                    k = 0;
                    int newPos[kPadsPerPage];
                    for (int i = 0; i < n; ++i) newPos[i] = (i == to) ? dragged : order[k++];
                    for (int i = 0; i < n; ++i) { px[i] = fPadPX[i]; py[i] = fPadPY[i]; }
                    // springs follow their pads
                    for (int i = 0; i < n; ++i) {
                        int old = -1;
                        for (int j = 0; j < n; ++j) if (fPosSlot[j] == newPos[i]) old = j;
                        fPadPX[i] = px[old]; fPadPY[i] = py[old];
                    }
                    for (int i = 0; i < n; ++i) fPosSlot[i] = newPos[i];
                    for (int i = 0; i < n; ++i) if (i != to) { fPadPX[i].set(padX(i)); fPadPY[i].set(padY(i)); }
                    fDragPos = to;
                    (void)from;
                }
                wake();
            }
            return true;
        }
        int hq = -1, q;
        if (!fPanelOpen && hitGridPad(mx, my, q)) { const int s = slotAtGrid(q); if (onEmptyPlus(s, fPadPX[q].x, fPadPY[q].x, mx, my)) hq = posDisplay(q); }
        if (hq != fPlusHover) { fPlusHover = hq; wake(); }
        return false;
    }
    // the + in the middle of an empty pad (bank B and C)
    static constexpr float kPlusR = 20.f;
    bool onEmptyPlus(int s, float px, float py, float mx, float my) const
    {
        return s >= 0 && kindOf(s) == 4 && inCircle(mx - px, my - py, kPadW * 0.5f, kPadH * 0.5f + 1.f, kPlusR);
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        if (fPanelOpen || fMenuOpen) return true;
        const float sc = (float)getWidth() / kW;
        const float mx = (float)ev.pos.getX() / sc, my = (float)ev.pos.getY() / sc;
        const float dy = (float)ev.delta.getY();
        if (hitNotes(mx, my) && fNoteStart >= 0) { setNoteStart((int)clampf((float)(fNoteStart + (dy > 0 ? 1 : -1)), 0.f, 112.f)); return true; }
        int param;
        if (!hitKnob(mx, my, param)) return false;
        if (now() - fScrollUndoAt > 1.0) pushUndo();
        fScrollUndoAt = now();
        float n = normOfParam(param, fParam[param]);
        bool stepped = param == pThrowTime;
        const bool slotFx = isSlotKnob(param) && effectAt(slotOf(param)) >= 0;
        if (slotFx) stepped = kEffects[effectAt(slotOf(param))].c[fieldOf(param) - F_K1].scale == kInt;
        if (stepped) {
            if (slotFx) {
                const CtlSpec& c = kEffects[effectAt(slotOf(param))].c[fieldOf(param) - F_K1];
                n = ctlToNorm(c, clampf(ctlFromNorm(c, n) + (dy > 0 ? 1.f : -1.f), c.min, c.max));
            } else n = clamp01(n + (dy > 0 ? 1.f : -1.f) / 12.f);
        } else n = clamp01(n + dy * 0.02f);
        const float v = valueOfNorm(param, jobSnap(param, n, fParam));
        setParam(param, v, true);
        fPop[param].x = 1.f; fPop[param].set(0.f);
        fKnobDisp[param].set(normOfParam(param, v));
        wake();
        return true;
    }

    // typing a preset name
    bool onKeyboard(const KeyboardEvent& ev) override
    {
        if (!fNaming || !ev.press) return false;
        if (ev.key == kKeyEscape) { closeSetup(); return true; }
        if (ev.key == kKeyEnter) { finishNaming(); return true; }
        if (ev.key == kKeyBackspace) { if (fNameFresh) fNameBuf[0] = '\0'; fNameFresh = false; const size_t n = std::strlen(fNameBuf); if (n) fNameBuf[n - 1] = '\0'; wake(); return true; }
        return true;
    }
    bool onCharacterInput(const CharacterInputEvent& ev) override
    {
        if (!fNaming) return false;
        const unsigned c = ev.character;
        if (c >= 32 && c < 127 && c != '/' && c != '\\' && c != ':' && c != '*' && c != '?' && c != '"' && c != '<' && c != '>' && c != '|') {
            if (fBpmEdit >= 0 && !((c >= '0' && c <= '9') || c == '.')) { wake(); return true; }
            if (fNameFresh) { fNameBuf[0] = '\0'; fNameFresh = false; }     // typing replaces the suggested name
            const size_t n = std::strlen(fNameBuf);
            if (n + 1 < sizeof(fNameBuf) && n < 32) { fNameBuf[n] = (char)c; fNameBuf[n + 1] = '\0'; }
        }
        wake();
        return true;
    }

    void detailClick(float lx, float ly)
    {
        const int s = fSel;
        const int kind = kindOf(s);
        if (lx >= 70.f && lx <= 70.f + fTypeChipW && ly >= 15.f && ly <= 45.f) { openTypeMenu(s, kDetX + 70.f, kDetY + 46.f, true); return; }
        if (kind == 4 && ly >= kEmptyBtnY && ly <= kEmptyBtnY + 34.f) {
            if (lx >= kEmptyBtnX[0] && lx <= kEmptyBtnX[0] + 150.f) { openTypeMenu(s, kDetX + kEmptyBtnX[0], kDetY + kEmptyBtnY + 40.f, true); return; }
            if (lx >= kEmptyBtnX[1] && lx <= kEmptyBtnX[1] + 150.f) { browseForPad(s); return; }
            if (lx >= kEmptyBtnX[2] && lx <= kEmptyBtnX[2] + 150.f) { browseForPad(s, true); return; }
        }
        if (kind == 4) return;
        if (ly >= kSegY && ly <= kSegY + kSegH) {
            const bool holdable = kind == 0 || (kind == 1 && moveIsHold(moveAt(s)));
            if (holdable && lx >= 20.f && lx <= 20.f + kSegW) { pushUndo(); setParam(slotParam(s, F_LATCH), lx < 20.f + kSegW * 0.5f ? 0.f : 1.f, true); wake(); return; }
            if (kind == 0 && kEffects[effectAt(s)].tail && lx >= 176.f && lx <= 326.f) { pushUndo(); setParam(slotParam(s, F_REL), lx < 251.f ? 0.f : 1.f, true); wake(); return; }
            if (kind == 0 && effectAt(s) == kFxDjLooper && lx >= 176.f && lx <= 286.f) { pushUndo(); setParam(slotParam(s, F_SNAP), lx < 231.f ? 0.f : 1.f, true); wake(); return; }
            if (kind == 2 && lx >= 20.f && lx <= 140.f) { browseForPad(s); return; }
            if (kind == 2 && lx >= 150.f && lx <= 260.f) { pushUndo(); removeSample(s); return; }
            if (kind == 3 && lx >= 20.f && lx <= 140.f) { if (fBeatMissing[s]) fRelinkSlot = s; browseForPad(s, true); return; }
            if (kind == 3 && lx >= 150.f && lx <= 260.f) { pushUndo(); setBeatState(s, nullptr); return; }
        }
        if (kind == 3 && ly >= kBeatRowY && ly <= kBeatRowY + 26.f) {
            if (lx >= 20.f && lx <= 132.f) { startBpmEdit(s); return; }
            if (lx >= 138.f && lx <= 182.f) { setBeatBpm(s, fBeatInfo[s].bpm * 0.5f); return; }
            if (lx >= 188.f && lx <= 232.f) { setBeatBpm(s, fBeatInfo[s].bpm * 2.f); return; }
            if (lx >= 242.f && lx <= 430.f) { fBeatMode = clampi((int)((lx - 242.f) / (188.f / 3.f)), 0, 2); char b[4]; std::snprintf(b, sizeof(b), "%d", fBeatMode); setState("beatmode", b); fPresetDirty = true; wake(); return; }
        }
        const float dx = lx - kRoundCX, dy = ly - kRoundCY;
        if (dx * dx + dy * dy <= (kRoundR + 5.f) * (kRoundR + 5.f)) {
            const int p = padOfSlot(s);
            pressPadParam(p);
            fPadScale[p].set(0.94f); fPressedVisual = p;
        }
    }

    void panelClick(float lx, float ly)
    {
        const float w = kPanW, h = kPanH;
        if ((lx - (w - 36.f)) * (lx - (w - 36.f)) + (ly - 36.f) * (ly - 36.f) <= 18.f * 18.f) { closeSetup(); return; }
        if (lx < 0.f || lx > w || ly < 0.f || ly > h) { if (!fNaming) closeSetup(); return; }
        if (fScr == kScrName && ly >= 205.f && ly <= 263.f) { fKbPrev = takeKeyboard(); wake(); return; }
        if (fScr == kScrHome && ly >= 296.f && ly <= 326.f && lx >= 24.f && lx <= w - 24.f) {
            setKnobMode(clampi((int)((lx - 24.f) / ((w - 48.f) / (float)kNumKnobModes)), 0, kNumKnobModes - 1));
            return;
        }
        if (fScr == kScrHome && ly >= 384.f && ly <= 412.f && lx >= 24.f && lx <= w - 24.f) {
            fKnobCountSet = clampi((int)((lx - 24.f) / ((w - 48.f) / 6.f)), 0, 5);
            Prefs up; up.kcount = fKnobCountSet; writePrefs(up); bumpController(); wake();
            return;
        }
        PanelBtn bt[8];
        const int n = panelButtons(bt);
        for (int i = 0; i < n; ++i) {
            const PanelBtn& b = bt[i];
            if (lx < b.x - 4.f || lx > b.x + b.w + 4.f || ly < b.y - 4.f || ly > b.y + b.h + 4.f) continue;
            switch (b.id) {
            case bSetupAgain: chooseController(); break;
            case bPadCtl: fKeyMode = false; startHear(); break;
            case bKeyboard: fKeyMode = true; startHear(); break;
            case bKeysManual:
                // start this bank from blank, so the keyboard shows only the keys you tap
                for (int q = 0; q < kPadsPerPage; ++q) fMap[kLearnPad0 + fKeyPage * kPadsPerPage + q] = -1;
                fKeyStart[fKeyPage] = -1;
                sendMidiMap();
                fKeyManual = true; fWizStep = fKeyPage * kPadsPerPage; fWizLastCode = -1;
                startLearn(fWizStep, false);
                break;
            case bKeysAuto: fKeyManual = false; fWizStep = fKeyPage * kPadsPerPage; startLearn(fWizStep, false); break;
            case bKeysRepick: fWizStep = fKeyPage * kPadsPerPage; startLearn(fWizStep, false); break;
            case bKeysNext: keysPageDone(); break;
            case bClearAll: for (int k = 0; k < kNumLearn; ++k) fMap[k] = -1; sendMidiMap(); break;
            case bDone: closeSetup(); break;
            case bMyself: markWelcomed(); closeSetup(); break;
            case bUseBackup:
                for (int k = 0; k < kNumLearn; ++k) fMap[k] = fProjectMap[k];
                sendMidiMap(); markWelcomed(); fScr = kScrDone;
                break;
            case bFresh: chooseController(); break;
            case bSkip:
                if (fScr == kScrHear) startMapping();
                else if (fScr == kScrKeys) keysNextPad();
                else wizardTo(fWizStep + 1);
                break;
            case bBack: {
                if (fScr == kScrSameBank) { fScr = kScrPads; wizardTo(fFillPage * kPadsPerPage + fQuickRef); break; }
                if (fScr == kScrPads && fBankQuick) { fMorePage = fWizStep / kPadsPerPage; cancelLearn(); fScr = kScrMorePads; fWizStep = -1; fBankQuick = false; break; }
                if (fScr == kScrKeys) {
                    fWizStep = std::max(fKeyPage * kPadsPerPage, fWizStep - 1);
                    startLearn(fWizStep, false);
                    break;
                }
                const int lo = fScr == kScrPads ? (fWizStep / kPadsPerPage) * kPadsPerPage : kLearnKnob0;
                wizardTo(std::max(lo, fWizStep - 1));
                break;
            }
            case bOutPads: endOfPadPage(true); break;
            case bTapAll: {                     // map this bank pad by pad after all
                const int pg = (fScr == kScrBankFilled || fScr == kScrSameBank) ? fFillPage : std::max(1, fWizStep / kPadsPerPage);
                fBankQuick = false; fScr = kScrPads; fWizLastCode = -1;
                fWizStep = pg * kPadsPerPage;
                wizardTo(pg * kPadsPerPage);
                break;
            }
            case bFollowYes: setFollow(true); startKnobs(); break;
            case bMorePads: {
                if (fScr == kScrBankFilled) fMorePage = fFillPage + 1;
                const int pg = fMorePage;
                if (fKeyMode) { startKeys(pg); break; }
                fScr = kScrPads; fWizLastCode = -1;
                setPagesShown(std::max(pagesShown(), pg + 1));
                // with bank A mapped, one press fills the bank: ask for the pad at bank A's first mapped spot
                int ref = -1;
                for (int q = 0; q < kPadsPerPage && ref < 0; ++q) if (fMap[kLearnPad0 + q] >= 0) ref = q;
                fBankQuick = ref >= 0; fQuickRef = std::max(0, ref); fFillPage = pg;
                fWizStep = pg * kPadsPerPage + fQuickRef;          // the walkthrough is now on this page
                wizardTo(pg * kPadsPerPage + fQuickRef);
                break;
            }
            case bNoMorePads: startKnobs(); break;
            case bOutKnobs: finishSetup(); break;
            case bStartPlaying: closeSetup(); break;
            case bCancel: closeSetup(); break;
            case bSaveName: finishNaming(); break;
            case bWinDialog: {
                const bool pad = fNameForPad >= 0;
                const std::string nm = saveNameDialog(fNativeWin, presetsDir(pad ? "Pads" : "Sets").c_str(), fNameBuf, pad ? ".ypad" : ".yset",
                                                      pad ? "Save pad preset" : "Save set");
                if (!nm.empty()) { std::snprintf(fNameBuf, sizeof(fNameBuf), "%s", nm.c_str()); fNameFresh = false; fNameConfirmed = true; finishNaming(); }
                break;
            }
            case bConfirm:
                if (fScr == kScrConfirmReplace) { saveNamed(fReplaceName); closeSetup(); }
                else if (fScr == kScrConfirmPads) { resetProjectPads(); closeSetup(); }
                else factoryReset();
                break;
            default: break;
            }
            wake();
            return;
        }
    }

    // --------------------------------------------------------------------------------------------
    // Actions

    void setParam(int i, float v, bool gesture)
    {
        fParam[i] = v;
        if (gesture) editParameter(i, true);
        setParameterValue(i, v);
        if (gesture) editParameter(i, false);
        if (isSlotParam(i)) fPresetDirty = true;
    }

    void toggleParam(int i) { setParam(i, fParam[i] > 0.5f ? 0.f : 1.f, true); if (i == pLimit) fLimitT.set(fParam[i]); wake(); }

    void pressButtonParam(int p)
    {
        if (fHeldPadParam >= 0) releasePadParam();
        fHeldPadParam = p;
        editParameter(p, true);
        setParameterValue(p, 1.f);
        fParam[p] = 1.f;
        wake();
    }
    void pressPadParam(int pad) { pressButtonParam(pPad0 + pad); }
    void releasePadParam()
    {
        const int p = fHeldPadParam;
        fHeldPadParam = -1;
        setParameterValue(p, 0.f);
        editParameter(p, false);
        fParam[p] = 0.f;
        wake();
    }

    void allOff()
    {
        fAllPress.x = 1.f; fAllPress.set(0.f);
        char buf[24]; std::snprintf(buf, sizeof(buf), "%d", ++fAllOffCount);
        setState("alloff", buf);
        wake();
    }

    float knobDefault(int param) const
    {
        if (isSlotKnob(param)) { const int e = effectAt(slotOf(param)); return e >= 0 ? defaultNorm(e, fieldOf(param) - F_K1) : 0.f; }
        if (isSlotParam(param) && fieldOf(param) == F_VOL) return kPadVolDefault;
        return paramDefault(param);
    }

    void beginKnobDrag(int param, float my, bool fine)
    {
        const double t = now();
        pushUndo();
        if (param == fLastClickParam && t - fLastClickTime < 0.32) {
            const float def = knobDefault(param);
            setParam(param, def, true);
            fKnobDisp[param].set(normOfParam(param, def));
            fPop[param].x = 1.f; fPop[param].set(0.f);
            fLastClickParam = -1;
            wake();
            return;
        }
        fLastClickParam = param; fLastClickTime = t;
        fDragKnob = param; fDragStartY = my; fDragFine = fine;
        fDragStartNorm = fDragNorm = normOfParam(param, fParam[param]);
        fPop[param].set(1.f);
        editParameter(param, true);
        wake();
    }

    void endKnobDrag()
    {
        const int p = fDragKnob;
        editParameter(p, false);
        fPop[p].set(0.f);
        fKnobDisp[p].set(normOfParam(p, fParam[p]));
        fDragKnob = -1;
        if (isSlotParam(p)) fPresetDirty = true;
        wake();
    }

    // the pad your knobs control (and the big card shows)
    void selectSlot(int s)
    {
        if (s < 0 || s >= kNumSlots) return;
        fSel = s;
        setParameterValue(pSelect, (float)s);
        fParam[pSelect] = (float)s;
        wake();
    }
    void showPage(int pg)
    {
        pg = std::max(0, std::min(kNumPages - 1, pg));
        if (pg == fPage) return;
        fPageSlide.x = pg > fPage ? 1.f : -1.f; fPageSlide.v = 0.f; fPageSlide.set(0.f);   // in from the side you went to
        fPage = pg;
        if (fFollow || bankButtonsMapped()) sendBank();          // the plug-in plays (or reports) this bank
        for (int q = 0; q < kPadsPerPage; ++q) { fPosSlot[q] = q; fPadPX[q].snap(padX(q)); fPadPY[q].snap(padY(q)); }
        if (pageOfSlot(fSel) != pg) selectSlot(slotOfPad(pg * kPadsPerPage + (pg == 0 ? 2 : 0)));
        wake();
    }
    void setPagesShown(int n)
    {
        fPagesShown = std::max(1, std::min((int)kNumPages, n));
        char b[8]; std::snprintf(b, sizeof(b), "%d", fPagesShown);
        setState("pages", b);
        wake();
    }

    // put an effect or a move on a pad; each pad keeps its settings for every effect it has had
    void setSlotType(int s, int t2)
    {
        pushUndo();
        if (fSmpLoaded[s]) removeSample(s);
        if (fBeatHas[s]) setBeatState(s, nullptr);
        const int t1 = typeOf(fParam, s);
        if (t1 == t2) return;
        const int e1 = effectOfType(t1), e2 = effectOfType(t2);
        if (e1 >= 0) {
            for (int k = 0; k < kKnobsPerSlot; ++k) fMem[s][e1][k] = fParam[slotParam(s, F_K1 + k)];
            fMemValid[s][e1] = true;
        }
        setParam(slotParam(s, F_TYPE), (float)t2, true);
        if (e2 >= 0)
            for (int k = 0; k < kKnobsPerSlot; ++k) {
                const float v = fMemValid[s][e2] ? fMem[s][e2][k] : defaultNorm(e2, k);
                const int pid = slotParam(s, F_K1 + k);
                setParam(pid, v, true);
                fKnobDisp[pid].snap(v);
            }
        sendMem();
        wake();
    }

    // ------------------------------------------------------------------------------ this computer's setup

    void bumpController() { char b[32]; std::snprintf(b, sizeof(b), "%.4f", now()); setState("ctlmap", b); }

    void setNoteStart(int n)
    {
        fNoteStart = (n >= 0 && n <= 112) ? n : -1;
        fNotesT.set(fNoteStart >= 0 ? 1.f : 0.f);
        Prefs up; up.notes = fNoteStart; writePrefs(up);
        bumpController();
        wake();
    }
    void setNoteOrder(int o)
    {
        fNoteOrder = o;
        Prefs up; up.norder = o; writePrefs(up);
        if (fNoteStart < 0) setNoteStart(36); else bumpController();
        wake();
    }
    void setKnobMode(int m)
    {
        fKnobMode = m;
        Prefs up; up.kmode = m; writePrefs(up);
        bumpController();
        wake();
    }
    // typing goes into the name box, not to the DAW
    uintptr_t takeKeyboard()
    {
#ifdef _WIN32
        return grabKeyboard(fNativeWin);
#else
        getWindow().focus();        // Mac: makes the plug-in view the key view (the host's window stays in front)
        return 0;
#endif
    }
    void markWelcomed() { fWelcomed = true; Prefs up; up.welcomed = 1; writePrefs(up); }
    void showToast(const char* msg) { std::snprintf(fToast, sizeof(fToast), "%s", msg); fToastUntil = now() + 4.5; wake(); }

    void startLearn(int slot, bool guarded = false)
    {
        fLearning = slot;
        char buf[16]; std::snprintf(buf, sizeof(buf), guarded ? "%d:g" : "%d", slot);
        setState("learn", buf);
        wake();
    }
    void cancelLearn() { fLearning = -1; setState("learn", "-1"); wake(); }

    void learnedFromDsp()
    {
        const int packed = (int)std::lround(fParam[oLearned]);
        const int slot = packed / 262144 - 1;
        const int code = packed % 262144;
        if (slot >= 0 && slot < kNumLearn) {
            for (int k = 0; k < kNumLearn; ++k) if (fMap[k] == code) fMap[k] = -1;
            fMap[slot] = code;
            sendMidiMap(false);
            if (slot < kNumPads) { fPadScale[slot].x = 0.9f; fPadScale[slot].set(1.f); }
        }
        if (fPanelOpen && fScr == kScrPads && fBankQuick && slot == fWizStep) {
            fWizLastCode = code; fWizDup = -1;
            if (fillBank(slot / kPadsPerPage, code)) { cancelLearn(); fWizStep = -1; fScr = kScrBankFilled; return; }
            fBankQuick = false;                     // couldn't match bank A: carry on pad by pad from here
            fWizStep = slot - fQuickRef; wizardTo(slot - fQuickRef + (fQuickRef == 0 ? 1 : 0));
            return;
        }
        if (fPanelOpen && (fScr == kScrPads || fScr == kScrKnobs) && slot == fWizStep) {
            fWizLastCode = code; fWizDup = -1;
            wizardTo(fWizStep + 1);
            return;
        }
        if (fPanelOpen && fScr == kScrKeys && slot == fWizStep) {
            if (!(code & 0x10000)) {                 // a knob or button, not a key: ask again
                fMap[slot] = -1; sendMidiMap(false);
                fKeyNotKeyAt = now();
                startLearn(fWizStep, false);
                return;
            }
            fWizLastCode = code; fWizDup = -1; fKeyNotKeyAt = -10.0;
            if (fKeyManual) { if (slot == fKeyPage * kPadsPerPage) fKeyStart[fKeyPage] = code; keysNextPad(); return; }
            keysAutoFill(fKeyPage, code);
            cancelLearn();
            return;
        }
        fLearning = -1;
        setState("learn", "-1");
    }

    // ------------------------------------------------------------------------------ the setup card

    void openSetup(int scr)
    {
        if (fMenuOpen) fMenuOpen = false;
        fPanelOpen = true;
        if (scr == kScrHear) chooseController(); else fScr = scr;
        wake();
    }
    // the first setup step: pad controller or keyboard?
    void chooseController() { cancelLearn(); fScr = kScrChoose; fPanelOpen = true; for (int& k : fKeyStart) k = -1; }
    void startMapping() { if (fKeyMode) startKeys(0); else startPads(0); }
    // keyboard setup for one bank: later banks start on the 16 keys right after the bank before
    void startKeys(int pg)
    {
        fScr = kScrKeys; fKeyPage = pg; fKeyManual = false; fWizLastCode = -1; fWizDup = -1;
        showPage(pg);
        fWizStep = pg * kPadsPerPage;
        if (pg > 0 && fKeyStart[pg] < 0 && fKeyStart[pg - 1] >= 0 && (fKeyStart[pg - 1] & 0x7f) + 16 + 15 <= 127)
            keysAutoFill(pg, fKeyStart[pg - 1] + 16);
        if (fKeyStart[pg] < 0) startLearn(fWizStep, false);
        else cancelLearn();
    }
    // a bank on 16 keys in a row, white and black, from the given first key
    void keysAutoFill(int pg, int code)
    {
        const int base = code & ~0x7f, n0 = code & 0x7f;
        for (int q = 0; q < kPadsPerPage; ++q) {
            if (n0 + q > 127) continue;
            const int c = base | (n0 + q);
            for (int k = 0; k < kNumLearn; ++k) if (fMap[k] == c) fMap[k] = -1;
        }
        for (int q = 0; q < kPadsPerPage; ++q) fMap[kLearnPad0 + pg * kPadsPerPage + q] = n0 + q <= 127 ? (base | (n0 + q)) : -1;
        fKeyStart[pg] = code;
        sendMidiMap();
    }
    void keysNextPad()
    {
        const int nx = fWizStep + 1;
        if (nx >= (fKeyPage + 1) * kPadsPerPage) { cancelLearn(); return; }      // this bank is done
        fWizStep = nx;
        startLearn(nx, false);
    }
    void keysPageDone()
    {
        cancelLearn();
        if (fKeyPage + 1 < kNumPages) { fMorePage = fKeyPage + 1; fScr = kScrMorePads; fWizStep = -1; }
        else startKnobs();
    }
    void startHear() { fScr = kScrHear; fHeardAt = 0.0; fHearSeq0 = fMidiSeq; fHearStart = now(); fPanelOpen = true; }
    void startPads(int step) { fScr = kScrPads; fBankQuick = false; fWizLastCode = -1; fWizDup = -1; showPage(0); wizardTo(step); }
    void startKnobs() { fScr = kScrKnobs; fBankQuick = false; fWizLastCode = -1; wizardTo(kLearnKnob0); }
    // a page of pads is finished (or the user has run out of pads on it)
    void endOfPadPage(bool early)
    {
        const int pg = fWizStep / kPadsPerPage;
        if (pg == 0 && early && fWizStep < kKillPad) { wizardTo(kKillPad); return; }   // still ask for a KILL button
        cancelLearn();
        if (pg + 1 < kNumPages) { fMorePage = pg + 1; fScr = kScrMorePads; fWizStep = -1; }
        else startKnobs();
    }
    void wizardTo(int step)
    {
        fWizDup = -1;
        if (fScr == kScrPads) {
            const int pg = fWizStep >= 0 ? fWizStep / kPadsPerPage : 0;
            if (step >= (pg + 1) * kPadsPerPage) { fWizStep = step - 1; endOfPadPage(false); return; }
        }
        if (fScr == kScrKnobs && step >= kLearnBank0) { finishSetup(); return; }
        fWizStep = step;
        if (fScr == kScrPads && step < kNumPads) {
            if (step / kPadsPerPage != fPage) showPage(step / kPadsPerPage);
            if (slotOfPad(step) >= 0) selectSlot(slotOfPad(step));
        }
        startLearn(step, true);
    }
    void finishSetup() { cancelLearn(); fWizStep = -1; fScr = kScrDone; sendMidiMap(); markWelcomed(); }
    void closeSetup()
    {
        if (fNaming) { returnKeyboard(fNativeWin, fKbPrev); fNaming = false; }
        fBpmEdit = -1;
        if (fLearning >= 0) cancelLearn();
        if (fScr == kScrHear || fScr == kScrBackup || fScr == kScrPads || fScr == kScrMorePads || fScr == kScrKnobs
            || fScr == kScrChoose || fScr == kScrKeys || fScr == kScrBankFilled || fScr == kScrSameBank) { sendMidiMap(); markWelcomed(); }
        fBankQuick = false;
        fWizStep = -1;
        fNaming = false;
        fPanelOpen = false;
        wake();
    }

    // ------------------------------------------------------------------------------ samples

    void browseForPad(int s, bool beat = false)
    {
        fBrowsePad = s; fBrowseBeat = beat;
        FileBrowserOptions opts;
        opts.title = beat ? "Load a beat (WAV, AIFF, FLAC or MP3)" : "Load a sample (WAV, AIFF, FLAC or MP3)";
        openFileBrowser(opts);
    }
    void uiFileBrowserSelected(const char* filename) override
    {
        if (filename != nullptr && filename[0] != '\0' && fBrowsePad >= 0) {
            if (fRelinkSlot >= 0) relinkBeat(fRelinkSlot, filename);
            else { pushUndo(); loadAudio(fBrowsePad, filename, fBrowseBeat); }
        }
        fBrowsePad = -1; fRelinkSlot = -1; fBrowseBeat = false;
    }
    void setSampleState(int s, const std::shared_ptr<const std::string>& enc)
    {
        char key[8]; std::snprintf(key, sizeof(key), "smp%d", s);
        setState(key, enc ? enc->c_str() : "");
        std::string name; double rate = 0.0; int ch = 0, frames = 0;
        fSmpLoaded[s] = enc && peekSample(enc->c_str(), name, rate, ch, frames);
        fSmpName[s] = fSmpLoaded[s] ? name : std::string();
        fSmpDur[s] = fSmpLoaded[s] ? (float)(frames / rate) : 0.f;
        fSmpEnc[s] = fSmpLoaded[s] ? enc : nullptr;
        fPresetDirty = true;
    }
    void loadSample(int s, const char* path)
    {
        if (s < 0 || s >= kNumSlots) return;
        SampleData sd;
        std::string err;
        if (!decodeAudioFile(path, sd, err)) {
            fSmpError[s] = err; fSmpErrorUntil[s] = now() + 3.0;
            showToast(err.c_str());
            return;
        }
        if (fBeatHas[s]) setBeatState(s, nullptr);
        setSampleState(s, std::make_shared<const std::string>(encodeSample(sd)));
        fSmpErrorUntil[s] = 0.0;
        selectSlot(s);
        fPadScale[padOfSlot(s)].x = 0.9f; fPadScale[padOfSlot(s)].set(1.f);
        wake();
    }
    void removeSample(int s) { setSampleState(s, nullptr); wake(); }

    static void onFileDropped(void* ctx, const char* path, float fx, float fy)
    {
        YanceyUI* const ui = static_cast<YanceyUI*>(ctx);
        const float sc = (float)ui->getWidth() / kW;
        const float x = fx * kW, y = fy * (float)ui->getHeight() / sc;
        int q;
        if (ui->hitGridPad(x, y, q)) { const int s = ui->slotAtGrid(q); if (s >= 0) { ui->pushUndo(); ui->loadAudio(s, path, false); } }
        else if (ui->hitDetail(x, y)) { ui->pushUndo(); ui->loadAudio(ui->fSel, path, false); }
        ui->repaint();
    }


    // ------------------------------------------------------------------------------ beats

    static std::string baseName(const std::string& p)
    {
        const size_t sl = p.find_last_of("\\/");
        return sl == std::string::npos ? p : p.substr(sl + 1);
    }
    static std::string dirName(const std::string& p)
    {
        const size_t sl = p.find_last_of("\\/");
        return sl == std::string::npos ? std::string() : p.substr(0, sl);
    }
    static std::string stem(const std::string& file)
    {
        const size_t dot = file.find_last_of('.');
        return dot == std::string::npos ? file : file.substr(0, dot);
    }

    // a beat pad's saved note changed: remember it, and look for the file if it isn't where it was
    void applyBeatInfo(int k, const std::shared_ptr<const std::string>& enc, bool send)
    {
        BeatInfo bi;
        const bool has = enc && beatFromString(enc->c_str(), bi);
        if (has && !fileExists(bi.path.c_str())) {
            const std::string file = baseName(bi.path);
            const std::string tries[3] = { fLastBeatDir, presetsDir("Beats") + kPathSep + fPresetName, presetsDir("Beats") };
            for (const std::string& d : tries) {
                if (d.empty()) continue;
                const std::string cand = d + kPathSep + file;
                if (fileExists(cand.c_str())) { bi.path = cand; send = true; break; }
            }
        }
        fBeatHas[k] = has;
        fBeatInfo[k] = has ? bi : BeatInfo();
        fBeatMissing[k] = has && !fileExists(bi.path.c_str());
        fBeatEnc[k] = has ? (send ? std::make_shared<const std::string>(beatToString(bi)) : enc) : nullptr;
        if (send) { char key[8]; std::snprintf(key, sizeof(key), "beat%d", k); setState(key, fBeatEnc[k] ? fBeatEnc[k]->c_str() : ""); }
    }
    void setBeatState(int k, const std::shared_ptr<const std::string>& enc)
    {
        char key[8]; std::snprintf(key, sizeof(key), "beat%d", k);
        setState(key, enc ? enc->c_str() : "");
        applyBeatInfo(k, enc, false);
        fPresetDirty = true;
        wake();
    }
    void setBeatBpm(int k, float bpm)
    {
        if (!fBeatHas[k]) return;
        pushUndo();
        BeatInfo bi = fBeatInfo[k];
        bi.bpm = clampf(std::round(bpm * 100.f) / 100.f, 20.f, 400.f);
        setBeatState(k, std::make_shared<const std::string>(beatToString(bi)));
    }

    // load a file onto a pad: a one-shot if it's short, a beat if it's long (or if asked for a beat)
    void loadAudio(int s, const char* path, bool asBeat)
    {
        if (s < 0 || s >= kNumSlots) return;
        SampleData sd; std::string err; double full = 0.0;
        if (!decodeAudioFile(path, sd, err, kMaxBeatSeconds, &full)) {
            fSmpError[s] = err; fSmpErrorUntil[s] = now() + 3.0; showToast(err.c_str()); return;
        }
        if (!asBeat && full <= kMaxSampleSeconds + 0.001) { loadSample(s, path); return; }
        BeatInfo bi;
        bi.path = path;
        bi.name = stem(baseName(path));
        bi.len = (float)(sd.frames / sd.rate);
        float bpm = bpmFromName(bi.name);
        if (bpm <= 0.f) bpm = detectBpm(sd);
        bi.bpm = bpm > 0.f ? bpm : 90.f;
        fLastBeatDir = dirName(path);
        if (fSmpLoaded[s]) removeSample(s);
        setBeatState(s, std::make_shared<const std::string>(beatToString(bi)));
        selectSlot(s);
        fPadScale[padOfSlot(s)].x = 0.9f; fPadScale[padOfSlot(s)].set(1.f);
        char b[96]; std::snprintf(b, sizeof(b), "Beat loaded at %.1f BPM. Click the tempo in the card if it's off.", bi.bpm);
        showToast(b);
    }

    // point a missing beat at its new place, and fix any other missing beats from that folder
    void relinkBeat(int k, const char* path)
    {
        const std::string dir = dirName(path);
        fLastBeatDir = dir;
        BeatInfo bi = fBeatInfo[k]; bi.path = path;
        setBeatState(k, std::make_shared<const std::string>(beatToString(bi)));
        int fixed = 1;
        for (int j = 0; j < kNumSlots; ++j) {
            if (j == k || !fBeatHas[j] || !fBeatMissing[j]) continue;
            const std::string cand = dir + kPathSep + baseName(fBeatInfo[j].path);
            if (!fileExists(cand.c_str())) continue;
            BeatInfo bj = fBeatInfo[j]; bj.path = cand;
            setBeatState(j, std::make_shared<const std::string>(beatToString(bj)));
            ++fixed;
        }
        char b[64]; std::snprintf(b, sizeof(b), fixed > 1 ? "Found %d beats." : "Found it.", fixed); showToast(b);
    }

    // copy every beat in this set into one folder and point the pads there
    void collectBeats()
    {
        std::string safe;
        for (char c : std::string(fPresetName)) safe += (std::strchr("\\/:*?\"<>|", c) ? '_' : c);
        if (safe.empty()) safe = "Set";
        const std::string dir = presetsDir("Beats") + kPathSep + safe;
        makeDir(dir.c_str());
        int copied = 0, missing = 0;
        for (int k = 0; k < kNumSlots; ++k) {
            if (!fBeatHas[k]) continue;
            if (fBeatMissing[k]) { ++missing; continue; }
            const std::string dst = dir + kPathSep + baseName(fBeatInfo[k].path);
            if (dst != fBeatInfo[k].path && !copyFileTo(fBeatInfo[k].path.c_str(), dst.c_str())) { ++missing; continue; }
            BeatInfo bi = fBeatInfo[k]; bi.path = dst;
            setBeatState(k, std::make_shared<const std::string>(beatToString(bi)));
            ++copied;
        }
        char b[200];
        std::snprintf(b, sizeof(b), missing ? "Collected %d beats into your Beats folder, %s (%d missing)." : "Collected %d beats into your Beats folder, %s.", copied, safe.c_str(), missing);
        showToast(b);
    }

    void startBpmEdit(int s)
    {
        fBpmEdit = s;
        std::snprintf(fNameBuf, sizeof(fNameBuf), "%.1f", fBeatInfo[s].bpm);
        fNaming = true; fNameFresh = true;
        openSetup(kScrName);
        fKbPrev = takeKeyboard();
    }

    // ------------------------------------------------------------------------------ settings

    void resetWindowSize()
    {
        applyScaleFactor(getScaleFactor(), 1.0);
        wake();
    }

    // ---- window size (Mac Retina fix). DPF's own auto-scaling is OFF (the UI(...) third argument): DPF already
    // makes the window scale-factor times bigger when it creates it, and auto-scaling then resized it again, so a
    // 2x screen got 4x and clicks missed. We size the window in real pixels ourselves; drawing and the mouse both
    // work from getWidth() / kW, so they always match. fScale = the screen's scale factor; the user's own size is
    // a scale on top of it (1 = default), kept between 0.75x and 2x.
    double fScale = 1.0;
    static double clampUserScale(double u) { return u < 0.75 ? 0.75 : (u > 2.0 ? 2.0 : u); }
    double userScale() const { return getWidth() / (kW * fScale); }
    void applyScaleFactor(double sf, double user)
    {
        fScale = sf > 0.1 ? sf : 1.0;
        user = clampUserScale(user);
        // minimum size (about 0.75x, rounded to a whole multiple of the aspect ratio so DPF's keep-aspect doesn't
        // shave a pixel off), keep the aspect ratio, and NO automatic scaling (4th argument false)
        const uint dw = (uint)kW, dh = (uint)kH;
        uint a = dw, b = dh; while (b) { const uint t = a % b; a = b; b = t; }
        const uint uw = dw / a, uh = dh / a, n = (uint)std::ceil(dw * 0.75 * fScale / uw);
        setGeometryConstraints(n * uw, n * uh, true, false);
        setSize((uint)std::lround(kW * user * fScale), (uint)std::lround(kH * user * fScale));
    }
    void uiScaleFactorChanged(double sf) override   // the window moved to a screen with a different scale
    {
        const double u = userScale();
        applyScaleFactor(sf, u > 0.5 ? u : 1.0);
    }
    // the size saved by older builds is a pixel ratio (it includes the screen's scale)
    double savedUserScale(const Prefs& pr) const
    {
        if (pr.uscale > 0.f) return pr.uscale;
        if (pr.scale > 0.f) return pr.scale / fScale;
        return 1.0;
    }


    void resetProjectPads()
    {
        pushUndo();
        allOff();
        float vals[kNumSlots * kSlotFields];
        for (int i = 0; i < kNumSlots * kSlotFields; ++i) vals[i] = paramDefault(i);
        applyParams(vals);
        for (int s = 0; s < kNumSlots; ++s) { if (fSmpLoaded[s]) removeSample(s); if (fBeatHas[s]) setBeatState(s, nullptr); for (int e = 0; e < kNumEffects; ++e) fMemValid[s][e] = false; }
        sendMem();
        std::snprintf(fPresetName, sizeof(fPresetName), "%s", factorySetName(0));
        fPresetUser = false; fPresetDirty = false;
        setState("preset", fPresetName);
        wake();
    }

    void factoryReset()
    {
        clearPrefs();
        Prefs empty;
        readComputerSetup(empty);
        fWelcomed = false;
        for (int i = 0; i < kNumLearn; ++i) fProjectMap[i] = -1;
        setState("midimap", "");
        bumpController();
        fDark = false; fDarkT.set(0.f); setState("dark", "0");
        resetWindowSize();
        fSavedScale = 0.f;
        resetProjectPads();
        setPagesShown(1); showPage(0);
        chooseController();
        wake();
    }

    // apply a full set of slot parameters (only the ones that differ are sent)
    void applyParams(const float* vals)
    {
        for (int i = 0; i < kNumSlots * kSlotFields; ++i)
            if (fParam[i] != vals[i]) { setParam(i, vals[i], true); fKnobDisp[i].snap(normOfParam(i, vals[i])); }
    }

    // move pad contents (settings, memory, samples) to match the dragged order on this page
    void commitOrder()
    {
        const int n = kPadsPerPage;
        bool changed = false;
        for (int q = 0; q < n; ++q) changed |= (fPosSlot[q] != q);
        if (!changed) return;
        pushUndo();
        int perm[kNumSlots];
        for (int s = 0; s < kNumSlots; ++s) perm[s] = s;
        for (int q = 0; q < n; ++q) perm[slotAtPagePos(q)] = slotAtPagePos(fPosSlot[q]);
        {
            std::string ro; char b[16];
            for (int s = 0; s < kNumSlots; ++s) { std::snprintf(b, sizeof(b), "%d,", perm[s]); ro += b; }
            std::snprintf(b, sizeof(b), "%d", ++fReorderCount); ro += b;
            setState("reorder", ro.c_str());
        }

        float vals[kNumSlots][kSlotFields];
        static float mem[kNumSlots][kNumEffects][kKnobsPerSlot];
        bool memValid[kNumSlots][kNumEffects], active[kNumSlots];
        std::shared_ptr<const std::string> enc[kNumSlots], benc[kNumSlots];
        for (int s = 0; s < kNumSlots; ++s) {
            benc[s] = fBeatEnc[s];
            for (int f = 0; f < kSlotFields; ++f) vals[s][f] = fParam[slotParam(s, f)];
            std::memcpy(mem[s], fMem[s], sizeof(fMem[s]));
            std::memcpy(memValid[s], fMemValid[s], sizeof(fMemValid[s]));
            enc[s] = fSmpEnc[s]; active[s] = fActive[s];
        }
        for (int s = 0; s < kNumSlots; ++s) {
            const int q = perm[s];
            if (q == s) continue;
            for (int f = 0; f < kSlotFields; ++f) {
                const int pid = slotParam(s, f);
                if (fParam[pid] != vals[q][f]) setParam(pid, vals[q][f], true);
                fKnobDisp[pid].snap(normOfParam(pid, vals[q][f]));
            }
            std::memcpy(fMem[s], mem[q], sizeof(fMem[s]));
            std::memcpy(fMemValid[s], memValid[q], sizeof(fMemValid[s]));
            if (enc[s] != enc[q]) setSampleState(s, enc[q]);
            if (benc[s] != benc[q]) setBeatState(s, benc[q]);
            fActive[s] = active[q];
        }
        int inv[kNumSlots];
        for (int s = 0; s < kNumSlots; ++s) inv[perm[s]] = s;
        selectSlot(inv[fSel]);
        for (int q = 0; q < kPadsPerPage; ++q) { fPosSlot[q] = q; fPadPX[q].set(padX(q)); fPadPY[q].set(padY(q)); }
        sendMem();
    }

    // the map is this computer's controller setup; the project keeps a copy only as a backup
    // bank B or C from bank A, from the one pad just pressed (see bankFromA)
    bool fillBank(int pg, int code)
    {
        int out[kPadsPerPage], dCh = 0, dNum = 0;
        if (!bankFromA(fMap, pg, fQuickRef, code, out, dCh, dNum)) return false;
        for (int q = 0; q < kPadsPerPage; ++q) fMap[kLearnPad0 + pg * kPadsPerPage + q] = out[q];
        fFillPage = pg;
        int lo = 128, hi = -1;
        for (int q = 0; q < kPadsPerPage; ++q) if (out[q] >= 0) { lo = std::min(lo, out[q] & 0x7f); hi = std::max(hi, out[q] & 0x7f); }
        const bool cc = (code & 0x20000) != 0;
        const char* what = cc ? "controls" : "notes";
        const char bank = (char)('A' + pg);
        if (dCh == 0) std::snprintf(fFillText, sizeof(fFillText), "Your bank %c is bank A, %d %s %s (%s %d-%d).", bank, std::abs(dNum), dNum == 1 || dNum == -1 ? (cc ? "control" : "note") : what, dNum > 0 ? "higher" : "lower", cc ? "CC" : "notes", lo, hi);
        else if (dNum == 0) std::snprintf(fFillText, sizeof(fFillText), "Your bank %c is bank A on MIDI channel %d.", bank, ((code >> 8) & 15) + 1);
        else std::snprintf(fFillText, sizeof(fFillText), "Your bank %c is bank A on MIDI channel %d, %d %s %s.", bank, ((code >> 8) & 15) + 1, std::abs(dNum), what, dNum > 0 ? "higher" : "lower");
        sendMidiMap();                     // the plug-in learned only the one pad: send it the whole bank
        return true;
    }
    // bank A's pads play the bank on screen (controllers that send the same notes on every bank)
    void setFollow(bool on)
    {
        fFollow = on;
        Prefs up; up.follow = on ? 1 : 0; writePrefs(up);
        sendBank();
        bumpController();
        wake();
    }
    bool bankButtonsMapped() const { for (int b = 0; b < kNumPages; ++b) if (fMap[kLearnBank0 + b] >= 0) return true; return false; }
    void sendBank() { char b[4]; std::snprintf(b, sizeof(b), "%d", fPage); setState("bank", b); }

    // when the same effect is on more than one pad of a bank, what tells them apart (e.g. Loop Grab "1/16"):
    // the knobs that differ between them, choices (lengths, types, modes) first, at most two
    std::string padVariant(int s) const
    {
        const int e = effectAt(s);
        if (e < 0) return std::string();
        const int pg = s / kPadsPerPage;
        bool vary[kKnobsPerSlot] = {}, dup = false;
        for (int o = pg * kPadsPerPage; o < (pg + 1) * kPadsPerPage; ++o) {
            if (o == s || effectOf(fParam, o) != e) continue;
            dup = true;
            for (int k = 0; k < numControls(e); ++k)
                if (std::fabs(fParam[slotParam(o, F_K1 + k)] - fParam[slotParam(s, F_K1 + k)]) > 1e-3f) vary[k] = true;
        }
        if (!dup) return std::string();
        std::string out; int n = 0;
        if (e == kFxIsolator) {                    // the bands it changes: "LOW KILL", "HIGH -6.0 DB"
            for (int k = 0; k < 3; ++k) {
                const float v = fParam[slotParam(s, F_K1 + k)];
                if (std::fabs(v - 0.5f) < 1e-3f) continue;
                char b[32]; formatFmt(fBipolarDb, v, b, sizeof(b));
                std::string t = std::string(kEffects[e].c[k].label) + " " + b;
                for (char& ch : t) ch = (char)std::toupper((unsigned char)ch);
                out += (n++ ? " " : "") + t;
            }
            return out;
        }
        for (int pass = 0; pass < 2 && n < 2; ++pass)
            for (int k = 0; k < numControls(e) && n < 2; ++k) {
                const CtlSpec& c = kEffects[e].c[k];
                if (!vary[k] || (pass == 0) != (c.scale == kInt)) continue;
                char b[32]; formatFmt(c.fmt, ctlFromNorm(c, fParam[slotParam(s, F_K1 + k)]), b, sizeof(b));
                std::string t = c.fmt == fBipolarDb ? std::string(c.label) + " " + b : std::string(b);   // isolator: "LOW KILL"
                for (char& ch : t) ch = (char)std::toupper((unsigned char)ch);
                out += (n ? " " : "") + t; ++n;
            }
        return out;
    }

    void sendMidiMap(bool reload = true)
    {
        std::string buf; char b[16];
        for (int k = 0; k < kNumLearn; ++k) { std::snprintf(b, sizeof(b), k ? ",%d" : "%d", fMap[k]); buf += b; }
        Prefs up; up.map = buf; writePrefs(up);
        bool any = false;
        for (int k = 0; k < kNumLearn; ++k) any |= fMap[k] >= 0;
        if (any) { setState("midimap", buf.c_str()); parseList(buf.c_str(), fProjectMap, kNumLearn); }
        if (reload) bumpController();
        wake();
    }

    // memory format: pads separated by ';', entries "effect:k1,k2,k3,k4,k5" separated by '|'
    void sendMem()
    {
        std::string out; char b[96];
        for (int s = 0; s < kNumSlots; ++s) {
            if (s) out += ';';
            bool first = true;
            for (int e = 0; e < kNumEffects; ++e) {
                if (!fMemValid[s][e]) continue;
                std::snprintf(b, sizeof(b), "%s%d:%.4f,%.4f,%.4f,%.4f,%.4f", first ? "" : "|", e, fMem[s][e][0], fMem[s][e][1], fMem[s][e][2], fMem[s][e][3], fMem[s][e][4]);
                out += b; first = false;
            }
        }
        setState("mem", out.c_str());
    }
    void parseMem(const char* v)
    {
        for (int s = 0; s < kNumSlots; ++s) for (int e = 0; e < kNumEffects; ++e) fMemValid[s][e] = false;
        if (v == nullptr) return;
        int s = 0;
        const char* p = v;
        while (*p && s < kNumSlots) {
            if (*p == ';') { ++s; ++p; continue; }
            if (*p == '|') { ++p; continue; }
            char* end = nullptr;
            const long e = std::strtol(p, &end, 10);
            if (end == p || *end != ':') { ++p; continue; }
            p = end + 1;
            float vals[kKnobsPerSlot] = {};
            for (int k = 0; k < kKnobsPerSlot; ++k) { vals[k] = std::strtof(p, &end); if (end == p) break; p = end; if (*p == ',') ++p; }
            if (e >= 0 && e < kNumEffects) { for (int k = 0; k < kKnobsPerSlot; ++k) fMem[s][e][k] = clamp01(vals[k]); fMemValid[s][e] = true; }
        }
    }

    // ------------------------------------------------------------------------------ undo / redo

    struct Snap {
        std::vector<float> params;
        std::shared_ptr<const std::string> smp[kNumSlots], beat[kNumSlots];
        int pages = 1;
    };
    Snap takeSnap() const
    {
        Snap sn;
        sn.params.assign(fParam, fParam + kNumSlots * kSlotFields);
        for (int s = 0; s < kNumSlots; ++s) { sn.smp[s] = fSmpEnc[s]; sn.beat[s] = fBeatEnc[s]; }
        sn.pages = fPagesShown;
        return sn;
    }
    void pushUndo()
    {
        fUndo.push_back(takeSnap());
        if (fUndo.size() > 64) fUndo.erase(fUndo.begin());
        fRedo.clear();
    }
    void restoreSnap(const Snap& sn)
    {
        applyParams(sn.params.data());
        for (int s = 0; s < kNumSlots; ++s) if (sn.smp[s] != fSmpEnc[s]) setSampleState(s, sn.smp[s]);
        for (int s = 0; s < kNumSlots; ++s) if (sn.beat[s] != fBeatEnc[s]) setBeatState(s, sn.beat[s]);
        if (sn.pages != fPagesShown) setPagesShown(sn.pages);
        fPresetDirty = true;
        wake();
    }
    void undo() { if (fUndo.empty()) return; fRedo.push_back(takeSnap()); restoreSnap(fUndo.back()); fUndo.pop_back(); }
    void redo() { if (fRedo.empty()) return; fUndo.push_back(takeSnap()); restoreSnap(fRedo.back()); fRedo.pop_back(); }

    // ------------------------------------------------------------------------------ presets

    // a built-in set (YanceySets.hpp; 0 = Default, the layout in kDefaultTypes); returns the banks it uses
    int factoryParams(int i, float* vals) const { return factorySetParams(i, vals); }

    void refreshSetList() { listFiles(presetsDir("Sets").c_str(), ".yset", fUserSets); }

    std::string serializeSet() const
    {
        std::string out = "YANCEYSET 1\n";
        char b[32];
        std::snprintf(b, sizeof(b), "pages=%d\n", fPagesShown); out += b;
        out += "params=";
        for (int i = 0; i < kNumSlots * kSlotFields; ++i) { std::snprintf(b, sizeof(b), i ? ",%.5g" : "%.5g", fParam[i]); out += b; }
        out += "\n";
        for (int s = 0; s < kNumSlots; ++s) if (fSmpEnc[s]) { std::snprintf(b, sizeof(b), "smp%d=", s); out += b; out += *fSmpEnc[s]; out += "\n"; }
        for (int s = 0; s < kNumSlots; ++s) if (fBeatEnc[s]) {
            std::string v = *fBeatEnc[s]; for (char& c : v) if (c == '\n') c = '|';
            std::snprintf(b, sizeof(b), "beat%d=", s); out += b; out += v; out += "\n";
        }
        std::snprintf(b, sizeof(b), "beatmode=%d\n", fBeatMode); out += b;
        return out;
    }

    bool applySetText(const std::string& text)
    {
        if (text.compare(0, 10, "YANCEYSET ") != 0) return false;
        float vals[kNumSlots * kSlotFields];
        for (int i = 0; i < kNumSlots * kSlotFields; ++i) vals[i] = fParam[i];
        std::shared_ptr<const std::string> smp[kNumSlots], beat[kNumSlots];
        int pages = 1, beatmode = 0;
        size_t pos = text.find('\n') + 1;
        while (pos < text.size()) {
            size_t nl = text.find('\n', pos); if (nl == std::string::npos) nl = text.size();
            const size_t eq = text.find('=', pos);
            if (eq != std::string::npos && eq < nl) {
                const std::string k = text.substr(pos, eq - pos);
                if (k == "pages") pages = std::atoi(text.c_str() + eq + 1);
                else if (k == "params") {
                    const char* p = text.c_str() + eq + 1;
                    for (int i = 0; i < kNumSlots * kSlotFields; ++i) { char* end = nullptr; const float v = std::strtof(p, &end); if (end == p) break; vals[i] = v; p = end; if (*p == ',') ++p; }
                } else if (k == "beatmode") beatmode = std::atoi(text.c_str() + eq + 1);
                else if (k.compare(0, 4, "beat") == 0) {
                    const int s = std::atoi(k.c_str() + 4);
                    std::string v = text.substr(eq + 1, nl - eq - 1); for (char& c : v) if (c == '|') c = '\n';
                    if (s >= 0 && s < kNumSlots) beat[s] = std::make_shared<const std::string>(v);
                } else if (k.compare(0, 3, "smp") == 0) {
                    const int s = std::atoi(k.c_str() + 3);
                    if (s >= 0 && s < kNumSlots) smp[s] = std::make_shared<const std::string>(text.substr(eq + 1, nl - eq - 1));
                }
            }
            pos = nl + 1;
        }
        pushUndo();
        allOff();
        applyParams(vals);
        for (int s = 0; s < kNumSlots; ++s) if (smp[s] != fSmpEnc[s] && (smp[s] || fSmpEnc[s])) setSampleState(s, smp[s]);
        for (int s = 0; s < kNumSlots; ++s) if ((beat[s] || fBeatEnc[s]) && !(beat[s] && fBeatEnc[s] && *beat[s] == *fBeatEnc[s])) setBeatState(s, beat[s]);
        fBeatMode = beatmode; { char b[4]; std::snprintf(b, sizeof(b), "%d", fBeatMode); setState("beatmode", b); }
        setPagesShown(std::max(pages, 1));
        return true;
    }

    void setPresetName(const char* name, bool user)
    {
        std::snprintf(fPresetName, sizeof(fPresetName), "%s", name);
        fPresetUser = user; fPresetDirty = false;
        setState("preset", fPresetName);
    }

    void loadSetByIndex(int i)
    {
        if (i < numFactorySets()) {
            pushUndo();
            allOff();
            float vals[kNumSlots * kSlotFields];
            const int banks = factoryParams(i, vals);
            applyParams(vals);
            for (int s = 0; s < kNumSlots; ++s) { if (fSmpLoaded[s]) removeSample(s); if (fBeatHas[s]) setBeatState(s, nullptr); }
            if (banks > pagesShown()) setPagesShown(banks);
            showPage(0);
            setPresetName(factorySetName(i), false);
            wake();
            return;
        }
        const size_t u = (size_t)(i - numFactorySets());
        if (u >= fUserSets.size()) return;
        std::vector<uint8_t> bytes;
        const std::string path = presetsDir("Sets") + kPathSep + fUserSets[u] + ".yset";
        if (!readFileBytes(path.c_str(), bytes) || !applySetText(std::string(bytes.begin(), bytes.end()))) { showToast("Couldn't read that set."); return; }
        setPresetName(fUserSets[u].c_str(), true);
        wake();
    }

    void stepSet(int dir)
    {
        refreshSetList();
        const int total = numFactorySets() + (int)fUserSets.size();
        int cur = -1;
        if (!fPresetUser) { for (int i = 0; i < numFactorySets(); ++i) if (std::strcmp(fPresetName, factorySetName(i)) == 0) cur = i; }
        else for (size_t i = 0; i < fUserSets.size(); ++i) if (fUserSets[i] == fPresetName) cur = numFactorySets() + (int)i;
        loadSetByIndex(((cur < 0 ? 0 : cur + dir) % total + total) % total);
    }

    void saveSet(const char* name)
    {
        const std::string dir = presetsDir("Sets");
        if (dir.empty() || !writeFileBytes((dir + kPathSep + name + ".yset").c_str(), serializeSet())) { showToast("Couldn't save the set."); return; }
        setPresetName(name, true);
        char b[96]; std::snprintf(b, sizeof(b), "Saved \"%s\".", name); showToast(b);
    }

    void deleteCurrentSet()
    {
        if (!fPresetUser) return;
        removeFilePath((presetsDir("Sets") + kPathSep + fPresetName + ".yset").c_str());
        char b[96]; std::snprintf(b, sizeof(b), "Deleted \"%s\". The pads stay as they are.", fPresetName); showToast(b);
        fPresetUser = false; fPresetDirty = true;
    }

    std::string serializePad(int s) const
    {
        std::string out = "YANCEYPAD 1\nfields=";
        char b[32];
        for (int f = 0; f < kSlotFields; ++f) { std::snprintf(b, sizeof(b), f ? ",%.5g" : "%.5g", fParam[slotParam(s, f)]); out += b; }
        out += "\n";
        if (fSmpEnc[s]) { out += "smp="; out += *fSmpEnc[s]; out += "\n"; }
        if (fBeatEnc[s]) { std::string v = *fBeatEnc[s]; for (char& c : v) if (c == '\n') c = '|'; out += "beat=" + v + "\n"; }
        return out;
    }

    void loadPadPreset(int s, const std::string& name)
    {
        std::vector<uint8_t> bytes;
        const std::string path = presetsDir("Pads") + kPathSep + name + ".ypad";
        if (!readFileBytes(path.c_str(), bytes)) { showToast("Couldn't read that pad preset."); return; }
        const std::string text(bytes.begin(), bytes.end());
        if (text.compare(0, 10, "YANCEYPAD ") != 0) return;
        pushUndo();
        size_t pos = text.find('\n') + 1;
        std::shared_ptr<const std::string> smp, beat;
        while (pos < text.size()) {
            size_t nl = text.find('\n', pos); if (nl == std::string::npos) nl = text.size();
            if (text.compare(pos, 7, "fields=") == 0) {
                const char* p = text.c_str() + pos + 7;
                for (int f = 0; f < kSlotFields; ++f) {
                    char* end = nullptr; const float v = std::strtof(p, &end); if (end == p) break; p = end; if (*p == ',') ++p;
                    const int pid = slotParam(s, f);
                    if (fParam[pid] != v) setParam(pid, v, true);
                    fKnobDisp[pid].snap(normOfParam(pid, v));
                }
            } else if (text.compare(pos, 4, "smp=") == 0) smp = std::make_shared<const std::string>(text.substr(pos + 4, nl - pos - 4));
            else if (text.compare(pos, 5, "beat=") == 0) { std::string v = text.substr(pos + 5, nl - pos - 5); for (char& c : v) if (c == '|') c = '\n'; beat = std::make_shared<const std::string>(v); }
            pos = nl + 1;
        }
        if (smp != fSmpEnc[s]) setSampleState(s, smp);
        if (beat || fBeatEnc[s]) setBeatState(s, beat);
        wake();
    }

    // the name card: a set (fNameForPad < 0) or a pad preset
    void startNaming(int padSlot)
    {
        fNameForPad = padSlot;
        if (padSlot >= 0) {
            const int k = kindOf(padSlot);
            std::string base = k == 2 ? fSmpName[padSlot] : (k == 1 ? kMoveNames[moveAt(padSlot)] : kEffects[effectAt(padSlot)].name);
            for (size_t i = 1; i < base.size(); ++i) base[i] = (char)std::tolower((unsigned char)base[i]);
            std::snprintf(fNameBuf, sizeof(fNameBuf), "%s", base.substr(0, 28).c_str());
        } else std::snprintf(fNameBuf, sizeof(fNameBuf), "%s", fPresetUser ? fPresetName : "My set");
        fNaming = true; fNameFresh = true;
        openSetup(kScrName);
        fKbPrev = takeKeyboard();      // typing goes into the name box, not to FL
    }
    void finishNaming()
    {
        if (fBpmEdit >= 0) {
            const float v = (float)std::atof(fNameBuf);
            const int k = fBpmEdit;
            fBpmEdit = -1;
            if (v >= 20.f && v <= 400.f) setBeatBpm(k, v);
            closeSetup();
            return;
        }
        const std::string nm = safeFileName(fNameBuf);
        if (nm.empty()) return;
        // saving over another saved set or pad preset: ask first (re-saving the set you're on doesn't ask)
        const bool pad = fNameForPad >= 0;
        const bool itself = !pad && fPresetUser && sameName(nm, fPresetName);
        const std::string dir = presetsDir(pad ? "Pads" : "Sets");
        if (!fNameConfirmed && !itself && !dir.empty() && fileExists((dir + kPathSep + nm + (pad ? ".ypad" : ".yset")).c_str())) {
            if (fNaming) { returnKeyboard(fNativeWin, fKbPrev); fNaming = false; }
            fReplaceName = nm;
            fScr = kScrConfirmReplace;
            wake();
            return;
        }
        saveNamed(nm);
        closeSetup();
    }
    void saveNamed(const std::string& nm)
    {
        fNameConfirmed = false;
        if (fNameForPad >= 0) {
            const std::string dir = presetsDir("Pads");
            if (dir.empty() || !writeFileBytes((dir + kPathSep + nm + ".ypad").c_str(), serializePad(fNameForPad))) showToast("Couldn't save the pad preset.");
            else { char b[96]; std::snprintf(b, sizeof(b), "Saved pad preset \"%s\".", nm.c_str()); showToast(b); }
        } else {
            const std::string old = fPresetName;
            // a rename that only changes capitals is the same file on Windows and Mac: don't delete it afterwards
            const bool rename = fRenaming && fPresetUser && !sameName(old, nm);
            saveSet(nm.c_str());
            if (rename) removeFilePath((presetsDir("Sets") + kPathSep + old + ".yset").c_str());
        }
        fRenaming = false;
    }
    static bool sameName(const std::string& a, const std::string& b)
    {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
        return true;
    }
    // a name every computer can save: no spaces or dots at the ends, and none of Windows' reserved names
    static std::string safeFileName(const char* in)
    {
        std::string nm = in;
        while (!nm.empty() && (nm.back() == ' ' || nm.back() == '.')) nm.pop_back();
        size_t a = 0; while (a < nm.size() && (nm[a] == ' ' || nm[a] == '.')) ++a;
        nm = nm.substr(a);
        if (nm.empty()) return nm;
        std::string up; for (char c : nm) up += (char)std::toupper((unsigned char)c);
        const size_t dot = up.find('.');
        const std::string stem = dot == std::string::npos ? up : up.substr(0, dot);
        static const char* const reserved[] = { "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
                                                "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9" };
        for (const char* r : reserved) if (stem == r) { nm += " set"; break; }
        return nm;
    }

    // --------------------------------------------------------------------------------------------
    // Geometry and hit tests

    static float padX(int pos) { return kPadX0 + kPadSX * (float)(pos % 4); }
    static float padY(int pos) { return kPadY0 + kPadSY * (float)(pos / 4); }
    static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
    int slotAtPagePos(int q) const { return slotOfPad(fPage * kPadsPerPage + q); }
    int gridPosOf(int pagePad) const { for (int q = 0; q < kPadsPerPage; ++q) if (fPosSlot[q] == pagePad) return q; return pagePad; }
    // the slot shown at grid position q right now (-1 for the kill pad)
    int slotAtGrid(int q) const { return (fPage == 0 && q == kKillPad) ? -1 : slotAtPagePos(fPosSlot[q]); }

    bool hitGridPad(float x, float y, int& q) const
    {
        for (int i = 0; i < kPadsPerPage; ++i) {
            const float px = (fPage == 0 && i == kKillPad) ? padX(i) : fPadPX[i].x;
            const float py = (fPage == 0 && i == kKillPad) ? padY(i) : fPadPY[i].x;
            if (x >= px && x <= px + kPadW && y >= py && y <= py + kPadH) { q = i; return true; }
        }
        return false;
    }
    bool hitDetail(float x, float y) const { return x >= kDetX && x <= kDetX + kDetW && y >= kDetY && y <= kDetY + kDetH; }
    bool hitNotes(float x, float y) { const float w = notesPillW(); return x >= kNotesX - 2.f && x <= kNotesX + w + 2.f && y >= kNotesY - 3.f && y <= kNotesY + kNotesH + 3.f; }
    bool hitMoon(float x, float y) { const float cx = kNotesX + notesPillW() + 15.f, cy = kNotesY + kNotesH * 0.5f; return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= 144.f; }
    bool hitCtrlPill(float x, float y) { const float px = ctrlPillX(), w = ctrlPillW(); return x >= px - 2.f && x <= px + w + 2.f && y >= kNotesY - 3.f && y <= kNotesY + kNotesH + 3.f; }
    bool hitGear(float x, float y) { const float cx = gearCX(), cy = kNotesY + kNotesH * 0.5f; return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= 196.f; }
    bool hitChip(float x, float y, int& k) const
    {
        if (y < kFootCY - 14.f || y > kFootCY + 14.f) return false;
        for (int j = 0; j < kNumJobs; ++j) if (x >= fChipX[j] && x <= fChipX[j] + fChipW[j]) { k = j; return true; }
        return false;
    }
    static bool inCircle(float x, float y, float cx, float cy, float r) { return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r; }

    bool hitKnob(float x, float y, int& param) const
    {
        if (inCircle(x, y, kOutCX, kOutCY, 26.f)) { param = pOut; return true; }
        const float lx = x - kDetX, ly = y - kDetY;
        for (int k = 0; k < kNumJobs; ++k) {
            int row = k / 4; if (fKnobGridBottom) row = 1 - row;
            const int t = jobTarget(k, fSel, fParam, fSmpLoaded[fSel]);
            if (t >= 0 && inCircle(lx, ly, kGridX[k % 4], row == 0 ? kGridY0 : kGridY1, kGridDot + 4.f)) { param = t; return true; }
        }
        return false;
    }

    // --------------------------------------------------------------------------------------------
    // Utils

    static float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
    static double now()
    {
        using namespace std::chrono;
        return duration<double>(steady_clock::now().time_since_epoch()).count();
    }
    void wake() { fWake = 3; }

    static int parseList(const char* s, int* out, int n)
    {
        for (int i = 0; i < n; ++i) out[i] = -1;
        if (s == nullptr) return 0;
        int idx = 0;
        const char* p = s;
        while (*p && idx < n) {
            char* end = nullptr;
            const long v = std::strtol(p, &end, 10);
            if (end == p) { ++p; continue; }
            out[idx++] = (int)v;
            p = end;
            while (*p == ',' || *p == ' ') ++p;
        }
        return idx;
    }


    // --------------------------------------------------------------------------------------------
    // State

    FontId fFontTitle = -1, fFontHeavy = -1, fFontBold = -1, fFontMed = -1;
    Theme fT, fTL, fTD;
    bool fDark = false;
    int fPlusHover = -1;
    bool fFollow = false;     // bank A's pads play the bank on screen
    bool fBankQuick = false;  // setup: the next bank comes from one press
    int fQuickRef = 0, fFillPage = 1;
    char fFillText[128] = {};
    double fWizHitAt[kNumPads] = {};      // the empty pad (by its number on the page, 0-15) whose + is under the mouse
    float fSavedScale = 0.f;

    float fParam[kParamCount];
    int fMap[kNumLearn], fProjectMap[kNumLearn];
    bool fActive[kNumSlots];
    float fMem[kNumSlots][kNumEffects][kKnobsPerSlot] = {};
    bool fMemValid[kNumSlots][kNumEffects];
    unsigned fMoves = 0;
    bool fKill = false;
    int fSel = 2, fPage = 0, fPagesShown = 1, fPickedMask = 0, fNoteStart = -1, fNoteOrder = 0, fKnobMode = kKnobAuto;
    int fKnobSpeed = 1, fKnobTypesSaved = 0, fLiveSeq = -1, fPadFeel = 0;
    float fAmt[kNumSlots] = {};
    bool fKnobJump = false, fLiveTried = false;
    int fKnobCountSet = 0;       // knobs on the controller: 0 = count the linked ones, 1-5 set by hand
    YanceyLive* fLiveIn = nullptr;
    int fLearning = -1, fLastLearnSeq = -1, fAllOffCount = 0, fReorderCount = 0;
    int fMidiSeq = 0, fMidiState = 0, fPadHitSeq = -1, fLearnDupSeq = -1, fMidiLastSeq = -1, fRepeatSeq = -1;
    double fMidiAt = -10.0, fTimingSetAt = 0.0, fHwKnobAt = 0.0, fScrollUndoAt = 0.0;
    bool fPanelOpen = false, fWelcomed = false, fWelcomeChecked = false;
    int fScr = kScrHome, fWizStep = -1, fWizLastCode = -1, fWizDup = -1, fHearSeq0 = 0, fMorePage = 1;
    // keyboard setup: which bank is being set up, each bank's first key (MIDI code), tap-each-key mode
    bool fKeyMode = false, fKeyManual = false;
    int fKeyPage = 0, fKeyStart[kNumPages] = { -1, -1, -1 };
    double fKeyNotKeyAt = -10.0;
    double fWizDupAt = 0.0, fHeardAt = 0.0, fHearStart = 0.0, fOpenedAt = 0.0, fToastUntil = 0.0;
    char fToast[128] = {};
    bool fSmpLoaded[kNumSlots];
    float fSmpDur[kNumSlots];
    double fSmpErrorUntil[kNumSlots];
    std::string fSmpName[kNumSlots], fSmpError[kNumSlots];
    std::shared_ptr<const std::string> fSmpEnc[kNumSlots];
    // beat pads
    BeatInfo fBeatInfo[kNumSlots];
    bool fBeatHas[kNumSlots] = {}, fBeatMissing[kNumSlots] = {}, fBrowseBeat = false;
    std::shared_ptr<const std::string> fBeatEnc[kNumSlots];
    int fBeatReadyMask[2] = { 0, 0 }, fBeatPlaying = -1, fBeatMode = 0, fRelinkSlot = -1, fBpmEdit = -1;
    std::string fLastBeatDir;
    int fBrowsePad = -1;
    uintptr_t fNativeWin = 0;
    float fJobLast[kNumJobs], fGhost[kNumJobs];
    double fGhostAt[kNumJobs];
    float fChipX[kNumJobs] = {}, fChipW[kNumJobs] = {}, fTypeChipW = 120.f;

    // presets and undo
    char fPresetName[64] = {};
    bool fPresetUser = false, fPresetDirty = false;
    std::vector<std::string> fUserSets, fPadPresetNames;
    int fSetPage = 0, fPadPresetPage = 0;          // which page of a long preset list the menu shows
    std::string fReplaceName;                       // waiting for "replace it?" on this name
    bool fNameConfirmed = false;                    // the Windows save dialog already asked about replacing
    std::vector<Snap> fUndo, fRedo;
    bool fNaming = false, fNameFresh = false, fRenaming = false, fKnobGridBottom = false;
    uintptr_t fKbPrev = 0;
    int fNameForPad = -1;
    char fNameBuf[40] = {};

    Spring fKnobDisp[kParamCount], fPop[kParamCount];
    Spring fPadPX[kPadsPerPage], fPadPY[kPadsPerPage], fLift[kNumSlots];
    Spring fPadScale[kNumPads], fRing[kNumPads], fJobPop[kNumJobs];
    Spring fLimitT, fTimingT, fNotesT, fAllPress, fSpin, fBounce, fDarkT, fPanelA, fToastA, fUndoPress, fRedoPress;
    Spring fMenuA;                                   // a menu growing out of where it was opened, and back
    Spring fHovX, fHovY, fHovW, fHovH, fHovA;        // the menu's hover highlight, gliding from item to item
    Spring fTabX;                                    // the bank tabs' marker
    Spring fPageSlide;                               // a new bank sliding in (-1 / +1 -> 0)
    Spring fCardA, fScrA;                            // the pad card and the setup screens easing in
    Spring fPlusA[kPadsPerPage];                     // the + on an empty pad lighting up
    int fCardSel = -1, fLastScr = -1;
    float fDonutAngle = 0.f, fLastLevel = 0.f;
    double fLastTime = 0.0;
    int fWake = 3;
    float fLastW = 0.f, fLastH = 0.f; double fSizeChangedAt = 0.0; bool fSizeDirty = false;

    float fMouseX = 0.f, fMouseY = 0.f;
    int fHeldPadParam = -1, fPressedVisual = -1;
    int fDragKnob = -1; float fDragStartY = 0.f, fDragStartNorm = 0.f, fDragNorm = 0.f; bool fDragFine = false;
    int fPosSlot[kPadsPerPage];
    int fDragPos = -1; bool fPadDragging = false;
    float fDragStartX = 0.f, fDragStartY2 = 0.f, fDragGrabX = 0.f, fDragGrabY = 0.f, fDragTilt = 0.f;
    int fLastClickParam = -1; double fLastClickTime = 0.0;

    bool fMenuOpen = false, fMenuFromDetail = false;
    int fMenuKind = 0, fMenuTarget = -1, fMenuSlot = -1, fMenuCount = 0, fMenuHover = -1, fMenuChecked = -1, fMenuChecked2 = -1, fMenuChecked3 = -1, fMenuChecked4 = -1, fMenuChecked5 = -1, fMenuCols = 1, fMenuCol = 0;
    float fMenuX = 0.f, fMenuY = 0.f, fMenuRX = 0.f, fMenuRY = 0.f, fMenuW = 200.f;
    enum { kMaxMenuItems = 96 };
    MenuItem fMenuItems[kMaxMenuItems];
    char fMenuHeader[64] = {}, fMenuSub[64] = {}, fMenuFoot[64] = {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(YanceyUI)
};

UI* createUI()
{
    return new YanceyUI();
}

END_NAMESPACE_DISTRHO
