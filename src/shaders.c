/*******************************************************************************************
*
*    shaders.c - Shader GLSL strings and ShaderUniformsInit implementation
*
*******************************************************************************************/

#include "raylib.h"
#include "geometry.h"
#include "shaders.h"

#define GLSL_DEFINE_VALUE(X) #X
#define GLSL_DEFINE(X) "#define " #X " " GLSL_DEFINE_VALUE(X) " \n"

#if defined(PLATFORM_WEB)
#define GLSL_VERSION "#version 300 es\n"
#define GLSL_PRECISION "precision highp float;\nprecision mediump int;\n"
#else
#define GLSL_VERSION "#version 330 core\n"
#define GLSL_PRECISION ""
#endif

#define GLSL_STRINGIFY_INNER(X) #X
#define GLSL_STRINGIFY(X) GLSL_STRINGIFY_INNER(X)

#define GLSL_HEADER \
  GLSL_VERSION \
  GLSL_DEFINE(AO_RATIO_MAX) \
  GLSL_DEFINE(AO_CAPSULES_MAX) \
  GLSL_DEFINE(SHADOW_CAPSULES_MAX) \
  GLSL_DEFINE(PI)

#define GLSL_SHADER(X) \
  GLSL_HEADER \
  GLSL_STRINGIFY(X)

#define GLSL_SHADER_WITH_PRECISION(X) \
  GLSL_HEADER \
  GLSL_PRECISION \
  GLSL_STRINGIFY(X)

const char* shaderVS = GLSL_SHADER(

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec2 vertexTexCoord2;
in vec3 vertexNormal;

uniform int isCapsule;
uniform vec3 capsulePosition;
uniform vec4 capsuleRotation;
uniform float capsuleHalfLength;
uniform float capsuleRadius;

uniform mat4 matModel;
uniform mat4 matNormal;
uniform mat4 matView;
uniform mat4 matProjection;

out vec3 fragPosition;
out vec2 fragTexCoord;
out vec2 fragTexCoord2;
out vec3 fragNormal;

vec3 Rotate(in vec4 q, vec3 v)
{
    vec3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

vec3 CapsuleStretch(vec3 pos, float hlength, float radius)
{
    vec3 scaled = pos * radius;
    scaled.x = scaled.x > 0.0 ? scaled.x + hlength : scaled.x - hlength;
    return scaled;
}

void main()
{
    fragTexCoord = vertexTexCoord;
    fragTexCoord2 = vertexTexCoord2;

    if (isCapsule == 1)
    {
        fragPosition = Rotate(capsuleRotation,
            CapsuleStretch(vertexPosition,
            capsuleHalfLength, capsuleRadius)) + capsulePosition;

        fragNormal = Rotate(capsuleRotation, vertexNormal);
    }
    else
    {
        fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));
        fragNormal = normalize(vec3(matNormal * vec4(vertexNormal, 1.0)));
    }

    gl_Position = matProjection * matView * vec4(fragPosition, 1.0);
}

);

const char* shaderFS = GLSL_SHADER_WITH_PRECISION(

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec2 fragTexCoord2;
in vec3 fragNormal;

uniform vec3 objectColor;
uniform float objectSpecularity;
uniform float objectGlossiness;
uniform float objectOpacity;

uniform sampler2D texture0;
uniform sampler2D texture1;
uniform sampler2D texture2;
uniform sampler2D texture3;
uniform sampler2D texture4;
uniform sampler2D texture5;
uniform int useTexture;

uniform int usePBR;
uniform int useMetalnessTexture;
uniform int useNormalTexture;
uniform int useRoughnessTexture;
uniform int useOcclusionTexture;
uniform int useEmissionTexture;
uniform float metallicFactor;
uniform float roughnessFactor;
uniform float normalScale;
uniform float occlusionStrength;
uniform vec3 emissionFactor;
uniform int baseColorUV;
uniform int metallicRoughnessUV;
uniform int normalUV;
uniform int occlusionUV;
uniform int emissionUV;
// GGX-prefiltered HDR cubemap. Mip N was baked at roughness N/maxLod.
uniform samplerCube environmentMap;
uniform float environmentMaxLod;

uniform int alphaMode;
uniform float alphaCutoff;

uniform int isCapsule;
uniform vec3 capsulePosition;
uniform vec4 capsuleRotation;
uniform float capsuleHalfLength;
uniform float capsuleRadius;
uniform vec3 capsuleStart;
uniform vec3 capsuleVector;

uniform int shadowCapsuleCount;
uniform vec3 shadowCapsuleStarts[SHADOW_CAPSULES_MAX];
uniform vec3 shadowCapsuleVectors[SHADOW_CAPSULES_MAX];
uniform float shadowCapsuleRadii[SHADOW_CAPSULES_MAX];
uniform sampler2D shadowLookupTable;
uniform vec2 shadowLookupResolution;

uniform int aoCapsuleCount;
uniform vec3 aoCapsuleStarts[AO_CAPSULES_MAX];
uniform vec3 aoCapsuleVectors[AO_CAPSULES_MAX];
uniform float aoCapsuleRadii[AO_CAPSULES_MAX];
uniform sampler2D aoLookupTable;
uniform vec2 aoLookupResolution;

uniform vec3 cameraPosition;

uniform float sunStrength;
uniform vec3 sunDir;
uniform vec3 sunColor;
uniform float skyStrength;
uniform vec3 skyColor;
uniform float ambientStrength;
uniform float groundStrength;
uniform float exposure;
uniform int enableLighting;

// PBR lighting adjustment coefficients
const float PBR_EXPOSURE_ADJUSTMENT = 0.35;
const float PBR_SUN_ADJUSTMENT = 5;
const float PBR_INDIRECT_DIFFUSE_ADJUSTMENT = 0.2;
const float PBR_INDIRECT_SPECULAR_ADJUSTMENT = 0.2;

// Display-linear contrast look around 18% middle gray. Values above 1.0 are
// intentionally left unclamped for the framebuffer conversion to handle.
const float AGX_LOOK_CONTRAST = 1.05;

out vec4 finalColor;

vec3 SRGBToLinear(in vec3 color)
{
    color = max(color, vec3(0.0));
    vec3 low = color / 12.92;
    vec3 high = pow((color + 0.055) / 1.055, vec3(2.4));
    return mix(low, high, step(vec3(0.04045), color));
}

vec3 LinearToSRGB(in vec3 color)
{
    color = max(color, vec3(0.0));
    vec3 low = color * 12.92;
    vec3 high = 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055;
    return mix(low, high, step(vec3(0.0031308), color));
}

// Preserve the original display calibration for procedural ground/capsules.
// The legacy lighting model was authored around this inverse gamma pair.
vec3 LegacyFromGamma(in vec3 color)
{
    return pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
}

vec3 LegacyToGamma(in vec3 color)
{
    return pow(max(color, vec3(0.0)), vec3(2.2));
}

vec3 AgXContrastApprox(in vec3 x)
{
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 -
        6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

vec3 AgXContrastLook(in vec3 color)
{
    const vec3 pivot = vec3(0.18);
    return max(pivot + AGX_LOOK_CONTRAST * (color - pivot), vec3(0.0));
}

// Compact shader approximation of Blender's default AgX Base view transform.
// Input and output are linear Rec. 709; the final sRGB display encoding
// (LinearToSRGB) is applied by the caller, so no pow(2.2) here.
vec3 AgXToneMapping(in vec3 color)
{
    color = vec3(
        dot(color, vec3(0.8424790623, 0.0784336000, 0.0792237451)),
        dot(color, vec3(0.0423282423, 0.8784686365, 0.0791661275)),
        dot(color, vec3(0.0423756549, 0.0784336000, 0.8791429738)));

    const float minEV = -12.47393;
    const float maxEV = 4.026069;
    color = clamp((log2(max(color, vec3(1e-10))) - minEV) / (maxEV - minEV), 0.0, 1.0);
    color = AgXContrastApprox(color);

    color = vec3(
        dot(color, vec3(1.1968790051, -0.0980208811, -0.0990297441)),
        dot(color, vec3(-0.0528968518, 1.1519031299, -0.0989611768)),
        dot(color, vec3(-0.0529716355, -0.0980434501, 1.1510736726)));
    return AgXContrastLook(color);
}

float Saturate(in float x)
{
    return clamp(x, 0.0, 1.0);
}

float Square(in float x)
{
    return x * x;
}

vec2 MaterialUV(in int uvSet)
{
    return uvSet == 1 ? fragTexCoord2 : fragTexCoord;
}

vec3 NormalFromMap(in vec3 position, in vec3 normal, in vec2 uv, in float scale)
{
    vec3 mapNormal = texture(texture2, uv).xyz * 2.0 - 1.0;
    mapNormal.xy *= scale;

    vec3 dpdx = dFdx(position);
    vec3 dpdy = dFdy(position);
    vec2 duvdx = dFdx(uv);
    vec2 duvdy = dFdy(uv);
    float determinant = duvdx.x * duvdy.y - duvdx.y * duvdy.x;
    if (abs(determinant) < 1e-8) return normal;

    vec3 tangent = normalize((dpdx * duvdy.y - dpdy * duvdx.y) / determinant);
    tangent = normalize(tangent - normal * dot(normal, tangent));
    vec3 bitangent = normalize(cross(normal, tangent)) * (determinant < 0.0 ? -1.0 : 1.0);
    return normalize(mat3(tangent, bitangent, normal) * mapNormal);
}

vec3 FresnelSchlick(in float cosTheta, in vec3 f0)
{
    return f0 + (1.0 - f0) * pow(1.0 - Saturate(cosTheta), 5.0);
}

float DistributionGGX(in vec3 normal, in vec3 halfway, in float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float nDotH = max(dot(normal, halfway), 0.0);
    float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-6);
}

float VisibilitySmithGGXCorrelated(in float nDotV, in float nDotL, in float roughness)
{
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float ggxV = nDotL * sqrt(nDotV * nDotV * (1.0 - alpha2) + alpha2);
    float ggxL = nDotV * sqrt(nDotL * nDotL * (1.0 - alpha2) + alpha2);
    return 0.5 / max(ggxV + ggxL, 1e-6);
}

vec3 EnvironmentBRDF(in vec3 f0, in float nDotV, in float roughness)
{
    vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * nDotV)) * r.x + r.y;
    vec2 ab = vec2(-1.04, 1.04) * a004 + r.zw;
    return max(f0 * ab.x + ab.y, vec3(0.0));
}

// The roughest prefiltered level is a low-frequency, positive environment
// estimate suitable for diffuse fill without reintroducing an SH approximation.
vec3 EnvironmentDiffuseIrradiance(in vec3 normal)
{
    return textureLod(environmentMap, normalize(normal), environmentMaxLod).rgb;
}

vec3 EnvironmentRadiance(in vec3 direction, in float perceptualRoughness)
{
    float lod = clamp(perceptualRoughness, 0.0, 1.0) * environmentMaxLod;
    return textureLod(environmentMap, normalize(direction), lod).rgb;
}

float FastAcos(in float x)
{
    float y = abs(x);
    float p = -0.1565827 * y + 1.570796;
    p *= sqrt(max(1.0 - y, 0.0));
    return x >= 0.0 ? p : PI - p;
}

float FastPositiveAcos(in float x)
{
    float p = -0.1565827 * x + 1.570796;
    return p * sqrt(max(1.0 - x, 0.0));
}

vec3 Rotate(in vec4 q, vec3 v)
{
    vec3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

vec3 Unrotate(in vec4 q, vec3 v)
{
    return Rotate(vec4(-q.x, -q.y, -q.z, q.w), v);
}

float Checker(in vec2 uv)
{
    vec4 uvDDXY = vec4(dFdx(uv), dFdy(uv));
    vec2 w = max(vec2(length(uvDDXY.xz), length(uvDDXY.yw)), vec2(1e-8));
    vec2 i = 2.0*(abs(fract((uv-0.5*w)*0.5)-0.5)-
                  abs(fract((uv+0.5*w)*0.5)-0.5))/w;
    return 0.5 - 0.5*i.x*i.y;
}

float Grid(in vec2 uv, in float lineWidth)
{
    vec4 uvDDXY = vec4(dFdx(uv), dFdy(uv));
    vec2 uvDeriv = max(vec2(length(uvDDXY.xz), length(uvDDXY.yw)), vec2(1e-8));
    float targetWidth = lineWidth > 0.5 ? 1.0 - lineWidth : lineWidth;
    vec2 drawWidth = clamp(
        vec2(targetWidth, targetWidth), uvDeriv, vec2(0.5, 0.5));
    vec2 lineAA = uvDeriv * 1.5;
    vec2 gridUV = abs(fract(uv) * 2.0 - 1.0);
    gridUV = lineWidth > 0.5 ? gridUV : 1.0 - gridUV;
    vec2 g2 = smoothstep(drawWidth + lineAA, drawWidth - lineAA, gridUV);
    g2 *= clamp(targetWidth / drawWidth, 0.0, 1.0);
    g2 = mix(g2, vec2(targetWidth, targetWidth),
        clamp(uvDeriv * 2.0 - 1.0, 0.0, 1.0));
    g2 = lineWidth > 0.5 ? 1.0 - g2 : g2;
    return mix(g2.x, 1.0, g2.y);
}

float SphereOcclusion(in vec3 pos, in vec3 nor, in vec3 sph, in float rad)
{
    vec3 di = sph - pos;
    float l = length(di);
    float nlAngle = FastAcos(dot(nor, di / l));
    float h  = l < rad ? 1.0 : l / rad;
    vec2 uvs = vec2(nlAngle / PI, (h - 1.0) / (AO_RATIO_MAX - 1.0));
    uvs = uvs * (aoLookupResolution - 1.0) / aoLookupResolution + 0.5 / aoLookupResolution;
    return texture(aoLookupTable, uvs).r;
}

float SphereDirectionalOcclusion(
    in vec3 pos, 
    in vec3 sphere, 
    in float radius,
    in vec3 coneDir)
{
    vec3 occluder = sphere - pos;
    float occluderLen2 = dot(occluder, occluder);
    vec3 occluderDir = occluder * inversesqrt(occluderLen2);
    float phi = FastAcos(dot(occluderDir, -coneDir));
    float theta = FastPositiveAcos(sqrt(occluderLen2 / (Square(radius) + occluderLen2)));

    vec2 uvs = vec2(phi / PI, theta / (PI / 2.0));
    uvs = uvs * (shadowLookupResolution - 1.0) / shadowLookupResolution + 0.5 / shadowLookupResolution;
    return texture(shadowLookupTable, uvs).r;
}

float CapsuleOcclusion(
    in vec3 pos, 
    in vec3 nor,
    in vec3 capStart, 
    in vec3 capVec, 
    in float radius)
{
    vec3 ba = capVec;
    vec3 pa = pos - capStart;
    float l = dot(ba, ba);
    float t = abs(l) < 1e-8f ? 0.0 : Saturate(dot(pa, ba) / l);
    return SphereOcclusion(pos, nor, capStart + t * ba, radius);
}

float CapsuleDirectionalOcclusion(
    in vec3 pos, in vec3 capStart, in vec3 capVec,
    in float capRadius, in vec3 coneDir)
{
    vec3 ba = capVec;
    vec3 pa = capStart - pos;
    vec3 cba = dot(-coneDir, ba) * -coneDir - ba;
    float t = Saturate(dot(pa, cba) / dot(cba, cba));

    return SphereDirectionalOcclusion(pos, capStart + t * ba, capRadius, coneDir);
}

vec2 CapsuleUVs(
    in vec3 pos, in vec3 capPos,
    in vec4 capRot, in float capHalfLength,
    in float capRadius, in vec2 scale)
{
    vec3 loc = Unrotate(capRot, pos - capPos);

    vec2 limit = vec2(
        2.0 * capHalfLength + 2.0 * capRadius,
        PI * capRadius);

    vec2 repeat = max(round(scale * limit), 1.0);

    return (repeat / limit) * vec2(loc.x, capRadius * atan(loc.z, loc.y));
}

vec3 CapsuleNormal(
    in vec3 pos, in vec3 capStart,
    in vec3 capVec)
{
    vec3 ba = capVec;
    vec3 pa = pos - capStart;
    float h = Saturate(dot(pa, ba) / dot(ba, ba));
    return normalize(pa - h*ba);
}

void main()
{
    vec3 pos = fragPosition;
    vec3 nor = fragNormal;
    vec2 uvs = fragTexCoord;

    if (isCapsule == 1)
    {
        uvs = CapsuleUVs(
            pos,
            capsulePosition,
            capsuleRotation,
            capsuleHalfLength,
            capsuleRadius,
            vec2(4.0, 4.0));
        
        nor = CapsuleNormal(pos, capsuleStart, capsuleVector);
    }

    nor = normalize(nor);
    if (usePBR == 1 && useNormalTexture == 1)
        nor = NormalFromMap(pos, nor, MaterialUV(normalUV), normalScale);

    vec3 lightDir = normalize(-sunDir);

    float sunShadow = 1.0;
    for (int i = 0; i < shadowCapsuleCount; i++)
    {
        sunShadow = min(sunShadow, CapsuleDirectionalOcclusion(
            pos,
            shadowCapsuleStarts[i],
            shadowCapsuleVectors[i],
            shadowCapsuleRadii[i],
            sunDir));
    }
    
    float ambShadow = 1.0;
    for (int i = 0; i < aoCapsuleCount; i++)
    {
        ambShadow = min(ambShadow, CapsuleOcclusion(
            pos, nor,
            aoCapsuleStarts[i],
            aoCapsuleVectors[i],
            aoCapsuleRadii[i]));
    }

    vec2 baseUVs = usePBR == 1 ? MaterialUV(baseColorUV) : uvs;
    vec4 baseSample = texture(texture0, baseUVs);
    vec3 final;

    if (usePBR == 1)
    {
        // glTF factors are linear multipliers; only color textures use sRGB.
        vec3 albedo = objectColor;
        if (useTexture == 1) albedo *= SRGBToLinear(baseSample.rgb);

        vec2 mrUVs = MaterialUV(metallicRoughnessUV);
        float metallic = metallicFactor;
        float roughness = roughnessFactor;
        if (useMetalnessTexture == 1) metallic *= texture(texture1, mrUVs).b;
        if (useRoughnessTexture == 1) roughness *= texture(texture3, mrUVs).g;
        metallic = Saturate(metallic);
        roughness = clamp(roughness, 0.045, 1.0);

        float materialAO = 1.0;
        if (useOcclusionTexture == 1)
            materialAO = mix(1.0, texture(texture4, MaterialUV(occlusionUV)).r, Saturate(occlusionStrength));

        vec3 emission = emissionFactor;
        if (useEmissionTexture == 1)
            emission *= SRGBToLinear(texture(texture5, MaterialUV(emissionUV)).rgb);

        vec3 viewDir = normalize(cameraPosition - pos);
        vec3 halfway = normalize(viewDir + lightDir);
        float nDotV = max(dot(nor, viewDir), 0.0);
        float nDotL = max(dot(nor, lightDir), 0.0);

        vec3 f0 = mix(vec3(0.01), albedo, metallic); // 0.04->0.01 make dark more pure
        vec3 fresnel = FresnelSchlick(max(dot(halfway, viewDir), 0.0), f0);
        float distribution = DistributionGGX(nor, halfway, roughness);
        float visibility = VisibilitySmithGGXCorrelated(nDotV, nDotL, roughness);
        vec3 specularBRDF = distribution * visibility * fresnel;
        vec3 diffuseBRDF = (vec3(1.0) - fresnel) * (1.0 - metallic) * albedo / PI;
        vec3 lightSunColor = SRGBToLinear(sunColor);
        // The UI light strength historically represented Lambert exit radiance.
        // Convert it to incident radiance so the physically-correct 1/PI diffuse
        // term does not make PBR materials about PI times darker.
        vec3 direct = sunShadow * (PI * sunStrength * PBR_SUN_ADJUSTMENT) * lightSunColor *
            (diffuseBRDF + specularBRDF) * nDotL;

        vec3 diffuseLighting = (ambientStrength * PBR_INDIRECT_DIFFUSE_ADJUSTMENT) * EnvironmentDiffuseIrradiance(nor);
        vec3 indirectDiffuse = albedo * (1.0 - metallic) * diffuseLighting;

        vec3 reflectionDir = reflect(-viewDir, nor);
        vec3 reflectionColor = (ambientStrength * PBR_INDIRECT_SPECULAR_ADJUSTMENT) * EnvironmentRadiance(reflectionDir, roughness);
        vec3 indirectSpecular = EnvironmentBRDF(f0, nDotV, roughness) *
            reflectionColor;

        if (enableLighting == 0)
            final = albedo;
        else
            final = direct + materialAO * (ambShadow * indirectDiffuse + indirectSpecular) + emission;
    }
    else
    {
        vec3 texColor = LegacyFromGamma(baseSample.rgb);
        float gridFine = Grid(20.0 * uvs, 0.025);
        float gridCoarse = Grid(2.0 * uvs, 0.02);
        float check = Checker(2.0 * uvs);
        vec3 proceduralColor = LegacyFromGamma(objectColor) * mix(mix(mix(0.9, 0.95, check), 0.85, gridFine), 1.0, gridCoarse);
        vec3 albedo = mix(proceduralColor, texColor, float(useTexture));
        float specularity = objectSpecularity * mix(mix(0.0, 0.75, check), 1.0, gridCoarse);
        vec3 eyeDir = normalize(pos - cameraPosition);
        vec3 lightSunColor = LegacyFromGamma(sunColor);
        vec3 lightSunHalf = normalize(sunDir + eyeDir);
        vec3 lightSkyColor = LegacyFromGamma(skyColor);
        vec3 skyDir = vec3(0.0, -1.0, 0.0);
        vec3 lightSkyHalf = normalize(skyDir + eyeDir);
        float sunFactorDiff = max(dot(nor, -sunDir), 0.0);
        float sunFactorSpec = specularity * ((objectGlossiness+2.0) / (8.0 * PI)) *
            pow(max(dot(nor, lightSunHalf), 0.0), objectGlossiness);
        float skyFactorDiff = max(dot(nor, -skyDir), 0.0);
        float skyFactorSpec = specularity * ((objectGlossiness+2.0) / (8.0 * PI)) *
            pow(max(dot(nor, lightSkyHalf), 0.0), objectGlossiness);
        float groundFactorDiff = max(dot(nor, skyDir), 0.0);
        vec3 ambient = ambShadow * ambientStrength * lightSkyColor * albedo;
        vec3 diffuse = sunShadow * sunStrength * lightSunColor * albedo * sunFactorDiff +
            groundStrength * lightSkyColor * albedo * groundFactorDiff +
            skyStrength * lightSkyColor * albedo * skyFactorDiff;
        float specular = sunShadow * sunStrength * sunFactorSpec + skyStrength * skyFactorSpec;
        if (enableLighting == 0)
            final = albedo;
        else
            final = diffuse + ambient + specular;
    }

    // Screen-door transparency using 8x8 Bayer dither matrix
    if (alphaMode == 1)
    {
        // MASK mode: hard alpha cutoff
        float texAlpha = baseSample.a * objectOpacity;
        if (texAlpha < alphaCutoff) discard;
    }
    else if (alphaMode == 2)
    {
        // BLEND mode: screen-door stipple dither
        const float bayer8[64] = float[64](
            0.0/64.0, 32.0/64.0,  8.0/64.0, 40.0/64.0,  2.0/64.0, 34.0/64.0, 10.0/64.0, 42.0/64.0,
            48.0/64.0, 16.0/64.0, 56.0/64.0, 24.0/64.0, 50.0/64.0, 18.0/64.0, 58.0/64.0, 26.0/64.0,
            12.0/64.0, 44.0/64.0,  4.0/64.0, 36.0/64.0, 14.0/64.0, 46.0/64.0,  6.0/64.0, 38.0/64.0,
            60.0/64.0, 28.0/64.0, 52.0/64.0, 20.0/64.0, 62.0/64.0, 30.0/64.0, 54.0/64.0, 22.0/64.0,
             3.0/64.0, 35.0/64.0, 11.0/64.0, 43.0/64.0,  1.0/64.0, 33.0/64.0,  9.0/64.0, 41.0/64.0,
            51.0/64.0, 19.0/64.0, 59.0/64.0, 27.0/64.0, 49.0/64.0, 17.0/64.0, 57.0/64.0, 25.0/64.0,
            15.0/64.0, 47.0/64.0,  7.0/64.0, 39.0/64.0, 13.0/64.0, 45.0/64.0,  5.0/64.0, 37.0/64.0,
            63.0/64.0, 31.0/64.0, 55.0/64.0, 23.0/64.0, 61.0/64.0, 29.0/64.0, 53.0/64.0, 21.0/64.0
        );
        float texAlpha = baseSample.a * objectOpacity;
        ivec2 pixel = ivec2(int(gl_FragCoord.x) % 8, int(gl_FragCoord.y) % 8);
        if (texAlpha <= bayer8[pixel.y * 8 + pixel.x]) discard;
    }

    if (usePBR == 1)
    {
        // Skip tone mapping and exposure when lighting is disabled.
        vec3 displayLinear = enableLighting == 0
            ? max(final, vec3(0.0))
            : AgXToneMapping(max((exposure * PBR_EXPOSURE_ADJUSTMENT) * final, vec3(0.0)));
        finalColor = vec4(LinearToSRGB(displayLinear), objectOpacity);
    }
    else
    {
        // Skip exposure when lighting is disabled (albedo is already LDR).
        vec3 displayLinear = enableLighting == 0
            ? max(final, vec3(0.0))
            : max(exposure * final, vec3(0.0));
        finalColor = vec4(LegacyToGamma(displayLinear), objectOpacity);
    }
}

);

void ShaderUniformsInit(ShaderUniforms* uniforms, Shader shader)
{
    uniforms->isCapsule = GetShaderLocation(shader, "isCapsule");
    uniforms->capsulePosition =  GetShaderLocation(shader, "capsulePosition");
    uniforms->capsuleRotation =  GetShaderLocation(shader, "capsuleRotation");
    uniforms->capsuleHalfLength =  GetShaderLocation(shader, "capsuleHalfLength");
    uniforms->capsuleRadius =  GetShaderLocation(shader, "capsuleRadius");
    uniforms->capsuleStart =  GetShaderLocation(shader, "capsuleStart");
    uniforms->capsuleVector =  GetShaderLocation(shader, "capsuleVector");

    uniforms->shadowCapsuleCount = GetShaderLocation(shader, "shadowCapsuleCount");
    uniforms->shadowCapsuleStarts =  GetShaderLocation(shader, "shadowCapsuleStarts");
    uniforms->shadowCapsuleVectors =  GetShaderLocation(shader, "shadowCapsuleVectors");
    uniforms->shadowCapsuleRadii =  GetShaderLocation(shader, "shadowCapsuleRadii");
    uniforms->shadowLookupTable =  GetShaderLocation(shader, "shadowLookupTable");
    uniforms->shadowLookupResolution =  GetShaderLocation(shader, "shadowLookupResolution");

    uniforms->aoCapsuleCount = GetShaderLocation(shader, "aoCapsuleCount");
    uniforms->aoCapsuleStarts =  GetShaderLocation(shader, "aoCapsuleStarts");
    uniforms->aoCapsuleVectors =  GetShaderLocation(shader, "aoCapsuleVectors");
    uniforms->aoCapsuleRadii =  GetShaderLocation(shader, "aoCapsuleRadii");
    uniforms->aoLookupTable =  GetShaderLocation(shader, "aoLookupTable");
    uniforms->aoLookupResolution =  GetShaderLocation(shader, "aoLookupResolution");

    uniforms->cameraPosition = GetShaderLocation(shader, "cameraPosition");

    uniforms->objectColor = GetShaderLocation(shader, "objectColor");
    uniforms->objectSpecularity = GetShaderLocation(shader, "objectSpecularity");
    uniforms->objectGlossiness = GetShaderLocation(shader, "objectGlossiness");
    uniforms->objectOpacity = GetShaderLocation(shader, "objectOpacity");
    uniforms->useTexture = GetShaderLocation(shader, "useTexture");
    uniforms->alphaMode = GetShaderLocation(shader, "alphaMode");
    uniforms->alphaCutoff = GetShaderLocation(shader, "alphaCutoff");

    uniforms->sunStrength = GetShaderLocation(shader, "sunStrength");
    uniforms->sunDir = GetShaderLocation(shader, "sunDir");
    uniforms->sunColor = GetShaderLocation(shader, "sunColor");
    uniforms->skyStrength = GetShaderLocation(shader, "skyStrength");
    uniforms->skyColor = GetShaderLocation(shader, "skyColor");
    uniforms->ambientStrength = GetShaderLocation(shader, "ambientStrength");
    uniforms->groundStrength = GetShaderLocation(shader, "groundStrength");

    uniforms->usePBR = GetShaderLocation(shader, "usePBR");
    uniforms->useMetalnessTexture = GetShaderLocation(shader, "useMetalnessTexture");
    uniforms->useNormalTexture = GetShaderLocation(shader, "useNormalTexture");
    uniforms->useRoughnessTexture = GetShaderLocation(shader, "useRoughnessTexture");
    uniforms->useOcclusionTexture = GetShaderLocation(shader, "useOcclusionTexture");
    uniforms->useEmissionTexture = GetShaderLocation(shader, "useEmissionTexture");
    uniforms->metallicFactor = GetShaderLocation(shader, "metallicFactor");
    uniforms->roughnessFactor = GetShaderLocation(shader, "roughnessFactor");
    uniforms->normalScale = GetShaderLocation(shader, "normalScale");
    uniforms->occlusionStrength = GetShaderLocation(shader, "occlusionStrength");
    uniforms->emissionFactor = GetShaderLocation(shader, "emissionFactor");
    uniforms->baseColorUV = GetShaderLocation(shader, "baseColorUV");
    uniforms->metallicRoughnessUV = GetShaderLocation(shader, "metallicRoughnessUV");
    uniforms->normalUV = GetShaderLocation(shader, "normalUV");
    uniforms->occlusionUV = GetShaderLocation(shader, "occlusionUV");
    uniforms->emissionUV = GetShaderLocation(shader, "emissionUV");
    uniforms->environmentMap = GetShaderLocation(shader, "environmentMap");
    uniforms->environmentMaxLod = GetShaderLocation(shader, "environmentMaxLod");

    // DrawMesh() binds material maps through this location table.
    shader.locs[SHADER_LOC_MAP_METALNESS] = GetShaderLocation(shader, "texture1");
    shader.locs[SHADER_LOC_MAP_NORMAL] = GetShaderLocation(shader, "texture2");
    shader.locs[SHADER_LOC_MAP_ROUGHNESS] = GetShaderLocation(shader, "texture3");
    shader.locs[SHADER_LOC_MAP_OCCLUSION] = GetShaderLocation(shader, "texture4");
    shader.locs[SHADER_LOC_MAP_EMISSION] = GetShaderLocation(shader, "texture5");

    uniforms->exposure = GetShaderLocation(shader, "exposure");
    uniforms->enableLighting = GetShaderLocation(shader, "enableLighting");
}
