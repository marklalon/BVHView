/*******************************************************************************************
*
*    bvh_data.h - BVH file data structures and parsing functions
*
*******************************************************************************************/

#ifndef BVH_DATA_H
#define BVH_DATA_H

#include <stdbool.h>
#include "raylib.h"
#include "parser.h"

// Types of "channels" that are possible in the BVH format
enum
{
    CHANNEL_X_POSITION = 0,
    CHANNEL_Y_POSITION = 1,
    CHANNEL_Z_POSITION = 2,
    CHANNEL_X_ROTATION = 3,
    CHANNEL_Y_ROTATION = 4,
    CHANNEL_Z_ROTATION = 5,
    CHANNELS_MAX = 6,
};

// Data associated with a single "joint" in the BVH format
typedef struct BVHJointData
{
    int parent;
    char* name;
    Vector3 offset;
    int channelCount;
    char channels[CHANNELS_MAX];
    bool endSite;

} BVHJointData;

void BVHJointDataInit(BVHJointData* data);
void BVHJointDataRename(BVHJointData* data, const char* name);
void BVHJointDataFree(BVHJointData* data);

// Data structure matching what is present in the BVH file format
typedef struct BVHData
{
    // Hierarchy Data
    int jointCount;
    BVHJointData* joints;

    // Motion Data
    int frameCount;
    int channelCount;
    float frameTime;
    float* motionData;

} BVHData;

void BVHDataInit(BVHData* bvh);
void BVHDataFree(BVHData* bvh);
int BVHDataAddJoint(BVHData* bvh);

// BVH Parser
void BVHParseWhitespace(Parser* par);
bool BVHParseString(Parser* par, const char* string);
bool BVHParseNewline(Parser* par);
bool BVHParseJointName(BVHJointData* jnt, Parser* par);
bool BVHParseFloat(float* out, Parser* par);
bool BVHParseInt(int* out, Parser* par);
bool BVHParseJointOffset(BVHJointData* jnt, Parser* par);
bool BVHParseChannelEnum(char* channel, Parser* par, const char* channelName, char channelValue);
bool BVHParseChannel(char* channel, Parser* par);
bool BVHParseJointChannels(BVHJointData* jnt, Parser* par);
bool BVHParseJoints(BVHData* bvh, int parent, Parser* par);
bool BVHParseFrames(BVHData* bvh, Parser* par);
bool BVHParseFrameTime(BVHData* bvh, Parser* par);
bool BVHParseMotionData(BVHData* bvh, Parser* par);
bool BVHParse(BVHData* bvh, Parser* par);
bool BVHDataLoad(BVHData* bvh, const char* filename, char* errMsg, int errMsgSize);

#endif // BVH_DATA_H
