/*******************************************************************************************
*
*    capsule_data.c - Capsule data implementation
*
*******************************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "raylib.h"
#include "raymath.h"
#include "capsule_data.h"
#include "geometry.h"
#include "math_utils.h"
#include "transform_data.h"
#include "character_data.h"

int CapsuleSortCompareGreater(const void* lhs, const void* rhs)
{
    const CapsuleSort* lhsSort = lhs;
    const CapsuleSort* rhsSort = rhs;
    return lhsSort->value > rhsSort->value ? 1 : -1;
}

int CapsuleSortCompareLess(const void* lhs, const void* rhs)
{
    const CapsuleSort* lhsSort = lhs;
    const CapsuleSort* rhsSort = rhs;
    return lhsSort->value < rhsSort->value ? 1 : -1;
}

void CapsuleDataUpdateAOLookupTable(CapsuleData* data)
{
    int width = (int)data->aoLookupResolution.x;
    int height = (int)data->aoLookupResolution.y;
    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            float nlAngle = (((float)x) / (width - 1)) * PI;
            float h = 1.0f + (AO_RATIO_MAX - 1.0f) * (((float)y) / (height - 1));
            ((unsigned char*)data->aoLookupImage.data)[y * width + x] = (unsigned char)Clamp(255.0 * SphereOcclusionLookup(nlAngle, h), 0.0, 255.0);
        }
    }
    UpdateTexture(data->aoLookupTable, data->aoLookupImage.data);
}

void CapsuleDataUpdateShadowLookupTable(CapsuleData* data, float coneAngle)
{
    int width = (int)data->shadowLookupResolution.x;
    int height = (int)data->shadowLookupResolution.y;
    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            float phi = (((float)x) / (width - 1)) * PI;
            float theta = ((float)y) / (height - 1) * (PI / 2.0f);
            ((unsigned char*)data->shadowLookupImage.data)[y * width + x] = (unsigned char)Clamp(255.0 * SphereDirectionalOcclusionLookup(phi, theta, coneAngle), 0.0, 255.0);
        }
    }
    UpdateTexture(data->shadowLookupTable, data->shadowLookupImage.data);
}

void CapsuleDataInit(CapsuleData* data)
{
    data->capsuleCount = 0;
    data->capsulePositions = NULL;
    data->capsuleRotations = NULL;
    data->capsuleRadii = NULL;
    data->capsuleHalfLengths = NULL;
    data->capsuleColors = NULL;
    data->capsuleOpacities = NULL;
    data->capsuleSort = NULL;
    data->aoCapsuleCount = 0;
    data->aoCapsuleStarts = NULL;
    data->aoCapsuleVectors = NULL;
    data->aoCapsuleRadii = NULL;
    data->aoCapsuleSort = NULL;
    data->shadowCapsuleCount = 0;
    data->shadowCapsuleStarts = NULL;
    data->shadowCapsuleVectors = NULL;
    data->shadowCapsuleRadii = NULL;
    data->shadowCapsuleSort = NULL;
    data->aoLookupImage = (Image){ .data = calloc(32 * 32, 1), .width = 32, .height = 32, .format = PIXELFORMAT_UNCOMPRESSED_GRAYSCALE, .mipmaps = 1 };
    data->aoLookupTable = LoadTextureFromImage(data->aoLookupImage);
    data->aoLookupResolution = (Vector2){ (float)data->aoLookupImage.width, (float)data->aoLookupImage.height };
    SetTextureWrap(data->aoLookupTable, TEXTURE_WRAP_CLAMP);
    SetTextureFilter(data->aoLookupTable, TEXTURE_FILTER_BILINEAR);
    CapsuleDataUpdateAOLookupTable(data);
    data->shadowLookupImage = (Image){ .data = calloc(256 * 128, 1), .width = 256, .height = 128, .format = PIXELFORMAT_UNCOMPRESSED_GRAYSCALE, .mipmaps = 1 };
    data->shadowLookupTable = LoadTextureFromImage(data->shadowLookupImage);
    data->shadowLookupResolution = (Vector2){ (float)data->shadowLookupImage.width, (float)data->shadowLookupImage.height };
    SetTextureWrap(data->shadowLookupTable, TEXTURE_WRAP_CLAMP);
    SetTextureFilter(data->shadowLookupTable, TEXTURE_FILTER_BILINEAR);
    CapsuleDataUpdateShadowLookupTable(data, 0.2f);
}

void CapsuleDataResize(CapsuleData* data, int maxCapsuleCount)
{
    data->capsulePositions = realloc(data->capsulePositions, maxCapsuleCount * sizeof(Vector3));
    data->capsuleRotations = realloc(data->capsuleRotations, maxCapsuleCount * sizeof(Quaternion));
    data->capsuleRadii = realloc(data->capsuleRadii, maxCapsuleCount * sizeof(float));
    data->capsuleHalfLengths = realloc(data->capsuleHalfLengths, maxCapsuleCount * sizeof(float));
    data->capsuleColors = realloc(data->capsuleColors, maxCapsuleCount * sizeof(Vector3));
    data->capsuleOpacities = realloc(data->capsuleOpacities, maxCapsuleCount * sizeof(float));
    data->capsuleSort = realloc(data->capsuleSort, maxCapsuleCount * sizeof(CapsuleSort));
    data->aoCapsuleStarts = realloc(data->aoCapsuleStarts, maxCapsuleCount * sizeof(Vector3));
    data->aoCapsuleVectors = realloc(data->aoCapsuleVectors, maxCapsuleCount * sizeof(Vector3));
    data->aoCapsuleRadii = realloc(data->aoCapsuleRadii, maxCapsuleCount * sizeof(float));
    data->aoCapsuleSort = realloc(data->aoCapsuleSort, maxCapsuleCount * sizeof(CapsuleSort));
    data->shadowCapsuleStarts = realloc(data->shadowCapsuleStarts, maxCapsuleCount * sizeof(Vector3));
    data->shadowCapsuleVectors = realloc(data->shadowCapsuleVectors, maxCapsuleCount * sizeof(Vector3));
    data->shadowCapsuleRadii = realloc(data->shadowCapsuleRadii, maxCapsuleCount * sizeof(float));
    data->shadowCapsuleSort = realloc(data->shadowCapsuleSort, maxCapsuleCount * sizeof(CapsuleSort));
}

void CapsuleDataFree(CapsuleData* data)
{
    free(data->capsulePositions); free(data->capsuleRotations); free(data->capsuleRadii);
    free(data->capsuleHalfLengths); free(data->capsuleColors); free(data->capsuleOpacities); free(data->capsuleSort);
    free(data->aoCapsuleStarts); free(data->aoCapsuleVectors); free(data->aoCapsuleRadii); free(data->aoCapsuleSort);
    free(data->shadowCapsuleStarts); free(data->shadowCapsuleVectors); free(data->shadowCapsuleRadii); free(data->shadowCapsuleSort);
    UnloadImage(data->aoLookupImage); UnloadTexture(data->aoLookupTable);
    UnloadImage(data->shadowLookupImage); UnloadTexture(data->shadowLookupTable);
}

void CapsuleDataReset(CapsuleData* data)
{
    data->capsuleCount = 0;
    data->aoCapsuleCount = 0;
    data->shadowCapsuleCount = 0;
}

void CapsuleDataAppendFromTransformData(CapsuleData* data, TransformData* xforms, float maxCapsuleRadius, Color color, float opacity, bool ignoreEndSite)
{
    for (int i = 0; i < xforms->jointCount; i++)
    {
        int p = xforms->parents[i];
        if (p == -1) continue;
        if (ignoreEndSite && xforms->endSite[i]) continue;
        float capsuleHalfLength = Vector3Length(xforms->localPositions[i]) / 2.0f;
        float capsuleRadius = Min(maxCapsuleRadius, capsuleHalfLength) + (i % 2) * 0.001f;
        if (capsuleRadius < 0.001f) continue;
        Vector3 capsulePosition = Vector3Scale(Vector3Add(xforms->globalPositions[i], xforms->globalPositions[p]), 0.5f);
        Quaternion capsuleRotation = QuaternionMultiply(xforms->globalRotations[p], QuaternionBetween((Vector3){ 1.0f, 0.0f, 0.0f }, Vector3Normalize(xforms->localPositions[i])));
        data->capsulePositions[data->capsuleCount] = capsulePosition;
        data->capsuleRotations[data->capsuleCount] = capsuleRotation;
        data->capsuleHalfLengths[data->capsuleCount] = capsuleHalfLength;
        data->capsuleRadii[data->capsuleCount] = capsuleRadius;
        data->capsuleColors[data->capsuleCount] = (Vector3){ color.r / 255.0f, color.g / 255.0f, color.b / 255.0f };
        data->capsuleOpacities[data->capsuleCount] = opacity;
        data->capsuleCount++;
    }
}

void CapsuleDataUpdateAOCapsulesForGroundSegment(CapsuleData* data, Vector3 groundSegmentPosition)
{
    data->aoCapsuleCount = 0;
    for (int i = 0; i < data->capsuleCount; i++)
    {
        Vector3 capsulePosition = data->capsulePositions[i];
        float capsuleHalfLength = data->capsuleHalfLengths[i];
        float capsuleRadius = data->capsuleRadii[i];
        if (Vector3Distance(groundSegmentPosition, capsulePosition) - sqrtf(2.0f) > capsuleHalfLength + AO_RATIO_MAX * capsuleRadius) continue;
        Quaternion capsuleRotation = data->capsuleRotations[i];
        Vector3 capsuleStart = CapsuleStart(capsulePosition, capsuleRotation, capsuleHalfLength);
        Vector3 capsuleEnd = CapsuleEnd(capsulePosition, capsuleRotation, capsuleHalfLength);
        Vector3 capsuleVector = CapsuleVector(capsulePosition, capsuleRotation, capsuleHalfLength);
        float capsuleTime;
        Vector3 groundPoint;
        NearestPointBetweenLineSegmentAndGroundSegment(&capsuleTime, &groundPoint, capsuleStart, capsuleEnd,
            (Vector3){ groundSegmentPosition.x - 1.0f, 0.0f, groundSegmentPosition.z - 1.0f },
            (Vector3){ groundSegmentPosition.x + 1.0f, 0.0f, groundSegmentPosition.z + 1.0f });
        Vector3 capsulePoint = Vector3Add(capsuleStart, Vector3Scale(capsuleVector, capsuleTime));
        if (Vector3Distance(groundPoint, capsulePoint) > AO_RATIO_MAX * capsuleRadius) continue;
        float capsuleOcclusion = Vector3Distance(groundPoint, capsulePoint) < capsuleRadius ? 0.0f :
            SphereOcclusion(groundPoint, (Vector3){ 0.0f, 1.0f, 0.0f }, capsulePoint, capsuleRadius);
        if (capsuleOcclusion < 0.99f) { data->aoCapsuleSort[data->aoCapsuleCount] = (CapsuleSort){ i, capsuleOcclusion }; data->aoCapsuleCount++; }
    }
    qsort(data->aoCapsuleSort, data->aoCapsuleCount, sizeof(CapsuleSort), CapsuleSortCompareGreater);
    for (int i = 0; i < data->aoCapsuleCount; i++)
    {
        int j = data->aoCapsuleSort[i].index;
        data->aoCapsuleStarts[i] = CapsuleStart(data->capsulePositions[j], data->capsuleRotations[j], data->capsuleHalfLengths[j]);
        data->aoCapsuleVectors[i] = CapsuleVector(data->capsulePositions[j], data->capsuleRotations[j], data->capsuleHalfLengths[j]);
        data->aoCapsuleRadii[i] = data->capsuleRadii[j];
    }
}

void CapsuleDataUpdateAOCapsulesForCapsule(CapsuleData* data, int capsuleIndex)
{
    Vector3 queryCapsulePosition = data->capsulePositions[capsuleIndex];
    float queryCapsuleHalfLength = data->capsuleHalfLengths[capsuleIndex];
    float queryCapsuleRadius = data->capsuleRadii[capsuleIndex];
    Quaternion queryCapsuleRotation = data->capsuleRotations[capsuleIndex];
    Vector3 queryCapsuleStart = CapsuleStart(queryCapsulePosition, queryCapsuleRotation, queryCapsuleHalfLength);
    Vector3 queryCapsuleEnd = CapsuleEnd(queryCapsulePosition, queryCapsuleRotation, queryCapsuleHalfLength);
    Vector3 queryCapsuleVector = CapsuleVector(queryCapsulePosition, queryCapsuleRotation, queryCapsuleHalfLength);
    data->aoCapsuleCount = 0;
    for (int i = 0; i < data->capsuleCount; i++)
    {
        if (i == capsuleIndex) continue;
        Vector3 capsulePosition = data->capsulePositions[i];
        float capsuleRadius = data->capsuleRadii[i];
        float capsuleHalfLength = data->capsuleHalfLengths[i];
        if (Vector3Distance(queryCapsulePosition, capsulePosition) - queryCapsuleHalfLength - queryCapsuleRadius > capsuleHalfLength + AO_RATIO_MAX * capsuleRadius) continue;
        Quaternion capsuleRotation = data->capsuleRotations[i];
        Vector3 capsuleStart = CapsuleStart(capsulePosition, capsuleRotation, capsuleHalfLength);
        Vector3 capsuleEnd = CapsuleEnd(capsulePosition, capsuleRotation, capsuleHalfLength);
        Vector3 capsuleVector = CapsuleVector(capsulePosition, capsuleRotation, capsuleHalfLength);
        float capsuleTime, queryTime;
        NearestPointBetweenLineSegments(&capsuleTime, &queryTime, capsuleStart, capsuleEnd, queryCapsuleStart, queryCapsuleEnd);
        Vector3 capsulePoint = Vector3Add(capsuleStart, Vector3Scale(capsuleVector, capsuleTime));
        Vector3 queryPoint = Vector3Add(queryCapsuleStart, Vector3Scale(queryCapsuleVector, queryTime));
        if (Vector3Distance(queryPoint, capsulePoint) - queryCapsuleRadius > AO_RATIO_MAX * capsuleRadius) continue;
        Vector3 surfaceNormal = Vector3Normalize(Vector3Subtract(capsulePoint, queryPoint));
        Vector3 surfacePoint = Vector3Add(queryPoint, Vector3Scale(surfaceNormal, queryCapsuleRadius));
        float capsuleOcclusion = Vector3Distance(queryPoint, capsulePoint) <= queryCapsuleRadius + capsuleRadius ? 0.0f :
            SphereOcclusion(surfacePoint, surfaceNormal, capsulePoint, capsuleRadius);
        if (capsuleOcclusion < 0.99f) { data->aoCapsuleSort[data->aoCapsuleCount] = (CapsuleSort){ i, capsuleOcclusion }; data->aoCapsuleCount++; }
    }
    qsort(data->aoCapsuleSort, data->aoCapsuleCount, sizeof(CapsuleSort), CapsuleSortCompareGreater);
    for (int i = 0; i < data->aoCapsuleCount; i++)
    {
        int j = data->aoCapsuleSort[i].index;
        data->aoCapsuleStarts[i] = CapsuleStart(data->capsulePositions[j], data->capsuleRotations[j], data->capsuleHalfLengths[j]);
        data->aoCapsuleVectors[i] = CapsuleVector(data->capsulePositions[j], data->capsuleRotations[j], data->capsuleHalfLengths[j]);
        data->aoCapsuleRadii[i] = data->capsuleRadii[j];
    }
}

void CapsuleDataUpdateShadowCapsulesForGroundSegment(CapsuleData* data, Vector3 groundSegmentPosition, Vector3 lightDir, float lightConeAngle)
{
    Vector3 lightRay = Vector3Scale(lightDir, 10.0f);
    data->shadowCapsuleCount = 0;
    for (int i = 0; i < data->capsuleCount; i++)
    {
        Vector3 capsulePosition = data->capsulePositions[i];
        float capsuleHalfLength = data->capsuleHalfLengths[i];
        float capsuleRadius = data->capsuleRadii[i];
        float midRayTime = NearestPointBetweenLineSegmentAndGroundPlane(capsulePosition, lightRay);
        Vector3 groundCapsuleMid = Vector3Add(capsulePosition, Vector3Scale(lightRay, midRayTime));
        float maxRatio = 4.0f;
        if (Vector3Distance(groundSegmentPosition, groundCapsuleMid) - sqrtf(2.0f) > capsuleHalfLength + maxRatio * capsuleRadius) continue;
        Quaternion capsuleRotation = data->capsuleRotations[i];
        Vector3 capsuleStart = CapsuleStart(capsulePosition, capsuleRotation, capsuleHalfLength);
        Vector3 capsuleEnd = CapsuleEnd(capsulePosition, capsuleRotation, capsuleHalfLength);
        Vector3 capsuleVector = CapsuleVector(capsulePosition, capsuleRotation, capsuleHalfLength);
        float startRayTime = NearestPointBetweenLineSegmentAndGroundPlane(capsuleStart, lightRay);
        float endRayTime = NearestPointBetweenLineSegmentAndGroundPlane(capsuleEnd, lightRay);
        Vector3 groundCapsuleStart = Vector3Add(capsuleStart, Vector3Scale(lightRay, startRayTime));
        Vector3 groundCapsuleEnd = Vector3Add(capsuleEnd, Vector3Scale(lightRay, endRayTime));
        groundCapsuleStart.x = Clamp(groundCapsuleStart.x, groundSegmentPosition.x - 1.0f, groundSegmentPosition.x + 1.0f);
        groundCapsuleStart.z = Clamp(groundCapsuleStart.z, groundSegmentPosition.z - 1.0f, groundSegmentPosition.z + 1.0f);
        groundCapsuleEnd.x = Clamp(groundCapsuleEnd.x, groundSegmentPosition.x - 1.0f, groundSegmentPosition.x + 1.0f);
        groundCapsuleEnd.z = Clamp(groundCapsuleEnd.z, groundSegmentPosition.z - 1.0f, groundSegmentPosition.z + 1.0f);
        if (Vector3Distance(groundSegmentPosition, groundCapsuleStart) - sqrtf(2.0f) > maxRatio * capsuleRadius &&
            Vector3Distance(groundSegmentPosition, groundCapsuleEnd) - sqrtf(2.0f) > maxRatio * capsuleRadius) continue;
        float capsuleOcclusion = Min(
            CapsuleDirectionalOcclusion(groundCapsuleStart, capsuleStart, capsuleVector, capsuleRadius, lightDir, lightConeAngle),
            CapsuleDirectionalOcclusion(groundCapsuleEnd, capsuleStart, capsuleVector, capsuleRadius, lightDir, lightConeAngle));
        if (capsuleOcclusion < 0.99f) { data->shadowCapsuleSort[data->shadowCapsuleCount] = (CapsuleSort){ i, capsuleOcclusion }; data->shadowCapsuleCount++; }
    }
    qsort(data->shadowCapsuleSort, data->shadowCapsuleCount, sizeof(CapsuleSort), CapsuleSortCompareGreater);
    for (int i = 0; i < data->shadowCapsuleCount; i++)
    {
        int j = data->shadowCapsuleSort[i].index;
        data->shadowCapsuleStarts[i] = CapsuleStart(data->capsulePositions[j], data->capsuleRotations[j], data->capsuleHalfLengths[j]);
        data->shadowCapsuleVectors[i] = CapsuleVector(data->capsulePositions[j], data->capsuleRotations[j], data->capsuleHalfLengths[j]);
        data->shadowCapsuleRadii[i] = data->capsuleRadii[j];
    }
}

void CapsuleDataUpdateShadowCapsulesForCapsule(CapsuleData* data, int capsuleIndex, Vector3 lightDir, float lightConeAngle)
{
    Vector3 lightRay = Vector3Scale(lightDir, 10.0f);
    Vector3 queryCapsulePosition = data->capsulePositions[capsuleIndex];
    float queryCapsuleHalfLength = data->capsuleHalfLengths[capsuleIndex];
    float queryCapsuleRadius = data->capsuleRadii[capsuleIndex];
    Quaternion queryCapsuleRotation = data->capsuleRotations[capsuleIndex];
    Vector3 queryCapsuleStart = CapsuleStart(queryCapsulePosition, queryCapsuleRotation, queryCapsuleHalfLength);
    Vector3 queryCapsuleEnd = CapsuleEnd(queryCapsulePosition, queryCapsuleRotation, queryCapsuleHalfLength);
    Vector3 queryCapsuleVector = CapsuleVector(queryCapsulePosition, queryCapsuleRotation, queryCapsuleHalfLength);
    data->shadowCapsuleCount = 0;
    for (int i = 0; i < data->capsuleCount; i++)
    {
        if (i == capsuleIndex) continue;
        Vector3 capsulePosition = data->capsulePositions[i];
        float capsuleHalfLength = data->capsuleHalfLengths[i];
        float capsuleRadius = data->capsuleRadii[i];
        float midRayTime = NearestPointOnLineSegment(capsulePosition, lightRay, queryCapsulePosition);
        Vector3 capsuleMid = Vector3Add(capsulePosition, Vector3Scale(lightRay, midRayTime));
        float maxRatio = 4.0f;
        if (Vector3Distance(queryCapsulePosition, capsuleMid) - queryCapsuleHalfLength - queryCapsuleRadius > capsuleHalfLength + maxRatio * capsuleRadius) continue;
        Quaternion capsuleRotation = data->capsuleRotations[i];
        Vector3 capsuleStart = CapsuleStart(capsulePosition, capsuleRotation, capsuleHalfLength);
        Vector3 capsuleEnd = CapsuleEnd(capsulePosition, capsuleRotation, capsuleHalfLength);
        Vector3 capsuleVector = CapsuleVector(capsulePosition, capsuleRotation, capsuleHalfLength);
        float queryCapsuleTime;
        Vector3 nearestRayPoint;
        NearestPointBetweenLineSegmentAndSweptLine(&queryCapsuleTime, &nearestRayPoint, queryCapsuleStart, queryCapsuleEnd, capsuleStart, capsuleEnd, lightRay);
        Vector3 queryCapsulePoint = Vector3Add(queryCapsuleStart, Vector3Scale(queryCapsuleVector, queryCapsuleTime));
        if (Vector3Distance(queryCapsulePoint, nearestRayPoint) - queryCapsuleRadius > capsuleHalfLength + maxRatio * capsuleRadius) continue;
        Vector3 surfaceNormal = Vector3Normalize(Vector3Subtract(nearestRayPoint, queryCapsulePoint));
        Vector3 surfacePoint = Vector3Add(queryCapsulePoint, Vector3Scale(surfaceNormal, queryCapsuleRadius));
        float capsuleOcclusion = Vector3Distance(queryCapsulePoint, nearestRayPoint) <= queryCapsuleRadius + capsuleRadius ? 0.0f :
            CapsuleDirectionalOcclusion(surfacePoint, capsuleStart, capsuleVector, capsuleRadius, lightDir, lightConeAngle);
        if (capsuleOcclusion < 0.99f) { data->shadowCapsuleSort[data->shadowCapsuleCount] = (CapsuleSort){ i, capsuleOcclusion }; data->shadowCapsuleCount++; }
    }
    qsort(data->shadowCapsuleSort, data->shadowCapsuleCount, sizeof(CapsuleSort), CapsuleSortCompareGreater);
    for (int i = 0; i < data->shadowCapsuleCount; i++)
    {
        int j = data->shadowCapsuleSort[i].index;
        data->shadowCapsuleStarts[i] = CapsuleStart(data->capsulePositions[j], data->capsuleRotations[j], data->capsuleHalfLengths[j]);
        data->shadowCapsuleVectors[i] = CapsuleVector(data->capsulePositions[j], data->capsuleRotations[j], data->capsuleHalfLengths[j]);
        data->shadowCapsuleRadii[i] = data->capsuleRadii[j];
    }
}

void CapsuleDataUpdateForCharacters(CapsuleData* capsuleData, CharacterData* characterData)
{
    int totalJointCount = 0;
    for (int i = 0; i < characterData->count; i++)
        totalJointCount += characterData->xformData[i].jointCount;
    CapsuleDataResize(capsuleData, totalJointCount);
}
