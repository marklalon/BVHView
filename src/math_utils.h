/*******************************************************************************************
*
*    math_utils.h - Math utilities, quaternion ops, interpolation, frustum culling
*
*******************************************************************************************/

#ifndef MATH_UTILS_H
#define MATH_UTILS_H

#include "raylib.h"

// Basic math helpers
float Max(float x, float y);
float Min(float x, float y);
float Saturate(float x);
float Square(float x);
int ClampInt(int x, int min, int max);
int MaxInt(int x, int y);
int MinInt(int x, int y);

// Quaternion utilities
Quaternion QuaternionBetween(Vector3 p, Vector3 q);
Quaternion QuaternionAbsolute(Quaternion q);
Quaternion QuaternionExp(Vector3 v);
Vector3 QuaternionLog(Quaternion q);
Vector3 QuaternionToScaledAngleAxis(Quaternion q);
Quaternion QuaternionFromScaledAngleAxis(Vector3 v);

// Cubic interpolation
Vector3 Vector3Hermite(Vector3 p0, Vector3 p1, Vector3 v0, Vector3 v1, float alpha);
Vector3 Vector3InterpolateCubic(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float alpha);
Quaternion QuaternionHermite(Quaternion r0, Quaternion r1, Vector3 v0, Vector3 v1, float alpha);
Quaternion QuaternionInterpolateCubic(Quaternion r0, Quaternion r1, Quaternion r2, Quaternion r3, float alpha);

// Frustum culling
typedef struct Frustum
{
    Vector4 back;
    Vector4 front;
    Vector4 bottom;
    Vector4 top;
    Vector4 right;
    Vector4 left;

} Frustum;

Frustum FrustumFromCameraMatrices(Matrix projection, Matrix modelview);
float FrustumPlaneDistanceToPoint(Vector4 plane, Vector3 position);
bool FrustumContainsSphere(Frustum frustum, Vector3 position, float radius);

#endif // MATH_UTILS_H
