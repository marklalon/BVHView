/*******************************************************************************************
*
*    glb_data.c - GLB/GLTF implementation
*
*******************************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <limits.h>
#include "raylib.h"
#include "build/raylib/raylib/src/external/cgltf.h"
#include "src/webp/decode.h"

// Private stb_image with full JPEG/PNG/BMP/GIF/PSD/HDR support.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "build/raylib/raylib/src/external/stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "raymath.h"
#include "rlgl.h"
#include "glb_data.h"
#include "transform_data.h"
#include "math_utils.h"

static bool GLBDataIsWebP(const cgltf_image* image, const unsigned char* bytes, int size)
{
    if (image != NULL && image->mime_type != NULL && strcmp(image->mime_type, "image/webp") == 0) return true;
    if (size >= 12 && memcmp(bytes, "RIFF", 4) == 0 && memcmp(bytes + 8, "WEBP", 4) == 0) return true;
    return false;
}

static unsigned char* GLBDataLoadImageURI(const char* modelFilename, const char* uri, int* size)
{
    *size = 0;
    if (uri == NULL || uri[0] == '\0') return NULL;
    if (strncmp(uri, "data:", 5) == 0)
    {
        const char* comma = strchr(uri, ',');
        if (comma == NULL || strstr(uri, ";base64") == NULL) return NULL;
        return DecodeDataBase64(comma + 1, size);
    }

    const char* slash = strrchr(modelFilename, '/');
    const char* backslash = strrchr(modelFilename, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash)) slash = backslash;
    bool absolute = (uri[0] == '/' || uri[0] == '\\' || (uri[0] != '\0' && uri[1] == ':'));
    size_t directoryLength = (!absolute && slash != NULL) ? (size_t)(slash - modelFilename + 1) : 0;
    size_t pathLength = directoryLength + strlen(uri);
    char* path = (char*)malloc(pathLength + 1);
    if (path == NULL) return NULL;
    if (directoryLength > 0) memcpy(path, modelFilename, directoryLength);
    memcpy(path + directoryLength, uri, strlen(uri) + 1);
    cgltf_decode_uri(path);
    unsigned char* bytes = LoadFileData(path, size);
    free(path);
    return bytes;
}

void GLBDataInit(GLBData* data)
{
    data->model = (Model){ 0 };
    data->animations = NULL;
    data->animCount = 0;
    data->activeAnim = 0;
    data->frameTime = 1.0f / 30.0f;
    data->sourceFrameCounts = NULL;
    data->sourceFrameTimes = NULL;
    data->sourceDurations = NULL;
    data->sourceData = NULL;
    data->sourceSkin = NULL;
    data->sourceRestPose = NULL;
    data->sourceLocalPose = NULL;
    data->sourceGlobalPose = NULL;
    data->sourceRootPose = NULL;
    data->topoOrder = NULL;
    data->invTopoOrder = NULL;
    data->materialInfo = NULL;
    data->materialInfoCount = 0;
    data->meshGroundOffset = 0.0f;
}

void GLBDataFree(GLBData* data)
{
    if (data->animations != NULL)
    {
        UnloadModelAnimations(data->animations, data->animCount);
        data->animations = NULL;
    }
    if (data->model.skeleton.boneCount > 0 || data->model.meshCount > 0)
    {
        UnloadModel(data->model);
    }
    free(data->sourceFrameCounts);
    free(data->sourceFrameTimes);
    free(data->sourceDurations);
    free(data->sourceRestPose);
    free(data->sourceLocalPose);
    free(data->sourceGlobalPose);
    free(data->sourceRootPose);
    if (data->sourceData != NULL) cgltf_free(data->sourceData);
    free(data->topoOrder);
    free(data->invTopoOrder);
    free(data->materialInfo);
    data->model = (Model){ 0 };
    data->animCount = 0;
    data->activeAnim = 0;
    data->frameTime = 1.0f / 30.0f;
    data->sourceFrameCounts = NULL;
    data->sourceFrameTimes = NULL;
    data->sourceDurations = NULL;
    data->sourceData = NULL;
    data->sourceSkin = NULL;
    data->sourceRestPose = NULL;
    data->sourceLocalPose = NULL;
    data->sourceGlobalPose = NULL;
    data->sourceRootPose = NULL;
    data->topoOrder = NULL;
    data->invTopoOrder = NULL;
    data->materialInfo = NULL;
    data->materialInfoCount = 0;
}

int GLBDataGetSourceFrameCount(const GLBData* data, int animIdx)
{
    if (data->animCount <= 0) return 0;
    animIdx = ClampInt(animIdx, 0, data->animCount - 1);
    if (data->sourceFrameCounts != NULL && data->sourceFrameCounts[animIdx] > 0)
        return data->sourceFrameCounts[animIdx];
    return data->animations[animIdx].keyframeCount;
}

float GLBDataGetSourceFrameTime(const GLBData* data, int animIdx)
{
    if (data->animCount <= 0) return data->frameTime;
    animIdx = ClampInt(animIdx, 0, data->animCount - 1);
    if (data->sourceFrameTimes != NULL && data->sourceFrameTimes[animIdx] > 0.0f)
        return data->sourceFrameTimes[animIdx];
    return data->frameTime;
}

float GLBDataGetSourceDuration(const GLBData* data, int animIdx)
{
    if (data->animCount <= 0) return 0.0f;
    animIdx = ClampInt(animIdx, 0, data->animCount - 1);
    if (data->sourceDurations != NULL && data->sourceDurations[animIdx] > 0.0f)
        return data->sourceDurations[animIdx];
    return (GLBDataGetSourceFrameCount(data, animIdx) - 1) * GLBDataGetSourceFrameTime(data, animIdx);
}

static Matrix GLBMatrixFromCgltf(const cgltf_float* m)
{
    return (Matrix){
        m[0], m[4], m[8], m[12],
        m[1], m[5], m[9], m[13],
        m[2], m[6], m[10], m[14],
        m[3], m[7], m[11], m[15]
    };
}

static Matrix GLBTransformToMatrix(Transform transform)
{
    return MatrixMultiply(
        MatrixMultiply(MatrixScale(transform.scale.x, transform.scale.y, transform.scale.z),
            QuaternionToMatrix(transform.rotation)),
        MatrixTranslate(transform.translation.x, transform.translation.y, transform.translation.z));
}

static void GLBDataUpdateModelAnimationVertexBuffers(Model model)
{
    for (int meshIndex = 0; meshIndex < model.meshCount; meshIndex++)
    {
        Mesh* mesh = &model.meshes[meshIndex];
        if (mesh->boneWeights == NULL || mesh->boneIndices == NULL || mesh->animVertices == NULL || mesh->animNormals == NULL) continue;
        bool bufferUpdateRequired = false;
        int boneCounter = 0;
        int vertexValuesCount = mesh->vertexCount * 3;
        for (int vertexIndex = 0; vertexIndex < vertexValuesCount; vertexIndex += 3)
        {
            mesh->animVertices[vertexIndex + 0] = 0.0f;
            mesh->animVertices[vertexIndex + 1] = 0.0f;
            mesh->animVertices[vertexIndex + 2] = 0.0f;
            mesh->animNormals[vertexIndex + 0] = 0.0f;
            mesh->animNormals[vertexIndex + 1] = 0.0f;
            mesh->animNormals[vertexIndex + 2] = 0.0f;
            for (int weightIndex = 0; weightIndex < 4; weightIndex++, boneCounter++)
            {
                float boneWeight = mesh->boneWeights[boneCounter];
                int boneIndex = mesh->boneIndices[boneCounter];
                if (boneWeight == 0.0f) continue;
                if (boneIndex < 0 || boneIndex >= model.skeleton.boneCount) continue;
                Vector3 animVertex = { mesh->vertices[vertexIndex], mesh->vertices[vertexIndex + 1], mesh->vertices[vertexIndex + 2] };
                animVertex = Vector3Transform(animVertex, model.boneMatrices[boneIndex]);
                mesh->animVertices[vertexIndex + 0] += animVertex.x * boneWeight;
                mesh->animVertices[vertexIndex + 1] += animVertex.y * boneWeight;
                mesh->animVertices[vertexIndex + 2] += animVertex.z * boneWeight;
                bufferUpdateRequired = true;
                if (mesh->normals != NULL)
                {
                    Vector3 animNormal = { mesh->normals[vertexIndex], mesh->normals[vertexIndex + 1], mesh->normals[vertexIndex + 2] };
                    animNormal = Vector3Transform(animNormal, MatrixTranspose(MatrixInvert(model.boneMatrices[boneIndex])));
                    mesh->animNormals[vertexIndex + 0] += animNormal.x * boneWeight;
                    mesh->animNormals[vertexIndex + 1] += animNormal.y * boneWeight;
                    mesh->animNormals[vertexIndex + 2] += animNormal.z * boneWeight;
                }
            }
        }
        if (bufferUpdateRequired)
        {
            rlUpdateVertexBuffer(mesh->vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION], mesh->animVertices, mesh->vertexCount * 3 * sizeof(float), 0);
            if (mesh->normals != NULL) rlUpdateVertexBuffer(mesh->vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_NORMAL], mesh->animNormals, mesh->vertexCount * 3 * sizeof(float), 0);
        }
    }
}

void GLBDataUpdateModelPose(GLBData* glb, const Transform* globalPose)
{
    if (glb->model.currentPose == NULL || glb->model.boneMatrices == NULL || glb->model.skeleton.bindPose == NULL) return;
    int boneCount = glb->model.skeleton.boneCount;
    for (int boneIndex = 0; boneIndex < boneCount; boneIndex++)
    {
        glb->model.currentPose[boneIndex] = globalPose[boneIndex];
        Matrix bindPoseMatrix = GLBTransformToMatrix(glb->model.skeleton.bindPose[boneIndex]);
        Matrix currentPoseMatrix = GLBTransformToMatrix(glb->model.currentPose[boneIndex]);
        glb->model.boneMatrices[boneIndex] = MatrixMultiply(MatrixInvert(bindPoseMatrix), currentPoseMatrix);
    }
    GLBDataUpdateModelAnimationVertexBuffers(glb->model);
}

static float GLBMatrixMaxAbsDiff(Matrix a, Matrix b)
{
    const float* pa = (const float*)&a;
    const float* pb = (const float*)&b;
    float maxDiff = 0.0f;
    for (int i = 0; i < 16; i++)
    {
        float diff = fabsf(pa[i] - pb[i]);
        if (diff > maxDiff) maxDiff = diff;
    }
    return maxDiff;
}

static void GLBMeshUndoWorldTransform(Mesh* mesh, Matrix inverseWorldMatrix, Matrix inverseWorldNormalMatrix)
{
    if (mesh->vertices != NULL)
    {
        for (int vertexIndex = 0; vertexIndex < mesh->vertexCount; vertexIndex++)
        {
            int base = vertexIndex * 3;
            Vector3 vertex = { mesh->vertices[base + 0], mesh->vertices[base + 1], mesh->vertices[base + 2] };
            vertex = Vector3Transform(vertex, inverseWorldMatrix);
            mesh->vertices[base + 0] = vertex.x;
            mesh->vertices[base + 1] = vertex.y;
            mesh->vertices[base + 2] = vertex.z;
        }
        if (mesh->vboId != NULL && mesh->vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION] != 0)
            rlUpdateVertexBuffer(mesh->vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION], mesh->vertices, mesh->vertexCount * 3 * sizeof(float), 0);
    }
    if (mesh->normals != NULL)
    {
        for (int normalIndex = 0; normalIndex < mesh->vertexCount; normalIndex++)
        {
            int base = normalIndex * 3;
            Vector3 normal = { mesh->normals[base + 0], mesh->normals[base + 1], mesh->normals[base + 2] };
            normal = Vector3Normalize(Vector3Transform(normal, inverseWorldNormalMatrix));
            mesh->normals[base + 0] = normal.x;
            mesh->normals[base + 1] = normal.y;
            mesh->normals[base + 2] = normal.z;
        }
        if (mesh->vboId != NULL && mesh->vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_NORMAL] != 0)
            rlUpdateVertexBuffer(mesh->vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_NORMAL], mesh->normals, mesh->vertexCount * 3 * sizeof(float), 0);
    }
    if (mesh->tangents != NULL)
    {
        for (int tangentIndex = 0; tangentIndex < mesh->vertexCount; tangentIndex++)
        {
            int base = tangentIndex * 4;
            Vector3 tangent = { mesh->tangents[base + 0], mesh->tangents[base + 1], mesh->tangents[base + 2] };
            tangent = Vector3Normalize(Vector3Transform(tangent, inverseWorldMatrix));
            mesh->tangents[base + 0] = tangent.x;
            mesh->tangents[base + 1] = tangent.y;
            mesh->tangents[base + 2] = tangent.z;
        }
        if (mesh->vboId != NULL && mesh->vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_TANGENT] != 0)
            rlUpdateVertexBuffer(mesh->vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_TANGENT], mesh->tangents, mesh->vertexCount * 4 * sizeof(float), 0);
    }
    if (mesh->animVertices != NULL && mesh->vertices != NULL)
        memcpy(mesh->animVertices, mesh->vertices, mesh->vertexCount * 3 * sizeof(float));
    if (mesh->animNormals != NULL && mesh->normals != NULL)
        memcpy(mesh->animNormals, mesh->normals, mesh->vertexCount * 3 * sizeof(float));
}

static void GLBDataUndoSkinnedMeshNodeTransforms(GLBData* data)
{
    if (data->sourceData == NULL || data->model.meshes == NULL) return;
    int meshIndex = 0;
    int correctedMeshCount = 0;
    for (cgltf_size nodeIndex = 0; nodeIndex < data->sourceData->nodes_count; nodeIndex++)
    {
        cgltf_node* node = &data->sourceData->nodes[nodeIndex];
        if (node->mesh == NULL) continue;
        cgltf_float worldTransform[16] = { 0 };
        cgltf_node_transform_world(node, worldTransform);
        Matrix worldMatrix = GLBMatrixFromCgltf(worldTransform);
        Matrix worldNormalMatrix = MatrixTranspose(MatrixInvert(worldMatrix));
        bool shouldUndoTransform = (node->skin != NULL) && (GLBMatrixMaxAbsDiff(worldMatrix, MatrixIdentity()) > 1e-6f);
        Matrix inverseWorldMatrix = shouldUndoTransform ? MatrixInvert(worldMatrix) : MatrixIdentity();
        Matrix inverseWorldNormalMatrix = shouldUndoTransform ? MatrixInvert(worldNormalMatrix) : MatrixIdentity();
        for (cgltf_size primitiveIndex = 0; primitiveIndex < node->mesh->primitives_count; primitiveIndex++)
        {
            if (node->mesh->primitives[primitiveIndex].type != cgltf_primitive_type_triangles) continue;
            if (meshIndex >= data->model.meshCount) return;
            if (shouldUndoTransform)
            {
                GLBMeshUndoWorldTransform(&data->model.meshes[meshIndex], inverseWorldMatrix, inverseWorldNormalMatrix);
                correctedMeshCount++;
            }
            meshIndex++;
        }
    }
    if (correctedMeshCount > 0)
        printf("INFO: Removed baked node transforms from %d skinned GLB mesh primitives\n", correctedMeshCount);
}

Matrix GLBDataGetModelTransform(const GLBData* glb, float scale, bool inplace)
{
    Matrix transform = MatrixScale(scale, scale, scale);
    if (glb->model.skeleton.boneCount == 0 && glb->meshGroundOffset != 0.0f)
        transform = MatrixMultiply(MatrixTranslate(0.0f, glb->meshGroundOffset * scale, 0.0f), transform);
    if (inplace && glb->model.currentPose != NULL && glb->topoOrder != NULL && glb->model.skeleton.boneCount > 0)
    {
        int rootBone = glb->topoOrder[0];
        Transform rootPose = glb->model.currentPose[rootBone];
        Quaternion yawRotation = { 0.0f, rootPose.rotation.y, 0.0f, rootPose.rotation.w };
        if (QuaternionLength(yawRotation) < 1e-8f) yawRotation = QuaternionIdentity();
        else yawRotation = QuaternionNormalize(yawRotation);
        Matrix translation = MatrixTranslate(-rootPose.translation.x * scale, 0.0f, -rootPose.translation.z * scale);
        Matrix rotation = QuaternionToMatrix(QuaternionInvert(yawRotation));
        transform = MatrixMultiply(transform, MatrixMultiply(translation, rotation));
    }
    return MatrixMultiply(glb->model.transform, transform);
}

static Transform GLBNodeLocalTransform(const cgltf_node* node)
{
    Transform transform = { .translation = { 0.0f, 0.0f, 0.0f }, .rotation = { 0.0f, 0.0f, 0.0f, 1.0f }, .scale = { 1.0f, 1.0f, 1.0f } };
    if (node == NULL) return transform;
    if (node->has_matrix)
    {
        MatrixDecompose(GLBMatrixFromCgltf(node->matrix), &transform.translation, &transform.rotation, &transform.scale);
        transform.rotation = QuaternionNormalize(transform.rotation);
        return transform;
    }
    if (node->has_translation)
        transform.translation = (Vector3){ node->translation[0], node->translation[1], node->translation[2] };
    if (node->has_rotation)
        transform.rotation = QuaternionNormalize((Quaternion){ node->rotation[0], node->rotation[1], node->rotation[2], node->rotation[3] });
    if (node->has_scale)
        transform.scale = (Vector3){ node->scale[0], node->scale[1], node->scale[2] };
    return transform;
}

int GLBFindSkinJointIndex(const cgltf_skin* skin, const cgltf_node* node)
{
    if (skin == NULL || node == NULL) return -1;
    for (cgltf_size i = 0; i < skin->joints_count; i++)
        if (skin->joints[i] == node) return (int)i;
    return -1;
}

bool GLBGetPoseAtTime(cgltf_interpolation_type interpolationType, cgltf_accessor* input, cgltf_accessor* output, float time, void* data)
{
    if (interpolationType >= cgltf_interpolation_type_max_enum) return false;
    if (input == NULL || output == NULL || input->count == 0) return false;
    float tstart = 0.0f;
    float tend = 0.0f;
    int keyframe = 0;
    if (input->count == 1)
    {
        if (!cgltf_accessor_read_float(input, 0, &tstart, 1)) return false;
        tend = tstart;
    }
    else
    {
        for (int i = 0; i < (int)input->count - 1; i++)
        {
            if (!cgltf_accessor_read_float(input, i, &tstart, 1)) return false;
            if (!cgltf_accessor_read_float(input, i + 1, &tend, 1)) return false;
            keyframe = i;
            if ((tstart <= time) && (time < tend)) break;
        }
    }
    if (FloatEquals(tend, tstart)) interpolationType = cgltf_interpolation_type_step;
    float duration = fmaxf(tend - tstart, EPSILON);
    float t = Clamp((time - tstart) / duration, 0.0f, 1.0f);
    if (output->component_type != cgltf_component_type_r_32f) return false;
    if (output->type == cgltf_type_vec3)
    {
        switch (interpolationType)
        {
            case cgltf_interpolation_type_step:
            { float tmp[3] = { 0 }; cgltf_accessor_read_float(output, keyframe, tmp, 3); *(Vector3*)data = (Vector3){ tmp[0], tmp[1], tmp[2] }; } break;
            case cgltf_interpolation_type_linear:
            { float tmp[3] = { 0 }; cgltf_accessor_read_float(output, keyframe, tmp, 3); Vector3 v1 = { tmp[0], tmp[1], tmp[2] }; cgltf_accessor_read_float(output, keyframe + 1, tmp, 3); Vector3 v2 = { tmp[0], tmp[1], tmp[2] }; *(Vector3*)data = Vector3Lerp(v1, v2, t); } break;
            case cgltf_interpolation_type_cubic_spline:
            { float tmp[3] = { 0 }; cgltf_accessor_read_float(output, 3*keyframe + 1, tmp, 3); Vector3 v1 = { tmp[0], tmp[1], tmp[2] }; cgltf_accessor_read_float(output, 3*keyframe + 2, tmp, 3); Vector3 tangent1 = { tmp[0], tmp[1], tmp[2] }; cgltf_accessor_read_float(output, 3*(keyframe + 1) + 1, tmp, 3); Vector3 v2 = { tmp[0], tmp[1], tmp[2] }; cgltf_accessor_read_float(output, 3*(keyframe + 1), tmp, 3); Vector3 tangent2 = { tmp[0], tmp[1], tmp[2] }; *(Vector3*)data = Vector3CubicHermite(v1, tangent1, v2, tangent2, t); } break;
            default: return false;
        }
    }
    else if (output->type == cgltf_type_vec4)
    {
        switch (interpolationType)
        {
            case cgltf_interpolation_type_step:
            { float tmp[4] = { 0 }; cgltf_accessor_read_float(output, keyframe, tmp, 4); *(Quaternion*)data = QuaternionNormalize((Quaternion){ tmp[0], tmp[1], tmp[2], tmp[3] }); } break;
            case cgltf_interpolation_type_linear:
            { float tmp[4] = { 0 }; cgltf_accessor_read_float(output, keyframe, tmp, 4); Quaternion v1 = QuaternionNormalize((Quaternion){ tmp[0], tmp[1], tmp[2], tmp[3] }); cgltf_accessor_read_float(output, keyframe + 1, tmp, 4); Quaternion v2 = QuaternionNormalize((Quaternion){ tmp[0], tmp[1], tmp[2], tmp[3] }); *(Quaternion*)data = QuaternionNormalize(QuaternionSlerp(v1, v2, t)); } break;
            case cgltf_interpolation_type_cubic_spline:
            { float tmp[4] = { 0 }; cgltf_accessor_read_float(output, 3*keyframe + 1, tmp, 4); Quaternion v1 = QuaternionNormalize((Quaternion){ tmp[0], tmp[1], tmp[2], tmp[3] }); cgltf_accessor_read_float(output, 3*keyframe + 2, tmp, 4); Vector4 outTangent1 = { tmp[0], tmp[1], tmp[2], 0.0f }; cgltf_accessor_read_float(output, 3*(keyframe + 1) + 1, tmp, 4); Quaternion v2 = QuaternionNormalize((Quaternion){ tmp[0], tmp[1], tmp[2], tmp[3] }); cgltf_accessor_read_float(output, 3*(keyframe + 1), tmp, 4); Vector4 inTangent2 = { tmp[0], tmp[1], tmp[2], 0.0f }; if (Vector4DotProduct(v1, v2) < 0.0f) v2 = Vector4Negate(v2); outTangent1 = Vector4Scale(outTangent1, duration); inTangent2 = Vector4Scale(inTangent2, duration); *(Quaternion*)data = QuaternionNormalize(QuaternionCubicHermiteSpline(v1, outTangent1, v2, inTangent2, t)); } break;
            default: return false;
        }
    }
    else return false;
    return true;
}

static bool GLBAnimationTargetsSkinJoint(const cgltf_skin* skin, const cgltf_node* node)
{
    if (skin == NULL || node == NULL) return false;
    for (cgltf_size i = 0; i < skin->joints_count; i++)
        if (skin->joints[i] == node) return true;
    return false;
}

static bool GLBDataLoadSourceTiming(GLBData* data, const char* filename)
{
    // Always parse the source GLTF data to obtain skeleton skin, rest pose, and
    // material information — even when animCount == 0 (static model preview).
    cgltf_options options = { 0 };
    cgltf_result result = cgltf_parse_file(&options, filename, &data->sourceData);
    if (result != cgltf_result_success) { printf("WARN: Failed to parse GLB source data for '%s'\n", filename); return false; }
    result = cgltf_load_buffers(&options, data->sourceData, filename);
    if (result != cgltf_result_success) { printf("WARN: Failed to load GLB source buffers for '%s'\n", filename); cgltf_free(data->sourceData); data->sourceData = NULL; return false; }
    data->sourceSkin = (data->sourceData->skins_count > 0) ? &data->sourceData->skins[0] : NULL;
    int boneCount = data->model.skeleton.boneCount;
    for (int boneIdx = 0; boneIdx < boneCount; boneIdx++)
    {
        data->sourceRestPose[boneIdx] = (Transform){ { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f } };
        data->sourceRootPose[boneIdx] = (Transform){ { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f } };
        if (data->sourceSkin != NULL && boneIdx < (int)data->sourceSkin->joints_count)
        {
            const cgltf_node* node = data->sourceSkin->joints[boneIdx];
            data->sourceRestPose[boneIdx] = GLBNodeLocalTransform(node);
            if (data->model.skeleton.bones[boneIdx].parent == -1 && node != NULL && node->parent != NULL)
            {
                cgltf_float worldMatrix[16] = { 0 };
                cgltf_node_transform_world(node->parent, worldMatrix);
                MatrixDecompose(GLBMatrixFromCgltf(worldMatrix), &data->sourceRootPose[boneIdx].translation, &data->sourceRootPose[boneIdx].rotation, &data->sourceRootPose[boneIdx].scale);
                data->sourceRootPose[boneIdx].rotation = QuaternionNormalize(data->sourceRootPose[boneIdx].rotation);
            }
        }
    }

    if (data->animCount <= 0) return true;

    const cgltf_skin* skin = data->sourceSkin;
    int parsedAnimCount = (data->animCount < (int)data->sourceData->animations_count) ? data->animCount : (int)data->sourceData->animations_count;
    for (int a = 0; a < parsedAnimCount; a++)
    {
        cgltf_animation* anim = &data->sourceData->animations[a];
        float duration = 0.0f;
        float minDelta = FLT_MAX;
        int maxInputCount = 0;
        bool hasTimeSamples = false;
        for (cgltf_size channelIdx = 0; channelIdx < anim->channels_count; channelIdx++)
        {
            cgltf_animation_channel* channel = &anim->channels[channelIdx];
            cgltf_animation_sampler* sampler = channel->sampler;
            if (skin != NULL && !GLBAnimationTargetsSkinJoint(skin, channel->target_node)) continue;
            if (sampler == NULL || sampler->input == NULL || sampler->input->count == 0) continue;
            int inputCount = (int)sampler->input->count;
            if (inputCount > maxInputCount) maxInputCount = inputCount;
            float prevTime = 0.0f;
            bool hasPrevTime = false;
            for (int sampleIdx = 0; sampleIdx < inputCount; sampleIdx++)
            {
                float time = 0.0f;
                if (!cgltf_accessor_read_float(sampler->input, sampleIdx, &time, 1)) break;
                hasTimeSamples = true;
                if (time > duration) duration = time;
                if (hasPrevTime) { float delta = time - prevTime; if (delta > 1e-6f && delta < minDelta) minDelta = delta; }
                prevTime = time;
                hasPrevTime = true;
            }
        }
        if (!hasTimeSamples) continue;
        int frameCount = maxInputCount > 0 ? maxInputCount : 1;
        float frameTime = data->frameTime;
        if (minDelta < FLT_MAX) { frameTime = minDelta; frameCount = 1 + (int)(duration / frameTime + 0.5f); }
        else if (frameCount > 1 && duration > 0.0f) { frameTime = duration / (float)(frameCount - 1); }
        if (frameCount < 1) frameCount = 1;
        if (frameTime <= 0.0f) frameTime = data->frameTime;
        data->sourceFrameCounts[a] = frameCount;
        data->sourceFrameTimes[a] = frameTime;
        data->sourceDurations[a] = duration;
    }
    return true;
}

void ComputeTopoOrder(int boneCount, BoneInfo* bones, int* topoOrder, int* invTopoOrder)
{
    for (int i = 0; i < boneCount; i++) invTopoOrder[i] = -1;
    bool* placed = (bool*)calloc(boneCount, sizeof(bool));
    int idx = 0;
    while (idx < boneCount)
    {
        int prevIdx = idx;
        for (int i = 0; i < boneCount; i++)
        {
            if (placed[i]) continue;
            int p = bones[i].parent;
            if (p == -1 || invTopoOrder[p] != -1)
            {
                topoOrder[idx] = i;
                invTopoOrder[i] = idx;
                placed[i] = true;
                idx++;
            }
        }
        if (idx == prevIdx) break;
    }
    free(placed);
}

bool GLBDataLoad(GLBData* data, const char* filename, char* errMsg, int errMsgSize)
{
    printf("INFO: Loading GLB '%s'\n", filename);
    const char* ext = strrchr(filename, '.');
    if (ext == NULL || (strcmp(ext, ".glb") != 0 && strcmp(ext, ".GLB") != 0 && strcmp(ext, ".gltf") != 0 && strcmp(ext, ".GLTF") != 0))
    { snprintf(errMsg, errMsgSize, "Error: File '%s' is not a .glb/.gltf file", filename); return false; }
    data->model = LoadModel(filename);
    int bc = data->model.skeleton.boneCount;
    // A file with neither bones nor a mesh has nothing to display.
    if (bc == 0 && data->model.meshCount == 0)
    { snprintf(errMsg, errMsgSize, "Error: Model '%s' has no skeleton and no mesh", filename); printf("ERROR: %s\n", errMsg); return false; }
    data->animations = LoadModelAnimations(filename, &data->animCount);
    data->activeAnim = 0;
    data->frameTime = 1.0f / 60.0f;
    // Mesh-only models (no skeleton) are supported for static mesh preview: skip all
    // skeleton/animation setup and fall through to material/texture loading below.
    if (bc == 0)
    {
        if (data->animations != NULL) { UnloadModelAnimations(data->animations, data->animCount); data->animations = NULL; }
        data->animCount = 0;
        printf("INFO: GLB '%s' has no skeleton - loading as mesh-only preview\n", filename);

        // Compute world-space bounding box to align mesh bottom to ground
        if (data->model.meshCount > 0)
        {
            float worldMinY = 1e+30f;
            Matrix m = data->model.transform;
            for (int meshIdx = 0; meshIdx < data->model.meshCount; meshIdx++)
            {
                Mesh* mesh = &data->model.meshes[meshIdx];
                if (mesh->vertices == NULL) continue;
                for (int v = 0; v < mesh->vertexCount; v++)
                {
                    Vector3 pos = { mesh->vertices[v * 3], mesh->vertices[v * 3 + 1], mesh->vertices[v * 3 + 2] };
                    pos = Vector3Transform(pos, m);
                    if (pos.y < worldMinY) worldMinY = pos.y;
                }
            }
            data->meshGroundOffset = (worldMinY < 1e+29f) ? -worldMinY : 0.0f;
            if (data->meshGroundOffset != 0.0f)
                printf("INFO: Mesh ground offset %.3f (world min Y = %.3f)\n", data->meshGroundOffset, worldMinY);
        }
    }
    else
    {
        // Allow loading models without animations (static preview in rest pose)
        if (data->animCount > 0)
        {
            data->sourceFrameCounts = (int*)malloc(data->animCount * sizeof(int));
            data->sourceFrameTimes = (float*)malloc(data->animCount * sizeof(float));
            data->sourceDurations = (float*)malloc(data->animCount * sizeof(float));
        }
        data->sourceRestPose = (Transform*)malloc(bc * sizeof(Transform));
        data->sourceLocalPose = (Transform*)malloc(bc * sizeof(Transform));
        data->sourceGlobalPose = (Transform*)malloc(bc * sizeof(Transform));
        data->sourceRootPose = (Transform*)malloc(bc * sizeof(Transform));
        if (data->sourceRestPose == NULL || data->sourceLocalPose == NULL || data->sourceGlobalPose == NULL || data->sourceRootPose == NULL
            || (data->animCount > 0 && (data->sourceFrameCounts == NULL || data->sourceFrameTimes == NULL || data->sourceDurations == NULL)))
        { GLBDataFree(data); snprintf(errMsg, errMsgSize, "Error: Out of memory while loading animation timing for '%s'", filename); printf("ERROR: %s\n", errMsg); return false; }
        for (int a = 0; a < data->animCount; a++)
        { data->sourceFrameCounts[a] = data->animations[a].keyframeCount; data->sourceFrameTimes[a] = data->frameTime; data->sourceDurations[a] = (data->animations[a].keyframeCount - 1) * data->frameTime; }
    }
    GLBDataLoadSourceTiming(data, filename);

    // Parse per-material alpha info from GLTF materials
    if (data->sourceData != NULL && data->model.materialCount > 0)
    {
        data->materialInfoCount = data->model.materialCount;
        data->materialInfo = (GLBMaterialInfo*)calloc(data->materialInfoCount, sizeof(GLBMaterialInfo));
        if (data->materialInfo != NULL)
        {
            for (int matIdx = 0; matIdx < data->materialInfoCount; matIdx++)
            {
                // raylib material 0 is a default material; GLTF materials start at raylib index 1
                int cgltfIdx = matIdx - 1;
                if (cgltfIdx >= 0 && cgltfIdx < (int)data->sourceData->materials_count)
                {
                    cgltf_material* mat = &data->sourceData->materials[cgltfIdx];
                    if (mat->alpha_mode == cgltf_alpha_mode_mask)
                    {
                        data->materialInfo[matIdx].alphaMode = 1;
                        data->materialInfo[matIdx].alphaCutoff = mat->alpha_cutoff;
                    }
                    else if (mat->alpha_mode == cgltf_alpha_mode_blend)
                    {
                        data->materialInfo[matIdx].alphaMode = 2;
                        data->materialInfo[matIdx].alphaCutoff = 0.0f;
                    }
                    else
                    {
                        data->materialInfo[matIdx].alphaMode = 0;
                        data->materialInfo[matIdx].alphaCutoff = 0.5f;
                    }
                }
            }
            printf("INFO: Parsed %d material alpha modes from GLTF\n", data->materialInfoCount);
        }
    }

    GLBDataUndoSkinnedMeshNodeTransforms(data);
    {
        unsigned int defaultTexId = rlGetTextureIdDefault();
        for (int matIdx = 0; matIdx < data->model.materialCount; matIdx++)
        {
            Texture2D tex = data->model.materials[matIdx].maps[MATERIAL_MAP_ALBEDO].texture;
            bool alreadyLoaded = (tex.id > 0 && tex.id != defaultTexId && (tex.width > 1 || tex.height > 1));
            if (data->sourceData == NULL) continue;
            int cgltfIdx = matIdx - 1;
            if (cgltfIdx < 0 || cgltfIdx >= (int)data->sourceData->materials_count) continue;
            cgltf_material* cgltfMat = &data->sourceData->materials[cgltfIdx];
            if (!cgltfMat->has_pbr_metallic_roughness) continue;
            cgltf_texture* cgltfTex = cgltfMat->pbr_metallic_roughness.base_color_texture.texture;
            if (cgltfTex == NULL) continue;

            // EXT_texture_webp stores its source separately from the standard
            // fallback image. Prefer it when present; required-only WebP files
            // have no cgltfTex->image at all.
            bool hasWebPSource = (cgltfTex->has_webp && cgltfTex->webp_image != NULL);
            cgltf_image* cgltfImg = hasWebPSource ? cgltfTex->webp_image : cgltfTex->image;
            if (cgltfImg == NULL || (alreadyLoaded && !hasWebPSource)) continue;

            const unsigned char* imgData = NULL;
            unsigned char* ownedImgData = NULL;
            int imgSize = 0;
            if (cgltfImg->buffer_view != NULL && cgltfImg->buffer_view->size <= INT_MAX)
            {
                cgltf_buffer_view* bv = cgltfImg->buffer_view;
                imgSize = (int)bv->size;
                if (bv->data != NULL) imgData = (const unsigned char*)bv->data;
                else if (bv->buffer != NULL && bv->buffer->data != NULL &&
                         bv->offset <= bv->buffer->size && bv->size <= bv->buffer->size - bv->offset)
                {
                    imgData = (const unsigned char*)bv->buffer->data + bv->offset;
                }
            }
            else if (cgltfImg->uri != NULL)
            {
                ownedImgData = GLBDataLoadImageURI(filename, cgltfImg->uri, &imgSize);
                imgData = ownedImgData;
            }
            if (imgData == NULL || imgSize <= 0)
            {
                if (ownedImgData != NULL) MemFree(ownedImgData);
                continue;
            }

            int w = 0, h = 0, comp = 0;
            bool isWebP = GLBDataIsWebP(cgltfImg, imgData, imgSize);
            unsigned char* pixels = isWebP
                ? WebPDecodeRGBA(imgData, (size_t)imgSize, &w, &h)
                : stbi_load_from_memory(imgData, imgSize, &w, &h, &comp, 4);
            if (ownedImgData != NULL) MemFree(ownedImgData);
            if (pixels != NULL)
            {
                Image img = { 0 };
                img.data = pixels; img.width = w; img.height = h; img.mipmaps = 1; img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
                Texture2D newTex = LoadTextureFromImage(img);
                GenTextureMipmaps(&newTex);
                SetTextureFilter(newTex, TEXTURE_FILTER_TRILINEAR);
                if (isWebP) WebPFree(pixels);
                else stbi_image_free(pixels);
                if (alreadyLoaded) UnloadTexture(tex);
                data->model.materials[matIdx].maps[MATERIAL_MAP_ALBEDO].texture = newTex;
                printf("INFO: Reloaded GLB texture (material %d) via %s: %dx%d\n", matIdx, isWebP ? "libwebp" : "stb_image", w, h);
            }
            else if (isWebP)
            {
                printf("WARN: Failed to decode WebP texture for material %d\n", matIdx);
            }
        }
    }
    // Enable mipmaps for all textures loaded by raylib's LoadModel (including those already loaded)
    {
        unsigned int defaultTexId = rlGetTextureIdDefault();
        for (int matIdx = 0; matIdx < data->model.materialCount; matIdx++)
        {
            Texture2D tex = data->model.materials[matIdx].maps[MATERIAL_MAP_ALBEDO].texture;
            if (tex.id > 0 && tex.id != defaultTexId && (tex.width > 1 || tex.height > 1))
            {
                GenTextureMipmaps(&tex);
                SetTextureFilter(tex, TEXTURE_FILTER_TRILINEAR);
            }
        }
    }
    if (bc > 0)
    {
        data->topoOrder = (int*)malloc(bc * sizeof(int));
        data->invTopoOrder = (int*)malloc(bc * sizeof(int));
        ComputeTopoOrder(bc, data->model.skeleton.bones, data->topoOrder, data->invTopoOrder);
    }
    printf("INFO: Loaded '%s' - %d bones, %d animations\n", filename, bc, data->animCount);
    return true;
}
