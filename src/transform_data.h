#ifndef TRANSFORM_DATA_H
#define TRANSFORM_DATA_H

#include <assert.h>
#include <stdlib.h>
#include "raylib.h"
#include "math_utils.h"

// Forward declarations
typedef struct BVHData BVHData;
typedef struct GLBData GLBData;

typedef struct TransformData {
    int jointCount;
    int* parents;
    bool* endSite;
    Vector3* localPositions;
    Quaternion* localRotations;
    Vector3* globalPositions;
    Quaternion* globalRotations;
} TransformData;

void TransformDataInit(TransformData* data);
void TransformDataResize(TransformData* data, BVHData* bvh);
void TransformDataResizeSimple(TransformData* data, int jointCount, int* parents, bool* endSite);
void TransformDataFree(TransformData* data);

// Sampling
void TransformDataSampleFrame(TransformData* data, BVHData* bvh, int frame, float scale);
void TransformDataSampleFrameNearest(TransformData* data, BVHData* bvh, float time, float scale);
void TransformDataSampleFrameLinear(TransformData* data, TransformData* tmp0, TransformData* tmp1, BVHData* bvh, float time, float scale);
void TransformDataSampleFrameCubic(TransformData* data, TransformData* tmp0, TransformData* tmp1, TransformData* tmp2, TransformData* tmp3, BVHData* bvh, float time, float scale);

// GLB sampling
void TransformDataSampleFrameGLB(TransformData* data, GLBData* glb, float time, float scale);

// Bind / rest pose sampling (GLB only; BVH has no separately-stored bind pose)
void TransformDataSampleRestPoseGLB(TransformData* data, GLBData* glb, float scale);

// Forward kinematics
void TransformDataForwardKinematics(TransformData* data);

// Utility
float TransformDataGetVerticalExtent(TransformData* data);

#endif // TRANSFORM_DATA_H
