/*******************************************************************************************
*
*    geometry.h - Geometric function declarations
*
*******************************************************************************************/

#ifndef GEOMETRY_H
#define GEOMETRY_H

#include "raylib.h"

#define AO_RATIO_MAX 4.0

// Line segment nearest point functions
float NearestPointOnLineSegment(Vector3 lineStart, Vector3 lineVector, Vector3 point);
void NearestPointBetweenLineSegments(float* nearestTime0, float* nearestTime1, Vector3 line0Start, Vector3 line0End, Vector3 line1Start, Vector3 line1End);
float NearestPointBetweenLineSegmentAndPlane(Vector3 lineStart, Vector3 lineVector, Vector3 planePosition, Vector3 planeNormal);
float NearestPointBetweenLineSegmentAndGroundPlane(Vector3 lineStart, Vector3 lineVector);
void NearestPointBetweenLineSegmentAndGroundSegment(float* nearestTimeOnLine, Vector3* nearestPointOnGround, Vector3 lineStart, Vector3 lineEnd, Vector3 groundMins, Vector3 groundMaxs);
void NearestPointBetweenLineSegmentAndSweptLine(float* nearestTimeOnLine, Vector3* nearestPointOnSweptLine, Vector3 lineStart, Vector3 lineEnd, Vector3 sweptLineStart, Vector3 sweptLineEnd, Vector3 sweptLineSweepVector);

// Sphere occlusion functions
float SphereOcclusionLookup(float nlAngle, float h);
float SphereOcclusion(Vector3 pos, Vector3 nor, Vector3 sph, float rad);
float SphereIntersectionArea(float r1, float r2, float d);
float SphereDirectionalOcclusionLookup(float phi, float theta, float coneAngle);
float SphereDirectionalOcclusion(Vector3 pos, Vector3 sphere, float radius, Vector3 coneDir, float coneAngle);

// Capsule helper functions
Vector3 CapsuleStart(Vector3 capsulePosition, Quaternion capsuleRotation, float capsuleHalfLength);
Vector3 CapsuleEnd(Vector3 capsulePosition, Quaternion capsuleRotation, float capsuleHalfLength);
Vector3 CapsuleVector(Vector3 capsulePosition, Quaternion capsuleRotation, float capsuleHalfLength);
float CapsuleDirectionalOcclusion(Vector3 pos, Vector3 capStart, Vector3 capVec, float capRadius, Vector3 coneDir, float coneAngle);

#endif // GEOMETRY_H
