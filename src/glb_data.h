/*******************************************************************************************
*
*    glb_data.h - GLB/GLTF data structures and functions
*
*******************************************************************************************/

#ifndef GLB_DATA_H
#define GLB_DATA_H

#include <stdbool.h>
#include "raylib.h"
#include "build/raylib/raylib/src/external/cgltf.h"

// Forward declaration
typedef struct TransformData TransformData;

// Per-material transparency info parsed from GLTF material
// alphaMode: 0 = OPAQUE, 1 = MASK (alpha cutoff), 2 = BLEND (screen-door)
typedef struct GLBMaterialInfo
{
    int alphaMode;
    float alphaCutoff;
    bool hasPBR;
    bool doubleSided;
    Vector4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
    Vector3 emissionFactor;
    int baseColorUV;
    int metallicRoughnessUV;
    int normalUV;
    int occlusionUV;
    int emissionUV;
} GLBMaterialInfo;

// Structure for storing GLB model and animation data
typedef struct GLBData
{
    Model model;
    ModelAnimation* animations;
    int animCount;
    int activeAnim;
    float frameTime;
    int* sourceFrameCounts;
    float* sourceFrameTimes;
    float* sourceDurations;
    cgltf_data* sourceData;
    cgltf_skin* sourceSkin;
    Transform* sourceRestPose;
    Transform* sourceLocalPose;
    Transform* sourceGlobalPose;
    Transform* sourceRootPose;
    int* topoOrder;
    int* invTopoOrder;
    GLBMaterialInfo* materialInfo;
    int materialInfoCount;
    float meshGroundOffset;     // vertical offset to place mesh bottom on ground (mesh-only GLBs)
} GLBData;

void GLBDataInit(GLBData* data);
void GLBDataFree(GLBData* data);
int GLBDataGetSourceFrameCount(const GLBData* data, int animIdx);
float GLBDataGetSourceFrameTime(const GLBData* data, int animIdx);
float GLBDataGetSourceDuration(const GLBData* data, int animIdx);
bool GLBDataLoad(GLBData* data, const char* filename, char* errMsg, int errMsgSize);
Matrix GLBDataGetModelTransform(const GLBData* glb, float scale, bool inplace);
void GLBDataUpdateModelPose(GLBData* glb, const Transform* globalPose);

// Topological ordering
void ComputeTopoOrder(int boneCount, BoneInfo* bones, int* topoOrder, int* invTopoOrder);

// GLB pose helpers (used by transform_data.c)
int GLBFindSkinJointIndex(const cgltf_skin* skin, const cgltf_node* node);
bool GLBGetPoseAtTime(cgltf_interpolation_type interpolationType, cgltf_accessor* input, cgltf_accessor* output, float time, void* data);
Matrix GLBMatrixFromCgltf(const cgltf_float* m);
Matrix GLBTransformToMatrix(Transform transform);
Transform GLBNodeLocalTransform(const cgltf_node* node);

#endif // GLB_DATA_H
