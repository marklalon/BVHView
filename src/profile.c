/*******************************************************************************************
*
*    profile.c - Profiling implementation
*
*******************************************************************************************/

#include "profile.h"

#if defined(ENABLE_PROFILE) && defined(_WIN32)

#include <string.h>

ProfileRecordData globalProfileRecords;
ProfileTickers globalProfileTickers;

void ProfileRecordDataInit(void)
{
    globalProfileRecords.num = 0;
    QueryPerformanceFrequency(&globalProfileRecords.freq);
    memset(globalProfileRecords.records, 0, sizeof(ProfileRecord*) * PROFILE_RECORD_MAX);
}

void ProfileRecordBegin(ProfileRecord* record, const char* name)
{
    if (!record->name && globalProfileRecords.num < PROFILE_RECORD_MAX)
    {
        record->name = name;
        record->idx = 0;
        record->num = 0;
        globalProfileRecords.records[globalProfileRecords.num] = record;
        globalProfileRecords.num++;
    }

    QueryPerformanceCounter(&record->samples[record->idx].start);
}

void ProfileRecordEnd(ProfileRecord* record)
{
    QueryPerformanceCounter(&record->samples[record->idx].end);
    record->idx = (record->idx + 1) % PROFILE_RECORD_SAMPLE_MAX;
    record->num++;
}

void ProfileTickersInit(void)
{
    globalProfileTickers.unitScale = 1000000;
    globalProfileTickers.alpha = 0.9f;
    memset(globalProfileTickers.samples, 0, sizeof(uint32_t) * PROFILE_RECORD_MAX);
    memset(globalProfileTickers.iterations, 0, sizeof(uint64_t) * PROFILE_RECORD_MAX);
    memset(globalProfileTickers.averages, 0, sizeof(double) * PROFILE_RECORD_MAX);
    memset(globalProfileTickers.times, 0, sizeof(double) * PROFILE_RECORD_MAX);
}

void ProfileTickersUpdate(void)
{
    for (int i = 0; i < globalProfileRecords.num; i++)
    {
        ProfileRecord* record = globalProfileRecords.records[i];

        if (record && record->name)
        {
            globalProfileTickers.samples[i] = record->num;

            int bufferedSampleNum = record->num < PROFILE_RECORD_SAMPLE_MAX ? record->num : PROFILE_RECORD_SAMPLE_MAX;

            for (int j = 0; j < bufferedSampleNum; j++)
            {
                double time = (double)((
                    record->samples[j].end.QuadPart -
                    record->samples[j].start.QuadPart) * globalProfileTickers.unitScale) /
                        (double)globalProfileRecords.freq.QuadPart;

                globalProfileTickers.iterations[i]++;
                globalProfileTickers.averages[i] = globalProfileTickers.alpha * globalProfileTickers.averages[i] + (1.0 - globalProfileTickers.alpha) * time;
                globalProfileTickers.times[i] = globalProfileTickers.averages[i] / (1.0 - pow(globalProfileTickers.alpha, globalProfileTickers.iterations[i]));
            }

            record->idx = 0;
            record->num = 0;
        }
    }
}

#endif // ENABLE_PROFILE && _WIN32
