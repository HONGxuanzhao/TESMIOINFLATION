// Price overlays are applied by Reputation itself, after rebasing its cache.
// Only the supported 2.1 image is adapted; its other systems remain intact.
#ifndef TESMIO_INFLATION_REPUTATION_H
#define TESMIO_INFLATION_REPUTATION_H

static Rep21Binding gPriceRep = {};
typedef void (*RepPriceFn)();
typedef void (__fastcall *EconomyFn)(void*, bool);
typedef void (__fastcall *PricePassFn)(void*);
static RepPriceFn gRepPriceOriginal = NULL, gRepRestoreOriginal = NULL;
static EconomyFn gEconomyOriginal = NULL;
static PricePassFn gPricePassOriginal = NULL;
static unsigned gPriceNativeDepth;
static bool gPriceBookValid;
static BYTE* gPriceBookPointer;
static size_t gPriceBookCount;
struct CompatPriceRow {
    char name[32];
    float rub, usd;
    float shownRub, shownUsd;
};
static CompatPriceRow gPriceBook[512];

static bool CompatGlobalResources(CompatResourceTable* out) {
    // The process-wide vector is the same verified table used by Reputation.
    uintptr_t b = 0, e = 0;
    if (!H->readablePtr(g_exeBase + 0x9E11C0, 16)) return false;
    memcpy(&b, g_exeBase + 0x9E11C0, 8); memcpy(&e, g_exeBase + 0x9E11C8, 8);
    if (!b || e < b || (e-b) % 832 || (e-b)/832 < 1 || (e-b)/832 > 512 ||
        !H->readablePtr((void*)b, (size_t)(e-b))) return false;
    out->begin = (BYTE*)b; out->count = (size_t)((e-b)/832); return true;
}
static float CompatReadPrice(BYTE* rec, unsigned offset) {
    float f; memcpy(&f, rec + offset, 4); return f;
}
static void CompatWritePrice(BYTE* rec, unsigned offset, float f) {
    memcpy(rec + offset, &f, 4);
}

// Preserve clean prices across a resource-vector relocation only if its
// name and previously displayed value still match. A new/rebuilt record is
// captured as new data. Never hold a resource pointer across a native pass.
static bool CompatEnsureBook(const CompatResourceTable& t) {
    if (gPriceBookValid && gPriceBookPointer == t.begin && gPriceBookCount == t.count)
        return true;
    CompatPriceRow rows[512] = {};
    for (size_t i = 0; i < t.count; ++i) {
        BYTE* rec = t.begin + i * 832;
        if (!CompatResourceName(rec, rows[i].name)) return false;
        float rub = CompatReadPrice(rec, 0x5C), usd = CompatReadPrice(rec, 0x58);
        if (!_finite(rub) || !_finite(usd)) return false;
        rows[i].rub = rows[i].shownRub = rub;
        rows[i].usd = rows[i].shownUsd = usd;
        if (gPriceBookValid && i < gPriceBookCount &&
            !strcmp(rows[i].name, gPriceBook[i].name) &&
            rub == gPriceBook[i].shownRub && usd == gPriceBook[i].shownUsd) {
            rows[i].rub = gPriceBook[i].rub; rows[i].usd = gPriceBook[i].usd;
        }
    }
    memcpy(gPriceBook, rows, sizeof(rows));
    gPriceBookPointer = t.begin; gPriceBookCount = t.count; gPriceBookValid = true;
    return true;
}
static void CompatRestoreBase(const CompatResourceTable& t) {
    for (size_t i = 0; i < t.count; ++i) {
        CompatWritePrice(t.begin + i*832, 0x5C, gPriceBook[i].rub);
        CompatWritePrice(t.begin + i*832, 0x58, gPriceBook[i].usd);
    }
}
static bool CompatCaptureBase(const CompatResourceTable& t) {
    for (size_t i = 0; i < t.count; ++i) {
        float rub = CompatReadPrice(t.begin + i*832, 0x5C);
        float usd = CompatReadPrice(t.begin + i*832, 0x58);
        if (!_finite(rub) || !_finite(usd)) return false;
        gPriceBook[i].rub = rub; gPriceBook[i].usd = usd;
    }
    return true;
}
static void CompatCaptureShown(const CompatResourceTable& t) {
    for (size_t i = 0; i < t.count; ++i) {
        gPriceBook[i].shownRub = CompatReadPrice(t.begin + i*832, 0x5C);
        gPriceBook[i].shownUsd = CompatReadPrice(t.begin + i*832, 0x58);
    }
}
static bool CompatRebaseRepCache(const CompatResourceTable& t) {
    int n = *(int*)(gPriceRep.base + 0x1D2530);
    if (n < 0 || n > 128) return false;
    int* idx = (int*)(gPriceRep.base + 0x1D1B30);
    double* rub = (double*)(gPriceRep.base + 0x1D1D30);
    double* usd = (double*)(gPriceRep.base + 0x1D2130);
    for (int k = 0; k < n; ++k) {
        if (idx[k] < 0 || (size_t)idx[k] >= t.count) continue;
        rub[k] = gPriceBook[idx[k]].rub; usd[k] = gPriceBook[idx[k]].usd;
    }
    return true;
}
static void CompatApplyOverlay(const CompatResourceTable& t) {
    if (!CompatRebaseRepCache(t)) {
        Logf("TESMIOINFLATION Reputation cache count invalid; overlay skipped");
        return;
    }
    gRepPriceOriginal();
    CompatCaptureShown(t);
}
static void CompatRepPriceDetour() {
    Rep21Enter(gPriceRep);
    __try {
        CompatResourceTable t = {};
        if (!gPriceNativeDepth && CompatGlobalResources(&t) && CompatEnsureBook(t)) {
            CompatRestoreBase(t); CompatApplyOverlay(t);
        } else gRepPriceOriginal();
    } __finally { Rep21Leave(gPriceRep); }
}
static void CompatRepRestoreDetour() {
    Rep21Enter(gPriceRep);
    __try {
        CompatResourceTable t = {};
        if (CompatGlobalResources(&t) && CompatEnsureBook(t)) {
            CompatRestoreBase(t); CompatRebaseRepCache(t);
        }
        gRepRestoreOriginal();
        gPriceBookValid = false; gPriceBookPointer = NULL; gPriceBookCount = 0;
    } __finally { Rep21Leave(gPriceRep); }
}
static void CompatNativeBegin() {
    if (!gPriceNativeDepth) {
        CompatResourceTable t = {};
        if (CompatGlobalResources(&t) && CompatEnsureBook(t)) CompatRestoreBase(t);
    }
    ++gPriceNativeDepth;
}
static void CompatNativeEnd() {
    --gPriceNativeDepth;
    if (!gPriceNativeDepth) {
        CompatResourceTable t = {};
        if (CompatGlobalResources(&t) && CompatEnsureBook(t) && CompatCaptureBase(t))
            CompatApplyOverlay(t);
    }
}
static void __fastcall CompatEconomyDetour(void* self, bool a) {
    Rep21Enter(gPriceRep);
    CompatNativeBegin();
    __try { gEconomyOriginal(self, a); }
    __finally { CompatNativeEnd(); Rep21Leave(gPriceRep); }
}
static void __fastcall CompatPricePassDetour(void* self) {
    Rep21Enter(gPriceRep);
    CompatNativeBegin();
    __try { gPricePassOriginal(self); }
    __finally { CompatNativeEnd(); Rep21Leave(gPriceRep); }
}

static bool CompatWriteBytes(BYTE* p, const BYTE* data, size_t n) {
    DWORD old = 0, ignored = 0;
    if (!VirtualProtect(p, n, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(p, data, n);
    FlushInstructionCache(GetCurrentProcess(), p, n);
    // The write is complete even if restoring protection fails. The caller
    // must count it as applied so transaction rollback includes these bytes.
    if (!VirtualProtect(p, n, old, &ignored))
        Logf("TESMIOINFLATION warning: cannot restore code page protection");
    return true;
}
struct CompatCodeEdit { unsigned rva; BYTE original[7]; unsigned length; BYTE replacement; };
static const CompatCodeEdit kRepCurrencyEdits[] = {
    {0x3D731,{0xF3,0x41,0x0F,0x10,0x51,0x58},6,0x5C},
    {0x3D73A,{0xF3,0x41,0x0F,0x10,0x59,0x5C},6,0x58},
    {0x2B7E, {0xF3,0x42,0x0F,0x11,0x4C,0x39,0x58},7,0x5C},
    {0x2B85, {0xF3,0x42,0x0F,0x11,0x44,0x39,0x5C},7,0x58},
    {0x53AEE,{0xF3,0x42,0x0F,0x11,0x44,0x11,0x58},7,0x5C},
    {0x53B03,{0xF3,0x42,0x0F,0x11,0x44,0x11,0x5C},7,0x58}
};
struct CompatHookSpec {
    BYTE* target; void* detour; void** original;
    const BYTE* expected; size_t length; const char* label;
};

static int StartInflationReputation() {
    if (!H->configInt(kIniName, kSection, "reputation_compat_enabled", 1)) return 0;
    int found = Rep21Bind(&gPriceRep);
    if (found == 0) { Logf("TESMIOINFLATION no Reputation provider: standalone formulas active"); return 0; }
    if (found < 0) { Logf("TESMIOINFLATION Reputation adapter refused: supported binary is 2.1 timestamp 6AB18B03; base formulas remain active"); return 0; }
    static const BYTE economy[] = {0x48,0x8B,0xC4,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x81,0xEC,0xD0,0,0,0};
    static const BYTE pass[] = {0x48,0x89,0x6C,0x24,0x18,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x30};
    static const BYTE price[] = {0x40,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x81,0xEC,0x60,0x0A,0,0};
    static const BYTE restore[] = {0x40,0x53,0x48,0x83,0xEC,0x40,0x45,0x33,0xD2,0x4C,0x89,0x54,0x24,0x50};
    CompatHookSpec hooks[] = {
        {g_exeBase+0x2FB1D0,(void*)CompatEconomyDetour,(void**)&gEconomyOriginal,economy,sizeof(economy),"INFLATION normal economy before Reputation"},
        {g_exeBase+0x2A9340,(void*)CompatPricePassDetour,(void**)&gPricePassOriginal,pass,sizeof(pass),"INFLATION production price before Reputation"},
        {gPriceRep.base+0x25F0,(void*)CompatRepPriceDetour,(void**)&gRepPriceOriginal,price,sizeof(price),"INFLATION Reputation daily price rebase"},
        {gPriceRep.base+0x53A70,(void*)CompatRepRestoreDetour,(void**)&gRepRestoreOriginal,restore,sizeof(restore),"INFLATION Reputation world price reset"}
    };
    if (!H->installInlineHook || !H->readablePtr(gPriceRep.base+0x1D1B30,0xA04) ||
        !H->readablePtr(gPriceRep.base+0xC4DA8,sizeof(CRITICAL_SECTION)) ||
        !*(int*)(gPriceRep.base+0xD72D4)) return 0;
    for (size_t i=0;i<4;++i)
        if (!H->readablePtr(hooks[i].target,hooks[i].length) ||
            memcmp(hooks[i].target,hooks[i].expected,hooks[i].length)) {
            Logf("TESMIOINFLATION Reputation price adapter refused: hook layout changed (%s)",hooks[i].label); return 0;
        }
    for (size_t i=0;i<6;++i)
        if (!Rep21Matches(gPriceRep.base,kRepCurrencyEdits[i].rva,
            kRepCurrencyEdits[i].original,kRepCurrencyEdits[i].length)) {
            Logf("TESMIOINFLATION Reputation currency adapter refused: layout changed"); return 0;
        }
    size_t installed=0, edited=0;
    for (;installed<4;++installed) {
        CompatHookSpec& s=hooks[installed];
        if (!H->installInlineHook(s.target,s.detour,s.original,s.expected,s.length,s.label)) break;
    }
    if (installed==4) {
        for (;edited<6;++edited) {
            const CompatCodeEdit& e=kRepCurrencyEdits[edited];
            if (!CompatWriteBytes(gPriceRep.base+e.rva+e.length-1,&e.replacement,1)) break;
        }
    }
    if (installed!=4 || edited!=6) {
        bool restored=true;
        while(edited) { const CompatCodeEdit& e=kRepCurrencyEdits[--edited];
            restored=CompatWriteBytes(gPriceRep.base+e.rva,e.original,e.length) && restored; }
        while(installed) { CompatHookSpec& s=hooks[--installed];
            restored=CompatWriteBytes(s.target,s.expected,s.length) && restored; }
        Logf(restored ? "TESMIOINFLATION Reputation price adapter rolled back; original Reputation behavior retained" :
            "TESMIOINFLATION ERROR: Reputation adapter rollback incomplete; keep this DLL loaded and restart before continuing"); return 0;
    }
    Logf("TESMIOINFLATION Reputation 2.1 price adapter active: live normal prices x original event/faction multipliers; RUB=5C USD=58; disk DLL unchanged");
    return 0;
}
#endif
