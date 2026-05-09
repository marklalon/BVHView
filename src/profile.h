/*******************************************************************************************
*
*    profile.h - Profiling utilities (Windows-only)
*
*******************************************************************************************/

#ifndef PROFILE_H
#define PROFILE_H

#include <stdint.h>

// Profiling only available on Windows
#if defined(ENABLE_PROFILE) && defined(_WIN32)

#include <profileapi.h>

enum
{
    PROFILE_RECORD_MAX = 512,
    PROFILE_RECORD_SAMPLE_MAX = 128,
};

typedef struct ProfileRecord
{
    const char* name;
    uint32_t idx;
    uint32_t num;

    struct {
        LARGE_INTEGER start;
        LARGE_INTEGER end;
    } samples[PROFILE_RECORD_SAMPLE_MAX];

} ProfileRecord;

typedef struct ProfileRecordData
{
    uint32_t num;
    LARGE_INTEGER freq;
    ProfileRecord* records[PROFILE_RECORD_MAX];

} ProfileRecordData;

extern ProfileRecordData globalProfileRecords;

void ProfileRecordDataInit(void);
void ProfileRecordBegin(ProfileRecord* record, const char* name);
void ProfileRecordEnd(ProfileRecord* record);

typedef struct ProfileTickers
{
    uint64_t unitScale;
    double alpha;
    uint32_t samples[PROFILE_RECORD_MAX];
    uint64_t iterations[PROFILE_RECORD_MAX];
    double averages[PROFILE_RECORD_MAX];
    double times[PROFILE_RECORD_MAX];

} ProfileTickers;

extern ProfileTickers globalProfileTickers;

void ProfileTickersInit(void);
void ProfileTickersUpdate(void);

#define PROFILE_INIT() ProfileRecordDataInit();
#define PROFILE_BEGIN(NAME) static ProfileRecord __PROFILE_RECORD_##NAME; ProfileRecordBegin(&__PROFILE_RECORD_##NAME, #NAME);
#define PROFILE_END(NAME) ProfileRecordEnd(&__PROFILE_RECORD_##NAME);
#define PROFILE_TICKERS_INIT() ProfileTickersInit();
#define PROFILE_TICKERS_UPDATE() ProfileTickersUpdate()

#else
#define PROFILE_INIT()
#define PROFILE_BEGIN(NAME)
#define PROFILE_END(NAME)
#define PROFILE_TICKERS_INIT()
#define PROFILE_TICKERS_UPDATE()
#endif

#endif // PROFILE_H
