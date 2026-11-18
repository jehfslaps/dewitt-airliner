// Airliner: the window, for all three plug-ins (Airliner, Airliner Looper, Airliner Vinyl). Dewitt's look (its
// colours, fonts, knobs, pills and light / dark themes) in a hardware layout: each effect is a panel with its
// three controls in a bar, a round screen showing what you touch, and INPUT and OUTPUT faders with meters.
// Every movement is a spring, so nothing jumps. Right-click any control to link a MIDI controller.
#include "DistrhoUI.hpp"
#include "AirlinerShared.hpp"
#include "FontData.hpp"
#include "YanceyPlatform.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

START_NAMESPACE_DISTRHO

using namespace airliner;

static Color withAlpha(const Color& c, float a) { Color r(c); r.alpha = a; return r; }
static Color hex(int rgb) { return Color((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255); }
static float clamp01(float x) { return x < 0.f ? 0.f : (x > 1.f ? 1.f : x); }

static const Color kAmberAcc(255, 179, 15);
static const Color kRedAcc(253, 21, 27);
static constexpr float kPi = 3.14159265f;

// ------------------------------------------------------------------------------------------------
// Themes (Dewitt's)

struct Theme {
    Color bg, text, outline, face, faceInk, shadow, pillFill, muted;
    Color menuBg, menuText, menuHover, menuHoverText;
    Color sel, selInk, tray, trayEdge, card, cardInk, bezel, screen, bar, glass;
    float shadowA;
};

static Theme makeTheme(bool dark)
{
    Theme t;
    if (!dark) {
        t.bg = hex(0xF7F1E3); t.text = hex(0x01295F); t.outline = hex(0x01295F); t.face = hex(0xF7F1E3);
        t.faceInk = hex(0x01295F); t.shadow = hex(0x01295F); t.shadowA = 0.2f; t.pillFill = hex(0xF7F1E3);
        t.muted = withAlpha(hex(0x01295F), 0.55f);
        t.menuBg = hex(0xF7F1E3); t.menuText = hex(0x01295F); t.menuHover = hex(0x01295F); t.menuHoverText = hex(0xF7F1E3);
        t.sel = hex(0x01295F); t.selInk = hex(0xF7F1E3);
        t.tray = hex(0xEEE7D6); t.trayEdge = hex(0xD6CDBA);
        t.card = hex(0x437F97); t.cardInk = hex(0x01295F);
        t.bezel = hex(0x1E222A); t.screen = hex(0x01295F);
        t.bar = hex(0x01295F); t.glass = hex(0x0A1A33);
    } else {
        t.bg = hex(0x0C1524); t.text = hex(0xEDE6D6); t.outline = hex(0x05090F); t.face = hex(0xE9E2D2);
        t.faceInk = hex(0x0A1A33); t.shadow = hex(0x000000); t.shadowA = 0.45f; t.pillFill = hex(0x1D3D6E);
        t.muted = withAlpha(hex(0xEDE6D6), 0.55f);
        t.menuBg = hex(0x13223A); t.menuText = hex(0xEDE6D6); t.menuHover = hex(0xEDE6D6); t.menuHoverText = hex(0x0C1524);
        t.sel = hex(0x0A1A33); t.selInk = hex(0xEDE6D6);
        t.tray = hex(0x13213A); t.trayEdge = hex(0x05090F);
        t.card = hex(0x3B7088); t.cardInk = hex(0x0A1A33);
        t.bezel = hex(0x05090F); t.screen = hex(0x0A1A33);
        t.bar = hex(0x081426); t.glass = hex(0x050B16);
    }
    return t;
}

static Theme lerpTheme(const Theme& a, const Theme& b, float t)
{
    if (t <= 0.f) return a;
    if (t >= 1.f) return b;
    Theme r;
    #define L(f) r.f = Color(a.f, b.f, t)
    L(bg); L(text); L(outline); L(face); L(faceInk); L(shadow); L(pillFill); L(muted);
    L(menuBg); L(menuText); L(menuHover); L(menuHoverText);
    L(sel); L(selInk); L(tray); L(trayEdge); L(card); L(cardInk); L(bezel); L(screen); L(bar); L(glass);
    #undef L
    r.shadowA = a.shadowA + (b.shadowA - a.shadowA) * t;
    return r;
}

// ------------------------------------------------------------------------------------------------
// Springs: response (seconds) and damping, as in the motion guide

struct Spring {
    float x = 0.f, v = 0.f, target = 0.f, k = 273.f, zeta = 0.78f;
    void tune(float response, float damping) { const float w = 2.f * kPi / response; k = w * w; zeta = damping; }
    void set(float t) { target = t; }
    void snap(float t) { target = x = t; v = 0.f; }
    bool step(float dt) {
        if (std::fabs(x - target) < 0.0005f && std::fabs(v) < 0.0005f) {
            if (x == target && v == 0.f) return false;
            x = target; v = 0.f; return true;
        }
        const float c = 2.f * zeta * std::sqrt(k);
        float rem = dt;
        while (rem > 0.f) {
            const float h = rem > (1.f / 240.f) ? (1.f / 240.f) : rem;
            const float a = -k * (x - target) - c * v;
            v += a * h; x += v * h; rem -= h;
        }
        return true;
    }
};

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Layout (design units). Airliner is 1180 x 630 with two panels; the single plug-ins are 600 x 630.

static constexpr float kW = kKind == kKindBoth ? 1180.f : 600.f, kH = 630.f;
static constexpr float kTitleX = 28.f, kTitleCY = 40.f, kTitleEm = 29.f;
static constexpr float kHdrCY = 34.f, kCapY = 66.f;
// header, laid out from the right edge in: LIMIT, (ALL OFF, signal, undo / redo,) preset
static constexpr float kLimW = 52.f, kLimH = 26.f, kLimX = kW - 28.f - kLimW, kLimY = 21.f;
static constexpr float kAllW = 96.f, kAllH = 32.f, kAllX = kLimX - 20.f - kAllW, kAllY = 18.f;
static constexpr float kOrdW = 200.f, kOrdH = 32.f, kOrdX = kAllX - 20.f - kOrdW, kOrdY = 18.f;
static constexpr float kUndoR = 15.f, kRedoCX = kOrdX - 36.f, kUndoCX = kRedoCX - 36.f;
static constexpr float kPresetW = kKind == kKindBoth ? 200.f : 140.f, kPresetH = 30.f, kPresetY = 19.f;
static constexpr float kPresetX = kKind == kKindBoth ? kUndoCX - kUndoR - 40.f - kPresetW : kLimX - 20.f - kPresetW;
// a panel
static constexpr float kCardY = 96.f, kCardW = 552.f, kCardH = 500.f;
static constexpr float kBarX = 26.f, kBarY = 78.f, kBarW = 500.f, kBarH = 160.f;
static constexpr float kSlotCY = 160.f, kKnobR = 32.f, kKnobDot = 44.f;
static constexpr float kLoopR = 30.f;
static constexpr float kFadW = 104.f, kFadH = 190.f, kFadY = 256.f, kFadInX = 26.f, kFadOutX = kCardW - 26.f - kFadW;
static constexpr float kScrCX = kCardW * 0.5f, kScrCY = 346.f, kScrR = 96.f;
static constexpr float kOnW = 120.f, kOnH = 30.f, kOnX = kCardW * 0.5f - kOnW * 0.5f, kOnY = 456.f;
static constexpr float kSegW = 124.f, kSegH = 28.f, kSegX = kCardW * 0.5f - 62.f + 20.f, kSegY = 458.f;
static inline float slotCX(int i) { return kBarX + kBarW * (1.f + 2.f * (float)i) / 6.f; }
static inline float cardX(int side) { return kKind == kKindBoth ? (side == 0 ? 24.f : 604.f) : 24.f; }
static inline bool sideShown(int side) { return side == 0 ? kHasLooper : kHasVinyl; }
static inline int sideOfParam(int p)
{
    if (isLooperParam(p)) return 0;
    if (p == pVinyl || p == pComp || p == pNoise || p == pFlu || p == pInB || p == pOutB) return 1;
    return -1;
}
// SET UP CONTROLLER (Airliner only: the single plug-ins have no room in the header)
static constexpr float kSetupY = 18.f, kSetupH = 32.f, kSetupR = kPresetX - 26.f;
// the setup card, over the panels
static constexpr float kWizW = 600.f, kWizH = 392.f, kWizX = (kW - kWizW) * 0.5f, kWizY = 128.f;
// the three controls in each panel's bar (-1: the LOOP button)
static const int kSlotParam[2][3] = { { pLen, pSpeed, -1 }, { pComp, pNoise, pFlu } };
static const int kFadParam[2][2] = { { pInA, pOutA }, { pInB, pOutB } };

static const char* const kTitle = "Airliner";
static const char* const kTag = kKind == kKindLooper ? "LOOPER" : (kKind == kKindVinyl ? "VINYL" : nullptr);
static const char* const kSideName[2] = { "DJ LOOPER", "VINYL SIM" };
static const char* const kSideSub[2] = { "GRAB A SLICE, BEND THE TAPE", "THE OLD-RECORD SOUND" };
static const char* const kFootHint = kKind == kKindBoth ? "Drag a knob or fader, double-click to reset. SET UP CONTROLLER (top) links your MIDI controller."
                                                        : "Right-click any control to link your MIDI controller.";
static const char* const kPresetFolder = kKind == kKindLooper ? "Looper Presets" : (kKind == kKindVinyl ? "Vinyl Presets" : "Presets");

#ifdef _WIN32
static const char* const kSep = "\\";
#else
static const char* const kSep = "/";
#endif

enum { kActPreset0 = 100, kActUser0 = 200, kActSave = 1, kActDelete = 2,
       kActLearn = 3, kActForget = 4, kActFollow = 5, kActEach = 6, kActPickup = 7, kActForgetAll = 8 };
enum { kDragNone = -1 };

class AirlinerUI : public UI, public IdleCallback
{
public:
    AirlinerUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT, false)   // false = no DPF auto-scale (see applyScaleFactor)
    {
        applyScaleFactor(getScaleFactor(), 1.0);

        fFontTitle = createFontFromMemory("air-title", kFontShrikhand, kFontShrikhandSize, false);
        fFontHeavy = createFontFromMemory("air-heavy", kFontDMSansXB, kFontDMSansXBSize, false);
        fFontBold  = createFontFromMemory("air-bold", kFontDMSansBold, kFontDMSansBoldSize, false);
        fFontMed   = createFontFromMemory("air-med", kFontDMSansMed, kFontDMSansMedSize, false);

        fTL = makeTheme(false); fTD = makeTheme(true); fT = fTL;
        for (int i = 0; i < kParamCount; ++i) {
            fP[i] = kSpecs[i].def;
            fKnob[i].tune(0.38f, 0.9f);
            fKnob[i].snap(isOutput(i) ? 0.f : normOf(i, fP[i]));
            fPop[i].tune(0.38f, 0.9f);
        }
        fP[oPhase] = -1.f;
        fLoopT.tune(0.38f, 0.78f); fVinT.tune(0.38f, 0.78f);
        fLoopPress.tune(0.10f, 1.f); fVinPress.tune(0.10f, 1.f);
        fLimitT.tune(0.38f, 0.78f); fLimitT.snap(fP[pLimit]);
        fModeT.tune(0.38f, 0.78f); fModeT.snap(fP[pPadMode]);
        fOrderT.tune(0.38f, 0.78f);
        fDarkT.tune(0.475f, 1.f);
        fRingA.tune(0.3f, 1.f);
        for (int s = 0; s < 2; ++s) { fShown[s] = s == 0 ? pLen : pFlu; fFlash[s].tune(0.475f, 1.f); fMsgA[s].tune(0.3f, 1.f); }
        for (int m = 0; m < 4; ++m) fMeterDb[m] = -60.f;
        fAllPress.tune(0.10f, 1.f); fUndoPress.tune(0.10f, 1.f); fRedoPress.tune(0.10f, 1.f);
        fUndoPress.snap(1.f); fRedoPress.snap(1.f);
        fBadge.tune(0.38f, 0.5f);
        fMenuA.tune(0.25f, 1.f);
        fWizA.tune(0.3f, 1.f); fWizStepA.tune(0.38f, 1.f); fSetupPress.tune(0.10f, 1.f); fWizPress.tune(0.10f, 1.f);
        std::snprintf(fPresetName, sizeof(fPresetName), "%s", "Default");
        fMsg[0][0] = fMsg[1][0] = '\0';

        yancey::Prefs pr;
        if (yancey::readPrefs(pr)) {
            if (pr.dark >= 0) { fDark = pr.dark != 0; fDarkT.snap(fDark ? 1.f : 0.f); fT = fDark ? fTD : fTL; }
            if (pr.uscale > 0.f || pr.scale > 0.f) { applyScaleFactor(fScale, savedUserScale(pr)); fSavedScale = (float)userScale(); }
            parseMap(pr.map.c_str());
            fJump = pr.kjump == 1;
            fOneEach = pr.kgrid == 1;
        }
        fSideT.tune(0.38f, 0.78f);
        refreshUserPresets();
        fNativeWin = getWindow().getNativeWindowHandle();
        fLastTime = now();
        fLastW = (float)getWidth(); fLastH = (float)getHeight();
        addIdleCallback(this, 16);
    }

    ~AirlinerUI() override { removeIdleCallback(this); }

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
    double savedUserScale(const yancey::Prefs& pr) const
    {
        if (pr.uscale > 0.f) return pr.uscale;
        if (pr.scale > 0.f) return pr.scale / fScale;
        return 1.0;
    }


protected:
    // --------------------------------------------------------------------------------------------
    // host -> window

    void parameterChanged(uint32_t index, float value) override
    {
        if (index >= (uint32_t)kParamCount) return;
        fP[index] = value;
        if (isOutput((int)index)) {
            // outputs start at 0 here, as they do in the plug-in; a host only sends one when it changes. A link
            // learned before this window opened isn't news; controller moves from before it are passed on once.
            if (index == oLearnSeq && now() - fOpenedAt < 0.5) fLastLearnSeq = value;
            if (index == oSide && now() - fOpenedAt < 0.5) fSideT.snap(value);
            if (index == oSide) { fSideT.set(value > 0.5f ? 1.f : 0.f); wake(); }
            return;
        }
        if ((int)index != fDrag) fKnob[index].set(normOf((int)index, value));
        if (index == pLimit) fLimitT.set(value > 0.5f ? 1.f : 0.f);
        if (index == pPadMode) fModeT.set(value > 0.5f ? 1.f : 0.f);
        if (index == pOrder) fOrderT.set(value > 0.5f ? 1.f : 0.f);
        if (index == pLoop || index == pVinyl) {
            if (index == pLoop) fLoopT.set(value > 0.5f ? 1.f : 0.f); else fVinT.set(value > 0.5f ? 1.f : 0.f);
        }
        if (!fInited[index]) {      // the first value from the host: no animation
            fInited[index] = true;
            fKnob[index].snap(normOf((int)index, value));
            if (index == pLimit) fLimitT.snap(value > 0.5f ? 1.f : 0.f);
            if (index == pPadMode) fModeT.snap(value > 0.5f ? 1.f : 0.f);
            if (index == pOrder) fOrderT.snap(value > 0.5f ? 1.f : 0.f);
            if (index == pLoop) fLoopT.snap(value > 0.5f ? 1.f : 0.f);
            if (index == pVinyl) fVinT.snap(value > 0.5f ? 1.f : 0.f);
        }
        wake();
    }

    void stateChanged(const char* key, const char* value) override
    {
        if (std::strcmp(key, "preset") == 0 && value) {
            std::snprintf(fPresetName, sizeof(fPresetName), "%s", value[0] ? value : "Default");
            wake();
        }
    }

    // --------------------------------------------------------------------------------------------
    // the frame clock: springs move only while something is moving

    static double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
    void wake() { fWake = 2; repaint(); }

    void idleCallback() override
    {
        const double t = now();
        float dt = (float)(t - fLastTime);
        fLastTime = t;
        if (dt > 0.05f) dt = 0.05f;
        bool moving = false;
        for (int i = 0; i < kParamCount; ++i) { moving |= fKnob[i].step(dt); moving |= fPop[i].step(dt); }
        moving |= fLoopT.step(dt); moving |= fVinT.step(dt);
        moving |= fLoopPress.step(dt); moving |= fVinPress.step(dt);
        moving |= fLimitT.step(dt); moving |= fModeT.step(dt); moving |= fOrderT.step(dt);
        moving |= fAllPress.step(dt); moving |= fUndoPress.step(dt); moving |= fRedoPress.step(dt);
        moving |= fBadge.step(dt); moving |= fMenuA.step(dt);
        moving |= fWizA.step(dt); moving |= fWizStepA.step(dt); moving |= fSetupPress.step(dt); moving |= fWizPress.step(dt);
        if (fWizLinkedAt > 0.0) { moving = true; if (t - fWizLinkedAt > 0.75) { fWizLinkedAt = 0.0; wizGo(fWiz + 1); } }
        if (fDarkT.step(dt)) { fT = lerpTheme(fTL, fTD, fDarkT.x); moving = true; }

        controllerIdle();
        moving |= fSideT.step(dt);
        if (fLearnSlot >= 0) moving = true;     // the link ring breathes while it waits

        // each screen: a message for a moment, then back to the control it shows
        for (int s = 0; s < 2; ++s) {
            fMsgA[s].set(t < fMsgUntil[s] ? 1.f : 0.f);
            moving |= fMsgA[s].step(dt);
            moving |= fFlash[s].step(dt);
        }
        const bool looping = fP[oPhase] >= 0.f;
        fRingA.set(looping ? 1.f : 0.f);
        moving |= fRingA.step(dt);

        // meters: rise at once, fall at 26 dB a second
        static const int mp[4] = { oMeterInA, oMeterOutA, oMeterInB, oMeterOutB };
        for (int m = 0; m < 4; ++m) {
            const float lv = fP[mp[m]];
            const float db = lv > 1e-6f ? 20.f * std::log10(lv) : -60.f;
            const float fall = fMeterDb[m] - 26.f * dt;
            const float nd = std::max(db, fall < -60.f ? -60.f : fall);
            if (std::fabs(nd - fMeterDb[m]) > 0.05f) moving = true;
            fMeterDb[m] = nd;
        }

        {
            const float W = (float)getWidth(), H = (float)getHeight();
            if (W != fLastW || H != fLastH) { fLastW = W; fLastH = H; fSizeChangedAt = t; fSizeDirty = true; }
            else if (fSizeDirty && t - fSizeChangedAt > 0.6) {
                fSizeDirty = false;
                const float sc = (float)userScale();
                if (std::fabs(sc - fSavedScale) > 0.003f) { fSavedScale = sc; yancey::Prefs up; up.uscale = sc; up.scale = W / kW; yancey::writePrefs(up); }
            }
        }
        if (moving || looping || fWake > 0) { if (fWake > 0) --fWake; repaint(); }
    }

    // --------------------------------------------------------------------------------------------
    // drawing helpers (Dewitt's)

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
    // left-aligned words, wrapped to a width
    void drawWrapped(const char* str, float x, float y, float maxW, float em, float lineH, const Color& c)
    {
        std::string line, word;
        std::vector<std::string> lines;
        const char* p = str;
        while (true) {
            if (*p == ' ' || *p == '\0') {
                const std::string tryL = line.empty() ? word : line + " " + word;
                if (!line.empty() && measure(fFontMed, em, tryL.c_str(), 0.f) > maxW) { lines.push_back(line); line = word; }
                else line = tryL;
                word.clear();
                if (*p == '\0') break;
            } else word += *p;
            ++p;
        }
        if (!line.empty()) lines.push_back(line);
        for (size_t i = 0; i < lines.size(); ++i)
            drawText(fFontMed, em, c, x, y + lineH * (float)i, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, lines[i].c_str());
    }
    void pill(float x, float y, float w, float h, const Color& f, const Color& s, float sw)
    {
        beginPath(); roundedRect(x, y, w, h, h * 0.5f);
        fillColor(f); fill();
        strokeColor(s); strokeWidth(sw); stroke();
    }
    void caption(float cx, const char* str, const Color& c)
    {
        drawText(fFontHeavy, 10.f, c, cx, kCapY, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, str, 0.3f);
    }
    void chevron(float cx, float cy, const Color& c, float flip = 0.f)
    {
        save();
        translate(cx, cy); rotate(kPi * flip);
        beginPath(); moveTo(-3.5f, -1.75f); lineTo(0.f, 1.75f); lineTo(3.5f, -1.75f);
        lineCap(NanoVG::ROUND); lineJoin(NanoVG::ROUND);
        strokeColor(c); strokeWidth(2.f); stroke();
        restore();
    }

    void knobShape(float cx, float cy, float n, const Color& ink, bool bipolar, float r, float dotR, float pop)
    {
        n = clamp01(n);
        for (int i = 0; i < 11; ++i) {
            const float t = i / 10.f;
            const float a = (-135.f + 270.f * t) * kPi / 180.f;
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
        const float ang = (-135.f + 270.f * n) * kPi / 180.f;
        const float sa = std::sin(ang), ca = std::cos(ang);
        beginPath();
        moveTo(cx + sa * 3.f, cy - ca * 3.f);
        lineTo(cx + sa * (rr - 6.f), cy - ca * (rr - 6.f));
        lineCap(NanoVG::ROUND);
        strokeColor(fT.outline); strokeWidth(3.f); stroke();
    }
    static Color invertInk(const Color& ink) { return (ink.red + ink.green + ink.blue) > 1.5f ? hex(0x0A1A33) : hex(0xF7F1E3); }

    void bigKnob(int param, float cx, float cy, int knobNo, float r, float dotR, const Color& ink)
    {
        const bool bipolar = param == pSpeed && kSpecs[pSpeed].min < 0.f;
        const float pop = fPop[param].x;
        knobShape(cx, cy, fKnob[param].x, ink, bipolar, r, dotR, pop);
        const char* label = kSpecs[param].label;
        char value[32]; formatValue(param, fP[param], value, sizeof(value));
        const float ly = cy + dotR + 15.f, em = r > 25.f ? 12.f : 11.5f;
        const float lw = measure(fFontBold, em, label, 0.2f);
        const float bx = cx - lw * 0.5f - 11.f;
        beginPath(); circle(bx, ly, 7.f); fillColor(ink); fill();
        char n[4]; std::snprintf(n, sizeof(n), "%d", knobNo);
        drawText(fFontHeavy, 8.5f, invertInk(ink), bx, ly, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, n);
        drawText(fFontBold, em, ink, cx + 5.f, ly, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, label, 0.2f);
        drawText(pop > 0.3f ? fFontBold : fFontMed, em, ink, cx, ly + 16.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, value);
    }

    void segSwitch(float x, float y, float w, float h, float selPos, const char* a, const char* b,
                   const Color& base, const Color& edge, const Color& selFill, const Color& selInk, const Color& offInk)
    {
        beginPath(); roundedRect(x, y, w, h, h * 0.5f); fillColor(base); fill();
        // the selected half slides across
        save();
        intersectScissor(x, y, w, h);
        const float hw = w * 0.5f, sx = x + hw * clamp01(selPos);
        beginPath(); roundedRect(sx, y, hw, h, h * 0.5f); fillColor(selFill); fill();
        restore();
        beginPath(); roundedRect(x, y, w, h, h * 0.5f); strokeColor(edge); strokeWidth(2.5f); stroke();
        const float s = clamp01(selPos);
        drawText(fFontHeavy, 11.f, Color(selInk, offInk, s), x + hw * 0.5f, y + h * 0.5f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, a, 0.3f);
        drawText(fFontHeavy, 11.f, Color(offInk, selInk, s), x + hw * 1.5f, y + h * 0.5f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, b, 0.3f);
    }

    // a big round button with a press (it sinks onto its shadow)
    void roundButton(float cx, float cy, float on, float press, const char* label)
    {
        const float r = kLoopR * (1.f - 0.04f * press);
        const float drop = 4.f * (1.f - press);
        beginPath(); circle(cx, cy + 4.f, r); fillColor(fT.outline); fill();
        const float fy = cy + (4.f - drop);
        beginPath(); circle(cx, fy, r); fillColor(Color(fT.face, kRedAcc, clamp01(on))); fill();
        strokeColor(fT.outline); strokeWidth(3.f); stroke();
        drawText(fFontHeavy, 14.f, Color(fT.faceInk, hex(0xF7F1E3), clamp01(on)), cx, fy, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, label, 0.3f);
    }

    // Airliner's badge: a little plane, banking
    void drawBadge(float cx, float cy)
    {
        const float sc = 1.f + 0.08f * fBadge.x;
        save();
        translate(cx, cy); scale(sc, sc);
        beginPath(); circle(0.f, 0.f, 17.f);
        fillColor(kAmberAcc); fill();
        strokeColor(fT.outline); strokeWidth(2.5f); stroke();
        rotate(kPi * 0.25f + 0.25f * fBadge.x);
        const Color ink = hex(0x01295F);
        beginPath(); roundedRect(-2.3f, -11.5f, 4.6f, 21.f, 2.3f); fillColor(ink); fill();
        beginPath();
        moveTo(-11.5f, 2.5f); lineTo(-1.5f, -4.f); lineTo(1.5f, -4.f); lineTo(11.5f, 2.5f);
        lineTo(11.5f, 5.f); lineTo(1.5f, 1.5f); lineTo(-1.5f, 1.5f); lineTo(-11.5f, 5.f); closePath();
        fillColor(ink); fill();
        beginPath();
        moveTo(-5.5f, 10.f); lineTo(-1.f, 6.5f); lineTo(1.f, 6.5f); lineTo(5.5f, 10.f);
        lineTo(5.5f, 12.f); lineTo(0.f, 10.5f); lineTo(-5.5f, 12.f); closePath();
        fillColor(ink); fill();
        restore();
    }

    // moon: a soft disc becomes a crescent as the theme changes (Dewitt's)
    void drawMoon(float cx, float cy)
    {
        const float m = clamp01(fDarkT.x);
        const float R = 9.f;
        beginPath(); circle(cx, cy, R);
        fillColor(Color(withAlpha(fT.bg, 0.f), fT.pillFill, m)); fill();
        const float r = R * 0.58f;
        beginPath(); circle(cx - 0.05f * R, cy + 0.05f * R, r);
        fillColor(Color(fT.muted, kAmberAcc, m)); fill();
        if (m > 0.01f) {
            const float ang = (-40.f - 20.f * m) * kPi / 180.f;
            const float dist = R * (0.9f - 0.6f * m);
            beginPath(); circle(cx + dist * std::cos(ang), cy + dist * std::sin(ang), r * 0.86f * std::sqrt(m));
            fillColor(Color(fT.bg, fT.pillFill, m)); fill();
        }
        beginPath(); circle(cx, cy, R);
        strokeColor(Color(fT.muted, fT.outline, m)); strokeWidth(1.5f); stroke();
    }

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

    // --------------------------------------------------------------------------------------------

    float titleRight() { return kTitleX + measure(fFontTitle, kTitleEm, kTitle, 0.f); }
    float tagW() { return kTag ? measure(fFontHeavy, 11.5f, kTag, 1.6f) + 24.f : 0.f; }
    float badgeCX() { return titleRight() + 10.f + (kTag ? tagW() + 8.f : 0.f) + 19.f; }
    float moonCX() const { return kKind == kKindBoth ? const_cast<AirlinerUI*>(this)->badgeCX() + 19.f + 20.f : 40.f; }
    float moonCY() const { return kKind == kKindBoth ? kTitleCY : kH - 22.f; }

    void onNanoDisplay() override
    {
        const float s = (float)getWidth() / kW;
        save();
        scale(s, s);
        const float dh = (float)getHeight() / s;
        beginPath(); rect(0, 0, kW, std::max(dh, kH)); fillColor(fT.bg); fill();
        drawHeader();
        for (int side = 0; side < 2; ++side) if (sideShown(side)) drawPanel(side);
        drawLearnRing();
        drawText(fFontMed, 10.5f, fT.muted, kW - 28.f, kH - 18.f, NanoVG::ALIGN_RIGHT | NanoVG::ALIGN_MIDDLE, "By Jehf Slaps");
        drawText(fFontMed, 10.5f, fT.muted, kKind == kKindBoth ? 28.f : 58.f, kH - 18.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, kFootHint);
        if (kKind != kKindBoth) drawMoon(moonCX(), moonCY());
        if (fMenuOpen || fMenuA.x > 0.01f) drawMenu();
        if (fWiz >= 0 || fWizA.x > 0.01f) drawWizard();
        restore();
    }

    void drawHeader()
    {
        drawText(fFontTitle, kTitleEm, fT.text, kTitleX, kTitleCY, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, kTitle, 0.f);
        float x = titleRight() + 10.f;
        if (kTag) {
            const float tw = tagW();
            beginPath(); roundedRect(x, kTitleCY - 16.f, tw, 24.f, 12.f); fillColor(fDark ? fT.pillFill : fT.text); fill();
            drawText(fFontHeavy, 11.5f, fDark ? fT.text : fT.bg, x + tw * 0.5f, kTitleCY - 4.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, kTag, 1.6f);
            x += tw + 8.f;
        }
        drawBadge(badgeCX(), kTitleCY - 4.f);
        if (kKind == kKindBoth) drawMoon(moonCX(), kTitleCY - 4.f);
        if (kKind == kKindBoth) drawSetupButton();

        // preset: < name >  (click the name for the list)
        {
            const bool open = fMenuOpen && fMenuIsPreset;
            pill(kPresetX, kPresetY, kPresetW, kPresetH, open ? fT.text : fT.pillFill, fT.outline, 2.5f);
            const Color ink = open ? fT.bg : fT.text;
            chevron(kPresetX + 16.f, kPresetY + kPresetH * 0.5f, ink, 0.5f);
            chevron(kPresetX + kPresetW - 16.f, kPresetY + kPresetH * 0.5f, ink, -0.5f);
            const std::string nm = fitText(fFontHeavy, 12.f, std::string(fPresetName) + (presetDirty() ? " *" : ""), kPresetW - 56.f, 0.2f);
            drawText(fFontHeavy, 12.f, ink, kPresetX + kPresetW * 0.5f, kPresetY + kPresetH * 0.5f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, nm.c_str(), 0.2f);
            caption(kPresetX + kPresetW * 0.5f, "PRESET", fT.muted);
        }
        if (kKind == kKindBoth) {
            drawUndoIcon(kUndoCX, kHdrCY, false, !fUndo.empty(), fUndoPress.x);
            drawUndoIcon(kRedoCX, kHdrCY, true, !fRedo.empty(), fRedoPress.x);
            caption(0.5f * (kUndoCX + kRedoCX), "UNDO \xC2\xB7 REDO", fT.muted);
            // the signal path: IN > A > B > OUT; click to swap A and B (they trade places)
            pill(kOrdX, kOrdY, kOrdW, kOrdH, fT.pillFill, fT.outline, 2.5f);
            const float my = kOrdY + kOrdH * 0.5f;
            drawText(fFontHeavy, 11.f, fT.text, kOrdX + 22.f, my, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "IN", 0.3f);
            drawText(fFontHeavy, 11.f, fT.text, kOrdX + kOrdW - 26.f, my, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "OUT", 0.3f);
            for (float ax : { kOrdX + 44.f, kOrdX + kOrdW * 0.5f, kOrdX + kOrdW - 54.f }) chevron(ax, my, withAlpha(fT.text, 0.6f), -0.5f);
            const float s1 = kOrdX + 72.f, s2 = kOrdX + kOrdW - 82.f;
            const float o = clamp01(fOrderT.x);
            const float lift = std::sin(kPi * o) * 5.f;
            chip(s1 + (s2 - s1) * fOrderT.x, my - lift, "A");
            chip(s2 + (s1 - s2) * fOrderT.x, my + lift, "B");
            caption(kOrdX + kOrdW * 0.5f, "SIGNAL \xC2\xB7 CLICK TO SWAP", fT.muted);
            const float ps = 1.f - 0.05f * fAllPress.x;
            save();
            translate(kAllX + kAllW * 0.5f, kAllY + kAllH * 0.5f); scale(ps, ps);
            pill(-kAllW * 0.5f, -kAllH * 0.5f, kAllW, kAllH, fT.pillFill, fT.outline, 3.f);
            drawText(fFontHeavy, 12.f, fT.text, 0.f, 0.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "ALL OFF", 0.3f);
            restore();
            caption(kAllX + kAllW * 0.5f, "PANIC", fT.muted);
        }
        {
            const float lt = fLimitT.x;
            pill(kLimX, kLimY, kLimW, kLimH, Color(fDark ? fT.pillFill : fT.text, kAmberAcc, clamp01(lt)), fT.outline, 3.f);
            const float thumbX = kLimX + 3.f + 10.f + (kLimW - 6.f - 20.f) * lt;
            beginPath(); circle(thumbX, kLimY + kLimH * 0.5f, 8.f); fillColor(fT.face); fill(); strokeColor(fT.outline); strokeWidth(2.f); stroke();
            caption(kLimX + kLimW * 0.5f, "LIMIT", fT.text);
        }
    }

    void chip(float cx, float cy, const char* l)
    {
        beginPath(); roundedRect(cx - 15.f, cy - 11.f, 30.f, 22.f, 11.f); fillColor(kAmberAcc); fill();
        strokeColor(fT.outline); strokeWidth(2.f); stroke();
        drawText(fFontHeavy, 11.f, hex(0x0A1A33), cx, cy, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, l);
    }
    static std::string upper(const char* s) { std::string r(s); for (char& c : r) c = (char)std::toupper((unsigned char)c); return r; }

    // a knob in the bar: name above, cream knob with its dot ring, value below in amber
    void barKnob(int p, float cx, float cy)
    {
        const Color cream = hex(0xF7F1E3);
        drawText(fFontHeavy, 12.5f, cream, cx, kBarY + 22.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, kSpecs[p].label, 1.2f);
        const bool bip = p == pSpeed && kSpecs[pSpeed].min < 0.f;
        const float pop = fPop[p].x, n = clamp01(fKnob[p].x);
        for (int i = 0; i < 11; ++i) {
            const float t = i / 10.f;
            const float a = (-135.f + 270.f * t) * kPi / 180.f;
            const bool lit = bip ? (t >= std::fmin(0.5f, n) - 0.001f && t <= std::fmax(0.5f, n) + 0.001f) : t <= n + 0.001f;
            beginPath();
            circle(cx + kKnobDot * std::sin(a), cy - kKnobDot * std::cos(a), lit ? 2.3f + pop * 0.3f : 1.7f);
            fillColor(lit ? kAmberAcc : withAlpha(cream, 0.4f));
            fill();
        }
        const float rr = kKnobR * (1.f + pop * 0.04f);
        beginPath(); circle(cx, cy + 4.f, rr); fillColor(withAlpha(hex(0x000000), 0.35f)); fill();
        beginPath(); circle(cx, cy, rr - 1.5f); fillColor(fT.face); fill();
        strokeColor(fT.outline); strokeWidth(3.f); stroke();
        const float ang = (-135.f + 270.f * n) * kPi / 180.f;
        const float sa = std::sin(ang), ca = std::cos(ang);
        beginPath();
        moveTo(cx + sa * 3.f, cy - ca * 3.f);
        lineTo(cx + sa * (rr - 7.f), cy - ca * (rr - 7.f));
        lineCap(NanoVG::ROUND);
        strokeColor(fT.outline); strokeWidth(3.5f); stroke();
        char v[32]; formatValue(p, fP[p], v, sizeof(v));
        drawText(pop > 0.3f ? fFontHeavy : fFontBold, 13.f, kAmberAcc, cx, kBarY + kBarH - 20.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, v);
    }

    // a fader in a cream well, with its level meter
    void fader(int p, float x, float y, int meter)
    {
        const float w = kFadW, h = kFadH;
        beginPath(); roundedRect(x, y + 4.f, w, h, 16.f); fillColor(withAlpha(fT.outline, 0.9f)); fill();
        beginPath(); roundedRect(x, y, w, h, 16.f); fillColor(fT.face); fill(); strokeColor(fT.outline); strokeWidth(3.f); stroke();
        const Color ink = fT.faceInk;
        drawText(fFontHeavy, 10.5f, ink, x + w * 0.5f, y + 20.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, p == pInA || p == pInB ? "INPUT" : "OUTPUT", 1.2f);
        const float tx = x + w * 0.38f, t0 = y + 40.f, t1 = y + h - 44.f;
        beginPath(); roundedRect(tx - 3.f, t0, 6.f, t1 - t0, 3.f); fillColor(withAlpha(ink, 0.85f)); fill();
        for (int i = 0; i < 7; ++i) {
            const float yy = t0 + (t1 - t0) * i / 6.f;
            beginPath(); moveTo(tx - 13.f, yy); lineTo(tx - 8.f, yy); strokeColor(withAlpha(ink, 0.45f)); strokeWidth(1.5f); stroke();
        }
        // unity gain mark
        {
            const float un = normOf(p, 0.f), uy = t1 - (t1 - t0) * un;
            beginPath(); moveTo(tx - 15.f, uy); lineTo(tx - 8.f, uy); strokeColor(ink); strokeWidth(2.5f); stroke();
        }
        const float n = clamp01(fKnob[p].x), ty = t1 - (t1 - t0) * n, pop = fPop[p].x;
        const float tw = 30.f + 3.f * pop;
        beginPath(); roundedRect(tx - tw * 0.5f, ty - 8.f + 3.f, tw, 16.f, 5.f); fillColor(fT.outline); fill();
        beginPath(); roundedRect(tx - tw * 0.5f, ty - 8.f, tw, 16.f, 5.f); fillColor(kAmberAcc); fill(); strokeColor(fT.outline); strokeWidth(2.5f); stroke();
        beginPath(); moveTo(tx - 8.f, ty); lineTo(tx + 8.f, ty); strokeColor(hex(0x01295F)); strokeWidth(2.f); stroke();
        // meter: -48 dB at the bottom, +6 at the top; the top two lights are red
        const float mx = x + w * 0.74f;
        const int nseg = 14;
        const float lvl = (fMeterDb[meter] + 48.f) / 54.f;
        for (int i = 0; i < nseg; ++i) {
            const float yy = t1 - (t1 - t0) * (i + 0.5f) / nseg;
            const bool lit = (float)i / nseg < lvl;
            const Color c = i >= nseg - 2 ? kRedAcc : kAmberAcc;
            beginPath(); roundedRect(mx - 5.f, yy - 3.f, 10.f, 6.f, 2.f);
            fillColor(lit ? c : withAlpha(ink, 0.15f)); fill();
        }
        char v[32]; formatValue(p, fP[p], v, sizeof(v));
        drawText(pop > 0.3f ? fFontHeavy : fFontBold, 12.f, ink, x + w * 0.5f, y + h - 20.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, v);
    }

    // the round screen: big amber digits over faint 8s, the control's name under them
    void roundScreen(int side, float cx, float cy)
    {
        const float r = kScrR;
        beginPath(); circle(cx, cy + 5.f, r); fillColor(withAlpha(fT.outline, 0.95f)); fill();
        beginPath(); circle(cx, cy, r); fillColor(fT.screen); fill(); strokeColor(fT.outline); strokeWidth(3.f); stroke();
        beginPath(); circle(cx, cy, r - 9.f); strokeColor(withAlpha(hex(0xF7F1E3), 0.18f)); strokeWidth(2.f); stroke();
        drawText(fFontHeavy, 12.5f, hex(0xF7F1E3), cx, cy - r * 0.52f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, kSideName[side], 1.7f);
        const float w = r * 1.5f, h = r * 0.62f, gx = cx - w * 0.5f, gy = cy - h * 0.5f - 4.f;
        beginPath(); roundedRect(gx, gy, w, h, 10.f); fillColor(fT.glass); fill(); strokeColor(hex(0x05090F)); strokeWidth(3.f); stroke();
        beginPath(); roundedRect(gx + 4.f, gy + 4.f, w - 8.f, h * 0.32f, 7.f); fillColor(withAlpha(hex(0xFFFFFF), 0.04f)); fill();

        // what it shows: learning, a pickup hint, or the control (the one under the mouse, else the last touched)
        char digits[16], cap[64];
        const int p = shownParam(side);
        screenValue(p, fP[p], digits, sizeof(digits), cap, sizeof(cap));
        bool blink = false;
        if (fLearnSlot >= 0 && learnSide(fLearnSlot) == side) {
            std::snprintf(digits, sizeof(digits), "---");
            std::snprintf(cap, sizeof(cap), "%s", fLearnSlot >= kLearnKnob0 ? "TURN A KNOB" : "HIT A PAD");
            blink = true;
        }
        const float ma = clamp01(fMsgA[side].x);
        const float ds = 22.f * kScrR / 96.f * 1.0f;
        const float dw = r * 0.36f, by = cy + h * 0.5f - 4.f - h * 0.17f;
        const float em = r * 0.6f / 1.302f;
        const float glow = blink ? 0.55f + 0.45f * std::sin((float)now() * 6.f) : 1.f;
        (void)ds;
        // digits right-aligned in three slots, like a real readout
        const size_t len = std::strlen(digits);
        char slots[3] = { ' ', ' ', ' ' };
        int filled = 0;
        for (int i = (int)len - 1, k = 2; i >= 0 && k >= 0; --i) {
            if (digits[i] == '.' && k < 2) continue;        // a decimal point rides on the digit before it
            slots[k--] = digits[i]; ++filled;
        }
        const char* dot = std::strchr(digits, '.');
        for (int k = 0; k < 3; ++k) {
            const float dx = cx + (float)(k - 1) * dw;
            drawText(fFontHeavy, em, withAlpha(kAmberAcc, 0.09f), dx, by, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_BASELINE, "8");
            if (slots[k] != ' ') { char c[2] = { slots[k], 0 }; drawText(fFontHeavy, em, withAlpha(kAmberAcc, glow), dx, by, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_BASELINE, c); }
        }
        if (dot) { beginPath(); circle(cx + 0.5f * dw, by - 2.f, 3.f); fillColor(kAmberAcc); fill(); }
        // the caption, or a message in its place for a moment
        const float capY = cy + r * 0.62f;
        if (ma < 0.99f) drawText(fFontHeavy, 10.f, withAlpha(kAmberAcc, 0.85f * (1.f - ma)), cx, capY + 4.f * ma, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, cap, 1.4f);
        if (ma > 0.01f) {
            const std::string m = fitText(fFontHeavy, 10.f, fMsg[side], r * 1.6f, 1.4f);
            drawText(fFontHeavy, 10.f, withAlpha(kAmberAcc, ma), cx, capY - 4.f * (1.f - ma), NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, m.c_str(), 1.4f);
        }
    }

    // a panel: steel, outlined, with a hard shadow; its name, the bar, the faders, the screen and its switch
    void drawPanel(int side)
    {
        save();
        translate(cardX(side), kCardY);
        beginPath(); roundedRect(0.f, 6.f, kCardW, kCardH, 22.f); fillColor(withAlpha(fT.shadow, fT.shadowA)); fill();
        beginPath(); roundedRect(1.5f, 1.5f, kCardW - 3.f, kCardH - 3.f, 20.5f); fillColor(fT.card); fill();
        strokeColor(fT.outline); strokeWidth(3.f); stroke();
        const Color ink = fT.cardInk;
        const char* name = kSideName[side];
        const float nw = std::round(measure(fFontHeavy, 13.f, name, 1.3f) + 30.f);
        float tx = kCardW * 0.5f - nw * 0.5f;
        if (kKind == kKindBoth) {
            tx = kCardW * 0.5f - (nw + 42.f) * 0.5f;
            beginPath(); roundedRect(tx, 18.f, 34.f, 26.f, 13.f); fillColor(kAmberAcc); fill(); strokeColor(fT.outline); strokeWidth(2.5f); stroke();
            drawText(fFontHeavy, 12.f, hex(0x0A1A33), tx + 17.f, 31.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, side == 0 ? "A" : "B");
            tx += 42.f;
        }
        beginPath(); roundedRect(tx, 18.f, nw, 26.f, 13.f); strokeColor(ink); strokeWidth(2.f); stroke();
        drawText(fFontHeavy, 13.f, ink, tx + nw * 0.5f, 31.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, name, 1.3f);
        drawText(fFontBold, 11.f, withAlpha(ink, 0.75f), kCardW * 0.5f, 58.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, kSideSub[side], 0.45f);
        // KNOBS: this side has the controller's knobs right now (Airliner, with knobs linked, in the follow layout)
        if (kKind == kKindBoth && followTagShown()) {
            const float f = clamp01(side == 0 ? 1.f - fSideT.x : fSideT.x);
            const float tw = 58.f, tgx = kCardW - 20.f - tw, tgy = 19.f, th = 24.f;
            beginPath(); roundedRect(tgx, tgy, tw, th, th * 0.5f);
            fillColor(withAlpha(kAmberAcc, f)); fill();
            strokeColor(withAlpha(ink, 0.35f + 0.65f * f)); strokeWidth(2.f); stroke();
            drawText(fFontHeavy, 9.5f, withAlpha(ink, 0.45f + 0.55f * f), tgx + tw * 0.5f, tgy + th * 0.5f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "KNOBS", 0.5f);
        }
        // the bar
        beginPath(); roundedRect(kBarX, kBarY, kBarW, kBarH, kBarH * 0.5f); fillColor(fT.bar); fill();
        beginPath(); roundedRect(kBarX + 4.f, kBarY + 4.f, kBarW - 8.f, kBarH - 8.f, kBarH * 0.5f - 4.f); strokeColor(withAlpha(hex(0xF7F1E3), 0.12f)); strokeWidth(2.f); stroke();
        for (int i = 0; i < 3; ++i) {
            const int p = kSlotParam[side][i];
            if (p >= 0) barKnob(p, slotCX(i), kBarY + kSlotCY - kBarY);
            else loopButton(slotCX(i), kSlotCY - 2.f);
        }
        fader(kFadParam[side][0], kFadInX, kFadY, side * 2);
        fader(kFadParam[side][1], kFadOutX, kFadY, side * 2 + 1);
        roundScreen(side, kScrCX, kScrCY);
        if (side == 0) {
            drawText(fFontHeavy, 9.5f, ink, kSegX - 10.f, kSegY + kSegH * 0.5f, NanoVG::ALIGN_RIGHT | NanoVG::ALIGN_MIDDLE, "LOOP BUTTON", 1.1f);
            segSwitch(kSegX, kSegY, kSegW, kSegH, fModeT.x, "HOLD", "LATCH", fT.face, fT.outline, fT.sel, fT.selInk, fT.faceInk);
        } else {
            onPill(kOnX, kOnY, kOnW, kOnH, fVinT.x, fVinPress.x);
        }
        restore();
    }

    // the LOOP button, in the bar's third slot, with a ring for where the loop is
    void loopButton(float cx, float cy)
    {
        const Color cream = hex(0xF7F1E3);
        drawText(fFontHeavy, 12.5f, cream, cx, kBarY + 22.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "LOOP", 1.2f);
        const float rr = kLoopR + 10.f;
        if (fRingA.x > 0.01f) {
            const float ph = fP[oPhase] >= 0.f ? fP[oPhase] : fLastPhase;
            if (fP[oPhase] >= 0.f) fLastPhase = fP[oPhase];
            beginPath(); circle(cx, cy + 2.f, rr); strokeColor(withAlpha(cream, 0.25f * fRingA.x)); strokeWidth(3.f); stroke();
            beginPath(); arc(cx, cy + 2.f, rr, -kPi * 0.5f, -kPi * 0.5f + 2.f * kPi * clamp01(ph) + 0.001f, NanoVG::CW);
            lineCap(NanoVG::ROUND); strokeColor(withAlpha(kAmberAcc, fRingA.x)); strokeWidth(3.5f); stroke();
        }
        const float on = clamp01(fLoopT.x), press = fLoopPress.x;
        const float r = kLoopR * (1.f - 0.04f * press);
        beginPath(); circle(cx, cy + 4.f, r); fillColor(fT.outline); fill();
        const float fy = cy + 4.f * std::max(press, 0.5f * on);
        beginPath(); circle(cx, fy, r); fillColor(Color(fT.face, kRedAcc, on)); fill();
        strokeColor(fT.outline); strokeWidth(3.f); stroke();
        drawText(fFontHeavy, 13.f, Color(fT.faceInk, cream, on), cx, fy, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "LOOP", 0.9f);
        drawText(fFontBold, 13.f, kAmberAcc, cx, kBarY + kBarH - 20.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, fP[pLoop] > 0.5f ? "LOOPING" : "OFF");
    }

    // the Vinyl Sim's ON button: a pill that sinks onto its shadow and goes red
    void onPill(float x, float y, float w, float h, float on, float press)
    {
        on = clamp01(on);
        const float sink = 3.f * std::max(press, 0.6f * on);
        beginPath(); roundedRect(x, y + 3.f, w, h, h * 0.5f); fillColor(fT.outline); fill();
        beginPath(); roundedRect(x, y + sink, w, h, h * 0.5f); fillColor(Color(fT.face, kRedAcc, on)); fill();
        strokeColor(fT.outline); strokeWidth(2.5f); stroke();
        drawText(fFontHeavy, 12.f, Color(fT.faceInk, hex(0xF7F1E3), on), x + w * 0.5f, y + sink + h * 0.5f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "ON", 1.2f);
    }

    int shownParam(int side) const
    {
        if (fDrag >= 0 && sideOfParam(fDrag) == side) return fDrag;
        if (fHover >= 0 && sideOfParam(fHover) == side) return fHover;
        return fShown[side];
    }

    // --------------------------------------------------------------------------------------------
    // the preset list

    struct MenuItem {
        std::string label; int act; bool header; bool check;
        MenuItem(const std::string& l, int a, bool h, bool c = false) : label(l), act(a), header(h), check(c) {}
    };
    std::vector<MenuItem> fMenu;
    bool fMenuOpen = false, fMenuIsPreset = false;
    int fMenuHover = -1;
    float kMenuX = kPresetX, kMenuY = kPresetY + kPresetH + 8.f, kMenuW = 230.f;
    float itemH(const MenuItem& it) const { return it.header ? 22.f : 26.f; }
    float menuH() const { float h = 12.f; for (const MenuItem& it : fMenu) h += itemH(it); return h; }

    void openMenu()
    {
        fMenu.clear();
        fMenu.push_back({ "BUILT IN", 0, true });
        for (int i = 0; i < kNumPresets; ++i) if (presetInBuild(i)) fMenu.push_back({ kPresets[i].name, kActPreset0 + i, false });
        if (!fUser.empty()) {
            fMenu.push_back({ "YOURS", 0, true });
            for (size_t i = 0; i < fUser.size() && i < 90; ++i) fMenu.push_back({ fUser[i], kActUser0 + (int)i, false });
        }
        fMenu.push_back({ "", 0, true });
        fMenu.push_back({ "Save preset...", kActSave, false });
        if (isUserPreset()) fMenu.push_back({ "Delete this preset", kActDelete, false });
        kMenuX = kPresetX; kMenuY = kPresetY + kPresetH + 8.f; kMenuW = 230.f;
        fMenuOpen = true; fMenuIsPreset = true; fMenuHover = -1;
        fMenuA.set(1.f);
        wake();
    }
    void closeMenu() { fMenuOpen = false; fMenuA.set(0.f); wake(); }
    int menuItemAt(float x, float y) const
    {
        if (x < kMenuX || x > kMenuX + kMenuW) return -1;
        float iy = kMenuY + 6.f;
        for (size_t i = 0; i < fMenu.size(); ++i) {
            const float h = itemH(fMenu[i]);
            if (y >= iy && y < iy + h) return fMenu[i].header ? -1 : (int)i;
            iy += h;
        }
        return -1;
    }
    void drawMenu()
    {
        const float a = clamp01(fMenuA.x);
        const float h = menuH() * (0.92f + 0.08f * a);
        globalAlpha(a);
        save();
        translate(0.f, -6.f * (1.f - a));
        beginPath(); roundedRect(kMenuX, kMenuY + 4.f, kMenuW, h, 12.f); fillColor(withAlpha(fT.shadow, fT.shadowA)); fill();
        beginPath(); roundedRect(kMenuX, kMenuY, kMenuW, h, 12.f); fillColor(fT.menuBg); fill();
        strokeColor(fDark ? fT.outline : fT.text); strokeWidth(3.f); stroke();
        save();
        intersectScissor(kMenuX, kMenuY, kMenuW, h);
        float iy = kMenuY + 6.f;
        for (size_t i = 0; i < fMenu.size(); ++i) {
            const MenuItem& it = fMenu[i];
            const float ih = itemH(it);
            if (it.header) {
                if (it.label.empty()) {
                    beginPath(); moveTo(kMenuX + 12.f, iy + ih * 0.5f); lineTo(kMenuX + kMenuW - 12.f, iy + ih * 0.5f);
                    strokeColor(withAlpha(fT.menuText, 0.2f)); strokeWidth(1.f); stroke();
                } else drawText(fFontHeavy, 9.f, withAlpha(fT.menuText, 0.5f), kMenuX + 14.f, iy + ih * 0.5f + 2.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, it.label.c_str(), 0.6f);
            } else {
                const bool hov = (int)i == fMenuHover;
                if (hov) { beginPath(); roundedRect(kMenuX + 6.f, iy, kMenuW - 12.f, ih, 8.f); fillColor(fT.menuHover); fill(); }
                const Color c = hov ? fT.menuHoverText : fT.menuText;
                const std::string lb = fitText(fFontBold, 12.f, it.label, kMenuW - 56.f, 0.f);
                drawText(fFontBold, 12.f, it.act == kActDelete && !hov ? kRedAcc : c, kMenuX + 14.f, iy + ih * 0.5f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, lb.c_str());
                if (it.check || (it.act >= kActPreset0 && it.label == fPresetName)) {
                    const float ccx = kMenuX + kMenuW - 22.f, ccy = iy + ih * 0.5f;
                    beginPath(); moveTo(ccx - 5.f, ccy); lineTo(ccx - 1.5f, ccy + 3.5f); lineTo(ccx + 5.f, ccy - 4.f);
                    lineCap(NanoVG::ROUND); lineJoin(NanoVG::ROUND); strokeColor(c); strokeWidth(2.2f); stroke();
                }
            }
            iy += ih;
        }
        restore();
        restore();
        globalAlpha(1.f);
    }
    void runMenu(int act)
    {
        if (act >= kActUser0) { const int i = act - kActUser0; if (i < (int)fUser.size()) loadUserPreset(fUser[(size_t)i]); }
        else if (act >= kActPreset0) loadFactory(act - kActPreset0);
        else if (act == kActSave) saveUserPreset();
        else if (act == kActDelete) deleteUserPreset();
        else if (act == kActLearn) startLearn(fMenuSlot);
        else if (act == kActForget) forgetLink(fMenuSlot);
        else if (act == kActFollow || act == kActEach) { fOneEach = act == kActEach; saveController(); showMsg(fOneEach ? "ONE KNOB PER CONTROL" : "KNOBS FOLLOW THE SIDE YOU PRESS", -1); }
        else if (act == kActPickup) { fJump = !fJump; saveController(); showMsg(fJump ? "PICKUP OFF" : "PICKUP ON", -1); }
        else if (act == kActForgetAll) { for (int i = 0; i < kNumLearn; ++i) fMap[i] = -1; saveController(); showMsg("ALL LINKS FORGOTTEN", -1); }
    }

    // --------------------------------------------------------------------------------------------
    // the MIDI controller: links are saved on this computer (Airliner's settings file), not in projects

    int fMap[kNumLearn] = { -1, -1, -1, -1, -1, -1, -1, -1 };
    bool fJump = false, fOneEach = false;
    int fLearnSlot = -1, fMenuSlot = -1, fCtlSaves = 0;
    float fLastLearnSeq = 0.f, fLastCtlSeq = 0.f;
    double fOpenedAt = now();
    Spring fSideT;

    void parseMap(const char* c)
    {
        for (int i = 0; i < kNumLearn; ++i) fMap[i] = -1;
        for (int i = 0; i < kNumLearn && c && *c; ++i) {
            fMap[i] = std::atoi(c);
            while (*c && *c != ',') ++c;
            if (*c == ',') ++c;
        }
    }
    void saveController()
    {
        yancey::Prefs up;
        for (int i = 0; i < kNumLearn; ++i) { char b[16]; std::snprintf(b, sizeof(b), i ? ",%d" : "%d", fMap[i]); up.map += b; }
        up.kjump = fJump ? 1 : 0;
        up.kgrid = fOneEach ? 1 : 0;
        yancey::writePrefs(up);
        char n[16]; std::snprintf(n, sizeof(n), "%d", ++fCtlSaves);
        setState("ctl", n);         // the plug-in reads it again
        wake();
    }
    bool anyKnobLinked() const { for (int j = 0; j < 3; ++j) if (fMap[kLearnKnob0 + j] >= 0) return true; return false; }
    bool followTagShown() const { return !fOneEach && anyKnobLinked(); }
    static std::string slotName(int slot)
    {
        if (slot == kLearnLoop) return "LOOP PAD";
        if (slot == kLearnVinyl) return "ON PAD";
        char b[16]; std::snprintf(b, sizeof(b), "KNOB %d", slot - kLearnKnob0 + 1); return b;
    }
    static std::string codeName(int code)
    {
        char b[32];
        if (code < 0) return "";
        std::snprintf(b, sizeof(b), (code & 0x4000) ? "CC %d \xC2\xB7 CH %d" : "NOTE %d \xC2\xB7 CH %d", code & 127, ((code >> 7) & 15) + 1);
        return b;
    }
    void startLearn(int slot)
    {
        if (slot < 0 || slot >= kNumLearn) return;
        fLearnSlot = slot;
        char b[32]; std::snprintf(b, sizeof(b), "%d:%d", slot, (int)std::lround(fP[oNonce]));
        setState("learn", b);
        wake();
    }
    void cancelLearn()
    {
        if (fLearnSlot < 0) return;
        fLearnSlot = -1;
        setState("learn", "-1");
        wake();
    }
    void forgetLink(int slot)
    {
        if (slot < 0 || slot >= kNumLearn) return;
        fMap[slot] = -1;
        saveController();
        showMsg(("FORGOT " + slotName(slot)).c_str(), learnSide(slot));
    }

    // what the controller did, from the plug-in: learned links, and knob and pad moves to pass on to the host
    void controllerIdle()
    {
        if (fP[oLearnSeq] != fLastLearnSeq) {
            fLastLearnSeq = fP[oLearnSeq];
            const int v = (int)std::lround(fP[oLearned]);
            const int slot = v / 32768 - 1, code = v % 32768;
            if (slot >= 0 && slot < kNumLearn) {
                for (int i = 0; i < kNumLearn; ++i) if (fMap[i] == code) fMap[i] = -1;
                fMap[slot] = code;
                fLearnSlot = -1;
                setState("learn", "-1");
                saveController();
                showMsg(("LINKED " + codeName(code)).c_str(), learnSide(slot));
                if (fWiz > 0 && wizSlot(fWiz) == slot) { fWizCode = code; fWizLinkedAt = now(); }
            }
        }
        if (fP[oCtlSeq] != fLastCtlSeq) {
            fLastCtlSeq = fP[oCtlSeq];
            for (int k = 0; k < 8; ++k) {
                const int p = kMirrorParam[k];
                const float mv = fP[oMirror0 + k];
                if (p == fDrag || std::fabs(mv - fP[p]) < 1e-5f) continue;
                setParam(p, mv);
                fKnob[p].snap(normOf(p, mv));       // a hand on a knob: the needle follows it 1:1
            }
            const int t = (int)std::lround(fP[oTouch]) / 4, st = (int)std::lround(fP[oTouch]) % 4;
            if (t >= 0 && t < kParamCount && isKnob(t) && now() - fOpenedAt > 0.5) {
                showKnob(t);
                if (st == 1 || st == 2) {
                    char m[64]; std::snprintf(m, sizeof(m), "TURN %s TO PICK UP", st == 1 ? "UP" : "DOWN");
                    showMsg(m, sideOfParam(t));
                }
            }
            wake();
        }
    }

    // which panel a link slot belongs to (the knobs: the side they're on now)
    int learnSide(int slot) const
    {
        if (slot == kLearnLoop) return 0;
        if (slot == kLearnVinyl) return 1;
        const int j = slot - kLearnKnob0;
        for (int s = 0; s < 2; ++s) {
            const int p = knobTarget(j, fOneEach, s);
            if (p >= 0 && sideShown(sideOfParam(p)) && (kKind != kKindBoth || fOneEach || s == (fSideT.target > 0.5f ? 1 : 0))) return sideOfParam(p);
        }
        return kHasLooper ? 0 : 1;
    }

    // a breathing ring around whatever is waiting for the controller
    void drawLearnRing()
    {
        if (fLearnSlot < 0) return;
        const float a = 0.55f + 0.45f * std::sin((float)now() * 6.f);
        auto ring = [&](float cx, float cy, float r) {
            beginPath(); circle(cx, cy, r);
            strokeColor(withAlpha(kAmberAcc, a)); strokeWidth(4.f); stroke();
        };
        if (fLearnSlot == kLearnLoop) { if (kHasLooper) ring(cardX(0) + slotCX(2), kCardY + kSlotCY, kLoopR + 16.f); return; }
        if (fLearnSlot == kLearnVinyl) {
            if (kHasVinyl) { beginPath(); roundedRect(cardX(1) + kOnX - 6.f, kCardY + kOnY - 6.f, kOnW + 12.f, kOnH + 14.f, 22.f); strokeColor(withAlpha(kAmberAcc, a)); strokeWidth(4.f); stroke(); }
            return;
        }
        const int j = fLearnSlot - kLearnKnob0;
        for (int s = 0; s < 2; ++s) {
            const int p = knobTarget(j, fOneEach, s);
            const int ps = p >= 0 ? sideOfParam(p) : -1;
            if (ps < 0 || !sideShown(ps)) continue;
            for (int i = 0; i < 3; ++i) if (kSlotParam[ps][i] == p) ring(cardX(ps) + slotCX(i), kCardY + kSlotCY, kKnobDot + 10.f);
            for (int i = 0; i < 2; ++i) if (kFadParam[ps][i] == p) {
                beginPath(); roundedRect(cardX(ps) + (i ? kFadOutX : kFadInX) - 6.f, kCardY + kFadY - 6.f, kFadW + 12.f, kFadH + 14.f, 20.f);
                strokeColor(withAlpha(kAmberAcc, a)); strokeWidth(4.f); stroke();
            }
        }
    }

    // --------------------------------------------------------------------------------------------
    // SET UP CONTROLLER: a guided setup, like Dewitt's. Steps: 0 how many knobs, 1 the LOOP pad, 2 the ON pad,
    // 3.. each knob, then a summary. Each link step waits for the controller (the same learning as right-click),
    // shows what it got for a moment and moves on. SKIP leaves a link as it is; BACK goes a step back.

    enum { kWzClose = 1, kWzThree, kWzSix, kWzMyself, kWzSkip, kWzBack, kWzDone };
    int fWiz = -1, fWizShown = 0, fWizKnobs = 3, fWizHover = -1, fWizDown = -1, fWizCode = -1;
    double fWizLinkedAt = 0.0;
    Spring fWizA, fWizStepA, fSetupPress, fWizPress;
    bool fSetupHover = false, fSetupDown = false;

    float setupX() { return std::max(moonCX() + 12.f + 22.f, kSetupR - 168.f); }
    bool onSetupButton(float x, float y) { return inRect(x, y, setupX(), kSetupY, kSetupR - setupX(), kSetupH); }
    bool anyLinked() const { for (int i = 0; i < kNumLearn; ++i) if (fMap[i] >= 0) return true; return false; }
    void drawSetupButton()
    {
        const float x0 = setupX(), w = kSetupR - x0;
        const float ps = 1.f - 0.05f * fSetupPress.x;
        save();
        translate(x0 + w * 0.5f, kSetupY + kSetupH * 0.5f); scale(ps, ps);
        const Color fill_ = fSetupHover ? fT.text : fT.pillFill, ink = fSetupHover ? fT.bg : fT.text;
        pill(-w * 0.5f, -kSetupH * 0.5f, w, kSetupH, fill_, fT.outline, 3.f);
        const bool dot = !anyLinked();      // nothing linked yet: an amber dot says "start here"
        const float tx = dot ? 7.f : 0.f;
        if (dot) { beginPath(); circle(-w * 0.5f + 16.f, 0.f, 4.5f); fillColor(kAmberAcc); fill(); strokeColor(fT.outline); strokeWidth(1.5f); stroke(); }
        drawText(fFontHeavy, 11.f, ink, tx, 0.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "SET UP CONTROLLER", 0.3f);
        restore();
        caption(x0 + w * 0.5f, "MIDI", fT.muted);
    }

    int wizLast() const { return 3 + fWizKnobs; }
    int wizSlot(int step) const
    {
        if (step == 1) return kLearnLoop;
        if (step == 2) return kLearnVinyl;
        if (step >= 3 && step < wizLast()) return kLearnKnob0 + (step - 3);
        return -1;
    }
    void openWizard()
    {
        if (fMenuOpen) closeMenu();
        cancelLearn();
        fWizKnobs = fOneEach ? 6 : 3;
        fWizHover = fWizDown = -1;
        fSetupHover = false;
        fWizA.set(1.f);
        wizGo(0);
    }
    void closeWizard()
    {
        cancelLearn();
        fWizLinkedAt = 0.0;
        fWiz = -1;
        fWizA.set(0.f);
        wake();
    }
    void wizGo(int step)
    {
        if (step < 0) step = 0;
        if (step > wizLast()) step = wizLast();
        fWiz = fWizShown = step;
        fWizLinkedAt = 0.0; fWizCode = -1;
        fWizStepA.snap(0.f); fWizStepA.set(1.f);
        const int slot = wizSlot(step);
        if (slot >= 0) startLearn(slot); else cancelLearn();
        wake();
    }
    void wizClick(int h)
    {
        if (h == kWzClose || h == kWzMyself || h == kWzDone) { closeWizard(); return; }
        if (h == kWzThree || h == kWzSix) {
            fWizKnobs = h == kWzSix ? 6 : 3;
            if (fOneEach != (h == kWzSix)) { fOneEach = h == kWzSix; saveController(); }
            wizGo(1);
            return;
        }
        if (h == kWzSkip && fWizLinkedAt == 0.0) { wizGo(fWiz + 1); return; }
        if (h == kWzBack && fWizLinkedAt == 0.0) { wizGo(fWiz - 1); return; }
    }

    // the card's parts (design units)
    static constexpr float kWzChoiceW = 250.f, kWzChoiceH = 150.f, kWzChoiceY = kWizY + 132.f;
    static float choiceX(int i) { return kW * 0.5f + (i == 0 ? -kWzChoiceW - 12.f : 12.f); }
    static constexpr float kWzBtnW = 96.f, kWzBtnH = 32.f, kWzBtnY = kWizY + kWizH - 52.f;
    static float wzBtnX(int i) { return kWizX + kWizW - 28.f - kWzBtnW - (float)i * (kWzBtnW + 10.f); }   // 0: rightmost
    int wizHit(float x, float y) const
    {
        if (fWiz < 0) return -1;
        if (inCircle(x, y, kWizX + kWizW - 30.f, kWizY + 30.f, 16.f)) return kWzClose;
        if (fWiz == 0) {
            for (int i = 0; i < 2; ++i) if (inRect(x, y, choiceX(i), kWzChoiceY, kWzChoiceW, kWzChoiceH)) return i == 0 ? kWzThree : kWzSix;
            if (inRect(x, y, kW * 0.5f - 170.f, kWizY + kWizH - 46.f, 340.f, 26.f)) return kWzMyself;
            return -1;
        }
        if (fWiz == wizLast()) return inRect(x, y, wzBtnX(0), kWzBtnY, kWzBtnW, kWzBtnH) ? kWzDone : -1;
        if (inRect(x, y, wzBtnX(0), kWzBtnY, kWzBtnW, kWzBtnH)) return kWzSkip;
        if (inRect(x, y, wzBtnX(1), kWzBtnY, kWzBtnW, kWzBtnH)) return kWzBack;
        return -1;
    }

    // what a knob runs, in words
    std::string knobJob(int j) const
    {
        if (fWizKnobs == 6) { const int p = kEachTarget[j]; return p == pOutB ? std::string("Runs the Vinyl Sim's OUTPUT.") : std::string("Runs ") + kSpecs[p].screen + "."; }
        if (j == 0) return std::string("Runs ") + kSpecs[pLen].screen + " on the DJ Looper and " + kSpecs[pComp].screen + " on the Vinyl Sim.";
        if (j == 1) return std::string("Runs ") + kSpecs[pSpeed].screen + " on the DJ Looper and " + kSpecs[pNoise].screen + " on the Vinyl Sim.";
        return std::string("Runs ") + kSpecs[pFlu].screen + " on the Vinyl Sim.";
    }
    void textCenter(FontId f, float em, const Color& c, float cx, float y, const char* str, float maxW, float lineH, float spacing = 0.f)
    {
        std::string line, word; std::vector<std::string> lines;
        for (const char* p = str; ; ++p) {
            if (*p == ' ' || *p == '\0') {
                const std::string tryL = line.empty() ? word : line + " " + word;
                if (!line.empty() && measure(f, em, tryL.c_str(), spacing) > maxW) { lines.push_back(line); line = word; }
                else line = tryL;
                word.clear();
                if (*p == '\0') break;
            } else word += *p;
        }
        if (!line.empty()) lines.push_back(line);
        for (size_t i = 0; i < lines.size(); ++i)
            drawText(f, em, c, cx, y + lineH * (float)i, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, lines[i].c_str(), spacing);
    }
    void wizButton(int id, float x, const char* label, bool primary)
    {
        const bool hov = fWizHover == id;
        const float ps = (fWizDown == id) ? 1.f - 0.05f * fWizPress.x : 1.f;
        save();
        translate(x + kWzBtnW * 0.5f, kWzBtnY + kWzBtnH * 0.5f); scale(ps, ps);
        const Color f = primary ? (hov ? fT.text : kAmberAcc) : (hov ? fT.text : fT.pillFill);
        const Color ink = primary ? (hov ? fT.bg : hex(0x0A1A33)) : (hov ? fT.bg : fT.text);
        pill(-kWzBtnW * 0.5f, -kWzBtnH * 0.5f, kWzBtnW, kWzBtnH, f, fT.outline, 3.f);
        drawText(fFontHeavy, 12.f, ink, 0.f, 0.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, label, 0.3f);
        restore();
    }

    void drawWizard()
    {
        const float a = clamp01(fWizA.x);
        const int step = fWizShown;
        globalAlpha(a);
        beginPath(); rect(0, 0, kW, std::max(kH, (float)getHeight() / ((float)getWidth() / kW)));
        fillColor(withAlpha(fT.shadow, fDark ? 0.55f : 0.35f)); fill();
        save();
        const float lift = 10.f * (1.f - a);
        translate(0.f, lift);
        beginPath(); roundedRect(kWizX, kWizY + 6.f, kWizW, kWizH, 18.f); fillColor(withAlpha(fT.shadow, fT.shadowA)); fill();
        beginPath(); roundedRect(kWizX, kWizY, kWizW, kWizH, 18.f); fillColor(fT.menuBg); fill();
        strokeColor(fDark ? fT.outline : fT.text); strokeWidth(3.f); stroke();
        const Color ink = fT.menuText, soft = withAlpha(fT.menuText, 0.62f);
        // title row
        drawText(fFontHeavy, 12.f, ink, kWizX + 28.f, kWizY + 30.f, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, "SET UP YOUR CONTROLLER", 1.2f);
        {
            const float cx = kWizX + kWizW - 30.f, cy = kWizY + 30.f;
            const bool hov = fWizHover == kWzClose;
            beginPath(); circle(cx, cy, 14.f); fillColor(hov ? fT.text : fT.pillFill); fill(); strokeColor(fT.outline); strokeWidth(2.5f); stroke();
            const Color xc = hov ? fT.bg : fT.text;
            beginPath(); moveTo(cx - 4.5f, cy - 4.5f); lineTo(cx + 4.5f, cy + 4.5f); moveTo(cx + 4.5f, cy - 4.5f); lineTo(cx - 4.5f, cy + 4.5f);
            lineCap(NanoVG::ROUND); strokeColor(xc); strokeWidth(2.2f); stroke();
        }
        // the steps along the top: PADS, KNOBS, DONE
        if (step > 0) {
            static const char* const names[3] = { "PADS", "KNOBS", "DONE" };
            const int at = step <= 2 ? 0 : (step < wizLast() ? 1 : 2);
            const float y = kWizY + 64.f, x0 = kW * 0.5f - 120.f;
            beginPath(); moveTo(x0, y); lineTo(x0 + 240.f, y); strokeColor(withAlpha(ink, 0.2f)); strokeWidth(2.f); stroke();
            for (int i = 0; i < 3; ++i) {
                const float cx = x0 + 120.f * (float)i;
                const bool done = i < at, now_ = i == at;
                beginPath(); circle(cx, y, 11.f); fillColor(done || now_ ? kAmberAcc : fT.menuBg); fill();
                strokeColor(done || now_ ? fT.outline : withAlpha(ink, 0.3f)); strokeWidth(2.f); stroke();
                char n[8]; std::snprintf(n, sizeof(n), "%d", i + 1);
                drawText(fFontHeavy, 10.f, done || now_ ? hex(0x0A1A33) : soft, cx, y, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, n);
                drawText(fFontHeavy, 9.f, now_ ? ink : soft, cx, y + 21.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, names[i], 0.8f);
            }
        }
        // this step's content fades and lifts in
        const float sa = clamp01(fWizStepA.x);
        globalAlpha(a * sa);
        save();
        translate(0.f, 8.f * (1.f - sa));
        const float cx = kW * 0.5f;
        if (step == 0) {
            drawText(fFontHeavy, 21.f, ink, cx, kWizY + 78.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "How many knobs does your controller have?");
            drawText(fFontMed, 12.5f, soft, cx, kWizY + 106.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "Then you'll link two pads and your knobs. It takes about a minute.");
            for (int i = 0; i < 2; ++i) {
                const int id = i == 0 ? kWzThree : kWzSix;
                const bool hov = fWizHover == id, cur = (fWizKnobs == 6) == (i == 1);
                const float x = choiceX(i), y = kWzChoiceY;
                const float ps = (fWizDown == id) ? 1.f - 0.03f * fWizPress.x : 1.f;
                save();
                translate(x + kWzChoiceW * 0.5f, y + kWzChoiceH * 0.5f); scale(ps, ps); translate(-kWzChoiceW * 0.5f, -kWzChoiceH * 0.5f);
                beginPath(); roundedRect(0.f, 0.f, kWzChoiceW, kWzChoiceH, 16.f);
                fillColor(hov ? fT.text : fT.tray); fill();
                strokeColor(hov || cur ? fT.outline : withAlpha(fT.outline, 0.35f)); strokeWidth(hov || cur ? 3.f : 2.f); stroke();
                const Color ci = hov ? fT.bg : ink, cs = hov ? withAlpha(fT.bg, 0.75f) : soft;
                drawText(fFontTitle, 40.f, hov ? kAmberAcc : ci, kWzChoiceW * 0.5f, 44.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, i == 0 ? "3" : "6+", 0.f);
                drawText(fFontHeavy, 12.f, ci, kWzChoiceW * 0.5f, 84.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, i == 0 ? "3 KNOBS" : "6 OR MORE KNOBS", 0.8f);
                textCenter(fFontMed, 11.5f, cs, kWzChoiceW * 0.5f, 108.f,
                           i == 0 ? "They follow the side you press: LOOP or ON." : "One knob for each control, always.", kWzChoiceW - 36.f, 15.f);
                restore();
            }
            const bool hov = fWizHover == kWzMyself;
            drawText(fFontBold, 11.5f, hov ? ink : soft, cx, kWizY + kWizH - 33.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "I'll link them myself (right-click any control)");
            if (hov) { const float w = measure(fFontBold, 11.5f, "I'll link them myself (right-click any control)", 0.f);
                       beginPath(); moveTo(cx - w * 0.5f, kWizY + kWizH - 24.f); lineTo(cx + w * 0.5f, kWizY + kWizH - 24.f); strokeColor(ink); strokeWidth(1.2f); stroke(); }
        } else if (step < wizLast()) {
            const int slot = wizSlot(step);
            const bool knob = slot >= kLearnKnob0;
            const bool linked = fWizLinkedAt > 0.0;
            // the thing to touch: a pad or a knob, breathing while it waits
            const float iy = kWizY + 146.f;
            const float br = linked ? 1.f : 0.5f + 0.5f * std::sin((float)now() * 5.f);
            if (knob) {
                beginPath(); circle(cx, iy, 34.f + 8.f * br); strokeColor(withAlpha(kAmberAcc, 0.25f + 0.5f * br)); strokeWidth(4.f); stroke();
                beginPath(); circle(cx, iy, 28.f); fillColor(fT.face); fill(); strokeColor(fT.outline); strokeWidth(3.f); stroke();
                const float ang = linked ? 0.6f : -0.9f + 0.5f * std::sin((float)now() * 2.f);
                beginPath(); moveTo(cx, iy); lineTo(cx + std::sin(ang) * 20.f, iy - std::cos(ang) * 20.f);
                lineCap(NanoVG::ROUND); strokeColor(fT.faceInk); strokeWidth(4.f); stroke();
            } else {
                beginPath(); roundedRect(cx - 34.f - 6.f * br, iy - 34.f - 6.f * br, 68.f + 12.f * br, 68.f + 12.f * br, 18.f);
                strokeColor(withAlpha(kAmberAcc, 0.25f + 0.5f * br)); strokeWidth(4.f); stroke();
                beginPath(); roundedRect(cx - 28.f, iy - 28.f, 56.f, 56.f, 12.f);
                fillColor(linked ? kAmberAcc : (step == 1 ? kRedAcc : fT.face)); fill(); strokeColor(fT.outline); strokeWidth(3.f); stroke();
                drawText(fFontHeavy, 12.f, step == 1 || linked ? hex(0x0A1A33) : fT.faceInk, cx, iy, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, step == 1 ? "LOOP" : "ON", 0.3f);
            }
            char title[64];
            if (step == 1) std::snprintf(title, sizeof(title), "Hit the pad you want for LOOP");
            else if (step == 2) std::snprintf(title, sizeof(title), "Hit the pad you want for ON");
            else std::snprintf(title, sizeof(title), "Turn knob %d", step - 2);
            drawText(fFontHeavy, 21.f, ink, cx, kWizY + 212.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, title);
            const std::string job = step == 1 ? "This pad grabs a loop of the beat. HOLD or LATCH (under LOOP) sets how it plays."
                                  : step == 2 ? "This pad turns the Vinyl Sim on and off."
                                  : knobJob(step - 3);
            textCenter(fFontMed, 12.5f, soft, cx, kWizY + 238.f, job.c_str(), kWizW - 120.f, 17.f);
            // status: listening, or what it just got
            if (linked) {
                const std::string got = "LINKED \xC2\xB7 " + codeName(fWizCode);
                drawText(fFontHeavy, 12.f, fDark ? kAmberAcc : fT.text, cx, kWizY + 280.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, got.c_str(), 0.8f);
            } else {
                const float w = measure(fFontHeavy, 11.f, "LISTENING FOR YOUR CONTROLLER", 0.8f);
                beginPath(); circle(cx - w * 0.5f - 12.f, kWizY + 280.f, 4.f); fillColor(withAlpha(kAmberAcc, 0.4f + 0.6f * br)); fill();
                drawText(fFontHeavy, 11.f, soft, cx, kWizY + 280.f, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "LISTENING FOR YOUR CONTROLLER", 0.8f);
            }
            drawWrapped("Nothing happening? Turn your controller on in your DAW's MIDI settings and send it to the track Airliner is on. FL Studio: give the controller a port number and set Airliner's MIDI input port to match.",
                        kWizX + 28.f, kWzBtnY + 2.f, kWizW - 2.f * (kWzBtnW + 10.f) - 56.f, 10.5f, 13.5f, withAlpha(fT.menuText, 0.5f));
            wizButton(kWzSkip, wzBtnX(0), "SKIP", false);
            wizButton(kWzBack, wzBtnX(1), "BACK", false);
        } else {
            const float ty = fWizKnobs == 6 ? kWizY + 116.f : kWizY + 128.f;
            drawText(fFontHeavy, 21.f, ink, cx, ty, NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "You're all set");
            const char* how = fWizKnobs == 6 ? "Each knob runs its own control."
                                             : "Hit LOOP and your knobs run the DJ Looper. Hit ON and they jump to the Vinyl Sim.";
            textCenter(fFontMed, 12.5f, soft, cx, ty + 26.f, how, kWizW - 80.f, 17.f);
            const int rows = 2 + fWizKnobs;
            const float rowH = fWizKnobs == 6 ? 19.f : 24.f, x0 = kW * 0.5f - 170.f, x1 = kW * 0.5f + 170.f;
            float y = kWizY + (fWizKnobs == 6 ? 172.f : 204.f);
            for (int r = 0; r < rows; ++r, y += rowH) {
                const int slot = r < 2 ? r : kLearnKnob0 + (r - 2);
                std::string nm = slotName(slot);
                if (slot >= kLearnKnob0) {
                    const int j = slot - kLearnKnob0;
                    if (fWizKnobs == 6) nm += std::string("  \xC2\xB7  ") + (kEachTarget[j] == pOutB ? "VINYL OUTPUT" : kSpecs[kEachTarget[j]].screen);
                    else nm += j == 0 ? "  \xC2\xB7  LENGTH / COMP" : (j == 1 ? "  \xC2\xB7  SPEED / NOISE" : "  \xC2\xB7  WOW FLUT");
                }
                drawText(fFontBold, 12.f, ink, x0, y, NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, nm.c_str());
                const bool has = fMap[slot] >= 0;
                drawText(fFontHeavy, 11.f, has ? ink : withAlpha(fT.menuText, 0.4f), x1, y, NanoVG::ALIGN_RIGHT | NanoVG::ALIGN_MIDDLE,
                         has ? codeName(fMap[slot]).c_str() : "NOT LINKED", 0.5f);
                if (r + 1 < rows) { beginPath(); moveTo(x0, y + rowH * 0.5f); lineTo(x1, y + rowH * 0.5f); strokeColor(withAlpha(ink, 0.12f)); strokeWidth(1.f); stroke(); }
            }
            drawWrapped("Change it any time: SET UP CONTROLLER at the top, or right-click any control.",
                        kWizX + 28.f, kWzBtnY + kWzBtnH * 0.5f, kWizW - kWzBtnW - 76.f, 11.f, 14.f, withAlpha(fT.menuText, 0.55f));
            wizButton(kWzDone, wzBtnX(0), "DONE", true);
        }
        restore();
        restore();
        globalAlpha(1.f);
    }

    // right-click: link this control, and the controller settings
    void openControllerMenu(int slot, const char* what, float x, float y)
    {
        fMenu.clear();
        fMenuSlot = slot;
        if (slot >= 0) {
            fMenu.push_back({ what, 0, true });
            const std::string nm = slotName(slot);
            if (fMap[slot] >= 0) {
                fMenu.push_back({ "Link " + nm + " again", kActLearn, false });
                fMenu.push_back({ "Forget " + nm + " (" + codeName(fMap[slot]) + ")", kActForget, false });
            } else fMenu.push_back({ "Link " + nm + " on my controller", kActLearn, false });
            fMenu.push_back({ "", 0, true });
        } else if (what && what[0]) {
            fMenu.push_back({ what, 0, true });
            fMenu.push_back({ "", 0, true });
        }
        if (kKind == kKindBoth) {
            fMenu.push_back({ "MY CONTROLLER'S KNOBS", 0, true });
            fMenu.push_back({ "3 knobs: follow the side I press", kActFollow, false, !fOneEach });
            fMenu.push_back({ "6 or more: one knob per control", kActEach, false, fOneEach });
        } else fMenu.push_back({ "MY CONTROLLER'S KNOBS", 0, true });
        fMenu.push_back({ "Pickup (knobs never jump)", kActPickup, false, !fJump });
        bool any = false; for (int i = 0; i < kNumLearn; ++i) any |= fMap[i] >= 0;
        if (any) { fMenu.push_back({ "", 0, true }); fMenu.push_back({ "Forget every link", kActForgetAll, false }); }
        kMenuW = 270.f;
        kMenuX = std::min(std::max(8.f, x), kW - kMenuW - 8.f);
        kMenuY = std::min(std::max(8.f, y), kH - menuH() - 8.f);
        fMenuOpen = true; fMenuIsPreset = false; fMenuHover = -1;
        fMenuA.set(1.f);
        wake();
    }

    // --------------------------------------------------------------------------------------------
    // presets and undo

    static constexpr int kSnapCount = 12;
    static int snapParam(int k) { static const int p[kSnapCount] = { pPadMode, pLen, pSpeed, pComp, pNoise, pFlu, pOrder, pLimit, pInA, pOutA, pInB, pOutB }; return p[k]; }
    struct Snap { float v[kSnapCount]; std::string name; };
    std::vector<Snap> fUndo, fRedo;
    Snap takeSnap() const { Snap s; for (int k = 0; k < kSnapCount; ++k) s.v[k] = fP[snapParam(k)]; s.name = fPresetName; return s; }
    void pushUndo() { fUndo.push_back(takeSnap()); if (fUndo.size() > 64) fUndo.erase(fUndo.begin()); fRedo.clear(); }
    void restoreSnap(const Snap& s)
    {
        for (int k = 0; k < kSnapCount; ++k) setParam(snapParam(k), s.v[k]);
        setPresetName(s.name.c_str());
    }
    void undo() { if (fUndo.empty()) return; fRedo.push_back(takeSnap()); restoreSnap(fUndo.back()); fUndo.pop_back(); }
    void redo() { if (fRedo.empty()) return; fUndo.push_back(takeSnap()); restoreSnap(fRedo.back()); fRedo.pop_back(); }

    static constexpr int kPresetParams = 6;
    static int presetParam(int k) { static const int p[kPresetParams] = { pLen, pSpeed, pComp, pNoise, pFlu, pOrder }; return p[k]; }
    bool fUserVals = false;
    float fUserV[kPresetParams] = {};
    bool isUserPreset() const { for (const std::string& u : fUser) if (u == fPresetName) return true; return false; }
    bool presetDirty() const
    {
        if (isUserPreset() && fUserVals) {
            for (int k = 0; k < kPresetParams; ++k) if (inBuild(presetParam(k)) && std::fabs(fP[presetParam(k)] - fUserV[k]) > 1e-3f) return true;
            return false;
        }
        for (int i = 0; i < kNumPresets; ++i) {
            if (std::strcmp(kPresets[i].name, fPresetName) != 0) continue;
            for (int k = 0; k < kPresetParams; ++k) if (inBuild(presetParam(k)) && std::fabs(fP[presetParam(k)] - presetValue(kPresets[i], presetParam(k))) > 1e-3f) return true;
            return false;
        }
        return false;
    }
    void setPresetName(const char* n) { std::snprintf(fPresetName, sizeof(fPresetName), "%s", n); setState("preset", fPresetName); }
    void loadFactory(int i)
    {
        if (i < 0 || i >= kNumPresets) return;
        pushUndo();
        for (int k = 0; k < kPresetParams; ++k) if (inBuild(presetParam(k))) setParam(presetParam(k), presetValue(kPresets[i], presetParam(k)));
        fUserVals = false;
        setPresetName(kPresets[i].name);
    }
    void stepPreset(int dir)
    {
        // built-in ones first (the ones this plug-in lists), then yours
        std::vector<int> list;
        for (int i = 0; i < kNumPresets; ++i) if (presetInBuild(i)) list.push_back(i);
        const int nb = (int)list.size();
        for (size_t i = 0; i < fUser.size(); ++i) list.push_back(1000 + (int)i);
        const int total = (int)list.size();
        int cur = 0;
        for (int k = 0; k < nb; ++k) if (std::strcmp(kPresets[list[(size_t)k]].name, fPresetName) == 0) cur = k;
        for (size_t i = 0; i < fUser.size(); ++i) if (fUser[i] == fPresetName) cur = nb + (int)i;
        const int n = ((cur + dir) % total + total) % total;
        if (n < nb) loadFactory(list[(size_t)n]); else loadUserPreset(fUser[(size_t)(n - nb)]);
    }
    std::string presetPath(const std::string& name) { return yancey::presetsDir(kPresetFolder) + kSep + name + ".apre"; }
    void refreshUserPresets() { yancey::listFiles(yancey::presetsDir(kPresetFolder).c_str(), ".apre", fUser); }
    void loadUserPreset(const std::string& name)
    {
        std::vector<uint8_t> b;
        if (!yancey::readFileBytes(presetPath(name).c_str(), b)) return;
        const std::string text(b.begin(), b.end());
        pushUndo();
        for (int k = 0; k < kPresetParams; ++k) {
            const int p = presetParam(k);
            const std::string key = std::string(kSpecs[p].symbol) + "=";
            const size_t at = text.find(key);
            const float v = at == std::string::npos ? kSpecs[p].def : (float)std::atof(text.c_str() + at + key.size());
            setParam(p, v);
            fUserV[k] = clampP(p, v);
        }
        fUserVals = true;
        setPresetName(name.c_str());
    }
    void saveUserPreset()
    {
        const std::string dir = yancey::presetsDir(kPresetFolder);
        if (dir.empty()) return;
        std::string name = yancey::saveNameDialog(fNativeWin, dir.c_str(), presetDirty() || !isUserPreset() ? "My preset" : fPresetName, ".apre", "Save Airliner preset");
#ifndef _WIN32
        if (name.empty()) { for (int i = 1; i < 999; ++i) { char b[32]; std::snprintf(b, sizeof(b), "My preset %d", i); if (!yancey::fileExists(presetPath(b).c_str())) { name = b; break; } } }
#endif
        if (name.empty()) return;
        std::string text;
        for (int k = 0; k < kPresetParams; ++k) {
            const int p = presetParam(k);
            char line[64]; std::snprintf(line, sizeof(line), "%s=%.4f\n", kSpecs[p].symbol, fP[p]);
            text += line;
            fUserV[k] = fP[p];
        }
        if (!yancey::writeFileBytes(presetPath(name).c_str(), text)) return;
        fUserVals = true;
        refreshUserPresets();
        setPresetName(name.c_str());
        showMsg("PRESET SAVED", -1);
    }
    void deleteUserPreset()
    {
        if (!isUserPreset()) return;
        yancey::removeFilePath(presetPath(fPresetName).c_str());
        refreshUserPresets();
        loadFactory(0);
    }

    // --------------------------------------------------------------------------------------------
    // input

    void setParam(int i, float v)
    {
        v = clampP(i, v);
        fP[i] = v;
        fKnob[i].set(normOf(i, v));
        if (i == pLimit) fLimitT.set(v);
        if (i == pPadMode) fModeT.set(v);
        if (i == pOrder) fOrderT.set(v);
        if (i == pLoop) fLoopT.set(v);
        if (i == pVinyl) fVinT.set(v);
        editParameter(i, true);
        setParameterValue(i, v);
        editParameter(i, false);
        wake();
    }
    // the control a panel's screen shows from now on
    void showKnob(int i)
    {
        const int s = sideOfParam(i);
        if (s < 0) return;
        if (fShown[s] != i) { fShown[s] = i; fFlash[s].x = 1.f; fFlash[s].set(0.f); }
        wake();
    }
    // a message in place of the caption for a moment (side -1: every screen)
    void showMsg(const char* m, int side)
    {
        for (int s = 0; s < 2; ++s) {
            if (side >= 0 && s != side) continue;
            std::snprintf(fMsg[s], sizeof(fMsg[s]), "%s", m);
            fMsgUntil[s] = now() + 1.6;
        }
        wake();
    }

    // which control is under the mouse (design units): a knob in a bar, or a fader
    int knobAt(float x, float y) const
    {
        for (int s = 0; s < 2; ++s) {
            if (!sideShown(s)) continue;
            const float cx0 = cardX(s);
            for (int i = 0; i < 3; ++i) {
                const int p = kSlotParam[s][i];
                if (p < 0) continue;
                const float cx = cx0 + slotCX(i), cy = kCardY + kSlotCY;
                if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= (kKnobDot + 8.f) * (kKnobDot + 8.f)) return p;
            }
            for (int i = 0; i < 2; ++i) {
                const float fx = cx0 + (i ? kFadOutX : kFadInX), fy = kCardY + kFadY;
                if (x >= fx && x <= fx + kFadW && y >= fy && y <= fy + kFadH) return kFadParam[s][i];
            }
        }
        return -1;
    }
    bool onLoopButton(float x, float y) const { return kHasLooper && inCircle(x, y, cardX(0) + slotCX(2), kCardY + kSlotCY, kLoopR + 6.f); }
    bool onVinylButton(float x, float y) const { return kHasVinyl && inRect(x, y, cardX(1) + kOnX, kCardY + kOnY, kOnW, kOnH + 4.f); }
    static bool inRect(float x, float y, float rx, float ry, float rw, float rh) { return x >= rx && x <= rx + rw && y >= ry && y <= ry + rh; }
    static bool inCircle(float x, float y, float cx, float cy, float r) { return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r; }
    // design units per full sweep: knobs 220 pixels; faders their track (1:1 with the thumb)
    static float dragSpan(int p) { return isGain(p) ? (kFadH - 84.f) : 220.f; }

    void toDesign(double px, double py, float& x, float& y) const
    {
        const float s = (float)getWidth() / kW;
        x = (float)px / s; y = (float)py / s;
    }

    bool onMouse(const MouseEvent& ev) override
    {
        float x, y; toDesign(ev.pos.getX(), ev.pos.getY(), x, y);
        if (fWiz >= 0) {
            if (ev.button == kMouseButtonLeft) {
                if (ev.press) { fWizDown = wizHit(x, y); if (fWizDown >= 0) { fWizPress.set(1.f); wake(); } }
                else { const int h = wizHit(x, y); fWizPress.set(0.f); if (h >= 0 && h == fWizDown) wizClick(h); fWizDown = -1; wake(); }
            }
            return true;        // the setup card keeps the clicks while it's open
        }
        if (ev.button == kMouseButtonRight && ev.press) {
            if (fMenuOpen) { closeMenu(); return true; }
            cancelLearn();
            controlMenuAt(x, y);
            return true;
        }
        if (ev.button != kMouseButtonLeft) return false;

        if (!ev.press) {
            if (fDrag != kDragNone) {
                editParameter(fDrag, false);
                fPop[fDrag].set(0.f);
                fDrag = kDragNone;
                wake();
            }
            if (fLoopHeld) {        // HOLD: letting go drops the loop
                fLoopHeld = false;
                setParam(pLoop, 0.f);
            }
            fLoopPress.set(0.f); fVinPress.set(0.f); fAllPress.set(0.f); fUndoPress.set(1.f); fRedoPress.set(1.f);
            if (fSetupDown) { fSetupDown = false; fSetupPress.set(0.f); if (kKind == kKindBoth && onSetupButton(x, y)) openWizard(); }
            return true;
        }

        if (fMenuOpen) {
            const int it = menuItemAt(x, y);
            closeMenu();
            if (it >= 0) runMenu(fMenu[(size_t)it].act);
            return true;
        }
        if (fLearnSlot >= 0) { cancelLearn(); return true; }     // a click anywhere stops waiting

        const double t = now();
        const int k = knobAt(x, y);
        if (k >= 0) {
            if (kKind == kKindBoth) setSide(sideOfParam(k), false);
            if (t - fLastClick < 0.3 && fLastClickKnob == k) {      // double-click: back to the default
                pushUndo();
                setParam(k, kSpecs[k].def);
                showKnob(k);
                fLastClick = 0.0;
                return true;
            }
            fLastClick = t; fLastClickKnob = k;
            pushUndo();
            fDrag = k; fDragY = y; fDragN = normOf(k, fP[k]);
            fPop[k].set(1.f);
            editParameter(k, true);
            showKnob(k);
            wake();
            return true;
        }

        // LOOP and ON
        if (onLoopButton(x, y)) {
            if (kKind == kKindBoth) setSide(0, false);
            fLoopPress.set(1.f);
            if (fP[pPadMode] > 0.5f) setParam(pLoop, fP[pLoop] > 0.5f ? 0.f : 1.f);
            else { fLoopHeld = true; setParam(pLoop, 1.f); }
            return true;
        }
        if (onVinylButton(x, y)) {
            if (kKind == kKindBoth) setSide(1, false);
            fVinPress.set(1.f);
            setParam(pVinyl, fP[pVinyl] > 0.5f ? 0.f : 1.f);
            return true;
        }
        if (kHasLooper && inRect(x, y, cardX(0) + kSegX, kCardY + kSegY, kSegW, kSegH)) {
            const float v = x < cardX(0) + kSegX + kSegW * 0.5f ? 0.f : 1.f;
            if (v != fP[pPadMode]) { pushUndo(); setParam(pPadMode, v); showMsg(v > 0.5f ? "LOOP BUTTON \xC2\xB7 LATCH" : "LOOP BUTTON \xC2\xB7 HOLD", 0); }
            return true;
        }

        // header
        if (kKind == kKindBoth && onSetupButton(x, y)) { fSetupDown = true; fSetupPress.set(1.f); wake(); return true; }
        if (inRect(x, y, kPresetX, kPresetY, kPresetW, kPresetH)) {
            if (x < kPresetX + 32.f) stepPreset(-1);
            else if (x > kPresetX + kPresetW - 32.f) stepPreset(1);
            else openMenu();
            return true;
        }
        if (kKind == kKindBoth) {
            if (inCircle(x, y, kUndoCX, kHdrCY, kUndoR + 3.f)) { fUndoPress.x = 0.88f; fUndoPress.set(1.f); undo(); wake(); return true; }
            if (inCircle(x, y, kRedoCX, kHdrCY, kUndoR + 3.f)) { fRedoPress.x = 0.88f; fRedoPress.set(1.f); redo(); wake(); return true; }
            if (inRect(x, y, kOrdX, kOrdY, kOrdW, kOrdH)) {
                pushUndo();
                setParam(pOrder, fP[pOrder] > 0.5f ? 0.f : 1.f);
                showMsg(fP[pOrder] > 0.5f ? "VINYL FIRST, THEN THE LOOP" : "LOOP FIRST, THEN THE VINYL", -1);
                return true;
            }
            if (inRect(x, y, kAllX, kAllY, kAllW, kAllH)) {
                fAllPress.set(1.f);
                setParam(pLoop, 0.f); setParam(pVinyl, 0.f);
                fLoopHeld = false;
                showMsg("ALL OFF", -1);
                return true;
            }
        }
        if (inRect(x, y, kLimX - 4.f, kLimY - 4.f, kLimW + 8.f, kLimH + 8.f)) {
            pushUndo();
            setParam(pLimit, fP[pLimit] > 0.5f ? 0.f : 1.f);
            showMsg(fP[pLimit] > 0.5f ? "LIMIT ON" : "LIMIT OFF", -1);
            return true;
        }
        if (inCircle(x, y, moonCX(), kKind == kKindBoth ? kTitleCY - 4.f : moonCY(), 12.f)) {
            fDark = !fDark; fDarkT.set(fDark ? 1.f : 0.f);
            yancey::Prefs up; up.dark = fDark ? 1 : 0; yancey::writePrefs(up);
            wake();
            return true;
        }
        if (inCircle(x, y, badgeCX(), kTitleCY - 4.f, 19.f)) { fBadge.v += 14.f; wake(); return true; }
        // a click on a panel gives it the controller's knobs
        if (kKind == kKindBoth) {
            for (int s = 0; s < 2; ++s) if (inRect(x, y, cardX(s), kCardY, kCardW, kCardH)) { setSide(s, true); return true; }
        }
        return false;
    }

    void setSide(int side, bool say)
    {
        if (kKind != kKindBoth) return;
        if ((fSideT.target > 0.5f ? 1 : 0) == side && !say) return;
        setState("side", side ? "1" : "0");
        fSideT.set((float)side);
        if (say && followTagShown()) showMsg("THE KNOBS ARE HERE", side);
        wake();
    }

    void controlMenuAt(float x, float y)
    {
        const int k = knobAt(x, y);
        char what[64];
        if (k >= 0) {
            const int j = knobForParam(k, fOneEach);
            if (j < 0) { std::snprintf(what, sizeof(what), "%s: NO CONTROLLER KNOB", kSpecs[k].screen); openControllerMenu(-1, what, x, y); return; }
            std::snprintf(what, sizeof(what), "%s \xC2\xB7 CONTROLLER KNOB %d", kSpecs[k].screen, j + 1);
            openControllerMenu(kLearnKnob0 + j, what, x, y);
            return;
        }
        if (onLoopButton(x, y)) { openControllerMenu(kLearnLoop, "LOOP \xC2\xB7 CONTROLLER PAD", x, y); return; }
        if (onVinylButton(x, y)) { openControllerMenu(kLearnVinyl, "VINYL ON \xC2\xB7 CONTROLLER PAD", x, y); return; }
        openControllerMenu(-1, "", x, y);
    }

    bool onMotion(const MotionEvent& ev) override
    {
        float x, y; toDesign(ev.pos.getX(), ev.pos.getY(), x, y);
        if (fWiz >= 0) { const int h = wizHit(x, y); if (h != fWizHover) { fWizHover = h; wake(); } return true; }
        { const bool hs = kKind == kKindBoth && onSetupButton(x, y); if (hs != fSetupHover) { fSetupHover = hs; wake(); } }
        if (fDrag != kDragNone) {
            // 1:1 with the mouse, Shift for fine moves
            const bool fine = (ev.mod & kModifierShift) != 0;
            const float span = dragSpan(fDrag) * (fine ? 5.f : 1.f);
            fDragN = clamp01(fDragN + (fDragY - y) / span);
            fDragY = y;
            const float v = valueOf(fDrag, fDragN);
            fKnob[fDrag].snap(kSpecs[fDrag].integer ? normOf(fDrag, v) : fDragN);
            if (v != fP[fDrag]) { fP[fDrag] = v; setParameterValue(fDrag, v); }
            wake();
            return true;
        }
        if (fMenuOpen) {
            const int h = menuItemAt(x, y);
            if (h != fMenuHover) { fMenuHover = h; wake(); }
            return true;
        }
        const int k = knobAt(x, y);
        if (k != fHover) { fHover = k; wake(); }
        return false;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        float x, y; toDesign(ev.pos.getX(), ev.pos.getY(), x, y);
        const int k = knobAt(x, y);
        if (k < 0) return false;
        const ParamSpec& s = kSpecs[k];
        const float step = s.integer ? 1.f : (isGain(k) ? 0.5f : (s.max - s.min) / 100.f);
        const float d = ev.delta.getY() > 0 ? 1.f : (ev.delta.getY() < 0 ? -1.f : 0.f);
        if (d == 0.f) return false;
        if (fScrollKnob != k || now() - fScrollAt > 1.0) pushUndo();
        fScrollKnob = k; fScrollAt = now();
        setParam(k, fP[k] + d * step * ((ev.mod & kModifierShift) ? 0.2f : 1.f));
        showKnob(k);
        return true;
    }

private:
    FontId fFontTitle, fFontHeavy, fFontBold, fFontMed;
    Theme fTL, fTD, fT;
    bool fDark = false;
    float fP[kParamCount];
    bool fInited[kParamCount] = {};
    Spring fKnob[kParamCount], fPop[kParamCount];
    Spring fLoopT, fVinT, fLoopPress, fVinPress, fLimitT, fModeT, fOrderT, fDarkT, fRingA;
    Spring fAllPress, fUndoPress, fRedoPress, fBadge, fMenuA;
    Spring fFlash[2], fMsgA[2];
    int fShown[2] = { pLen, pFlu };
    char fMsg[2][64];
    double fMsgUntil[2] = { 0.0, 0.0 };
    float fMeterDb[4];
    float fLastPhase = 0.f;
    char fPresetName[128];
    std::vector<std::string> fUser;
    uintptr_t fNativeWin = 0;
    int fDrag = kDragNone, fHover = -1, fLastClickKnob = -1, fScrollKnob = -1;
    float fDragY = 0.f, fDragN = 0.f;
    double fLastClick = 0.0, fScrollAt = 0.0;
    bool fLoopHeld = false;
    double fLastTime = 0.0, fSizeChangedAt = 0.0;
    float fLastW = 0.f, fLastH = 0.f, fSavedScale = 1.f;
    bool fSizeDirty = false;
    int fWake = 0;
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AirlinerUI)
};

UI* createUI() { return new AirlinerUI(); }

END_NAMESPACE_DISTRHO
