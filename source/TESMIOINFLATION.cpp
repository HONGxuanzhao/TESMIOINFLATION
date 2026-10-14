// TESMIOINFLATION - runtime inflation and loan-rate controls for Workers &
// Resources: Soviet Republic through TesmioLoader.
//
// This plugin changes data in the running SOVIET64.exe process only. It does
// not edit SOVIET64.exe, C3DDLL64.dll, save files, or any loader file.

#include "tesmio_plugin.h"

#include <float.h>
#include <stdint.h>
#include "reputation_21.h"

#if !defined(_WIN64)
#error TESMIOINFLATION must be built as an x64 DLL.
#endif

static const char kIniName[] = "plugins\\TESMIOINFLATION.ini";
static const char kSection[] = "TESMIOINFLATION";

// These RVAs are for the WRSR v1.1.1.9 executable currently supported by the
// local TesmioLoader installation. They are deliberately guarded by the exact
// instruction bytes below; a different game build is refused.
//
// The data RVAs below are *read-only verification anchors*. They must never be
// overwritten: the same cells are referenced by many unrelated game
// functions. The plugin redirects only the operand of a verified instruction
// inside the intended inflation/loan-preview function to plugin-owned data.
static const unsigned kEconomyFunctionRva = 0x002FB1D0;
static const unsigned kCommonMinimumRva   = 0x00909B90; // 0.01
static const unsigned kUsdMaximumRva     = 0x00909BD0; // 0.03
static const unsigned kRubMaximumRva     = 0x00909C78; // 0.10
static const unsigned kCyclesScaleRva    = 0x00909B9C; // 1 / 73
static const unsigned kNeutralFactorRva  = 0x00909F58; // 1.0, checked only
static const unsigned kNoiseMaximumRva   = 0x0090A060; // +1.5
static const unsigned kNoiseMinimumRva   = 0x0090B340; // -1.5
static const unsigned kNoiseCountScaleRva = 0x0090B1CC; // 50000, checked only
// The resource-price loop has its own random draw. These cells are also
// shared data, so they are verification anchors only; the two reads inside
// the resource loop are redirected to private plugin storage below.
static const unsigned kResourceNoiseMaximumRva = 0x0090A618; // +4.0
static const unsigned kResourceNoiseMinimumRva = 0x0090B410; // -4.0

// The loan-rate function is separate from the market-inflation function. It
// returns the annual contract rate in percent; the caller stores that result
// in Loan.YearInterestRate and stores twice that value as the penalty rate.
// The hook below replays this function's formula with INI-controlled factors.
static const unsigned kLoanRateFunctionRva       = 0x004B9A80;
static const unsigned kResourceGetFunctionRva    = 0x002AA830;
static const unsigned kLoanVectorBeginOffset    = 0x0010BD0;
static const unsigned kLoanVectorEndOffset      = 0x0010BD8;
static const unsigned kLoanRecordStride         = 0x28;
static const unsigned kLoanRecordTypeOffset     = 0x08;
static const unsigned kLoanRecordAmountOffset   = 0x14;
static const unsigned kLoanRecordPenaltyOffset  = 0x18;

// These are the data cells referenced by the formula. They are used as
// verification anchors only; the plugin does not write them because several
// are shared by unrelated game code.
static const unsigned kLoanRubBaseRva           = 0x0090A180; // 1.8
static const unsigned kLoanUsdBaseRva           = 0x0090A364; // 2.5
static const unsigned kLoanTermFactorRva        = 0x00909D04; // 0.25
static const unsigned kLoanOtherDebtWeightRva   = 0x00909DDC; // 0.5
static const unsigned kLoanReferencePriceRva    = 0x0090AF6C; // 550.0
static const unsigned kLoanPenaltyScaleRva      = 0x0090B1FC; // 1,000,000
static const unsigned kLoanBalanceScaleRva      = 0x0090B214; // 15,000,000
static const unsigned kLoanDaysPerYearRva       = 0x0090AE84; // 365.0

// The Economy & Trade panel has a second, inline copy of the loan-rate
// formula. It reads the same data cells for all factors. These are the exact
// instruction sites in the panel's RUB and USD branches. The plugin redirects
// them to its private shadow values; it does not alter the shared cells.
static const unsigned kLoanPreviewFunctionRva = 0x004B4D90;

struct LoanSettings
{
    float rubBasePercent;
    float usdBasePercent;
    float termFactor;
    float otherCurrencyDebtWeight;
    float referenceSteelPrice;
    float penaltyDebtScale;
    float balanceDebtScale;
    float daysPerYear;
};

struct InflationSettings
{
    float minimumPercent;
    float rubMaximumPercent;
    float usdMaximumPercent;
    float cyclesPerYear;
    float noiseMinimum;
    float noiseMaximum;
    float resourceNoiseMinimum;
    float resourceNoiseMaximum;
    LoanSettings loan;
};

static LoanSettings gLoanSettings = {};

struct RuntimeCells
{
    float inflationMinimum;
    float inflationRubMaximum;
    float inflationUsdMaximum;
    float inflationCyclesScale;
    float inflationNoiseMaximum;
    float inflationNoiseMinimum;
    float resourceNoiseMaximum;
    float resourceNoiseMinimum;

    float loanRubBase;
    float loanUsdBase;
    float loanTermFactor;
    float loanOtherDebtWeight;
    float loanReferencePrice;
    float loanPenaltyScale;
    float loanBalanceScale;
    float loanDaysPerYear;
};

// allocNear() normally supplies a persistent page near the game's code so all
// rewritten RIP-relative displacements remain representable in x64. The
// fallback is used only when the loader does not expose allocNear(); every
// target is range-checked before any instruction is changed.
static RuntimeCells gFallbackRuntimeCells = {};
static RuntimeCells* gRuntimeCells = NULL;

struct RipPatchSpec
{
    unsigned siteRva;
    unsigned displacementOffset;
    const unsigned char* expectedBytes;
    size_t expectedLength;
    unsigned expectedTargetRva;
    const char* label;
};

struct ActiveRipPatch
{
    RipPatchSpec spec;
    const void* replacementTarget;
};

static const unsigned char kInflationRubMaximumBytes[] = {
    0xF3, 0x0F, 0x10, 0x0D, 0x42, 0xEA, 0x60, 0x00
};
static const unsigned char kInflationCommonMinimumBytes[] = {
    0xF3, 0x0F, 0x10, 0x35, 0x52, 0xE9, 0x60, 0x00
};
static const unsigned char kInflationCyclesScaleBytes[] = {
    0xF3, 0x44, 0x0F, 0x10, 0x1D, 0x48, 0xE9, 0x60, 0x00
};
static const unsigned char kInflationUsdMaximumBytes[] = {
    0xF3, 0x0F, 0x10, 0x0D, 0x61, 0xE9, 0x60, 0x00
};
static const unsigned char kInflationNoiseMaximumBytes[] = {
    0xF3, 0x0F, 0x10, 0x3D, 0x46, 0xED, 0x60, 0x00
};
static const unsigned char kInflationNoiseMinimumBytes[] = {
    0xF3, 0x0F, 0x10, 0x35, 0x1B, 0x00, 0x61, 0x00
};

static const RipPatchSpec kInflationPatchSpecs[] = {
    { 0x002FB22E, 4, kInflationRubMaximumBytes,
      sizeof(kInflationRubMaximumBytes), kRubMaximumRva,
      "inflation RUB maximum" },
    { 0x002FB236, 4, kInflationCommonMinimumBytes,
      sizeof(kInflationCommonMinimumBytes), kCommonMinimumRva,
      "inflation common minimum" },
    { 0x002FB24B, 5, kInflationCyclesScaleBytes,
      sizeof(kInflationCyclesScaleBytes), kCyclesScaleRva,
      "inflation cycles scale" },
    { 0x002FB267, 4, kInflationUsdMaximumBytes,
      sizeof(kInflationUsdMaximumBytes), kUsdMaximumRva,
      "inflation USD maximum" },
    { 0x002FB312, 4, kInflationNoiseMaximumBytes,
      sizeof(kInflationNoiseMaximumBytes), kNoiseMaximumRva,
      "inflation noise maximum" },
    { 0x002FB31D, 4, kInflationNoiseMinimumBytes,
      sizeof(kInflationNoiseMinimumBytes), kNoiseMinimumRva,
      "inflation noise minimum" }
};

// These are deliberately separate from market_noise_*: the first six sites
// belong to the general currency/market path, while these two sites are the
// only resource-price random-factor reads in the resource-record loop.
static const unsigned char kResourceNoiseMaximumBytes[] = {
    0xF3, 0x0F, 0x10, 0x35, 0xF4, 0xF1, 0x60, 0x00
};
static const unsigned char kResourceNoiseMinimumBytes[] = {
    0xF3, 0x0F, 0x10, 0x3D, 0xE4, 0xFF, 0x60, 0x00
};

static const RipPatchSpec kResourceInflationPatchSpecs[] = {
    { 0x002FB41C, 4, kResourceNoiseMaximumBytes,
      sizeof(kResourceNoiseMaximumBytes), kResourceNoiseMaximumRva,
      "resource inflation noise maximum" },
    { 0x002FB424, 4, kResourceNoiseMinimumBytes,
      sizeof(kResourceNoiseMinimumBytes), kResourceNoiseMinimumRva,
      "resource inflation noise minimum" }
};

static const unsigned char kLoanPreviewReferenceRubBytes[] = {
    0xF3, 0x0F, 0x5E, 0x3D, 0x9B, 0x61, 0x45, 0x00
};
static const unsigned char kLoanPreviewWeightRubBytes[] = {
    0xF3, 0x0F, 0x10, 0x0D, 0x88, 0x4F, 0x45, 0x00
};
static const unsigned char kLoanPreviewDaysRubBytes[] = {
    0xF3, 0x44, 0x0F, 0x10, 0x1D, 0x27, 0x60, 0x45, 0x00
};
static const unsigned char kLoanPreviewBalanceRubBytes[] = {
    0xF3, 0x0F, 0x5E, 0x15, 0x8D, 0x63, 0x45, 0x00
};
static const unsigned char kLoanPreviewPenaltyRubBytes[] = {
    0xF3, 0x0F, 0x5E, 0x1D, 0x62, 0x63, 0x45, 0x00
};
static const unsigned char kLoanPreviewRubBaseBytes[] = {
    0xF3, 0x0F, 0x58, 0x15, 0xD1, 0x52, 0x45, 0x00
};
static const unsigned char kLoanPreviewTermRubBytes[] = {
    0xF3, 0x0F, 0x59, 0x0D, 0x36, 0x4E, 0x45, 0x00
};
static const unsigned char kLoanPreviewReferenceUsdBytes[] = {
    0xF3, 0x0F, 0x5E, 0x3D, 0x47, 0x60, 0x45, 0x00
};
static const unsigned char kLoanPreviewWeightUsdBytes[] = {
    0xF3, 0x0F, 0x10, 0x0D, 0x34, 0x4E, 0x45, 0x00
};
static const unsigned char kLoanPreviewDaysUsdBytes[] = {
    0xF3, 0x44, 0x0F, 0x10, 0x1D, 0xD3, 0x5E, 0x45, 0x00
};
static const unsigned char kLoanPreviewBalanceUsdBytes[] = {
    0xF3, 0x0F, 0x5E, 0x15, 0x3D, 0x62, 0x45, 0x00
};
static const unsigned char kLoanPreviewPenaltyUsdBytes[] = {
    0xF3, 0x0F, 0x5E, 0x1D, 0x0E, 0x62, 0x45, 0x00
};
static const unsigned char kLoanPreviewUsdBaseBytes[] = {
    0xF3, 0x0F, 0x58, 0x15, 0x61, 0x53, 0x45, 0x00
};
static const unsigned char kLoanPreviewTermUsdBytes[] = {
    0xF3, 0x0F, 0x59, 0x0D, 0xE2, 0x4C, 0x45, 0x00
};

static const RipPatchSpec kLoanPreviewPatchSpecs[] = {
    { 0x004B4DC9, 4, kLoanPreviewReferenceRubBytes,
      sizeof(kLoanPreviewReferenceRubBytes), kLoanReferencePriceRva,
      "loan preview RUB reference price" },
    { 0x004B4E4C, 4, kLoanPreviewWeightRubBytes,
      sizeof(kLoanPreviewWeightRubBytes), kLoanOtherDebtWeightRva,
      "loan preview RUB other-debt weight" },
    { 0x004B4E54, 5, kLoanPreviewDaysRubBytes,
      sizeof(kLoanPreviewDaysRubBytes), kLoanDaysPerYearRva,
      "loan preview RUB days per year" },
    { 0x004B4E7F, 4, kLoanPreviewBalanceRubBytes,
      sizeof(kLoanPreviewBalanceRubBytes), kLoanBalanceScaleRva,
      "loan preview RUB balance scale" },
    { 0x004B4E92, 4, kLoanPreviewPenaltyRubBytes,
      sizeof(kLoanPreviewPenaltyRubBytes), kLoanPenaltyScaleRva,
      "loan preview RUB penalty scale" },
    { 0x004B4EA7, 4, kLoanPreviewRubBaseBytes,
      sizeof(kLoanPreviewRubBaseBytes), kLoanRubBaseRva,
      "loan preview RUB base rate" },
    { 0x004B4EC6, 4, kLoanPreviewTermRubBytes,
      sizeof(kLoanPreviewTermRubBytes), kLoanTermFactorRva,
      "loan preview RUB term factor" },
    { 0x004B4F1D, 4, kLoanPreviewReferenceUsdBytes,
      sizeof(kLoanPreviewReferenceUsdBytes), kLoanReferencePriceRva,
      "loan preview USD reference price" },
    { 0x004B4FA0, 4, kLoanPreviewWeightUsdBytes,
      sizeof(kLoanPreviewWeightUsdBytes), kLoanOtherDebtWeightRva,
      "loan preview USD other-debt weight" },
    { 0x004B4FA8, 5, kLoanPreviewDaysUsdBytes,
      sizeof(kLoanPreviewDaysUsdBytes), kLoanDaysPerYearRva,
      "loan preview USD days per year" },
    { 0x004B4FCF, 4, kLoanPreviewBalanceUsdBytes,
      sizeof(kLoanPreviewBalanceUsdBytes), kLoanBalanceScaleRva,
      "loan preview USD balance scale" },
    { 0x004B4FE6, 4, kLoanPreviewPenaltyUsdBytes,
      sizeof(kLoanPreviewPenaltyUsdBytes), kLoanPenaltyScaleRva,
      "loan preview USD penalty scale" },
    { 0x004B4FFB, 4, kLoanPreviewUsdBaseBytes,
      sizeof(kLoanPreviewUsdBaseBytes), kLoanUsdBaseRva,
      "loan preview USD base rate" },
    { 0x004B501A, 4, kLoanPreviewTermUsdBytes,
      sizeof(kLoanPreviewTermUsdBytes), kLoanTermFactorRva,
      "loan preview USD term factor" }
};

typedef float (__fastcall *LoanRateFunction)(void* gameObject,
                                             int currencyType,
                                             int durationDays);
typedef void* (__fastcall *ResourceGetFunction)(void* gameObject,
                                                const char* resourceName);
static LoanRateFunction gOriginalLoanRate = NULL;

static bool ReadFloatSetting(const char* key, float fallback, float* result)
{
    char text[96] = {};
    int present = H->configString(kIniName, kSection, key,
                                  text, (int)sizeof(text), "");
    if (!present || !text[0])
    {
        *result = fallback;
        return true;
    }

    char* end = NULL;
    double value = strtod(text, &end);
    while (end && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n'))
        ++end;
    if (end == text || !end || *end != '\0' || !_finite(value))
    {
        Logf("TESMIOINFLATION invalid numeric setting %s=%s", key, text);
        return false;
    }
    *result = (float)value;
    return true;
}

static bool ReadSettings(InflationSettings* s)
{
    // The game uses one 0.01 lower bound for both currency random draws.
    // The two upper bounds are separate.
    if (!ReadFloatSetting("inflation_min_percent", 1.0f, &s->minimumPercent))
        return false;
    if (!ReadFloatSetting("rub_inflation_max_percent", 10.0f,
                          &s->rubMaximumPercent))
        return false;
    if (!ReadFloatSetting("usd_inflation_max_percent", 3.0f,
                          &s->usdMaximumPercent))
        return false;
    if (!ReadFloatSetting("cycles_per_year", 73.0f, &s->cyclesPerYear))
        return false;
    // These two values are the raw arguments of C3DRandom_Float, not decimal
    // fractions. Vanilla is -1.5 .. +1.5.
    if (!ReadFloatSetting("market_noise_min", -1.5f, &s->noiseMinimum))
        return false;
    if (!ReadFloatSetting("market_noise_max", 1.5f, &s->noiseMaximum))
        return false;
    // These are the raw arguments of the separate random draw used only by
    // the resource-record price loop. Vanilla is -4.0 .. +4.0.
    if (!ReadFloatSetting("resource_market_noise_min", -4.0f,
                          &s->resourceNoiseMinimum))
        return false;
    if (!ReadFloatSetting("resource_market_noise_max", 4.0f,
                          &s->resourceNoiseMaximum))
        return false;

    // WRSR passes currency type 2 for RUB and 1 for USD. These defaults are
    // the two constants used by the game's loan-rate function.
    if (!ReadFloatSetting("rub_loan_base_interest_percent", 1.8f,
                          &s->loan.rubBasePercent))
        return false;
    if (!ReadFloatSetting("usd_loan_base_interest_percent", 2.5f,
                          &s->loan.usdBasePercent))
        return false;
    if (!ReadFloatSetting("loan_term_factor", 0.25f,
                          &s->loan.termFactor))
        return false;
    if (!ReadFloatSetting("loan_other_currency_debt_weight", 0.5f,
                          &s->loan.otherCurrencyDebtWeight))
        return false;
    if (!ReadFloatSetting("loan_reference_steel_price", 550.0f,
                          &s->loan.referenceSteelPrice))
        return false;
    if (!ReadFloatSetting("loan_penalty_debt_scale", 1000000.0f,
                          &s->loan.penaltyDebtScale))
        return false;
    if (!ReadFloatSetting("loan_balance_debt_scale", 15000000.0f,
                          &s->loan.balanceDebtScale))
        return false;
    if (!ReadFloatSetting("loan_days_per_year", 365.0f,
                          &s->loan.daysPerYear))
        return false;

    if (s->minimumPercent < -99.0f || s->minimumPercent > 1000.0f ||
        s->rubMaximumPercent < -99.0f || s->rubMaximumPercent > 1000.0f ||
        s->usdMaximumPercent < -99.0f || s->usdMaximumPercent > 1000.0f ||
        s->minimumPercent > s->rubMaximumPercent ||
        s->minimumPercent > s->usdMaximumPercent ||
        s->cyclesPerYear <= 0.001f || s->cyclesPerYear > 100000.0f ||
        s->noiseMinimum < -1000.0f || s->noiseMinimum > 1000.0f ||
        s->noiseMaximum < -1000.0f || s->noiseMaximum > 1000.0f ||
        s->noiseMinimum > s->noiseMaximum ||
        s->resourceNoiseMinimum < -1000.0f ||
            s->resourceNoiseMinimum > 1000.0f ||
        s->resourceNoiseMaximum < -1000.0f ||
            s->resourceNoiseMaximum > 1000.0f ||
        s->resourceNoiseMinimum > s->resourceNoiseMaximum ||
        s->resourceNoiseMinimum <= -s->cyclesPerYear ||
        s->loan.rubBasePercent < 0.0f || s->loan.rubBasePercent > 1000.0f ||
        s->loan.usdBasePercent < 0.0f || s->loan.usdBasePercent > 1000.0f ||
        s->loan.termFactor < -1000.0f || s->loan.termFactor > 1000.0f ||
        s->loan.otherCurrencyDebtWeight < -1000.0f ||
            s->loan.otherCurrencyDebtWeight > 1000.0f ||
        s->loan.referenceSteelPrice <= 0.001f ||
            s->loan.referenceSteelPrice > 1000000000.0f ||
        s->loan.penaltyDebtScale <= 0.001f ||
            s->loan.penaltyDebtScale > 1000000000000.0f ||
        s->loan.balanceDebtScale <= 0.001f ||
            s->loan.balanceDebtScale > 1000000000000.0f ||
        s->loan.daysPerYear <= 0.001f || s->loan.daysPerYear > 100000.0f)
    {
        Logf("TESMIOINFLATION rejected settings: bounds or cycles are invalid");
        return false;
    }
    return true;
}

static bool RangeReadable(unsigned rva, size_t length)
{
    if (!g_exeBase ||
        !HostHasField(offsetof(TsmHost, readablePtr),
                      sizeof(H->readablePtr)) ||
        !H->readablePtr)
        return false;
    if (g_exeSize && ((size_t)rva + length > (size_t)g_exeSize))
        return false;
    return H->readablePtr(g_exeBase + rva, length) != 0;
}

static bool BytesMatch(unsigned rva, const unsigned char* expected, size_t length)
{
    return RangeReadable(rva, length) &&
           memcmp(g_exeBase + rva, expected, length) == 0;
}

static bool VerifyRipTarget(unsigned instructionRva, unsigned displacementOffset,
                            unsigned expectedTargetRva)
{
    if (!RangeReadable(instructionRva + displacementOffset, sizeof(int)))
        return false;
    int displacement = 0;
    memcpy(&displacement, g_exeBase + instructionRva + displacementOffset,
           sizeof(displacement));
    unsigned actualTarget = instructionRva + displacementOffset + sizeof(int) +
                            (unsigned)displacement;
    return actualTarget == expectedTargetRva;
}

static bool VerifyRipPatchSpecs(const RipPatchSpec* specs, size_t count)
{
    if (!specs || count == 0)
        return false;
    for (size_t i = 0; i < count; ++i)
    {
        const RipPatchSpec& spec = specs[i];
        if (!BytesMatch(spec.siteRva, spec.expectedBytes,
                        spec.expectedLength) ||
            !VerifyRipTarget(spec.siteRva, spec.displacementOffset,
                              spec.expectedTargetRva))
            return false;
    }
    return true;
}

static bool VerifyGameLayout()
{
    static const unsigned char prologue[] = {
        0x48, 0x8B, 0xC4, 0x56, 0x57, 0x41, 0x54, 0x41,
        0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0xD0, 0x00
    };
    static const unsigned char neutralSite[] = {
        0xF3, 0x44, 0x0F, 0x10, 0x25, 0xF6, 0xEC, 0x60, 0x00
    };
    static const unsigned char noiseScaleSite[] = {
        0xF3, 0x44, 0x0F, 0x5E, 0x2D, 0xBA, 0xFE, 0x60, 0x00
    };

    bool ok = true;
    ok = ok && BytesMatch(kEconomyFunctionRva, prologue, sizeof(prologue));
    ok = ok && VerifyRipPatchSpecs(
        kInflationPatchSpecs,
        sizeof(kInflationPatchSpecs) / sizeof(kInflationPatchSpecs[0]));
    ok = ok && VerifyRipPatchSpecs(
        kResourceInflationPatchSpecs,
        sizeof(kResourceInflationPatchSpecs) /
            sizeof(kResourceInflationPatchSpecs[0]));
    ok = ok && BytesMatch(0x002FB259, neutralSite, sizeof(neutralSite));
    ok = ok && BytesMatch(0x002FB309, noiseScaleSite, sizeof(noiseScaleSite));

    // The displacement checks make the intended data cells explicit and stop
    // a coincidentally similar instruction sequence from being patched.
    ok = ok && VerifyRipTarget(0x002FB259, 5, kNeutralFactorRva);
    ok = ok && VerifyRipTarget(0x002FB309, 5, kNoiseCountScaleRva);

    if (!ok)
    {
        Logf("TESMIOINFLATION refused: unsupported SOVIET64 layout; no memory was changed");
        return false;
    }
    return true;
}

static bool VerifyLoanLayout()
{
    static const unsigned char loanRatePrologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
        0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x40
    };
    static const unsigned char referencePriceSite[] = {
        0xF3, 0x0F, 0x5E, 0x2D, 0x8D, 0x14, 0x45, 0x00
    };
    static const unsigned char debtWeightSiteA[] = {
        0xF3, 0x0F, 0x59, 0x1D, 0x2A, 0x02, 0x45, 0x00
    };
    static const unsigned char debtWeightSiteB[] = {
        0xF3, 0x0F, 0x59, 0x15, 0x22, 0x02, 0x45, 0x00
    };
    static const unsigned char daysPerYearSite[] = {
        0xF3, 0x0F, 0x5E, 0x05, 0xB3, 0x12, 0x45, 0x00
    };
    static const unsigned char usdBaseSite[] = {
        0xF3, 0x0F, 0x10, 0x25, 0x7E, 0x07, 0x45, 0x00
    };
    static const unsigned char rubBaseSite[] = {
        0xF3, 0x0F, 0x10, 0x25, 0x8B, 0x05, 0x45, 0x00
    };
    static const unsigned char penaltyScaleSite[] = {
        0xF3, 0x0F, 0x5E, 0x15, 0xFF, 0x15, 0x45, 0x00
    };
    static const unsigned char balanceScaleSite[] = {
        0xF3, 0x0F, 0x5E, 0x1D, 0x05, 0x16, 0x45, 0x00
    };
    static const unsigned char termFactorSite[] = {
        0xF3, 0x0F, 0x59, 0x05, 0xED, 0x00, 0x45, 0x00
    };

    bool ok = true;
    ok = ok && BytesMatch(kLoanRateFunctionRva, loanRatePrologue,
                          sizeof(loanRatePrologue));
    // Steel is now read from the validated live vector below. ResourceGet is
    // intentionally not an anchor: resources.dll legitimately hooks it first.
    ok = ok && BytesMatch(0x004B9AD7, referencePriceSite,
                          sizeof(referencePriceSite));
    ok = ok && BytesMatch(0x004B9BAA, debtWeightSiteA,
                          sizeof(debtWeightSiteA));
    ok = ok && BytesMatch(0x004B9BB2, debtWeightSiteB,
                          sizeof(debtWeightSiteB));
    ok = ok && BytesMatch(0x004B9BC9, daysPerYearSite,
                          sizeof(daysPerYearSite));
    ok = ok && BytesMatch(0x004B9BDE, usdBaseSite, sizeof(usdBaseSite));
    ok = ok && BytesMatch(0x004B9BED, rubBaseSite, sizeof(rubBaseSite));
    ok = ok && BytesMatch(0x004B9BF5, penaltyScaleSite,
                          sizeof(penaltyScaleSite));
    ok = ok && BytesMatch(0x004B9C07, balanceScaleSite,
                          sizeof(balanceScaleSite));
    ok = ok && BytesMatch(0x004B9C0F, termFactorSite,
                          sizeof(termFactorSite));

    // Validate every data read in both inline preview branches, not only the
    // two base-rate additions. All of these operands are redirected together
    // later, so a partial or shifted preview formula is refused.
    ok = ok && VerifyRipPatchSpecs(
        kLoanPreviewPatchSpecs,
        sizeof(kLoanPreviewPatchSpecs) / sizeof(kLoanPreviewPatchSpecs[0]));

    ok = ok && VerifyRipTarget(0x004B9AD7, 4, kLoanReferencePriceRva);
    ok = ok && VerifyRipTarget(0x004B9BAA, 4, kLoanOtherDebtWeightRva);
    ok = ok && VerifyRipTarget(0x004B9BB2, 4, kLoanOtherDebtWeightRva);
    ok = ok && VerifyRipTarget(0x004B9BC9, 4, kLoanDaysPerYearRva);
    ok = ok && VerifyRipTarget(0x004B9BDE, 4, kLoanUsdBaseRva);
    ok = ok && VerifyRipTarget(0x004B9BED, 4, kLoanRubBaseRva);
    ok = ok && VerifyRipTarget(0x004B9BF5, 4, kLoanPenaltyScaleRva);
    ok = ok && VerifyRipTarget(0x004B9C07, 4, kLoanBalanceScaleRva);
    ok = ok && VerifyRipTarget(0x004B9C0F, 4, kLoanTermFactorRva);
    if (!ok)
    {
        Logf("TESMIOINFLATION refused: unsupported loan-rate layout; no loan hook installed");
        return false;
    }
    return true;
}

static bool ReadRuntimeValue(const void* address, size_t length, void* out)
{
    if (!HostHasField(offsetof(TsmHost, readablePtr),
                      sizeof(H->readablePtr)) ||
        !H->readablePtr || !address || !out || length == 0)
        return false;
    if (!H->readablePtr(address, length))
        return false;
    memcpy(out, address, length);
    return true;
}

static float OriginalLoanRateOrZero(void* gameObject, int currencyType,
                                    int durationDays)
{
    if (!gOriginalLoanRate)
        return 0.0f;
    return gOriginalLoanRate(gameObject, currencyType, durationDays);
}

static bool ComputeConfiguredLoanRate(void* gameObject, int currencyType,
                                      int durationDays, float* result)
{
    if (!result || !gameObject || (currencyType != 1 && currencyType != 2))
        return false;

    BYTE* self = static_cast<BYTE*>(gameObject);
    uintptr_t begin = 0;
    uintptr_t end = 0;
    if (!ReadRuntimeValue(self + kLoanVectorBeginOffset, sizeof(begin), &begin) ||
        !ReadRuntimeValue(self + kLoanVectorEndOffset, sizeof(end), &end))
        return false;
    if (end < begin)
        return false;

    uintptr_t byteCount = end - begin;
    if (byteCount % kLoanRecordStride != 0)
        return false;
    uintptr_t recordCount = byteCount / kLoanRecordStride;
    if (recordCount > 100000)
        return false;
    if (byteCount != 0 &&
        (!H || !H->readablePtr ||
         !H->readablePtr(reinterpret_cast<const void*>(begin),
                         (size_t)byteCount)))
        return false;

    float sameCurrencyAmount = 0.0f;
    float sameCurrencyPenalty = 0.0f;
    float otherCurrencyAmount = 0.0f;
    float otherCurrencyPenalty = 0.0f;
    for (uintptr_t i = 0; i < recordCount; ++i)
    {
        uintptr_t record = begin + i * kLoanRecordStride;
        int recordType = 0;
        float amount = 0.0f;
        float penalty = 0.0f;
        if (!ReadRuntimeValue(reinterpret_cast<const void*>(record +
                                                            kLoanRecordTypeOffset),
                              sizeof(recordType), &recordType) ||
            !ReadRuntimeValue(reinterpret_cast<const void*>(record +
                                                            kLoanRecordAmountOffset),
                              sizeof(amount), &amount) ||
            !ReadRuntimeValue(reinterpret_cast<const void*>(record +
                                                            kLoanRecordPenaltyOffset),
                              sizeof(penalty), &penalty) ||
            !_finite(amount) || !_finite(penalty))
            return false;

        // The original function adds the amount and penalty fields to one of
        // two pairs of accumulators. Type 1 is USD and type 2 is RUB.
        if (recordType == currencyType)
        {
            sameCurrencyPenalty += penalty;
            float total = penalty;
            total += amount;
            sameCurrencyAmount += total;
        }
        else
        {
            otherCurrencyPenalty += penalty;
            otherCurrencyAmount += amount;
            otherCurrencyAmount += penalty;
        }
    }

    // The game obtains the current steel price from the resource table. Its
    // +0x58 field is USD and +0x5C is RUB in this loan function.
    CompatResourceTable resources = {};
    if (!CompatReadResources(gameObject, &resources)) return false;
    void* steel = NULL;
    for (size_t i = 0; i < resources.count; ++i)
    {
        BYTE* rec = resources.begin + i * 832;
        char name[32] = {};
        if (!CompatResourceName(rec, name)) return false;
        if (!strcmp(name, "steel")) { steel = rec; break; }
    }
    if (!steel)
        return false;
    const unsigned priceOffset = (currencyType == 2) ? 0x5C : 0x58;
    float steelPrice = 0.0f;
    if (!ReadRuntimeValue(static_cast<BYTE*>(steel) + priceOffset,
                          sizeof(steelPrice), &steelPrice) ||
        !_finite(steelPrice))
        return false;

    float steelFactor = steelPrice / gLoanSettings.referenceSteelPrice;
    if (!_finite(steelFactor) || steelFactor <= 0.0f)
        return false;

    // This is the game's formula, with its constants replaced by the
    // corresponding INI values. The result is an annual percentage, not a
    // fraction: 1.8 means 1.8%.
    float balancePressure = otherCurrencyAmount *
                            gLoanSettings.otherCurrencyDebtWeight;
    balancePressure += sameCurrencyAmount;
    balancePressure /= steelFactor;
    balancePressure /= gLoanSettings.balanceDebtScale;

    float penaltyPressure = otherCurrencyPenalty *
                            gLoanSettings.otherCurrencyDebtWeight;
    penaltyPressure += sameCurrencyPenalty;
    penaltyPressure /= steelFactor;
    penaltyPressure /= gLoanSettings.penaltyDebtScale;

    float rate = (currencyType == 2) ? gLoanSettings.rubBasePercent
                                     : gLoanSettings.usdBasePercent;
    float debtPressure = penaltyPressure;
    debtPressure += balancePressure;
    rate += debtPressure;

    float term = static_cast<float>(durationDays);
    term /= gLoanSettings.daysPerYear;
    term *= gLoanSettings.termFactor;
    term *= rate;
    rate += term;

    if (!_finite(rate))
        return false;
    *result = rate;
    return true;
}

static float __fastcall LoanRateDetour(void* gameObject, int currencyType,
                                       int durationDays)
{
    float result = 0.0f;
    if (ComputeConfiguredLoanRate(gameObject, currencyType, durationDays,
                                  &result))
        return result;
    return OriginalLoanRateOrZero(gameObject, currencyType, durationDays);
}

static bool InstallLoanRateHook()
{
    static const unsigned char expect[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
        0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x40
    };
    if (!HostHasField(offsetof(TsmHost, installInlineHook),
                      sizeof(H->installInlineHook)) ||
        !H->installInlineHook)
    {
        Logf("TESMIOINFLATION refused: TesmioLoader has no inline-hook API");
        return false;
    }
    int installed = H->installInlineHook(
        g_exeBase + kLoanRateFunctionRva,
        reinterpret_cast<void*>(&LoanRateDetour),
        reinterpret_cast<void**>(&gOriginalLoanRate),
        expect, sizeof(expect), "TESMIOINFLATION loan-rate formula");
    if (!installed)
    {
        Logf("TESMIOINFLATION refused: loan-rate inline hook was not installed");
        return false;
    }
    Logf("TESMIOINFLATION loan-rate formula hook installed at RVA 0x%08X",
         kLoanRateFunctionRva);
    return true;
}

static bool ComputeRipDisplacement(unsigned siteRva, unsigned displacementOffset,
                                   const void* target, int32_t* result)
{
    if (!target || !result || !g_exeBase)
        return false;

    const BYTE* nextInstruction = g_exeBase + siteRva +
                                   displacementOffset + sizeof(int32_t);
    intptr_t targetAddress = static_cast<intptr_t>(
        reinterpret_cast<uintptr_t>(target));
    intptr_t nextAddress = static_cast<intptr_t>(
        reinterpret_cast<uintptr_t>(nextInstruction));
    intptr_t delta = targetAddress - nextAddress;
    if (delta < static_cast<intptr_t>(-2147483648LL) ||
        delta > static_cast<intptr_t>(2147483647LL))
        return false;

    *result = static_cast<int32_t>(delta);
    return true;
}

static bool WriteRipDisplacement(unsigned siteRva, unsigned displacementOffset,
                                 int32_t displacement)
{
    BYTE* address = g_exeBase + siteRva + displacementOffset;
    DWORD oldProtect = 0;
    if (!VirtualProtect(address, sizeof(displacement),
                        PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    memcpy(address, &displacement, sizeof(displacement));
    DWORD ignored = 0;
    BOOL restored = VirtualProtect(address, sizeof(displacement), oldProtect,
                                   &ignored);
    FlushInstructionCache(g_exe, address, sizeof(displacement));
    return restored != FALSE;
}

static bool StoreRuntimeCells(const RuntimeCells& values)
{
    if (!gRuntimeCells)
        return false;

    DWORD oldProtect = 0;
    if (!VirtualProtect(gRuntimeCells, sizeof(RuntimeCells), PAGE_READWRITE,
                        &oldProtect))
    {
        Logf("TESMIOINFLATION refused: private runtime storage is not writable (%lu)",
             (unsigned long)GetLastError());
        return false;
    }

    memcpy(gRuntimeCells, &values, sizeof(values));
    DWORD ignored = 0;
    BOOL restored = VirtualProtect(gRuntimeCells, sizeof(RuntimeCells),
                                   oldProtect, &ignored);
    if (!restored)
    {
        Logf("TESMIOINFLATION refused: private runtime storage protection restore failed (%lu)",
             (unsigned long)GetLastError());
        return false;
    }
    return true;
}

static bool PrepareRuntimeCells(const InflationSettings& s)
{
    RuntimeCells values = {};
    values.inflationMinimum = s.minimumPercent / 100.0f;
    values.inflationRubMaximum = s.rubMaximumPercent / 100.0f;
    values.inflationUsdMaximum = s.usdMaximumPercent / 100.0f;
    values.inflationCyclesScale = 1.0f / s.cyclesPerYear;
    values.inflationNoiseMaximum = s.noiseMaximum;
    values.inflationNoiseMinimum = s.noiseMinimum;
    values.resourceNoiseMaximum = s.resourceNoiseMaximum;
    values.resourceNoiseMinimum = s.resourceNoiseMinimum;
    values.loanRubBase = s.loan.rubBasePercent;
    values.loanUsdBase = s.loan.usdBasePercent;
    values.loanTermFactor = s.loan.termFactor;
    values.loanOtherDebtWeight = s.loan.otherCurrencyDebtWeight;
    values.loanReferencePrice = s.loan.referenceSteelPrice;
    values.loanPenaltyScale = s.loan.penaltyDebtScale;
    values.loanBalanceScale = s.loan.balanceDebtScale;
    values.loanDaysPerYear = s.loan.daysPerYear;

    RuntimeCells* candidate = NULL;
    if (HostHasField(offsetof(TsmHost, allocNear), sizeof(H->allocNear)) &&
        H->allocNear)
    {
        candidate = reinterpret_cast<RuntimeCells*>(H->allocNear(
            g_exeBase + kEconomyFunctionRva, 0x1000));
    }
    if (!candidate)
    {
        candidate = &gFallbackRuntimeCells;
        Logf("TESMIOINFLATION warning: allocNear unavailable; using range-checked DLL storage");
    }

    gRuntimeCells = candidate;
    if (!StoreRuntimeCells(values))
    {
        gRuntimeCells = NULL;
        return false;
    }
    return true;
}

struct AppliedRipPatch
{
    const RipPatchSpec* spec;
    int32_t oldDisplacement;
};

static bool ApplyRipPatches(const ActiveRipPatch* patches, size_t count,
                            const char* group)
{
    if (!patches || count == 0 || count > 32)
        return false;

    AppliedRipPatch applied[32] = {};
    size_t appliedCount = 0;
    for (size_t i = 0; i < count; ++i)
    {
        const RipPatchSpec& spec = patches[i].spec;
        if (!BytesMatch(spec.siteRva, spec.expectedBytes,
                        spec.expectedLength) ||
            !VerifyRipTarget(spec.siteRva, spec.displacementOffset,
                              spec.expectedTargetRva))
        {
            Logf("TESMIOINFLATION refused: %s site no longer matches the supported layout",
                 spec.label);
            goto fail;
        }

        int32_t oldDisplacement = 0;
        memcpy(&oldDisplacement,
               g_exeBase + spec.siteRva + spec.displacementOffset,
               sizeof(oldDisplacement));
        int32_t newDisplacement = 0;
        if (!ComputeRipDisplacement(spec.siteRva, spec.displacementOffset,
                                    patches[i].replacementTarget,
                                    &newDisplacement))
        {
            Logf("TESMIOINFLATION refused: %s private target is outside x64 RIP range",
                 spec.label);
            goto fail;
        }

        applied[appliedCount].spec = &spec;
        applied[appliedCount].oldDisplacement = oldDisplacement;
        ++appliedCount;
        if (!WriteRipDisplacement(spec.siteRva, spec.displacementOffset,
                                   newDisplacement))
        {
            Logf("TESMIOINFLATION refused: could not patch %s (%lu)",
                 spec.label, (unsigned long)GetLastError());
            goto fail;
        }
    }

    Logf("TESMIOINFLATION redirected %u %s data reads to private storage",
         (unsigned)count, group ? group : "");
    return true;

fail:
    while (appliedCount != 0)
    {
        --appliedCount;
        const RipPatchSpec* spec = applied[appliedCount].spec;
        if (!WriteRipDisplacement(spec->siteRva, spec->displacementOffset,
                                  applied[appliedCount].oldDisplacement))
        {
            Logf("TESMIOINFLATION ERROR: failed to roll back %s",
                 spec->label);
        }
    }
    return false;
}

static bool ApplyMarketSettings(const InflationSettings& s)
{
    if (!PrepareRuntimeCells(s))
        return false;

    ActiveRipPatch patches[
        sizeof(kInflationPatchSpecs) / sizeof(kInflationPatchSpecs[0]) +
        sizeof(kResourceInflationPatchSpecs) /
            sizeof(kResourceInflationPatchSpecs[0])] = {};
    for (unsigned i = 0; i < sizeof(patches) / sizeof(patches[0]); ++i)
    {
        if (i < sizeof(kInflationPatchSpecs) / sizeof(kInflationPatchSpecs[0]))
            patches[i].spec = kInflationPatchSpecs[i];
        else
            patches[i].spec = kResourceInflationPatchSpecs[
                i - sizeof(kInflationPatchSpecs) /
                    sizeof(kInflationPatchSpecs[0])];
    }
    patches[0].replacementTarget = &gRuntimeCells->inflationRubMaximum;
    patches[1].replacementTarget = &gRuntimeCells->inflationMinimum;
    patches[2].replacementTarget = &gRuntimeCells->inflationCyclesScale;
    patches[3].replacementTarget = &gRuntimeCells->inflationUsdMaximum;
    patches[4].replacementTarget = &gRuntimeCells->inflationNoiseMaximum;
    patches[5].replacementTarget = &gRuntimeCells->inflationNoiseMinimum;
    patches[6].replacementTarget = &gRuntimeCells->resourceNoiseMaximum;
    patches[7].replacementTarget = &gRuntimeCells->resourceNoiseMinimum;

    if (!ApplyRipPatches(patches, sizeof(patches) / sizeof(patches[0]),
                         "market-inflation/resource-inflation"))
    {
        gRuntimeCells = NULL;
        return false;
    }

    Logf("TESMIOINFLATION redirected 2 resource-inflation reads at RVA 0x002FB41C/0x002FB424; shared resource data cells were not written");
    Logf("TESMIOINFLATION market settings are scoped to economy RVA 0x%08X; shared data cells were not written",
         kEconomyFunctionRva);
    return true;
}

static bool ApplyLoanPreviewSettings()
{
    if (!gRuntimeCells)
        return false;

    ActiveRipPatch patches[sizeof(kLoanPreviewPatchSpecs) /
                           sizeof(kLoanPreviewPatchSpecs[0])] = {};
    for (unsigned i = 0; i < sizeof(patches) / sizeof(patches[0]); ++i)
        patches[i].spec = kLoanPreviewPatchSpecs[i];

    const void* targets[] = {
        &gRuntimeCells->loanReferencePrice,
        &gRuntimeCells->loanOtherDebtWeight,
        &gRuntimeCells->loanDaysPerYear,
        &gRuntimeCells->loanBalanceScale,
        &gRuntimeCells->loanPenaltyScale,
        &gRuntimeCells->loanRubBase,
        &gRuntimeCells->loanTermFactor,
        &gRuntimeCells->loanReferencePrice,
        &gRuntimeCells->loanOtherDebtWeight,
        &gRuntimeCells->loanDaysPerYear,
        &gRuntimeCells->loanBalanceScale,
        &gRuntimeCells->loanPenaltyScale,
        &gRuntimeCells->loanUsdBase,
        &gRuntimeCells->loanTermFactor
    };
    for (unsigned i = 0; i < sizeof(patches) / sizeof(patches[0]); ++i)
        patches[i].replacementTarget = targets[i];

    if (!ApplyRipPatches(patches, sizeof(patches) / sizeof(patches[0]),
                         "Economy & Trade loan-preview"))
        return false;

    Logf("TESMIOINFLATION loan preview reads are scoped to function RVA 0x%08X; shared loan cells were not written",
         kLoanPreviewFunctionRva);
    return true;
}

// Kept below only as a source-history reference. It is intentionally excluded
// from the build because it writes shared game data cells.
#if 0
static bool ApplySettings(const InflationSettings& s)
{
    // All six editable cells lie in this one read-only data-page range.
    // Make it writable only for the short write window, then restore the old
    // protection. The executable file on disk is never opened or modified.
    const unsigned pageRva = 0x00909000;
    const SIZE_T pageSize = 0x3000;
    if (!RangeReadable(pageRva, pageSize))
    {
        Logf("TESMIOINFLATION refused: expected constant page is not readable");
        return false;
    }

    struct Patch
    {
        const char* name;
        unsigned rva;
        float value;
    } patches[] = {
        { "inflation_min_percent", kCommonMinimumRva, s.minimumPercent / 100.0f },
        { "rub_inflation_max_percent", kRubMaximumRva, s.rubMaximumPercent / 100.0f },
        { "usd_inflation_max_percent", kUsdMaximumRva, s.usdMaximumPercent / 100.0f },
        { "cycles_per_year", kCyclesScaleRva, 1.0f / s.cyclesPerYear },
        { "market_noise_max", kNoiseMaximumRva, s.noiseMaximum },
        { "market_noise_min", kNoiseMinimumRva, s.noiseMinimum }
    };

    float oldValues[sizeof(patches) / sizeof(patches[0])] = {};
    for (unsigned i = 0; i < sizeof(patches) / sizeof(patches[0]); ++i)
    {
        if (!RangeReadable(patches[i].rva, sizeof(float)))
        {
            Logf("TESMIOINFLATION refused: target %s is not readable", patches[i].name);
            return false;
        }
        memcpy(&oldValues[i], g_exeBase + patches[i].rva, sizeof(float));
    }

    DWORD oldProtect = 0;
    BYTE* page = g_exeBase + pageRva;
    if (!VirtualProtect(page, pageSize, PAGE_READWRITE, &oldProtect))
    {
        Logf("TESMIOINFLATION refused: VirtualProtect failed (%lu)",
             (unsigned long)GetLastError());
        return false;
    }

    for (unsigned i = 0; i < sizeof(patches) / sizeof(patches[0]); ++i)
        memcpy(g_exeBase + patches[i].rva, &patches[i].value, sizeof(float));

    DWORD restoreError = ERROR_SUCCESS;
    if (!VirtualProtect(page, pageSize, oldProtect, &oldProtect))
        restoreError = GetLastError();
    FlushInstructionCache(g_exe, page, pageSize);

    if (restoreError != ERROR_SUCCESS)
    {
        Logf("TESMIOINFLATION warning: data was patched but page protection restore failed (%lu)",
             (unsigned long)restoreError);
    }

    Logf("TESMIOINFLATION applied to WRSR v1.1.1.9 at economy RVA 0x%08X",
         kEconomyFunctionRva);
    for (unsigned i = 0; i < sizeof(patches) / sizeof(patches[0]); ++i)
    {
        Logf("TESMIOINFLATION %s: %.9g -> %.9g",
             patches[i].name, (double)oldValues[i], (double)patches[i].value);
    }
    return true;
}

static bool ApplyLoanFormulaConstants(const LoanSettings& s)
{
    // These cells are shared by the original loan function and the inline
    // Economy & Trade preview formula. The write is process-local and the
    // original page protection is restored immediately afterwards.
    const unsigned pageRva = 0x00909000;
    const SIZE_T pageSize = 0x3000;
    if (!RangeReadable(pageRva, pageSize))
    {
        Logf("TESMIOINFLATION refused: loan constant page is not readable");
        return false;
    }

    struct Patch
    {
        const char* name;
        unsigned rva;
        float value;
    } patches[] = {
        { "rub_loan_base_interest_percent", kLoanRubBaseRva,
          s.rubBasePercent },
        { "usd_loan_base_interest_percent", kLoanUsdBaseRva,
          s.usdBasePercent },
        { "loan_term_factor", kLoanTermFactorRva, s.termFactor },
        { "loan_other_currency_debt_weight", kLoanOtherDebtWeightRva,
          s.otherCurrencyDebtWeight },
        { "loan_reference_steel_price", kLoanReferencePriceRva,
          s.referenceSteelPrice },
        { "loan_penalty_debt_scale", kLoanPenaltyScaleRva,
          s.penaltyDebtScale },
        { "loan_balance_debt_scale", kLoanBalanceScaleRva,
          s.balanceDebtScale },
        { "loan_days_per_year", kLoanDaysPerYearRva, s.daysPerYear }
    };

    float oldValues[sizeof(patches) / sizeof(patches[0])] = {};
    for (unsigned i = 0; i < sizeof(patches) / sizeof(patches[0]); ++i)
    {
        if (!RangeReadable(patches[i].rva, sizeof(float)))
        {
            Logf("TESMIOINFLATION refused: loan target %s is not readable",
                 patches[i].name);
            return false;
        }
        memcpy(&oldValues[i], g_exeBase + patches[i].rva, sizeof(float));
    }

    DWORD oldProtect = 0;
    BYTE* page = g_exeBase + pageRva;
    if (!VirtualProtect(page, pageSize, PAGE_READWRITE, &oldProtect))
    {
        Logf("TESMIOINFLATION refused: loan VirtualProtect failed (%lu)",
             (unsigned long)GetLastError());
        return false;
    }

    for (unsigned i = 0; i < sizeof(patches) / sizeof(patches[0]); ++i)
        memcpy(g_exeBase + patches[i].rva, &patches[i].value, sizeof(float));

    DWORD restoreError = ERROR_SUCCESS;
    if (!VirtualProtect(page, pageSize, oldProtect, &oldProtect))
        restoreError = GetLastError();
    FlushInstructionCache(g_exe, page, pageSize);

    if (restoreError != ERROR_SUCCESS)
    {
        Logf("TESMIOINFLATION warning: loan constants were patched but page protection restore failed (%lu)",
             (unsigned long)restoreError);
    }

    Logf("TESMIOINFLATION synchronized Economy & Trade loan preview constants");
    for (unsigned i = 0; i < sizeof(patches) / sizeof(patches[0]); ++i)
    {
        Logf("TESMIOINFLATION loan %s: %.9g -> %.9g",
             patches[i].name, (double)oldValues[i], (double)patches[i].value);
    }
    return true;
}

#endif

#include "inflation_reputation.h"

extern "C" __declspec(dllexport) int TsmPluginStart(void)
{
    // Called after every plugin has initialized its optional services.
    if (!gRuntimeCells) return 0;
    return StartInflationReputation();
}

extern "C" __declspec(dllexport) unsigned TsmPluginApiVersion(void)
{
    return TSM_API_VERSION;
}

extern "C" __declspec(dllexport) int TsmPluginInit(const TsmHost* host,
                                                     TsmPluginInfo* info)
{
    if (!host || !info)
        return 1;

    TsmBind(host);
    info->name = "TESMIOINFLATION";
    info->version = "1.5.0-reputation2.1";

    if (!H->configInt(kIniName, kSection, "enabled", 1))
    {
        Logf("TESMIOINFLATION disabled by plugins\\TESMIOINFLATION.ini");
        return 1;
    }

    InflationSettings settings = {};
    if (!ReadSettings(&settings) || !VerifyGameLayout())
        return 1;

    int loanRatesEnabled =
        H->configInt(kIniName, kSection, "loan_rates_enabled", 1);
    int loanPreviewSyncEnabled =
        H->configInt(kIniName, kSection, "loan_preview_sync_enabled", 1);
    if ((loanRatesEnabled || loanPreviewSyncEnabled) && !VerifyLoanLayout())
    {
        Logf("TESMIOINFLATION warning: loan layout is unsupported; loan features disabled");
        loanRatesEnabled = 0;
        loanPreviewSyncEnabled = 0;
    }

    gLoanSettings = settings.loan;
    if (!ApplyMarketSettings(settings))
        return 1;

    if (loanRatesEnabled && !InstallLoanRateHook())
    {
        // Do not fail the whole plugin after market code has been redirected:
        // a failed loan hook must not leave a loader that may unload this DLL
        // while the game still points at it.
        Logf("TESMIOINFLATION warning: loan-rate hook failed; loan features disabled");
        loanRatesEnabled = 0;
        loanPreviewSyncEnabled = 0;
    }

    if (loanPreviewSyncEnabled && loanRatesEnabled &&
        !ApplyLoanPreviewSettings())
    {
        Logf("TESMIOINFLATION warning: loan preview patch failed; preview synchronization disabled");
        loanPreviewSyncEnabled = 0;
    }

    if (!loanRatesEnabled)
        Logf("TESMIOINFLATION loan-rate hook inactive (INI/layout/hook status above)");
    if (!loanPreviewSyncEnabled)
        Logf("TESMIOINFLATION Economy & Trade loan preview synchronization inactive (status above)");
    else if (!loanRatesEnabled)
        Logf("TESMIOINFLATION loan preview synchronization skipped because loan_rates_enabled=0");
    return 0;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(module);
    return TRUE;
}
