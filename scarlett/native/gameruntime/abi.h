#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdbool.h>

#define NOT_INITIALIZED ((HRESULT)0x89240100)
#define VERSION_MISMATCH ((HRESULT)0x89240102)
#define ACTIVE_OBJECTS ((HRESULT)0x89240105)
#define UNSUPPORTED_OPTIONS ((HRESULT)0x8924010a)
#define SDK_VERSION UINT64_C(0x633600002712)
#define SDK_BUILD UINT64_C(0x65f400001edd)

typedef struct {
    uint32_t version, flags;
    const char *game_config;
} InitOptions;

typedef struct {
    uint16_t major, minor, build, revision;
} Version;
typedef struct {
    Version os, hosting_os;
    char family[64], form[64];
} Analytics;

static const GUID feature_id = {0x8836fe87,0xedb9,0x4fe3,{0x8d,0xad,0x05,0xf0,0xd2,0xcd,0x5b,0x40}};
static const GUID error_id = {0x8ca467f7,0x22e8,0x4096,{0x84,0x56,0xbb,0x8a,0xa1,0x3f,0x79,0xd8}};
static const GUID analytics_id = {0xb884675d,0xb738,0x4a9c,{0x81,0x5d,0x9a,0x9a,0x1e,0x0c,0x6c,0x9b}};
static const GUID unknown_id = {0,0,0,{0xc0,0,0,0,0,0,0,0x46}};
typedef struct Api Api;
typedef bool (WINAPI *ErrorCallback)(HRESULT, const char *, void *);
typedef struct {
    HRESULT (WINAPI *query)(Api *, const GUID *, void **);
    ULONG (WINAPI *add_ref)(Api *);
    ULONG (WINAPI *release)(Api *);
} UnknownTable;
typedef struct { UnknownTable base; bool (WINAPI *available)(Api *, uint32_t); } FeatureTable;
typedef struct { UnknownTable base; Analytics *(WINAPI *get)(Api *, Analytics *); } AnalyticsTable;
typedef struct {
    UnknownTable base;
    void (WINAPI *report)(Api *, HRESULT, const char *);
    void (WINAPI *set_callback)(Api *, ErrorCallback, void *);
    void (WINAPI *set_options)(Api *, uint32_t, uint32_t);
} ErrorTable;
struct Api { const void *vtable; const GUID *id; ULONG refs; };
_Static_assert(sizeof(InitOptions) == 16 && sizeof(Analytics) == 0x90 &&
               sizeof(UnknownTable) == 0x18, "Win64 ABI required");
