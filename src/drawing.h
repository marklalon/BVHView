/*******************************************************************************************
*
*    drawing.h - Draw function declarations
*
*******************************************************************************************/

#ifndef DRAWING_H
#define DRAWING_H

#include "raylib.h"

// Forward declarations
typedef struct TransformData TransformData;
typedef struct CapsuleData CapsuleData;

void DrawTransform(const Vector3 position, const Quaternion rotation, const float size);
void DrawSkeleton(TransformData* xformData, bool drawEndSites, Color color, Color endSiteColor, int highlightedBone);
void DrawTransforms(TransformData* xformData);
void DrawWireFrames(CapsuleData* capsuleData, Color color);

#endif // DRAWING_H
