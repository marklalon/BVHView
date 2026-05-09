/*******************************************************************************************
*
*    capsule_data.h - Capsule data structures and functions
*
*******************************************************************************************/

#ifndef CAPSULE_DATA_H
#define CAPSULE_DATA_H

#include <stdbool.h>
#include "raylib.h"

// Forward declarations
typedef struct CharacterData CharacterData;
typedef struct TransformData TransformData;

typedef struct CapsuleSort
{
    int index;
    float value;
} CapsuleSort;

int CapsuleSortCompareGreater(const void* lhs, const void* rhs);
int CapsuleSortCompareLess(const void* lhs, const void* rhs);

typedef struct CapsuleData
{
    int capsuleCount;
    Vector3* capsulePositions;
    Quaternion* capsuleRotations;
    float* capsuleRadii;
    float* capsuleHalfLengths;
    Vector3* capsuleColors;
    float* capsuleOpacities;
    CapsuleSort* capsuleSort;

    int aoCapsuleCount;
    Vector3* aoCapsuleStarts;
    Vector3* aoCapsuleVectors;
    float* aoCapsuleRadii;
    CapsuleSort* aoCapsuleSort;

    int shadowCapsuleCount;
    Vector3* shadowCapsuleStarts;
    Vector3* shadowCapsuleVectors;
    float* shadowCapsuleRadii;
    CapsuleSort* shadowCapsuleSort;

    Image aoLookupImage;
    Texture2D aoLookupTable;
    Vector2 aoLookupResolution;

    Image shadowLookupImage;
    Texture2D shadowLookupTable;
    Vector2 shadowLookupResolution;

} CapsuleData;

void CapsuleDataInit(CapsuleData* data);
void CapsuleDataResize(CapsuleData* data, int maxCapsuleCount);
void CapsuleDataFree(CapsuleData* data);
void CapsuleDataReset(CapsuleData* data);
void CapsuleDataAppendFromTransformData(CapsuleData* data, TransformData* xforms, float maxCapsuleRadius, Color color, float opacity, bool ignoreEndSite);
void CapsuleDataUpdateAOCapsulesForGroundSegment(CapsuleData* data, Vector3 groundSegmentPosition);
void CapsuleDataUpdateAOCapsulesForCapsule(CapsuleData* data, int capsuleIndex);
void CapsuleDataUpdateShadowCapsulesForGroundSegment(CapsuleData* data, Vector3 groundSegmentPosition, Vector3 lightDir, float lightConeAngle);
void CapsuleDataUpdateShadowCapsulesForCapsule(CapsuleData* data, int capsuleIndex, Vector3 lightDir, float lightConeAngle);
void CapsuleDataUpdateForCharacters(CapsuleData* capsuleData, CharacterData* characterData);
void CapsuleDataUpdateAOLookupTable(CapsuleData* data);
void CapsuleDataUpdateShadowLookupTable(CapsuleData* data, float coneAngle);

#endif // CAPSULE_DATA_H
