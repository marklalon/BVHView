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

static unsigned char* GLBDataDecodeImageRGBA(const char* modelFilename, const cgltf_image* image,
    int* width, int* height, bool* webpPixels)
{
    if (image == NULL) return NULL;
    const unsigned char* bytes = NULL;
    unsigned char* ownedBytes = NULL;
    int size = 0;
    if (image->buffer_view != NULL && image->buffer_view->size <= INT_MAX)
    {
        const cgltf_buffer_view* view = image->buffer_view;
        size = (int)view->size;
        if (view->data != NULL) bytes = (const unsigned char*)view->data;
        else if (view->buffer != NULL && view->buffer->data != NULL &&
                 view->offset <= view->buffer->size && view->size <= view->buffer->size - view->offset)
            bytes = (const unsigned char*)view->buffer->data + view->offset;
    }
    else if (image->uri != NULL)
    {
        ownedBytes = GLBDataLoadImageURI(modelFilename, image->uri, &size);
        bytes = ownedBytes;
    }
    if (bytes == NULL || size <= 0)
    {
        if (ownedBytes != NULL) UnloadFileData(ownedBytes);
        return NULL;
    }

    int components = 0;
    *webpPixels = GLBDataIsWebP(image, bytes, size);
    unsigned char* pixels = *webpPixels
        ? WebPDecodeRGBA(bytes, (size_t)size, width, height)
        : stbi_load_from_memory(bytes, size, width, height, &components, 4);
    if (ownedBytes != NULL) UnloadFileData(ownedBytes);
    return pixels;
}

static unsigned char* GLBDataDecodeTextureRGBA(const char* modelFilename, const cgltf_texture* texture,
    int* width, int* height, bool* webpPixels)
{
    if (texture == NULL) return NULL;
    if (texture->has_webp && texture->webp_image != NULL)
    {
        unsigned char* pixels = GLBDataDecodeImageRGBA(modelFilename, texture->webp_image, width, height, webpPixels);
        if (pixels != NULL) return pixels;
    }
    return GLBDataDecodeImageRGBA(modelFilename, texture->image, width, height, webpPixels);
}

static int GLBDataTextureWrap(cgltf_wrap_mode wrap)
{
    if (wrap == cgltf_wrap_mode_clamp_to_edge) return RL_TEXTURE_WRAP_CLAMP;
    if (wrap == cgltf_wrap_mode_mirrored_repeat) return RL_TEXTURE_WRAP_MIRROR_REPEAT;
    return RL_TEXTURE_WRAP_REPEAT;
}

static Texture2D GLBDataLoadTextureView(const char* modelFilename, const cgltf_texture_view* view, int channel)
{
    Texture2D texture = { 0 };
    if (view == NULL || view->texture == NULL) return texture;

    int width = 0, height = 0;
    bool webpPixels = false;
    unsigned char* rgba = GLBDataDecodeTextureRGBA(modelFilename, view->texture, &width, &height, &webpPixels);
    if (rgba == NULL || width <= 0 || height <= 0) return texture;

    Image image = { 0 };
    unsigned char* grayscale = NULL;
    if (channel >= 0 && channel < 4)
    {
        grayscale = (unsigned char*)malloc((size_t)width * (size_t)height);
        if (grayscale != NULL)
        {
            for (int i = 0; i < width * height; i++) grayscale[i] = rgba[i * 4 + channel];
            image = (Image){ grayscale, width, height, 1, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE };
        }
    }
    else image = (Image){ rgba, width, height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };

    if (channel < 0 || grayscale != NULL) texture = LoadTextureFromImage(image);
    free(grayscale);
    if (webpPixels) WebPFree(rgba);
    else stbi_image_free(rgba);

    if (texture.id > 0)
    {
        GenTextureMipmaps(&texture);
        SetTextureFilter(texture, TEXTURE_FILTER_TRILINEAR);
        if (view->texture->sampler != NULL)
        {
            rlTextureParameters(texture.id, RL_TEXTURE_WRAP_S, GLBDataTextureWrap(view->texture->sampler->wrap_s));
            rlTextureParameters(texture.id, RL_TEXTURE_WRAP_T, GLBDataTextureWrap(view->texture->sampler->wrap_t));
        }
    }
    return texture;
}

static void GLBDataReplaceMaterialTexture(Material* material, int mapIndex, Texture2D replacement)
{
    if (replacement.id == 0) return;
    unsigned int defaultTexture = rlGetTextureIdDefault();
    Texture2D previous = material->maps[mapIndex].texture;
    if (previous.id > 0 && previous.id != defaultTexture) UnloadTexture(previous);
    material->maps[mapIndex].texture = replacement;
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

static void GLBDataUnloadModelTextures(Model* model)
{
    unsigned int defaultTexture = rlGetTextureIdDefault();
    for (int materialIndex = 0; materialIndex < model->materialCount; materialIndex++)
    {
        Material* material = &model->materials[materialIndex];
        if (material->maps == NULL) continue;
        for (int mapIndex = 0; mapIndex <= MATERIAL_MAP_BRDF; mapIndex++)
        {
            Texture2D texture = material->maps[mapIndex].texture;
            if (texture.id == 0 || texture.id == defaultTexture) continue;
            bool alreadyUnloaded = false;
            for (int previousMaterial = 0; previousMaterial <= materialIndex && !alreadyUnloaded; previousMaterial++)
            {
                Material* previous = &model->materials[previousMaterial];
                int mapLimit = previousMaterial == materialIndex ? mapIndex : MATERIAL_MAP_BRDF + 1;
                if (previous->maps == NULL) continue;
                for (int previousMap = 0; previousMap < mapLimit; previousMap++)
                {
                    if (previous->maps[previousMap].texture.id == texture.id)
                    {
                        alreadyUnloaded = true;
                        break;
                    }
                }
            }
            if (!alreadyUnloaded) UnloadTexture(texture);
        }
    }
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
        GLBDataUnloadModelTextures(&data->model);
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

Matrix GLBMatrixFromCgltf(const cgltf_float* m)
{
    return (Matrix){
        m[0], m[4], m[8], m[12],
        m[1], m[5], m[9], m[13],
        m[2], m[6], m[10], m[14],
        m[3], m[7], m[11], m[15]
    };
}

Matrix GLBTransformToMatrix(Transform transform)
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
        if (mesh->boneWeights == NULL || mesh->boneIndices == NULL || mesh->animVertices == NULL) continue;
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
                if (mesh->normals != NULL && mesh->animNormals != NULL)
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
            if (mesh->normals != NULL && mesh->animNormals != NULL)
                rlUpdateVertexBuffer(mesh->vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_NORMAL], mesh->animNormals, mesh->vertexCount * 3 * sizeof(float), 0);
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

static bool GLBPrimitiveHasSkinAttributes(const cgltf_primitive* primitive)
{
    if (primitive == NULL) return false;
    for (cgltf_size attributeIndex = 0; attributeIndex < primitive->attributes_count; attributeIndex++)
    {
        cgltf_attribute_type type = primitive->attributes[attributeIndex].type;
        if (type == cgltf_attribute_type_joints || type == cgltf_attribute_type_weights) return true;
    }
    return false;
}

static int GLBFindNearestAncestorJointIndex(const cgltf_skin* skin, const cgltf_node* node)
{
    for (const cgltf_node* ancestor = node; ancestor != NULL; ancestor = ancestor->parent)
    {
        int jointIndex = GLBFindSkinJointIndex(skin, ancestor);
        if (jointIndex >= 0) return jointIndex;
    }
    return -1;
}

// raylib bakes each glTF node's bind-pose world transform into its mesh vertices.
// Represent an unskinned mesh in a joint subtree as a 100%-weighted rigid mesh:
// applying that joint's bind-to-current matrix then preserves every static local
// transform between the joint and the mesh while making the whole object follow
// the joint. raylib handles direct joint children itself; this pass also covers
// meshes on joint nodes and meshes below intermediate attachment nodes.
static bool GLBDataBindRigidAttachments(GLBData* data)
{
    if (data->sourceData == NULL || data->sourceSkin == NULL || data->model.meshes == NULL) return true;

    int meshIndex = 0;
    int attachmentCount = 0;
    for (cgltf_size nodeIndex = 0; nodeIndex < data->sourceData->nodes_count; nodeIndex++)
    {
        cgltf_node* node = &data->sourceData->nodes[nodeIndex];
        if (node->mesh == NULL) continue;

        int jointIndex = (node->skin == NULL)
            ? GLBFindNearestAncestorJointIndex(data->sourceSkin, node)
            : -1;
        for (cgltf_size primitiveIndex = 0; primitiveIndex < node->mesh->primitives_count; primitiveIndex++)
        {
            cgltf_primitive* primitive = &node->mesh->primitives[primitiveIndex];
            if (primitive->type != cgltf_primitive_type_triangles) continue;
            if (meshIndex >= data->model.meshCount)
            {
                printf("WARN: GLB mesh/node mapping ended early while binding rigid attachments\n");
                return true;
            }

            Mesh* mesh = &data->model.meshes[meshIndex++];
            if (jointIndex < 0 || GLBPrimitiveHasSkinAttributes(primitive)) continue;
            if (jointIndex > UCHAR_MAX)
            {
                printf("WARN: Rigid GLB attachment joint index %d exceeds the supported limit\n", jointIndex);
                continue;
            }

            if (mesh->boneIndices == NULL)
                mesh->boneIndices = (unsigned char*)calloc((size_t)mesh->vertexCount * 4, sizeof(unsigned char));
            if (mesh->boneWeights == NULL)
                mesh->boneWeights = (float*)calloc((size_t)mesh->vertexCount * 4, sizeof(float));
            if (mesh->animVertices == NULL && mesh->vertices != NULL)
            {
                mesh->animVertices = (float*)malloc((size_t)mesh->vertexCount * 3 * sizeof(float));
                if (mesh->animVertices != NULL)
                    memcpy(mesh->animVertices, mesh->vertices, (size_t)mesh->vertexCount * 3 * sizeof(float));
            }
            if (mesh->animNormals == NULL && mesh->normals != NULL)
            {
                mesh->animNormals = (float*)malloc((size_t)mesh->vertexCount * 3 * sizeof(float));
                if (mesh->animNormals != NULL)
                    memcpy(mesh->animNormals, mesh->normals, (size_t)mesh->vertexCount * 3 * sizeof(float));
            }
            if (mesh->boneIndices == NULL || mesh->boneWeights == NULL || mesh->animVertices == NULL ||
                (mesh->normals != NULL && mesh->animNormals == NULL))
            {
                printf("WARN: Out of memory while binding a rigid GLB attachment\n");
                return false;
            }

            memset(mesh->boneIndices, 0, (size_t)mesh->vertexCount * 4 * sizeof(unsigned char));
            memset(mesh->boneWeights, 0, (size_t)mesh->vertexCount * 4 * sizeof(float));
            for (int vertexIndex = 0; vertexIndex < mesh->vertexCount; vertexIndex++)
            {
                mesh->boneIndices[vertexIndex * 4] = (unsigned char)jointIndex;
                mesh->boneWeights[vertexIndex * 4] = 1.0f;
            }
            mesh->boneCount = data->model.skeleton.boneCount;
            attachmentCount++;
        }
    }

    if (meshIndex != data->model.meshCount)
    {
        printf("WARN: GLB mesh/node mapping mismatch while binding rigid attachments (%d/%d)\n",
            meshIndex, data->model.meshCount);
        return true;
    }
    if (attachmentCount > 0)
        printf("INFO: Bound %d rigid GLB mesh primitives to ancestor joints\n", attachmentCount);
    return true;
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

Transform GLBNodeLocalTransform(const cgltf_node* node)
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

static bool GLBDataLoadSource(GLBData* data, const char* filename)
{
    // Always parse the source GLTF data to obtain skeleton skin, rest pose, and
    // material information — even when there are no animations (static preview).
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

    return true;
}

static bool GLBAnimationInputWasScanned(const cgltf_animation* anim, cgltf_size channelIndex,
    const cgltf_accessor* input, const cgltf_skin* skin)
{
    for (cgltf_size previousIndex = 0; previousIndex < channelIndex; previousIndex++)
    {
        const cgltf_animation_channel* previous = &anim->channels[previousIndex];
        if (skin != NULL && !GLBAnimationTargetsSkinJoint(skin, previous->target_node)) continue;
        if (previous->sampler != NULL && previous->sampler->input == input) return true;
    }
    return false;
}

// The viewer samples cgltf curves directly during playback. Keep only the tiny
// ModelAnimation name records required by the GUI instead of asking raylib to
// bake every animation at 60 fps into keyframeCount * boneCount transforms.
static bool GLBDataInitSourceAnimations(GLBData* data)
{
    if (data->sourceData == NULL || data->sourceSkin == NULL || data->model.skeleton.boneCount <= 0) return true;
    if (data->sourceData->animations_count > INT_MAX) return false;

    data->animCount = (int)data->sourceData->animations_count;
    if (data->animCount <= 0) return true;

    data->animations = (ModelAnimation*)calloc((size_t)data->animCount, sizeof(ModelAnimation));
    data->sourceFrameCounts = (int*)calloc((size_t)data->animCount, sizeof(int));
    data->sourceFrameTimes = (float*)calloc((size_t)data->animCount, sizeof(float));
    data->sourceDurations = (float*)calloc((size_t)data->animCount, sizeof(float));
    if (data->animations == NULL || data->sourceFrameCounts == NULL ||
        data->sourceFrameTimes == NULL || data->sourceDurations == NULL) return false;

    const cgltf_skin* skin = data->sourceSkin;
    for (int a = 0; a < data->animCount; a++)
    {
        cgltf_animation* anim = &data->sourceData->animations[a];
        data->animations[a].boneCount = data->model.skeleton.boneCount;
        // keyframeCount intentionally remains zero: UnloadModelAnimations can
        // safely free these metadata-only records without frame-pose storage.
        if (anim->name != NULL)
            strncpy(data->animations[a].name, anim->name, sizeof(data->animations[a].name) - 1);

        float duration = 0.0f;
        float minDelta = FLT_MAX;
        int maxInputCount = 0;
        bool hasTimeSamples = false;
        for (cgltf_size channelIdx = 0; channelIdx < anim->channels_count; channelIdx++)
        {
            cgltf_animation_channel* channel = &anim->channels[channelIdx];
            cgltf_animation_sampler* sampler = channel->sampler;
            if (!GLBAnimationTargetsSkinJoint(skin, channel->target_node)) continue;
            if (sampler == NULL || sampler->input == NULL || sampler->input->count == 0) continue;
            if (GLBAnimationInputWasScanned(anim, channelIdx, sampler->input, skin)) continue;

            cgltf_size inputCount = sampler->input->count;
            if (inputCount > (cgltf_size)maxInputCount)
                maxInputCount = inputCount > INT_MAX ? INT_MAX : (int)inputCount;
            float prevTime = 0.0f;
            bool hasPrevTime = false;
            for (cgltf_size sampleIdx = 0; sampleIdx < inputCount; sampleIdx++)
            {
                float time = 0.0f;
                if (!cgltf_accessor_read_float(sampler->input, sampleIdx, &time, 1)) break;
                hasTimeSamples = true;
                if (time > duration) duration = time;
                if (hasPrevTime)
                {
                    float delta = time - prevTime;
                    if (delta > 1e-6f && delta < minDelta) minDelta = delta;
                }
                prevTime = time;
                hasPrevTime = true;
            }
        }

        int frameCount = 1;
        float frameTime = data->frameTime;
        if (hasTimeSamples)
        {
            frameCount = maxInputCount > 0 ? maxInputCount : 1;
            if (minDelta < FLT_MAX)
            {
                double estimatedCount = 1.0 + floor((double)duration / (double)minDelta + 0.5);
                frameCount = estimatedCount > INT_MAX ? INT_MAX : (int)estimatedCount;
                frameTime = minDelta;
            }
            else if (frameCount > 1 && duration > 0.0f)
            {
                frameTime = duration / (float)(frameCount - 1);
            }
        }
        if (frameCount < 1) frameCount = 1;
        if (frameTime <= 0.0f) frameTime = data->frameTime;
        data->sourceFrameCounts[a] = frameCount;
        data->sourceFrameTimes[a] = frameTime;
        data->sourceDurations[a] = duration;
    }
    return true;
}

static bool GLBDataLoadBakedAnimationFallback(GLBData* data, const char* filename)
{
    data->animations = LoadModelAnimations(filename, &data->animCount);
    if (data->animCount <= 0) return true;

    data->sourceFrameCounts = (int*)malloc((size_t)data->animCount * sizeof(int));
    data->sourceFrameTimes = (float*)malloc((size_t)data->animCount * sizeof(float));
    data->sourceDurations = (float*)malloc((size_t)data->animCount * sizeof(float));
    if (data->animations == NULL || data->sourceFrameCounts == NULL ||
        data->sourceFrameTimes == NULL || data->sourceDurations == NULL) return false;

    for (int a = 0; a < data->animCount; a++)
    {
        data->sourceFrameCounts[a] = data->animations[a].keyframeCount;
        data->sourceFrameTimes[a] = data->frameTime;
        data->sourceDurations[a] = (data->animations[a].keyframeCount - 1) * data->frameTime;
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
    double loadStartTime = GetTime();
    const char* ext = strrchr(filename, '.');
    if (ext == NULL || (strcmp(ext, ".glb") != 0 && strcmp(ext, ".GLB") != 0 && strcmp(ext, ".gltf") != 0 && strcmp(ext, ".GLTF") != 0))
    { snprintf(errMsg, errMsgSize, "Error: File '%s' is not a .glb/.gltf file", filename); return false; }
    data->model = LoadModel(filename);
    int bc = data->model.skeleton.boneCount;
    // A file with neither bones nor a mesh has nothing to display.
    if (bc == 0 && data->model.meshCount == 0)
    { snprintf(errMsg, errMsgSize, "Error: Model '%s' has no skeleton and no mesh", filename); printf("ERROR: %s\n", errMsg); return false; }
    data->activeAnim = 0;
    data->frameTime = 1.0f / 60.0f;

    if (bc > 0)
    {
        data->sourceRestPose = (Transform*)malloc((size_t)bc * sizeof(Transform));
        data->sourceLocalPose = (Transform*)malloc((size_t)bc * sizeof(Transform));
        data->sourceGlobalPose = (Transform*)malloc((size_t)bc * sizeof(Transform));
        data->sourceRootPose = (Transform*)malloc((size_t)bc * sizeof(Transform));
        if (data->sourceRestPose == NULL || data->sourceLocalPose == NULL ||
            data->sourceGlobalPose == NULL || data->sourceRootPose == NULL)
        {
            GLBDataFree(data);
            snprintf(errMsg, errMsgSize, "Error: Out of memory while loading animation data for '%s'", filename);
            printf("ERROR: %s\n", errMsg);
            return false;
        }
    }

    bool sourceLoaded = GLBDataLoadSource(data, filename);
    if (bc > 0)
    {
        bool animationDataReady = false;
        if (sourceLoaded && data->sourceSkin != NULL)
            animationDataReady = GLBDataInitSourceAnimations(data);
        else
        {
            printf("WARN: Source animation curves unavailable for '%s'; using baked animation fallback\n", filename);
            animationDataReady = GLBDataLoadBakedAnimationFallback(data, filename);
        }
        if (!animationDataReady)
        {
            GLBDataFree(data);
            snprintf(errMsg, errMsgSize, "Error: Out of memory while loading animations for '%s'", filename);
            printf("ERROR: %s\n", errMsg);
            return false;
        }
    }

    // Mesh-only models (no skeleton) are supported for static mesh preview: skip all
    // skeleton/animation setup and fall through to material/texture loading below.
    if (bc == 0)
    {
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

    // Parse per-material alpha info from GLTF materials
    if (data->sourceData != NULL && data->model.materialCount > 0)
    {
        data->materialInfoCount = data->model.materialCount;
        data->materialInfo = (GLBMaterialInfo*)calloc(data->materialInfoCount, sizeof(GLBMaterialInfo));
        if (data->materialInfo != NULL)
        {
            for (int matIdx = 0; matIdx < data->materialInfoCount; matIdx++)
            {
                GLBMaterialInfo* info = &data->materialInfo[matIdx];
                info->alphaMode = 0;
                info->alphaCutoff = 0.5f;
                info->baseColorFactor = (Vector4){ 1.0f, 1.0f, 1.0f, 1.0f };
                info->metallicFactor = 1.0f;
                info->roughnessFactor = 1.0f;
                info->normalScale = 1.0f;
                info->occlusionStrength = 1.0f;
                // raylib material 0 is a default material; GLTF materials start at raylib index 1
                int cgltfIdx = matIdx - 1;
                if (cgltfIdx >= 0 && cgltfIdx < (int)data->sourceData->materials_count)
                {
                    cgltf_material* mat = &data->sourceData->materials[cgltfIdx];
                    info->hasPBR = mat->has_pbr_metallic_roughness;
                    info->doubleSided = mat->double_sided;
                    if (info->hasPBR)
                    {
                        const cgltf_pbr_metallic_roughness* pbr = &mat->pbr_metallic_roughness;
                        info->baseColorFactor = (Vector4){
                            pbr->base_color_factor[0], pbr->base_color_factor[1],
                            pbr->base_color_factor[2], pbr->base_color_factor[3]
                        };
                        info->metallicFactor = pbr->metallic_factor;
                        info->roughnessFactor = pbr->roughness_factor;
                        info->baseColorUV = pbr->base_color_texture.has_transform && pbr->base_color_texture.transform.has_texcoord
                            ? pbr->base_color_texture.transform.texcoord : pbr->base_color_texture.texcoord;
                        info->metallicRoughnessUV = pbr->metallic_roughness_texture.has_transform && pbr->metallic_roughness_texture.transform.has_texcoord
                            ? pbr->metallic_roughness_texture.transform.texcoord : pbr->metallic_roughness_texture.texcoord;
                    }
                    info->normalScale = mat->normal_texture.scale;
                    info->occlusionStrength = mat->occlusion_texture.scale;
                    info->normalUV = mat->normal_texture.has_transform && mat->normal_texture.transform.has_texcoord
                        ? mat->normal_texture.transform.texcoord : mat->normal_texture.texcoord;
                    info->occlusionUV = mat->occlusion_texture.has_transform && mat->occlusion_texture.transform.has_texcoord
                        ? mat->occlusion_texture.transform.texcoord : mat->occlusion_texture.texcoord;
                    info->emissionUV = mat->emissive_texture.has_transform && mat->emissive_texture.transform.has_texcoord
                        ? mat->emissive_texture.transform.texcoord : mat->emissive_texture.texcoord;
                    float emissionStrength = mat->has_emissive_strength ? mat->emissive_strength.emissive_strength : 1.0f;
                    info->emissionFactor = (Vector3){
                        mat->emissive_factor[0] * emissionStrength,
                        mat->emissive_factor[1] * emissionStrength,
                        mat->emissive_factor[2] * emissionStrength
                    };
                    if (mat->alpha_mode == cgltf_alpha_mode_mask)
                    {
                        info->alphaMode = 1;
                        info->alphaCutoff = mat->alpha_cutoff;
                    }
                    else if (mat->alpha_mode == cgltf_alpha_mode_blend)
                    {
                        info->alphaMode = 2;
                        info->alphaCutoff = 0.0f;
                    }
                }
            }
            printf("INFO: Parsed %d GLTF material descriptions\n", data->materialInfoCount);
        }
    }

    if (!GLBDataBindRigidAttachments(data))
    {
        GLBDataFree(data);
        snprintf(errMsg, errMsgSize, "Error: Failed to bind rigid attachments in '%s'", filename);
        printf("ERROR: %s\n", errMsg);
        return false;
    }
    GLBDataUndoSkinnedMeshNodeTransforms(data);
    // Reload all core PBR maps through the local decoder. This keeps embedded,
    // external and EXT_texture_webp images on the same material path.
    {
        if (data->sourceData != NULL)
        {
            int materialCount = (int)data->sourceData->materials_count;
            for (int cgltfIdx = 0; cgltfIdx < materialCount; cgltfIdx++)
            {
                int matIdx = cgltfIdx + 1;
                if (matIdx >= data->model.materialCount) break;
                cgltf_material* source = &data->sourceData->materials[cgltfIdx];
                if (!source->has_pbr_metallic_roughness) continue;
                Material* target = &data->model.materials[matIdx];
                const cgltf_pbr_metallic_roughness* pbr = &source->pbr_metallic_roughness;

                GLBDataReplaceMaterialTexture(target, MATERIAL_MAP_ALBEDO,
                    GLBDataLoadTextureView(filename, &pbr->base_color_texture, -1));
                // glTF packs metallic in B, roughness in G of one texture.
                // The shader reads metallic from texture1.r and roughness from texture3.r,
                // so load the same combined texture into both slots.
                GLBDataReplaceMaterialTexture(target, MATERIAL_MAP_METALNESS,
                    GLBDataLoadTextureView(filename, &pbr->metallic_roughness_texture, -1));
                GLBDataReplaceMaterialTexture(target, MATERIAL_MAP_ROUGHNESS,
                    GLBDataLoadTextureView(filename, &pbr->metallic_roughness_texture, -1));
                GLBDataReplaceMaterialTexture(target, MATERIAL_MAP_NORMAL,
                    GLBDataLoadTextureView(filename, &source->normal_texture, -1));
                GLBDataReplaceMaterialTexture(target, MATERIAL_MAP_OCCLUSION,
                    GLBDataLoadTextureView(filename, &source->occlusion_texture, -1));
                GLBDataReplaceMaterialTexture(target, MATERIAL_MAP_EMISSION,
                    GLBDataLoadTextureView(filename, &source->emissive_texture, -1));
            }
        }
    }
    // Enable mipmaps for any maps retained from raylib's loader.
    {
        unsigned int defaultTexId = rlGetTextureIdDefault();
        for (int matIdx = 0; matIdx < data->model.materialCount; matIdx++)
        {
            for (int mapIdx = MATERIAL_MAP_ALBEDO; mapIdx <= MATERIAL_MAP_EMISSION; mapIdx++)
            {
                Texture2D* texture = &data->model.materials[matIdx].maps[mapIdx].texture;
                if (texture->id > 0 && texture->id != defaultTexId &&
                    (texture->width > 1 || texture->height > 1))
                {
                    if (texture->mipmaps <= 1) GenTextureMipmaps(texture);
                    SetTextureFilter(*texture, TEXTURE_FILTER_TRILINEAR);
                }
            }
        }
    }
    if (bc > 0)
    {
        data->topoOrder = (int*)malloc(bc * sizeof(int));
        data->invTopoOrder = (int*)malloc(bc * sizeof(int));
        ComputeTopoOrder(bc, data->model.skeleton.bones, data->topoOrder, data->invTopoOrder);
    }
    printf("INFO: Loaded '%s' - %d bones, %d animations in %.3f s\n",
        filename, bc, data->animCount, GetTime() - loadStartTime);
    fflush(stdout);
    return true;
}
