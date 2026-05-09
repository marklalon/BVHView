/*******************************************************************************************
*
*    bvh_data.c - BVH parsing implementation
*
*******************************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <assert.h>
#include "raylib.h"
#include "raymath.h"
#include "bvh_data.h"

void BVHJointDataInit(BVHJointData* data)
{
    data->parent = -1;
    data->name = NULL;
    data->offset = (Vector3){ 0.0f, 0.0f, 0.0f };
    data->channelCount = 0;
    data->endSite = false;
}

void BVHJointDataRename(BVHJointData* data, const char* name)
{
    data->name = realloc(data->name, strlen(name) + 1);
    strcpy(data->name, name);
}

void BVHJointDataFree(BVHJointData* data)
{
    free(data->name);
    data->name = NULL;
}

void BVHDataInit(BVHData* bvh)
{
    bvh->jointCount = 0;
    bvh->joints = NULL;
    bvh->frameCount = 0;
    bvh->channelCount = 0;
    bvh->frameTime = 0.0f;
    bvh->motionData = NULL;
}

void BVHDataFree(BVHData* bvh)
{
    for (int i = 0; i < bvh->jointCount; i++)
    {
        BVHJointDataFree(&bvh->joints[i]);
    }
    free(bvh->joints);
    free(bvh->motionData);
    bvh->jointCount = 0;
    bvh->joints = NULL;
    bvh->frameCount = 0;
    bvh->channelCount = 0;
    bvh->frameTime = 0.0f;
    bvh->motionData = NULL;
}

int BVHDataAddJoint(BVHData* bvh)
{
    bvh->joints = (BVHJointData*)realloc(bvh->joints, (bvh->jointCount + 1) * sizeof(BVHJointData));
    bvh->jointCount++;
    BVHJointDataInit(&bvh->joints[bvh->jointCount - 1]);
    return bvh->jointCount - 1;
}

// Parse any whitespace
void BVHParseWhitespace(Parser* par)
{
    while (ParserOneOf(par, " \r\t\v")) { ParserInc(par); }
}

// Parse the given string (in a non-case sensitive way)
bool BVHParseString(Parser* par, const char* string)
{
    if (ParserStartsWithCaseless(par, string))
    {
        ParserAdvance(par, strlen(string));
        return true;
    }
    else
    {
        ParserError(par, "expected '%s' at '%s'", string, ParserCharName(ParserPeek(par)));
        return false;
    }
}

// Parse any whitespace followed by a newline
bool BVHParseNewline(Parser* par)
{
    BVHParseWhitespace(par);
    if (ParserMatch(par, '\n'))
    {
        ParserInc(par);
        BVHParseWhitespace(par);
        return true;
    }
    else
    {
        ParserError(par, "expected newline at '%s'", ParserCharName(ParserPeek(par)));
        return false;
    }
}

// Parse any whitespace and then an identifier for the name of a joint
bool BVHParseJointName(BVHJointData* jnt, Parser* par)
{
    BVHParseWhitespace(par);
    char buffer[256];
    int chrnum = 0;
    while (chrnum < 255 && ParserOneOf(par,
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_:-."))
    {
        buffer[chrnum] = ParserPeek(par);
        chrnum++;
        ParserInc(par);
    }
    buffer[chrnum] = '\0';

    if (chrnum > 0)
    {
        BVHJointDataRename(jnt, buffer);
        BVHParseWhitespace(par);
        return true;
    }
    else
    {
        ParserError(par, "expected joint name at '%s'", ParserCharName(ParserPeek(par)));
        return false;
    }
}

// Parse a float value
bool BVHParseFloat(float* out, Parser* par)
{
    BVHParseWhitespace(par);
    char* end;
    errno = 0;
    (*out) = strtod(par->data + par->offset, &end);
    if (errno == 0)
    {
        ParserAdvance(par, end - (par->data + par->offset));
        return true;
    }
    else
    {
        ParserError(par, "expected float at '%s'", ParserCharName(ParserPeek(par)));
        return false;
    }
}

// Parse an integer value
bool BVHParseInt(int* out, Parser* par)
{
    BVHParseWhitespace(par);
    char* end;
    errno = 0;
    (*out) = (int)strtol(par->data + par->offset, &end, 10);
    if (errno == 0)
    {
        ParserAdvance(par, end - (par->data + par->offset));
        return true;
    }
    else
    {
        ParserError(par, "expected integer at '%s'", ParserCharName(ParserPeek(par)));
        return false;
    }
}

// Parse the "joint offset" part of the BVH File
bool BVHParseJointOffset(BVHJointData* jnt, Parser* par)
{
    if (!BVHParseString(par, "OFFSET")) { return false; }
    if (!BVHParseFloat(&jnt->offset.x, par)) { return false; }
    if (!BVHParseFloat(&jnt->offset.y, par)) { return false; }
    if (!BVHParseFloat(&jnt->offset.z, par)) { return false; }
    if (!BVHParseNewline(par)) { return false; }
    return true;
}

// Parse a channel type
bool BVHParseChannelEnum(char* channel, Parser* par, const char* channelName, char channelValue)
{
    BVHParseWhitespace(par);
    if (!BVHParseString(par, channelName)) { return false; }
    BVHParseWhitespace(par);
    *channel = channelValue;
    return true;
}

bool BVHParseChannel(char* channel, Parser* par)
{
    BVHParseWhitespace(par);
    if (ParserPeek(par) == '\0')
    {
        ParserError(par, "expected channel at end of file");
        return false;
    }
    if (ParserPeek(par) == 'X' && ParserPeekForward(par, 1) == 'p')
        return BVHParseChannelEnum(channel, par, "Xposition", CHANNEL_X_POSITION);
    if (ParserPeek(par) == 'Y' && ParserPeekForward(par, 1) == 'p')
        return BVHParseChannelEnum(channel, par, "Yposition", CHANNEL_Y_POSITION);
    if (ParserPeek(par) == 'Z' && ParserPeekForward(par, 1) == 'p')
        return BVHParseChannelEnum(channel, par, "Zposition", CHANNEL_Z_POSITION);
    if (ParserPeek(par) == 'X' && ParserPeekForward(par, 1) == 'r')
        return BVHParseChannelEnum(channel, par, "Xrotation", CHANNEL_X_ROTATION);
    if (ParserPeek(par) == 'Y' && ParserPeekForward(par, 1) == 'r')
        return BVHParseChannelEnum(channel, par, "Yrotation", CHANNEL_Y_ROTATION);
    if (ParserPeek(par) == 'Z' && ParserPeekForward(par, 1) == 'r')
        return BVHParseChannelEnum(channel, par, "Zrotation", CHANNEL_Z_ROTATION);
    ParserError(par, "expected channel type");
    return false;
}

// Parse the "channels" part
bool BVHParseJointChannels(BVHJointData* jnt, Parser* par)
{
    if (!BVHParseString(par, "CHANNELS")) { return false; }
    if (!BVHParseInt(&jnt->channelCount, par)) { return false; }
    for (int i = 0; i < jnt->channelCount; i++)
    {
        if (!BVHParseChannel(&jnt->channels[i], par)) { return false; }
    }
    if (!BVHParseNewline(par)) { return false; }
    return true;
}

// Parse a joint
bool BVHParseJoints(BVHData* bvh, int parent, Parser* par)
{
    while (ParserOneOf(par, "JEje"))
    {
        int j = BVHDataAddJoint(bvh);
        bvh->joints[j].parent = parent;
        if (ParserMatch(par, 'J'))
        {
            if (!BVHParseString(par, "JOINT")) { return false; }
            if (!BVHParseJointName(&bvh->joints[j], par)) { return false; }
            if (!BVHParseNewline(par)) { return false; }
            if (!BVHParseString(par, "{")) { return false; }
            if (!BVHParseNewline(par)) { return false; }
            if (!BVHParseJointOffset(&bvh->joints[j], par)) { return false; }
            if (!BVHParseJointChannels(&bvh->joints[j], par)) { return false; }
            if (!BVHParseJoints(bvh, j, par)) { return false; }
            if (!BVHParseString(par, "}")) { return false; }
            if (!BVHParseNewline(par)) { return false; }
        }
        else if (ParserMatch(par, 'E'))
        {
            bvh->joints[j].endSite = true;
            if (!BVHParseString(par, "End Site")) { return false; }
            BVHJointDataRename(&bvh->joints[j], "End Site");
            if (!BVHParseNewline(par)) { return false; }
            if (!BVHParseString(par, "{")) { return false; }
            if (!BVHParseNewline(par)) { return false; }
            if (!BVHParseJointOffset(&bvh->joints[j], par)) { return false; }
            if (!BVHParseString(par, "}")) { return false; }
            if (!BVHParseNewline(par)) { return false; }
        }
    }
    return true;
}

// Parse frame count
bool BVHParseFrames(BVHData* bvh, Parser* par)
{
    if (!BVHParseString(par, "Frames:")) { return false; }
    if (!BVHParseInt(&bvh->frameCount, par)) { return false; }
    if (!BVHParseNewline(par)) { return false; }
    return true;
}

// Parse frame time
bool BVHParseFrameTime(BVHData* bvh, Parser* par)
{
    if (!BVHParseString(par, "Frame Time:")) { return false; }
    if (!BVHParseFloat(&bvh->frameTime, par)) { return false; }
    if (!BVHParseNewline(par)) { return false; }
    if (bvh->frameTime == 0.0f) { bvh->frameTime = 1.0f / 60.0f; }
    return true;
}

// Parse motion data
bool BVHParseMotionData(BVHData* bvh, Parser* par)
{
    int channelCount = 0;
    for (int i = 0; i < bvh->jointCount; i++)
    {
        channelCount += bvh->joints[i].channelCount;
    }
    bvh->channelCount = channelCount;
    bvh->motionData = malloc(bvh->frameCount * channelCount * sizeof(float));
    for (int i = 0; i < bvh->frameCount; i++)
    {
        for (int j = 0; j < channelCount; j++)
        {
            if (!BVHParseFloat(&bvh->motionData[i * channelCount + j], par)) { return false; }
        }
        if (!BVHParseNewline(par)) { return false; }
    }
    return true;
}

// Parse entire BVH file
bool BVHParse(BVHData* bvh, Parser* par)
{
    if (!BVHParseString(par, "HIERARCHY")) { return false; }
    if (!BVHParseNewline(par)) { return false; }
    int j = BVHDataAddJoint(bvh);
    if (!BVHParseString(par, "ROOT")) { return false; }
    if (!BVHParseJointName(&bvh->joints[j], par)) { return false; }
    if (!BVHParseNewline(par)) { return false; }
    if (!BVHParseString(par, "{")) { return false; }
    if (!BVHParseNewline(par)) { return false; }
    if (!BVHParseJointOffset(&bvh->joints[j], par)) { return false; }
    if (!BVHParseJointChannels(&bvh->joints[j], par)) { return false; }
    if (!BVHParseJoints(bvh, j, par)) { return false; }
    if (!BVHParseString(par, "}")) { return false; }
    if (!BVHParseNewline(par)) { return false; }
    if (!BVHParseString(par, "MOTION")) { return false; }
    if (!BVHParseNewline(par)) { return false; }
    if (!BVHParseFrames(bvh, par)) { return false; }
    if (!BVHParseFrameTime(bvh, par)) { return false; }
    if (!BVHParseMotionData(bvh, par)) { return false; }
    return true;
}

// Load file and parse as BVH
bool BVHDataLoad(BVHData* bvh, const char* filename, char* errMsg, int errMsgSize)
{
    FILE* f = fopen(filename, "rb");
    if (f == NULL)
    {
        snprintf(errMsg, errMsgSize, "Error: Could not find file '%s'\n", filename);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long int length = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buffer = malloc(length + 1);
    fread(buffer, 1, length, f);
    buffer[length] = '\n';
    fclose(f);

    BVHDataFree(bvh);
    BVHDataInit(bvh);

    Parser par;
    ParserInit(&par, filename, buffer);
    bool result = BVHParse(bvh, &par);

    free(buffer);

    if (!result)
    {
        snprintf(errMsg, errMsgSize, "Error: Could not parse BVH file:\n    %s", par.err);
    }
    else
    {
        errMsg[0] = '\0';
        printf("INFO: parsed '%s' successfully\n", filename);
    }
    return result;
}
