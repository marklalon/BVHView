/*******************************************************************************************
*
*    drawing.c - Draw function implementations
*
*******************************************************************************************/

#include "raylib.h"
#include "raymath.h"
#include "drawing.h"
#include "transform_data.h"
#include "capsule_data.h"
#include "geometry.h"

void DrawTransform(const Vector3 position, const Quaternion rotation, const float size)
{
    DrawLine3D(position, Vector3Add(position, Vector3RotateByQuaternion((Vector3){ size, 0.0, 0.0 }, rotation)), RED);
    DrawLine3D(position, Vector3Add(position, Vector3RotateByQuaternion((Vector3){ 0.0, size, 0.0 }, rotation)), GREEN);
    DrawLine3D(position, Vector3Add(position, Vector3RotateByQuaternion((Vector3){ 0.0, 0.0, size }, rotation)), BLUE);
}

void DrawSkeleton(TransformData* xformData, bool drawEndSites, Color color, Color endSiteColor, int highlightedBone)
{
    for (int i = 0; i < xformData->jointCount; i++)
    {
        bool isHighlighted = (i == highlightedBone);
        Color c = isHighlighted ? YELLOW : color;
        Color ec = isHighlighted ? ORANGE : endSiteColor;
        if (!xformData->endSite[i])
            DrawSphereWires(xformData->globalPositions[i], isHighlighted ? 0.02f : 0.01f, 4, 6, c);
        else if (drawEndSites)
            DrawCubeWiresV(xformData->globalPositions[i], (Vector3){ 0.02f, 0.02f, 0.02f }, ec);
        if (xformData->parents[i] != -1)
        {
            if (!xformData->endSite[i])
                DrawLine3D(xformData->globalPositions[i], xformData->globalPositions[xformData->parents[i]], c);
            else if (drawEndSites)
                DrawLine3D(xformData->globalPositions[i], xformData->globalPositions[xformData->parents[i]], ec);
        }
    }
}

void DrawTransforms(TransformData* xformData)
{
    for (int i = 0; i < xformData->jointCount; i++)
    {
        if (!xformData->endSite[i])
            DrawTransform(xformData->globalPositions[i], xformData->globalRotations[i], 0.1f);
    }
}

void DrawWireFrames(CapsuleData* capsuleData, Color color)
{
    for (int i = 0; i < capsuleData->capsuleCount; i++)
    {
        Vector3 capsuleStart = CapsuleStart(capsuleData->capsulePositions[i], capsuleData->capsuleRotations[i], capsuleData->capsuleHalfLengths[i]);
        Vector3 capsuleEnd = CapsuleEnd(capsuleData->capsulePositions[i], capsuleData->capsuleRotations[i], capsuleData->capsuleHalfLengths[i]);
        float capsuleRadius = capsuleData->capsuleRadii[i];
        DrawSphereWires(capsuleStart, capsuleRadius, 4, 6, color);
        DrawSphereWires(capsuleEnd, capsuleRadius, 4, 6, color);
        DrawCylinderWiresEx(capsuleStart, capsuleEnd, capsuleRadius, capsuleRadius, 6, color);
    }
}
