/*******************************************************************************************
*
*    math_utils.c - Math utilities implementation
*
*******************************************************************************************/

#include <assert.h>
#include <math.h>
#include "raylib.h"
#include "raymath.h"
#include "math_utils.h"

float Max(float x, float y) { return x > y ? x : y; }
float Min(float x, float y) { return x < y ? x : y; }
float Saturate(float x) { return Clamp(x, 0.0f, 1.0f); }
float Square(float x) { return x * x; }
int ClampInt(int x, int min, int max) { return x < min ? min : x > max ? max : x; }
int MaxInt(int x, int y) { return x > y ? x : y; }
int MinInt(int x, int y) { return x < y ? x : y; }

Quaternion QuaternionBetween(Vector3 p, Vector3 q)
{
    Vector3 c = Vector3CrossProduct(p, q);
    Quaternion o = { c.x, c.y, c.z, sqrtf(Vector3DotProduct(p, p) * Vector3DotProduct(q, q)) + Vector3DotProduct(p, q) };
    return QuaternionLength(o) < 1e-8f ?
        QuaternionFromAxisAngle((Vector3){ 1.0f, 0.0f, 0.0f }, PI) :
        QuaternionNormalize(o);
}

Quaternion QuaternionAbsolute(Quaternion q)
{
    if (q.w < 0.0f) { q.x = -q.x; q.y = -q.y; q.z = -q.z; q.w = -q.w; }
    return q;
}

Quaternion QuaternionExp(Vector3 v)
{
    float halfangle = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
    if (halfangle < 1e-4f) {
        return QuaternionNormalize((Quaternion){ v.x, v.y, v.z, 1.0f });
    } else {
        float c = cosf(halfangle);
        float s = sinf(halfangle) / halfangle;
        return (Quaternion){ s * v.x, s * v.y, s * v.z, c };
    }
}

Vector3 QuaternionLog(Quaternion q)
{
    float length = sqrtf(q.x*q.x + q.y*q.y + q.z*q.z);
    if (length < 1e-4f) {
        return (Vector3){ q.x, q.y, q.z };
    } else {
        float halfangle = atan2f(length, q.w);
        return Vector3Scale((Vector3){ q.x, q.y, q.z }, halfangle / length);
    }
}

Vector3 QuaternionToScaledAngleAxis(Quaternion q) { return Vector3Scale(QuaternionLog(q), 2.0f); }
Quaternion QuaternionFromScaledAngleAxis(Vector3 v) { return QuaternionExp(Vector3Scale(v, 0.5f)); }

Vector3 Vector3Hermite(Vector3 p0, Vector3 p1, Vector3 v0, Vector3 v1, float alpha)
{
    float x = alpha;
    float w0 = 2*x*x*x - 3*x*x + 1;
    float w1 = 3*x*x - 2*x*x*x;
    float w2 = x*x*x - 2*x*x + x;
    float w3 = x*x*x - x*x;
    return Vector3Add(Vector3Add(Vector3Scale(p0, w0), Vector3Scale(p1, w1)),
        Vector3Add(Vector3Scale(v0, w2), Vector3Scale(v1, w3)));
}

Vector3 Vector3InterpolateCubic(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float alpha)
{
    Vector3 v1 = Vector3Scale(Vector3Add(Vector3Subtract(p1, p0), Vector3Subtract(p2, p1)), 0.5f);
    Vector3 v2 = Vector3Scale(Vector3Add(Vector3Subtract(p2, p1), Vector3Subtract(p3, p2)), 0.5f);
    return Vector3Hermite(p1, p2, v1, v2, alpha);
}

Quaternion QuaternionHermite(Quaternion r0, Quaternion r1, Vector3 v0, Vector3 v1, float alpha)
{
    float x = alpha;
    float w1 = 3*x*x - 2*x*x*x;
    float w2 = x*x*x - 2*x*x + x;
    float w3 = x*x*x - x*x;
    Vector3 r1r0 = QuaternionToScaledAngleAxis(QuaternionAbsolute(QuaternionMultiply(r1, QuaternionInvert(r0))));
    return QuaternionMultiply(QuaternionFromScaledAngleAxis(
        Vector3Add(Vector3Add(Vector3Scale(r1r0, w1), Vector3Scale(v0, w2)), Vector3Scale(v1, w3))), r0);
}

Quaternion QuaternionInterpolateCubic(Quaternion r0, Quaternion r1, Quaternion r2, Quaternion r3, float alpha)
{
    Vector3 r1r0 = QuaternionToScaledAngleAxis(QuaternionAbsolute(QuaternionMultiply(r1, QuaternionInvert(r0))));
    Vector3 r2r1 = QuaternionToScaledAngleAxis(QuaternionAbsolute(QuaternionMultiply(r2, QuaternionInvert(r1))));
    Vector3 r3r2 = QuaternionToScaledAngleAxis(QuaternionAbsolute(QuaternionMultiply(r3, QuaternionInvert(r2))));
    Vector3 v1 = Vector3Scale(Vector3Add(r1r0, r2r1), 0.5f);
    Vector3 v2 = Vector3Scale(Vector3Add(r2r1, r3r2), 0.5f);
    return QuaternionHermite(r1, r2, v1, v2, alpha);
}

Vector4 FrustumPlaneNormalize(Vector4 plane)
{
    float magnitude = sqrtf(Square(plane.x) + Square(plane.y) + Square(plane.z));
    plane.x /= magnitude; plane.y /= magnitude; plane.z /= magnitude; plane.w /= magnitude;
    return plane;
}

Frustum FrustumFromCameraMatrices(Matrix projection, Matrix modelview)
{
    Matrix planes = { 0 };
    planes.m0 = modelview.m0 * projection.m0 + modelview.m1 * projection.m4 + modelview.m2 * projection.m8 + modelview.m3 * projection.m12;
    planes.m1 = modelview.m0 * projection.m1 + modelview.m1 * projection.m5 + modelview.m2 * projection.m9 + modelview.m3 * projection.m13;
    planes.m2 = modelview.m0 * projection.m2 + modelview.m1 * projection.m6 + modelview.m2 * projection.m10 + modelview.m3 * projection.m14;
    planes.m3 = modelview.m0 * projection.m3 + modelview.m1 * projection.m7 + modelview.m2 * projection.m11 + modelview.m3 * projection.m15;
    planes.m4 = modelview.m4 * projection.m0 + modelview.m5 * projection.m4 + modelview.m6 * projection.m8 + modelview.m7 * projection.m12;
    planes.m5 = modelview.m4 * projection.m1 + modelview.m5 * projection.m5 + modelview.m6 * projection.m9 + modelview.m7 * projection.m13;
    planes.m6 = modelview.m4 * projection.m2 + modelview.m5 * projection.m6 + modelview.m6 * projection.m10 + modelview.m7 * projection.m14;
    planes.m7 = modelview.m4 * projection.m3 + modelview.m5 * projection.m7 + modelview.m6 * projection.m11 + modelview.m7 * projection.m15;
    planes.m8 = modelview.m8 * projection.m0 + modelview.m9 * projection.m4 + modelview.m10 * projection.m8 + modelview.m11 * projection.m12;
    planes.m9 = modelview.m8 * projection.m1 + modelview.m9 * projection.m5 + modelview.m10 * projection.m9 + modelview.m11 * projection.m13;
    planes.m10 = modelview.m8 * projection.m2 + modelview.m9 * projection.m6 + modelview.m10 * projection.m10 + modelview.m11 * projection.m14;
    planes.m11 = modelview.m8 * projection.m3 + modelview.m9 * projection.m7 + modelview.m10 * projection.m11 + modelview.m11 * projection.m15;
    planes.m12 = modelview.m12 * projection.m0 + modelview.m13 * projection.m4 + modelview.m14 * projection.m8 + modelview.m15 * projection.m12;
    planes.m13 = modelview.m12 * projection.m1 + modelview.m13 * projection.m5 + modelview.m14 * projection.m9 + modelview.m15 * projection.m13;
    planes.m14 = modelview.m12 * projection.m2 + modelview.m13 * projection.m6 + modelview.m14 * projection.m10 + modelview.m15 * projection.m14;
    planes.m15 = modelview.m12 * projection.m3 + modelview.m13 * projection.m7 + modelview.m14 * projection.m11 + modelview.m15 * projection.m15;

    Frustum frustum;
    frustum.back = FrustumPlaneNormalize((Vector4){ planes.m3 - planes.m2, planes.m7 - planes.m6, planes.m11 - planes.m10, planes.m15 - planes.m14 });
    frustum.front = FrustumPlaneNormalize((Vector4){ planes.m3 + planes.m2, planes.m7 + planes.m6, planes.m11 + planes.m10, planes.m15 + planes.m14 });
    frustum.bottom = FrustumPlaneNormalize((Vector4){ planes.m3 + planes.m1, planes.m7 + planes.m5, planes.m11 + planes.m9, planes.m15 + planes.m13 });
    frustum.top = FrustumPlaneNormalize((Vector4){ planes.m3 - planes.m1, planes.m7 - planes.m5, planes.m11 - planes.m9, planes.m15 - planes.m13 });
    frustum.left = FrustumPlaneNormalize((Vector4){ planes.m3 + planes.m0, planes.m7 + planes.m4, planes.m11 + planes.m8, planes.m15 + planes.m12 });
    frustum.right = FrustumPlaneNormalize((Vector4){ planes.m3 - planes.m0, planes.m7 - planes.m4, planes.m11 - planes.m8, planes.m15 - planes.m12 });
    return frustum;
}

float FrustumPlaneDistanceToPoint(Vector4 plane, Vector3 position)
{
    return (plane.x * position.x + plane.y * position.y + plane.z * position.z + plane.w);
}

bool FrustumContainsSphere(Frustum frustum, Vector3 position, float radius)
{
    if (FrustumPlaneDistanceToPoint(frustum.back, position) < -radius) return false;
    if (FrustumPlaneDistanceToPoint(frustum.front, position) < -radius) return false;
    if (FrustumPlaneDistanceToPoint(frustum.bottom, position) < -radius) return false;
    if (FrustumPlaneDistanceToPoint(frustum.top, position) < -radius) return false;
    if (FrustumPlaneDistanceToPoint(frustum.left, position) < -radius) return false;
    if (FrustumPlaneDistanceToPoint(frustum.right, position) < -radius) return false;
    return true;
}
