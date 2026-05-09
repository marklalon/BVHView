/*******************************************************************************************
*
*    geometry.c - Geometric function implementations
*
*******************************************************************************************/

#include <assert.h>
#include <math.h>
#include "raylib.h"
#include "raymath.h"
#include "geometry.h"
#include "math_utils.h"

float NearestPointOnLineSegment(Vector3 lineStart, Vector3 lineVector, Vector3 point)
{
    Vector3 ap = Vector3Subtract(point, lineStart);
    float lengthsq = Vector3LengthSqr(lineVector);
    return lengthsq < 1e-8f ? 0.5f : Saturate(Vector3DotProduct(lineVector, ap) / lengthsq);
}

void NearestPointBetweenLineSegments(float* nearestTime0, float* nearestTime1, Vector3 line0Start, Vector3 line0End, Vector3 line1Start, Vector3 line1End)
{
    Vector3 line0Vec = Vector3Subtract(line0End, line0Start);
    Vector3 line1Vec = Vector3Subtract(line1End, line1Start);
    float d0 = Vector3LengthSqr(Vector3Subtract(line1Start, line0Start));
    float d1 = Vector3LengthSqr(Vector3Subtract(line1End, line0Start));
    float d2 = Vector3LengthSqr(Vector3Subtract(line1Start, line0End));
    float d3 = Vector3LengthSqr(Vector3Subtract(line1End, line0End));
    *nearestTime0 = (d2 < d0 || d2 < d1 || d3 < d0 || d3 < d1) ? 1.0f : 0.0f;
    *nearestTime1 = NearestPointOnLineSegment(line1Start, line1Vec, Vector3Add(line0Start, Vector3Scale(line0Vec, *nearestTime0)));
    *nearestTime0 = NearestPointOnLineSegment(line0Start, line0Vec, Vector3Add(line1Start, Vector3Scale(line1Vec, *nearestTime1)));
}

float NearestPointBetweenLineSegmentAndPlane(Vector3 lineStart, Vector3 lineVector, Vector3 planePosition, Vector3 planeNormal)
{
    float denom = Vector3DotProduct(planeNormal, lineVector);
    if (fabs(denom) < 1e-8f) return 0.5f;
    return Saturate(Vector3DotProduct(Vector3Subtract(planePosition, lineStart), planeNormal) / denom);
}

float NearestPointBetweenLineSegmentAndGroundPlane(Vector3 lineStart, Vector3 lineVector)
{
    return fabs(lineVector.y) < 1e-8f ? 0.5f : Saturate((-lineStart.y) / lineVector.y);
}

void NearestPointBetweenLineSegmentAndGroundSegment(float* nearestTimeOnLine, Vector3* nearestPointOnGround, Vector3 lineStart, Vector3 lineEnd, Vector3 groundMins, Vector3 groundMaxs)
{
    Vector3 lineVec = Vector3Subtract(lineEnd, lineStart);
    *nearestTimeOnLine = NearestPointBetweenLineSegmentAndGroundPlane(lineStart, lineVec);
    *nearestPointOnGround = (Vector3){ lineStart.x + (*nearestTimeOnLine) * lineVec.x, 0.0f, lineStart.z + (*nearestTimeOnLine) * lineVec.z };
    if (nearestPointOnGround->x >= groundMins.x && nearestPointOnGround->x <= groundMaxs.x && nearestPointOnGround->z >= groundMins.z && nearestPointOnGround->z <= groundMaxs.z) return;
    Vector3 edgeStart0 = (Vector3){ groundMins.x, 0.0f, groundMins.z };
    Vector3 edgeEnd0 = (Vector3){ groundMins.x, 0.0f, groundMaxs.z };
    Vector3 edgeStart1 = (Vector3){ groundMins.x, 0.0f, groundMaxs.z };
    Vector3 edgeEnd1 = (Vector3){ groundMaxs.x, 0.0f, groundMaxs.z };
    Vector3 edgeStart2 = (Vector3){ groundMaxs.x, 0.0f, groundMaxs.z };
    Vector3 edgeEnd2 = (Vector3){ groundMaxs.x, 0.0f, groundMins.z };
    Vector3 edgeStart3 = (Vector3){ groundMaxs.x, 0.0f, groundMins.z };
    Vector3 edgeEnd3 = (Vector3){ groundMins.x, 0.0f, groundMins.z };
    float ntl0, ntl1, ntl2, ntl3, nte0, nte1, nte2, nte3;
    NearestPointBetweenLineSegments(&ntl0, &nte0, lineStart, lineEnd, edgeStart0, edgeEnd0);
    NearestPointBetweenLineSegments(&ntl1, &nte1, lineStart, lineEnd, edgeStart1, edgeEnd1);
    NearestPointBetweenLineSegments(&ntl2, &nte2, lineStart, lineEnd, edgeStart2, edgeEnd2);
    NearestPointBetweenLineSegments(&ntl3, &nte3, lineStart, lineEnd, edgeStart3, edgeEnd3);
    Vector3 npl0 = Vector3Add(lineStart, Vector3Scale(lineVec, ntl0));
    Vector3 npl1 = Vector3Add(lineStart, Vector3Scale(lineVec, ntl1));
    Vector3 npl2 = Vector3Add(lineStart, Vector3Scale(lineVec, ntl2));
    Vector3 npl3 = Vector3Add(lineStart, Vector3Scale(lineVec, ntl3));
    Vector3 npe0 = Vector3Add(edgeStart0, Vector3Scale(Vector3Subtract(edgeEnd0, edgeStart0), nte0));
    Vector3 npe1 = Vector3Add(edgeStart1, Vector3Scale(Vector3Subtract(edgeEnd1, edgeStart1), nte1));
    Vector3 npe2 = Vector3Add(edgeStart2, Vector3Scale(Vector3Subtract(edgeEnd2, edgeStart2), nte2));
    Vector3 npe3 = Vector3Add(edgeStart3, Vector3Scale(Vector3Subtract(edgeEnd3, edgeStart3), nte3));
    float dist0 = Vector3Distance(npl0, npe0);
    float dist1 = Vector3Distance(npl1, npe1);
    float dist2 = Vector3Distance(npl2, npe2);
    float dist3 = Vector3Distance(npl3, npe3);
    if (dist0 <= dist1 && dist0 <= dist2 && dist0 <= dist3) { *nearestTimeOnLine = ntl0; *nearestPointOnGround = npe0; return; }
    if (dist1 <= dist0 && dist1 <= dist2 && dist1 <= dist3) { *nearestTimeOnLine = ntl1; *nearestPointOnGround = npe1; return; }
    if (dist2 <= dist0 && dist2 <= dist1 && dist2 <= dist3) { *nearestTimeOnLine = ntl2; *nearestPointOnGround = npe2; return; }
    *nearestTimeOnLine = ntl3; *nearestPointOnGround = npe3;
}

static Vector3 ProjectPointOntoSweptLine(Vector3 sweptLineStart, Vector3 sweptLineVec, Vector3 sweptLineSweepVec, Vector3 position)
{
    Vector3 w = Vector3Subtract(position, sweptLineStart);
    Vector3 u = Vector3Normalize(sweptLineVec);
    Vector3 v = Vector3Normalize(sweptLineSweepVec);
    float a1 = Vector3DotProduct(u, u);
    float b1 = Vector3DotProduct(u, v);
    float c1 = Vector3DotProduct(w, u);
    float a2 = Vector3DotProduct(v, u);
    float b2 = Vector3DotProduct(v, v);
    float c2 = Vector3DotProduct(w, v);
    float x = ((c1 * b2) - (b1 * c2)) / (a1 * b2 - b1 * a2);
    float y = (c1 - x * a1) / b1;
    x = Clamp(x, 0.0f, Vector3Length(sweptLineVec));
    y = Clamp(y, 0.0f, Vector3Length(sweptLineSweepVec));
    return Vector3Add(sweptLineStart, Vector3Add(Vector3Scale(u, x), Vector3Scale(v, y)));
}

void NearestPointBetweenLineSegmentAndSweptLine(float* nearestTimeOnLine, Vector3* nearestPointOnSweptLine, Vector3 lineStart, Vector3 lineEnd, Vector3 sweptLineStart, Vector3 sweptLineEnd, Vector3 sweptLineSweepVector)
{
    Vector3 lineVec = Vector3Subtract(lineEnd, lineStart);
    Vector3 sweptLineVec = Vector3Subtract(sweptLineEnd, sweptLineStart);
    Vector3 planeNormal = Vector3Length(sweptLineVec) < 1e-8f ?
        Vector3Normalize(Vector3CrossProduct((Vector3){ 0.0f, 1.0f, 0.0f }, sweptLineSweepVector)) :
        Vector3Normalize(Vector3CrossProduct(sweptLineVec, sweptLineSweepVector));
    float ntl0 = NearestPointBetweenLineSegmentAndPlane(lineStart, lineVec, sweptLineStart, planeNormal);
    Vector3 npl0 = Vector3Add(lineStart, Vector3Scale(lineVec, ntl0));
    Vector3 npsl0;
    if (Vector3Length(sweptLineVec) > 1e-8f)
        npsl0 = ProjectPointOntoSweptLine(sweptLineStart, sweptLineVec, sweptLineSweepVector, npl0);
    else
    {
        float ntsl = NearestPointOnLineSegment(sweptLineStart, sweptLineSweepVector, npl0);
        npsl0 = Vector3Add(sweptLineStart, Vector3Scale(sweptLineSweepVector, ntsl));
    }
    float dist0 = Vector3Distance(npl0, npsl0);
    Vector3 es1 = sweptLineStart, ee1 = Vector3Add(sweptLineStart, sweptLineSweepVector);
    Vector3 es2 = sweptLineEnd, ee2 = Vector3Add(sweptLineEnd, sweptLineSweepVector);
    Vector3 es3 = sweptLineStart, ee3 = sweptLineEnd;
    float ntl1, ntl2, ntl3, nte1, nte2, nte3;
    NearestPointBetweenLineSegments(&ntl1, &nte1, lineStart, lineEnd, es1, ee1);
    NearestPointBetweenLineSegments(&ntl2, &nte2, lineStart, lineEnd, es2, ee2);
    NearestPointBetweenLineSegments(&ntl3, &nte3, lineStart, lineEnd, es3, ee3);
    Vector3 npl1 = Vector3Add(lineStart, Vector3Scale(lineVec, ntl1));
    Vector3 npl2 = Vector3Add(lineStart, Vector3Scale(lineVec, ntl2));
    Vector3 npl3 = Vector3Add(lineStart, Vector3Scale(lineVec, ntl3));
    Vector3 npsl1 = Vector3Add(es1, Vector3Scale(Vector3Subtract(ee1, es1), nte1));
    Vector3 npsl2 = Vector3Add(es2, Vector3Scale(Vector3Subtract(ee2, es2), nte2));
    Vector3 npsl3 = Vector3Add(es3, Vector3Scale(Vector3Subtract(ee3, es3), nte3));
    float dist1 = Vector3Distance(npl1, npsl1);
    float dist2 = Vector3Distance(npl2, npsl2);
    float dist3 = Vector3Distance(npl3, npsl3);
    if (dist0 <= dist1 && dist0 <= dist2 && dist0 <= dist3) { *nearestTimeOnLine = ntl0; *nearestPointOnSweptLine = npsl0; return; }
    if (dist1 <= dist0 && dist1 <= dist2 && dist1 <= dist3) { *nearestTimeOnLine = ntl1; *nearestPointOnSweptLine = npsl1; return; }
    if (dist2 <= dist0 && dist2 <= dist1 && dist2 <= dist3) { *nearestTimeOnLine = ntl2; *nearestPointOnSweptLine = npsl2; return; }
    *nearestTimeOnLine = ntl3; *nearestPointOnSweptLine = npsl3;
}

float SphereOcclusionLookup(float nlAngle, float h)
{
    float nl = cosf(nlAngle);
    float h2 = h*h;
    float res = Max(nl, 0.0) / h2;
    float k2 = 1.0 - h2*nl*nl;
    if (k2 > 1e-4f)
    {
        res = nl * acosf(Clamp(-nl*sqrtf((h2 - 1.0f) / Max(1.0f - nl*nl, 1e-8f)), -1.0f, 1.0f)) - sqrtf(k2*(h2 - 1.0f));
        res = (res / h2 + atanf(sqrt(k2 / (h2 - 1.0f)))) / PI;
    }
    float decay = Max(1.0f - (h - 1.0f) / ((float)AO_RATIO_MAX - 1.0f), 0.0f);
    return 1.0f - res * decay;
}

float SphereOcclusion(Vector3 pos, Vector3 nor, Vector3 sph, float rad)
{
    Vector3 di = Vector3Subtract(sph, pos);
    float l = Vector3Length(di);
    float nlAngle = acosf(Clamp(Vector3DotProduct(nor, Vector3Scale(di, 1.0f / Max(l, 1e-8f))), -1.0f, 1.0f));
    float h = l < rad ? 1.0 : l / rad;
    return SphereOcclusionLookup(nlAngle, h);
}

float SphereIntersectionArea(float r1, float r2, float d)
{
    if (Min(r1, r2) <= Max(r1, r2) - d) return 1.0f - Max(cosf(r1), cosf(r2));
    if (r1 + r2 <= d) return 0.0f;
    float delta = fabs(r1 - r2);
    float x = 1.0f - Saturate((d - delta) / Max(r1 + r2 - delta, 1e-8f));
    float area = Square(x) * (-2.0f * x + 3.0f);
    return area * (1.0f - Max(cosf(r1), cosf(r2)));
}

float SphereDirectionalOcclusionLookup(float phi, float theta, float coneAngle)
{
    return 1.0f - SphereIntersectionArea(theta, coneAngle / 2.0f, phi) / (1.0f - cosf(coneAngle / 2.0f));
}

float SphereDirectionalOcclusion(Vector3 pos, Vector3 sphere, float radius, Vector3 coneDir, float coneAngle)
{
    Vector3 occluder = Vector3Subtract(sphere, pos);
    float occluderLen2 = Vector3DotProduct(occluder, occluder);
    Vector3 occluderDir = Vector3Scale(occluder, 1.0f / Max(sqrtf(occluderLen2), 1e-8f));
    float phi = acosf(Clamp(Vector3DotProduct(occluderDir, Vector3Negate(coneDir)), -1.0f, 1.0f));
    float theta = acosf(Clamp(sqrtf(occluderLen2 / (Square(radius) + occluderLen2)), -1.0f, 1.0f));
    return SphereDirectionalOcclusionLookup(phi, theta, coneAngle);
}

Vector3 CapsuleStart(Vector3 capsulePosition, Quaternion capsuleRotation, float capsuleHalfLength)
{
    return Vector3Add(capsulePosition, Vector3RotateByQuaternion((Vector3){+capsuleHalfLength, 0.0f, 0.0f}, capsuleRotation));
}

Vector3 CapsuleEnd(Vector3 capsulePosition, Quaternion capsuleRotation, float capsuleHalfLength)
{
    return Vector3Add(capsulePosition, Vector3RotateByQuaternion((Vector3){-capsuleHalfLength, 0.0f, 0.0f}, capsuleRotation));
}

Vector3 CapsuleVector(Vector3 capsulePosition, Quaternion capsuleRotation, float capsuleHalfLength)
{
    Vector3 capsuleStart = CapsuleStart(capsulePosition, capsuleRotation, capsuleHalfLength);
    return Vector3Subtract(Vector3Add(capsulePosition, Vector3RotateByQuaternion((Vector3){-capsuleHalfLength, 0.0f, 0.0f}, capsuleRotation)), capsuleStart);
}

float CapsuleDirectionalOcclusion(Vector3 pos, Vector3 capStart, Vector3 capVec, float capRadius, Vector3 coneDir, float coneAngle)
{
    Vector3 ba = capVec;
    Vector3 pa = Vector3Subtract(capStart, pos);
    Vector3 cba = Vector3Subtract(Vector3Scale(Vector3Negate(coneDir), Vector3DotProduct(Vector3Negate(coneDir), ba)), ba);
    float t = Saturate(Vector3DotProduct(pa, cba) / Max(Vector3DotProduct(cba, cba), 1e-8f));
    return SphereDirectionalOcclusion(pos, Vector3Add(capStart, Vector3Scale(ba, t)), capRadius, coneDir, coneAngle);
}
