// Optional, version-locked adapter for the installed Reputation 2.1 binary.
// No Reputation DLL, INI, EXE or save is changed on disk. All RVAs are guarded.
#ifndef TESMIO_REPUTATION_21_H
#define TESMIO_REPUTATION_21_H
#include <stdint.h>
#include <float.h>

struct RepV1Prefix {
    unsigned structSize;
    double (*standing)(int);
    void (*add)(int, double, const char*);
    int (*eventActive)(const char*);
    int (*eventDaysLeft)(const char*);
    int (*blocked)(int, int);
    int (*contractCount)();
    const char* (*contractId)(int);
    int (*contractState)(int);
    int (*contractStateOf)(const char*);
    double (*contractProgress)(int);
    int (*inWorld)();
    int (*year)();
    int (*dayOfYear)();
    double (*money)(int);
};

struct Rep21Binding {
    BYTE* base;
    const RepV1Prefix* api;
};

static bool Rep21Matches(BYTE* base, unsigned rva, const BYTE* bytes, size_t n) {
    return H->readablePtr(base + rva, n) && !memcmp(base + rva, bytes, n);
}

static uint64_t Rep21Fingerprint(const BYTE* p, size_t n) {
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}

// 0: no provider, 1: supported provider, -1: a provider with another layout.
static int Rep21Bind(Rep21Binding* out) {
    memset(out, 0, sizeof(*out));
    if (!HostHasField(offsetof(TsmHost, consume), sizeof(H->consume)) || !H->consume)
        return 0;
    const RepV1Prefix* api = (const RepV1Prefix*)H->consume("reputation", 1);
    if (!api) return 0;
    if (!H->readablePtr(api, sizeof(*api)) || api->structSize < sizeof(*api) ||
        !api->standing || !api->inWorld) return -1;
    HMODULE module = NULL;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCWSTR)api->standing, &module)) return -1;
    BYTE* b = (BYTE*)module;
    if (!H->readablePtr(b, sizeof(IMAGE_DOS_HEADER))) return -1;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)b;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
        dos->e_lfanew > 4096 || !H->readablePtr(b + dos->e_lfanew,
        sizeof(IMAGE_NT_HEADERS64))) return -1;
    IMAGE_NT_HEADERS64* nt = (IMAGE_NT_HEADERS64*)(b + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.TimeDateStamp != 0x6AB18B03 ||
        nt->OptionalHeader.SizeOfImage != 0x6B2000 ||
        !H->readablePtr(b + 0xA1F6C, 4) || memcmp(b + 0xA1F6C, "2.1", 4) ||
        !H->readablePtr(b + 0x77A0, 0x158) ||
        Rep21Fingerprint(b + 0x77A0, 0x158) != 0x878F4E1B7A82123DULL)
        return -1;
    out->base = b; out->api = api;
    return 1;
}

static void Rep21Enter(const Rep21Binding& r) {
    EnterCriticalSection((CRITICAL_SECTION*)(r.base + 0xC4DA8));
}
static void Rep21Leave(const Rep21Binding& r) {
    LeaveCriticalSection((CRITICAL_SECTION*)(r.base + 0xC4DA8));
}

// Calls the exact verified 2.1 aggregate, preserving its active-event and
// eligible-faction rules. No event list is duplicated or guessed from the INI.
static double Rep21BirthMultiplier(const Rep21Binding& r) {
    if (!r.base || !r.api->inWorld() || *(int*)(r.base + 0xC3088)) return 1.0;
    typedef double (*Aggregate)();
    double m = ((Aggregate)(r.base + 0x77A0))();
    return (_finite(m) && m > 0.0 && m <= 1000000.0) ? m : 1.0;
}

struct CompatResourceTable { BYTE* begin; size_t count; };
static bool CompatReadResources(void* gameObject, CompatResourceTable* out) {
    uintptr_t b = 0, e = 0;
    BYTE* object = (BYTE*)gameObject;
    if (!object || !H->readablePtr(object + 0xC2B0, 16)) return false;
    memcpy(&b, object + 0xC2B0, 8); memcpy(&e, object + 0xC2B8, 8);
    if (!b || e < b || (e - b) % 832 || (e - b) / 832 < 1 ||
        (e - b) / 832 > 512 || !H->readablePtr((void*)b, (size_t)(e - b)))
        return false;
    out->begin = (BYTE*)b; out->count = (size_t)((e - b) / 832);
    return true;
}
static bool CompatResourceName(const BYTE* rec, char out[32]) {
    memcpy(out, rec, 32);
    return memchr(out, 0, 32) != NULL;
}
#endif
