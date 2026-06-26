#include "raymath.h"
#include "transform_data.h"
#include "bvh_data.h"
#include "glb_data.h"

void TransformDataInit(TransformData* data)
{
    data->jointCount = 0;
    data->parents = NULL;
    data->endSite = NULL;
    data->localPositions = NULL;
    data->localRotations = NULL;
    data->globalPositions = NULL;
    data->globalRotations = NULL;
}

void TransformDataResize(TransformData* data, BVHData* bvh)
{
    data->jointCount = bvh->jointCount;
    data->parents = realloc(data->parents, data->jointCount * sizeof(int));
    data->endSite = realloc(data->endSite, data->jointCount * sizeof(bool));
    data->localPositions = realloc(data->localPositions, data->jointCount * sizeof(Vector3));
    data->localRotations = realloc(data->localRotations, data->jointCount * sizeof(Quaternion));
    data->globalPositions = realloc(data->globalPositions, data->jointCount * sizeof(Vector3));
    data->globalRotations = realloc(data->globalRotations, data->jointCount * sizeof(Quaternion));
    for (int i = 0; i < data->jointCount; i++) {
        data->endSite[i] = bvh->joints[i].endSite;
        data->parents[i] = bvh->joints[i].parent;
    }
}

void TransformDataResizeSimple(TransformData* data, int jointCount, int* parents, bool* endSite)
{
    data->jointCount = jointCount;
    data->parents = realloc(data->parents, jointCount * sizeof(int));
    data->endSite = realloc(data->endSite, jointCount * sizeof(bool));
    data->localPositions = realloc(data->localPositions, jointCount * sizeof(Vector3));
    data->localRotations = realloc(data->localRotations, jointCount * sizeof(Quaternion));
    data->globalPositions = realloc(data->globalPositions, jointCount * sizeof(Vector3));
    data->globalRotations = realloc(data->globalRotations, jointCount * sizeof(Quaternion));
    for (int i = 0; i < jointCount; i++) {
        data->parents[i] = parents[i];
        data->endSite[i] = endSite[i];
    }
}

void TransformDataFree(TransformData* data)
{
    free(data->parents);
    free(data->endSite);
    free(data->localPositions);
    free(data->localRotations);
    free(data->globalPositions);
    free(data->globalRotations);
    data->jointCount = 0;
    data->parents = NULL;
    data->endSite = NULL;
    data->localPositions = NULL;
    data->localRotations = NULL;
    data->globalPositions = NULL;
    data->globalRotations = NULL;
}

void TransformDataSampleFrame(TransformData* data, BVHData* bvh, int frame, float scale)
{
    // No motion data: use joint offsets as rest pose
    if (bvh->frameCount == 0)
    {
        for (int i = 0; i < bvh->jointCount; i++)
        {
            data->localPositions[i] = Vector3Scale(bvh->joints[i].offset, scale);
            data->localRotations[i] = QuaternionIdentity();
        }
        return;
    }
    frame = frame < 0 ? 0 : frame >= bvh->frameCount ? bvh->frameCount - 1 : frame;
    int offset = 0;
    for (int i = 0; i < bvh->jointCount; i++) {
        Vector3 position = Vector3Scale(bvh->joints[i].offset, scale);
        Quaternion rotation = QuaternionIdentity();
        for (int c = 0; c < bvh->joints[i].channelCount; c++) {
            switch (bvh->joints[i].channels[c]) {
                case CHANNEL_X_POSITION: position.x = scale * bvh->motionData[frame * bvh->channelCount + offset]; offset++; break;
                case CHANNEL_Y_POSITION: position.y = scale * bvh->motionData[frame * bvh->channelCount + offset]; offset++; break;
                case CHANNEL_Z_POSITION: position.z = scale * bvh->motionData[frame * bvh->channelCount + offset]; offset++; break;
                case CHANNEL_X_ROTATION: rotation = QuaternionMultiply(rotation, QuaternionFromAxisAngle((Vector3){1,0,0}, DEG2RAD * bvh->motionData[frame * bvh->channelCount + offset])); offset++; break;
                case CHANNEL_Y_ROTATION: rotation = QuaternionMultiply(rotation, QuaternionFromAxisAngle((Vector3){0,1,0}, DEG2RAD * bvh->motionData[frame * bvh->channelCount + offset])); offset++; break;
                case CHANNEL_Z_ROTATION: rotation = QuaternionMultiply(rotation, QuaternionFromAxisAngle((Vector3){0,0,1}, DEG2RAD * bvh->motionData[frame * bvh->channelCount + offset])); offset++; break;
            }
        }
        data->localPositions[i] = position;
        data->localRotations[i] = rotation;
    }
    assert(offset == bvh->channelCount);
}

void TransformDataSampleFrameNearest(TransformData* data, BVHData* bvh, float time, float scale)
{
    int frame = ClampInt((int)(time / bvh->frameTime + 0.5f), 0, bvh->frameCount - 1);
    TransformDataSampleFrame(data, bvh, frame, scale);
}

void TransformDataSampleFrameLinear(TransformData* data, TransformData* tmp0, TransformData* tmp1, BVHData* bvh, float time, float scale)
{
    const float alpha = fmod(time / bvh->frameTime, 1.0f);
    int frame0 = ClampInt((int)(time / bvh->frameTime) + 0, 0, bvh->frameCount - 1);
    int frame1 = ClampInt((int)(time / bvh->frameTime) + 1, 0, bvh->frameCount - 1);
    TransformDataSampleFrame(tmp0, bvh, frame0, scale);
    TransformDataSampleFrame(tmp1, bvh, frame1, scale);
    for (int i = 0; i < data->jointCount; i++) {
        data->localPositions[i] = Vector3Lerp(tmp0->localPositions[i], tmp1->localPositions[i], alpha);
        data->localRotations[i] = QuaternionSlerp(tmp0->localRotations[i], tmp1->localRotations[i], alpha);
    }
}

void TransformDataSampleFrameCubic(TransformData* data, TransformData* tmp0, TransformData* tmp1, TransformData* tmp2, TransformData* tmp3, BVHData* bvh, float time, float scale)
{
    const float alpha = fmod(time / bvh->frameTime, 1.0f);
    int frame0 = ClampInt((int)(time / bvh->frameTime) - 1, 0, bvh->frameCount - 1);
    int frame1 = ClampInt((int)(time / bvh->frameTime) + 0, 0, bvh->frameCount - 1);
    int frame2 = ClampInt((int)(time / bvh->frameTime) + 1, 0, bvh->frameCount - 1);
    int frame3 = ClampInt((int)(time / bvh->frameTime) + 2, 0, bvh->frameCount - 1);
    TransformDataSampleFrame(tmp0, bvh, frame0, scale);
    TransformDataSampleFrame(tmp1, bvh, frame1, scale);
    TransformDataSampleFrame(tmp2, bvh, frame2, scale);
    TransformDataSampleFrame(tmp3, bvh, frame3, scale);
    for (int i = 0; i < data->jointCount; i++) {
        data->localPositions[i] = Vector3InterpolateCubic(tmp0->localPositions[i], tmp1->localPositions[i], tmp2->localPositions[i], tmp3->localPositions[i], alpha);
        data->localRotations[i] = QuaternionInterpolateCubic(tmp0->localRotations[i], tmp1->localRotations[i], tmp2->localRotations[i], tmp3->localRotations[i], alpha);
    }
}

void TransformDataForwardKinematics(TransformData* data)
{
    for (int i = 0; i < data->jointCount; i++) {
        int p = data->parents[i];
        assert(p <= i);
        if (p == -1) {
            data->globalPositions[i] = data->localPositions[i];
            data->globalRotations[i] = data->localRotations[i];
        } else {
            data->globalPositions[i] = Vector3Add(Vector3RotateByQuaternion(data->localPositions[i], data->globalRotations[p]), data->globalPositions[p]);
            data->globalRotations[i] = QuaternionMultiply(data->globalRotations[p], data->localRotations[i]);
        }
    }
}

float TransformDataGetMaxHeight(TransformData* data)
{
    float height = 1e-8f;
    for (int j = 0; j < data->jointCount; j++) {
        height = Max(height, data->globalPositions[j].y);
    }
    return height;
}

// GLB exact sampling helpers (declared in glb_data.h, used here)
// These are now non-static and declared in glb_data.h

// ---- FK and pose-conversion helpers ----

static void TransformDataComputeGLBSourceFK(GLBData* glb, int boneCount)
{
    for (int sortedIdx = 0; sortedIdx < boneCount; sortedIdx++)
    {
        int boneIdx = glb->topoOrder[sortedIdx];
        int parentIdx = glb->model.skeleton.bones[boneIdx].parent;
        Transform local = glb->sourceLocalPose[boneIdx];
        if (parentIdx == -1)
        {
            Transform root = glb->sourceRootPose[boneIdx];
            glb->sourceGlobalPose[boneIdx].rotation = QuaternionNormalize(QuaternionMultiply(root.rotation, local.rotation));
            glb->sourceGlobalPose[boneIdx].scale = Vector3Multiply(local.scale, root.scale);
            glb->sourceGlobalPose[boneIdx].translation = Vector3Multiply(local.translation, root.scale);
            glb->sourceGlobalPose[boneIdx].translation = Vector3RotateByQuaternion(glb->sourceGlobalPose[boneIdx].translation, root.rotation);
            glb->sourceGlobalPose[boneIdx].translation = Vector3Add(glb->sourceGlobalPose[boneIdx].translation, root.translation);
        }
        else
        {
            Transform parent = glb->sourceGlobalPose[parentIdx];
            glb->sourceGlobalPose[boneIdx].rotation = QuaternionNormalize(QuaternionMultiply(parent.rotation, local.rotation));
            glb->sourceGlobalPose[boneIdx].scale = Vector3Multiply(local.scale, parent.scale);
            glb->sourceGlobalPose[boneIdx].translation = Vector3Multiply(local.translation, parent.scale);
            glb->sourceGlobalPose[boneIdx].translation = Vector3RotateByQuaternion(glb->sourceGlobalPose[boneIdx].translation, parent.rotation);
            glb->sourceGlobalPose[boneIdx].translation = Vector3Add(glb->sourceGlobalPose[boneIdx].translation, parent.translation);
        }
    }
}

static void TransformDataGlobalPoseToLocal(TransformData* data, int boneCount, const int* topoOrder,
                                            const Transform* globalPose, float scale)
{
    int n = data->jointCount;
    if (n > boneCount) n = boneCount;
    for (int i = 0; i < n; i++)
    {
        int orig = topoOrder[i];
        Vector3 gPos = globalPose[orig].translation;
        Quaternion gRot = globalPose[orig].rotation;
        int parent = data->parents[i];
        if (parent == -1)
        {
            data->localPositions[i] = Vector3Scale(gPos, scale);
            data->localRotations[i] = gRot;
        }
        else
        {
            int origParent = topoOrder[parent];
            Vector3 pPos = globalPose[origParent].translation;
            Quaternion pRot = globalPose[origParent].rotation;
            Quaternion invPRot = QuaternionInvert(pRot);
            Vector3 delta = Vector3Subtract(gPos, pPos);
            data->localPositions[i] = Vector3Scale(Vector3RotateByQuaternion(delta, invPRot), scale);
            data->localRotations[i] = QuaternionNormalize(QuaternionMultiply(invPRot, gRot));
        }
    }
}

static bool TransformDataSampleRestPoseGLBExact(TransformData* data, GLBData* glb, float scale)
{
    if (glb->sourceRestPose == NULL || glb->sourceGlobalPose == NULL || glb->sourceLocalPose == NULL || glb->topoOrder == NULL) return false;
    int boneCount = glb->model.skeleton.boneCount;
    for (int boneIdx = 0; boneIdx < boneCount; boneIdx++)
        glb->sourceLocalPose[boneIdx] = glb->sourceRestPose[boneIdx];
    TransformDataComputeGLBSourceFK(glb, boneCount);
    GLBDataUpdateModelPose(glb, glb->sourceGlobalPose);
    TransformDataGlobalPoseToLocal(data, boneCount, glb->topoOrder, glb->sourceGlobalPose, scale);
    return true;
}

void TransformDataSampleRestPoseGLB(TransformData* data, GLBData* glb, float scale)
{
    if (TransformDataSampleRestPoseGLBExact(data, glb, scale)) return;

    // Fallback: use the model's current bind-pose directly
    if (glb->model.currentPose == NULL || glb->model.boneMatrices == NULL || glb->topoOrder == NULL) return;
    TransformDataGlobalPoseToLocal(data, glb->model.skeleton.boneCount, glb->topoOrder, glb->model.currentPose, scale);
}

static bool TransformDataSampleFrameGLBExact(TransformData* data, GLBData* glb, float time, float scale)
{
    // No animations: use rest pose directly for static preview
    if (glb->animCount == 0)
    {
        return TransformDataSampleRestPoseGLBExact(data, glb, scale);
    }

    if (glb->sourceData == NULL || glb->sourceSkin == NULL) return false;
    if (glb->sourceRestPose == NULL || glb->sourceLocalPose == NULL || glb->sourceGlobalPose == NULL || glb->sourceRootPose == NULL) return false;
    if (glb->topoOrder == NULL) return false;
    int animIdx = glb->activeAnim;
    if (animIdx < 0 || animIdx >= (int)glb->sourceData->animations_count) return false;
    cgltf_animation* anim = &glb->sourceData->animations[animIdx];
    float duration = GLBDataGetSourceDuration(glb, animIdx);
    float timeClamped = Clamp(time, 0.0f, duration > 0.0f ? duration : time);
    int boneCount = glb->model.skeleton.boneCount;
    for (int boneIdx = 0; boneIdx < boneCount; boneIdx++)
        glb->sourceLocalPose[boneIdx] = glb->sourceRestPose[boneIdx];
    for (cgltf_size channelIdx = 0; channelIdx < anim->channels_count; channelIdx++)
    {
        cgltf_animation_channel* channel = &anim->channels[channelIdx];
        if (channel->sampler == NULL) return false;
        int boneIndex = GLBFindSkinJointIndex(glb->sourceSkin, channel->target_node);
        if (boneIndex < 0 || boneIndex >= boneCount) continue;
        switch (channel->target_path)
        {
            case cgltf_animation_path_type_translation:
                if (!GLBGetPoseAtTime(channel->sampler->interpolation, channel->sampler->input, channel->sampler->output, timeClamped, &glb->sourceLocalPose[boneIndex].translation)) return false;
                break;
            case cgltf_animation_path_type_rotation:
                if (!GLBGetPoseAtTime(channel->sampler->interpolation, channel->sampler->input, channel->sampler->output, timeClamped, &glb->sourceLocalPose[boneIndex].rotation)) return false;
                glb->sourceLocalPose[boneIndex].rotation = QuaternionNormalize(glb->sourceLocalPose[boneIndex].rotation);
                break;
            case cgltf_animation_path_type_scale:
                if (!GLBGetPoseAtTime(channel->sampler->interpolation, channel->sampler->input, channel->sampler->output, timeClamped, &glb->sourceLocalPose[boneIndex].scale)) return false;
                break;
            default: break;
        }
    }

    // ---- Phase 1: accumulate animated overrides for non-skin-joint nodes
    // that are parents of root bones. These carry root motion (e.g. Y
    // translation on an "Armature" node above the skin skeleton).
    // We must accumulate all channels before computing the parent world,
    // because a parent may have separate translation + rotation channels.
    Transform* parentLocals = (Transform*)calloc((size_t)boneCount, sizeof(Transform));
    bool* parentHasAnim = (bool*)calloc((size_t)boneCount, sizeof(bool));
    if (parentLocals == NULL || parentHasAnim == NULL)
    {
        free(parentLocals);
        free(parentHasAnim);
        return false;
    }
    for (int boneIdx = 0; boneIdx < boneCount; boneIdx++)
    {
        if (glb->model.skeleton.bones[boneIdx].parent != -1) continue;
        if (boneIdx >= (int)glb->sourceSkin->joints_count) continue;
        const cgltf_node* skinJoint = glb->sourceSkin->joints[boneIdx];
        if (skinJoint == NULL || skinJoint->parent == NULL) continue;
        parentLocals[boneIdx] = GLBNodeLocalTransform(skinJoint->parent);
    }
    for (cgltf_size channelIdx = 0; channelIdx < anim->channels_count; channelIdx++)
    {
        cgltf_animation_channel* channel = &anim->channels[channelIdx];
        if (channel->sampler == NULL) { free(parentLocals); free(parentHasAnim); return false; }
        if (GLBFindSkinJointIndex(glb->sourceSkin, channel->target_node) >= 0) continue;
        for (int boneIdx = 0; boneIdx < boneCount; boneIdx++)
        {
            if (glb->model.skeleton.bones[boneIdx].parent != -1) continue;
            if (boneIdx >= (int)glb->sourceSkin->joints_count) continue;
            const cgltf_node* skinJoint = glb->sourceSkin->joints[boneIdx];
            if (skinJoint == NULL || skinJoint->parent != channel->target_node) continue;
            parentHasAnim[boneIdx] = true;
            switch (channel->target_path)
            {
                case cgltf_animation_path_type_translation:
                    if (!GLBGetPoseAtTime(channel->sampler->interpolation, channel->sampler->input, channel->sampler->output, timeClamped, &parentLocals[boneIdx].translation)) { free(parentLocals); free(parentHasAnim); return false; }
                    break;
                case cgltf_animation_path_type_rotation:
                    if (!GLBGetPoseAtTime(channel->sampler->interpolation, channel->sampler->input, channel->sampler->output, timeClamped, &parentLocals[boneIdx].rotation)) { free(parentLocals); free(parentHasAnim); return false; }
                    parentLocals[boneIdx].rotation = QuaternionNormalize(parentLocals[boneIdx].rotation);
                    break;
                case cgltf_animation_path_type_scale:
                    if (!GLBGetPoseAtTime(channel->sampler->interpolation, channel->sampler->input, channel->sampler->output, timeClamped, &parentLocals[boneIdx].scale)) { free(parentLocals); free(parentHasAnim); return false; }
                    break;
                default: break;
            }
            break;
        }
    }

    // ---- Phase 2: compute parent world transforms from accumulated locals.
    for (int boneIdx = 0; boneIdx < boneCount; boneIdx++)
    {
        if (!parentHasAnim[boneIdx]) continue;
        const cgltf_node* skinJoint = glb->sourceSkin->joints[boneIdx];
        const cgltf_node* parentNode = skinJoint->parent;
        Matrix grandparentWorld = MatrixIdentity();
        if (parentNode->parent != NULL)
        {
            cgltf_float gpMatrix[16] = { 0 };
            cgltf_node_transform_world(parentNode->parent, gpMatrix);
            grandparentWorld = GLBMatrixFromCgltf(gpMatrix);
        }
        Matrix parentLocalMatrix = GLBTransformToMatrix(parentLocals[boneIdx]);
        Matrix parentWorldMatrix = MatrixMultiply(grandparentWorld, parentLocalMatrix);
        MatrixDecompose(parentWorldMatrix, &glb->sourceRootPose[boneIdx].translation, &glb->sourceRootPose[boneIdx].rotation, &glb->sourceRootPose[boneIdx].scale);
        glb->sourceRootPose[boneIdx].rotation = QuaternionNormalize(glb->sourceRootPose[boneIdx].rotation);
    }
    free(parentLocals);
    free(parentHasAnim);

    TransformDataComputeGLBSourceFK(glb, boneCount);
    GLBDataUpdateModelPose(glb, glb->sourceGlobalPose);
    TransformDataGlobalPoseToLocal(data, boneCount, glb->topoOrder, glb->sourceGlobalPose, scale);
    return true;
}

void TransformDataSampleFrameGLB(TransformData* data, GLBData* glb, float time, float scale)
{
    if (TransformDataSampleFrameGLBExact(data, glb, time, scale)) return;

    // No animations: use model's current bind-pose (rest pose) directly
    if (glb->animCount == 0)
    {
        if (glb->model.currentPose == NULL || glb->model.boneMatrices == NULL || glb->topoOrder == NULL) return;
        TransformDataGlobalPoseToLocal(data, glb->model.skeleton.boneCount, glb->topoOrder, glb->model.currentPose, scale);
        return;
    }

    if (glb->model.currentPose == NULL) return;
    if (glb->model.boneMatrices == NULL) return;
    if (glb->topoOrder == NULL) return;
    int animIdx = glb->activeAnim;
    ModelAnimation anim = glb->animations[animIdx];
    float frame = time / glb->frameTime;
    float frameClamped = frame;
    if (frameClamped < 0.0f) frameClamped = 0.0f;
    if (frameClamped > (float)(anim.keyframeCount - 1)) frameClamped = (float)(anim.keyframeCount - 1);
    UpdateModelAnimation(glb->model, anim, frameClamped);
    TransformDataGlobalPoseToLocal(data, glb->model.skeleton.boneCount, glb->topoOrder, glb->model.currentPose, scale);
}
