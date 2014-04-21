/******************************************************************************
 * Spine Runtimes Software License
 * Version 2
 * 
 * Copyright (c) 2013, Esoteric Software
 * All rights reserved.
 * 
 * You are granted a perpetual, non-exclusive, non-sublicensable and
 * non-transferable license to install, execute and perform the Spine Runtimes
 * Software (the "Software") solely for internal use. Without the written
 * permission of Esoteric Software, you may not (a) modify, translate, adapt or
 * otherwise create derivative works, improvements of the Software or develop
 * new applications using the Software or (b) remove, delete, alter or obscure
 * any trademarks or any copyright, trademark, patent or other intellectual
 * property or proprietary rights notices on or in the Software, including
 * any copy thereof. Redistributions in binary or source form must include
 * this license and terms. THIS SOFTWARE IS PROVIDED BY ESOTERIC SOFTWARE
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL ESOTERIC SOFTARE BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *****************************************************************************/

#include <spine/CCSkeleton.h>
#include <spine/spine-cocos2dx.h>
#include <algorithm>

USING_NS_CC;
using std::min;
using std::max;

namespace spine {

Skeleton* Skeleton::createWithData (spSkeletonData* skeletonData, bool isOwnsSkeletonData) {
	Skeleton* node = new Skeleton(skeletonData, isOwnsSkeletonData);
	node->autorelease();
	return node;
}

Skeleton* Skeleton::createWithFile (const char* skeletonDataFile, spAtlas* atlas, float scale) {
	Skeleton* node = new Skeleton(skeletonDataFile, atlas, scale);
	node->autorelease();
	return node;
}

Skeleton* Skeleton::createWithFile (const char* skeletonDataFile, const char* atlasFile, float scale) {
	Skeleton* node = new Skeleton(skeletonDataFile, atlasFile, scale);
	node->autorelease();
	return node;
}

void Skeleton::initialize () {
	atlas = 0;
	debugSlots = false;
	debugBones = false;
	timeScale = 1;

    blendFunc.src = BlendFunc::ALPHA_PREMULTIPLIED.src;
    blendFunc.dst = BlendFunc::ALPHA_PREMULTIPLIED.dst;
    
	setOpacityModifyRGB(true);

    setShaderProgram(ShaderCache::getInstance()->getProgram(GLProgram::SHADER_NAME_POSITION_TEXTURE_COLOR));
}

void Skeleton::setSkeletonData (spSkeletonData *skeletonData, bool isOwnsSkeletonData) {
	skeleton = spSkeleton_create(skeletonData);
	rootBone = skeleton->bones[0];
	this->ownsSkeletonData = isOwnsSkeletonData;
}

Skeleton::Skeleton () {
	initialize();
}

Skeleton::Skeleton (spSkeletonData *skeletonData, bool isOwnsSkeletonData) {
	initialize();

	setSkeletonData(skeletonData, isOwnsSkeletonData);
}

Skeleton::Skeleton (const char* skeletonDataFile, spAtlas* aAtlas, float scale) {
	initialize();

	spSkeletonJson* json = spSkeletonJson_create(aAtlas);
	json->scale = scale == 0 ? (1 / Director::getInstance()->getContentScaleFactor()) : scale;
	spSkeletonData* skeletonData = spSkeletonJson_readSkeletonDataFile(json, skeletonDataFile);
	CCAssert(skeletonData, json->error ? json->error : "Error reading skeleton data.");
	spSkeletonJson_dispose(json);

	setSkeletonData(skeletonData, true);
}

Skeleton::Skeleton (const char* skeletonDataFile, const char* atlasFile, float scale) {
	initialize();

	atlas = spAtlas_readAtlasFile(atlasFile);
	CCAssert(atlas, "Error reading atlas file.");

	spSkeletonJson* json = spSkeletonJson_create(atlas);
	json->scale = scale == 0 ? (1 / Director::getInstance()->getContentScaleFactor()) : scale;
    spSkeletonData* skeletonData = spSkeletonJson_readSkeletonDataFile(json, skeletonDataFile);
    
	CCAssert(skeletonData, json->error ? json->error : "Error reading skeleton data file.");
	spSkeletonJson_dispose(json);

	setSkeletonData(skeletonData, true);
}

Skeleton::~Skeleton () {
	if (ownsSkeletonData) spSkeletonData_dispose(skeleton->data);
	if (atlas) spAtlas_dispose(atlas);
	spSkeleton_dispose(skeleton);
}

void Skeleton::update (float deltaTime) {
	spSkeleton_update(skeleton, deltaTime * timeScale);
}

void Skeleton::draw(cocos2d::Renderer *renderer, const kmMat4 &transform, bool transformUpdated)
{
    _customCommand.init(_globalZOrder);
    _customCommand.func = CC_CALLBACK_0(Skeleton::onDraw, this, transform, transformUpdated);
    renderer->addCommand(&_customCommand);
}
    
void resizeUntilLimit(TriangleTextureAtlas * atlas, int limit)
{
    // resizing logic
    while (atlas->getCapacity() <= limit) {
        atlas->drawTriangles();
        atlas->removeAllTriangles();
        if (!atlas->resizeCapacity(atlas->getCapacity() * 2)) return;
    }
}
    
void Skeleton::onDraw(const kmMat4 &transform, bool transformUpdated)
{
    getShaderProgram()->use();
    getShaderProgram()->setUniformsForBuiltins(transform);

    GL::blendFunc(blendFunc.src, blendFunc.dst);
	Color3B color = getColor();
	skeleton->r = color.r / (float)255;
	skeleton->g = color.g / (float)255;
	skeleton->b = color.b / (float)255;
	skeleton->a = getOpacity() / (float)255;
	if (premultipliedAlpha) {
		skeleton->r *= skeleton->a;
		skeleton->g *= skeleton->a;
		skeleton->b *= skeleton->a;
	}

	bool additive = false;
	bool isPremultipliedAlpha = false;
    setFittedBlendingFunc(false, false);
    TriangleTextureAtlas* textureAtlas = nullptr;
    
    V3F_C4B_T2F_Triangle triangle;
    triangle.a.vertices.z = 0;
    triangle.b.vertices.z = 0;
    triangle.c.vertices.z = 0;
    int quadTriangleIds[6] = {
            0, 1, 2,
            2, 3, 0
        };
	
    for (int i = 0, n = skeleton->slotCount; i < n; i++) {
		spSlot* slot = skeleton->drawOrder[i];
        if (!slot->attachment) continue;
        TriangleTextureAtlas* nextTextureAtlas = nullptr;
        if (slot->attachment->type == ATTACHMENT_REGION)
        {
            spRegionAttachment* attachment = (spRegionAttachment*)slot->attachment;
            nextTextureAtlas = getTextureAtlas(attachment);
        }
        else if (slot->attachment->type == ATTACHMENT_MESH)
        {
            spMeshAttachment * attachment = (spMeshAttachment*)slot->attachment;
            nextTextureAtlas = getTextureAtlas(attachment);
        }
        else
        {
            continue;
        }

        if (!nextTextureAtlas || !nextTextureAtlas->getTexture()) continue;
        
        // If different atlas then draw existsing one
        if (nextTextureAtlas != textureAtlas)
        {
            if (textureAtlas) {
                drawAndClear(textureAtlas);
            }
            textureAtlas = nullptr;
        }
        
        bool wonderAdditive = slot->data->additiveBlending;
        bool wonderPremultipliedAlpha = nextTextureAtlas->getTexture()->hasPremultipliedAlpha();
        // If current blending mode is not same then change mode
        if ( additive != wonderAdditive || isPremultipliedAlpha != wonderPremultipliedAlpha) {
            if (textureAtlas) {
                drawAndClear(textureAtlas);
            }
            additive = wonderAdditive;
            isPremultipliedAlpha = wonderPremultipliedAlpha;
            setFittedBlendingFunc(isPremultipliedAlpha, additive);
        }
        
        textureAtlas = nextTextureAtlas;

        if (slot->attachment->type == ATTACHMENT_REGION)
        {
            spRegionAttachment* attachment = (spRegionAttachment*)slot->attachment;

            resizeUntilLimit(textureAtlas, textureAtlas->getTotalTriangles());
            
            V3F_C4B_T2F vertices[4];
            float verticesPos[8];
            unsigned int startVerticle = textureAtlas->getTotalVertices();

            spRegionAttachment_computeWorldVertices(attachment, slot->skeleton->x, slot->skeleton->y, slot->bone, verticesPos);

            spRegionAttachment_updateVertices(attachment, slot, vertices, premultipliedAlpha, verticesPos);
            textureAtlas->updateVertices(vertices, textureAtlas->getTotalVertices(), 4);
            
            textureAtlas->setCurrentTriangles(2 + textureAtlas->getCurrentTriangles());
            textureAtlas->updateTrianglesIndices(quadTriangleIds, 6, startVerticle);
        }
        else if (slot->attachment->type == ATTACHMENT_MESH)
        {
            spMeshAttachment * attachment = (spMeshAttachment*)slot->attachment;

            unsigned int verticesCount = attachment->verticesLength / 2;
            unsigned int trianglesCount = attachment->trianglesIndicesLength / 3;
            
            // Resizing when buffer is too small.
            resizeUntilLimit(textureAtlas, trianglesCount + textureAtlas->getTotalTriangles());
            
            spMeshAttachment_computeWorldVertices(attachment, slot->skeleton->x, slot->skeleton->y, slot->bone);
            
            unsigned int initVertexCount = textureAtlas->getTotalVertices();
            V3F_C4B_T2F * vertices = (V3F_C4B_T2F *)alloca(sizeof(V3F_C4B_T2F) * attachment->verticesLength / 2);
            
            spMeshAttachment_updateVertices(attachment, slot, vertices, premultipliedAlpha);
            textureAtlas->updateVertices(vertices, textureAtlas->getTotalVertices(), verticesCount);

            textureAtlas->setCurrentTriangles(trianglesCount + textureAtlas->getCurrentTriangles());
            textureAtlas->updateTrianglesIndices(attachment->trianglesIndices, attachment->trianglesIndicesLength, initVertexCount);
        }

	}

	if (textureAtlas) {
        drawAndClear(textureAtlas);
	}
    

    if(debugBones || debugSlots) {
        kmGLPushMatrix();
        kmGLLoadMatrix(&transform);

        if (debugSlots) {
            // Slots.
            DrawPrimitives::setDrawColor4B(0, 0, 255, 255);
            glLineWidth(1);
            Point points[4];
            V3F_C4B_T2F_Quad tmpQuad;
            for (int i = 0, n = skeleton->slotCount; i < n; i++) {
                spSlot* slot = skeleton->drawOrder[i];
                if (!slot->attachment || slot->attachment->type != ATTACHMENT_REGION) continue;
                spRegionAttachment* attachment = (spRegionAttachment*)slot->attachment;
                spRegionAttachment_updateQuad(attachment, slot, &tmpQuad);
                points[0] = Point(tmpQuad.bl.vertices.x, tmpQuad.bl.vertices.y);
                points[1] = Point(tmpQuad.br.vertices.x, tmpQuad.br.vertices.y);
                points[2] = Point(tmpQuad.tr.vertices.x, tmpQuad.tr.vertices.y);
                points[3] = Point(tmpQuad.tl.vertices.x, tmpQuad.tl.vertices.y);
                DrawPrimitives::drawPoly(points, 4, true);
            }
        }
        if (debugBones) {
            // Bone lengths.
            glLineWidth(2);
            DrawPrimitives::setDrawColor4B(255, 0, 0, 255);
            for (int i = 0, n = skeleton->boneCount; i < n; i++) {
                spBone *bone = skeleton->bones[i];
                float x = bone->data->length * bone->m00 + bone->worldX;
                float y = bone->data->length * bone->m10 + bone->worldY;
                DrawPrimitives::drawLine(Point(bone->worldX, bone->worldY), Point(x, y));
            }
            // Bone origins.
            DrawPrimitives::setPointSize(4);
            DrawPrimitives::setDrawColor4B(0, 0, 255, 255); // Root bone is blue.
            for (int i = 0, n = skeleton->boneCount; i < n; i++) {
                spBone *bone = skeleton->bones[i];
                DrawPrimitives::drawPoint(Point(bone->worldX, bone->worldY));
                if (i == 0) DrawPrimitives::setDrawColor4B(0, 255, 0, 255);
            }
        }
        
        kmGLPopMatrix();
    }
}

TriangleTextureAtlas* Skeleton::getTextureAtlas (spRegionAttachment* regionAttachment) const {
	return (TriangleTextureAtlas*)((spAtlasRegion*)regionAttachment->rendererObject)->page->rendererObject;
}

TriangleTextureAtlas* Skeleton::getTextureAtlas (spMeshAttachment* meshAttachment) const {
    return (TriangleTextureAtlas*)((spAtlasRegion*)meshAttachment->rendererObject)->page->rendererObject;
}

    
Rect Skeleton::getBoundingBox () const {
	float scaleX = getScaleX();
	float scaleY = getScaleY();
	Rect localBounds = getLocalBounds();
	Point position = getPosition();
    return Rect(position.x + localBounds.getMinX() * scaleX,
                position.y + localBounds.getMinY() * scaleY,
                localBounds.size.width * scaleX,
                localBounds.size.height * scaleY);
}

void Skeleton::onEnter() {
	Node::onEnter();
	scheduleUpdate();
}
	
void Skeleton::onExit() {
	Node::onExit();
	unscheduleUpdate();
}

Rect Skeleton::getLocalBounds() const
{
    bool first = true;
    float minX = 0, minY = 0, maxX = 0, maxY = 0;
    
    for (int i = 0, n = skeleton->slotCount; i < n; i++) {
		spSlot* slot = skeleton->drawOrder[i];
        if (!slot->attachment) continue;
        if (slot->attachment->type == ATTACHMENT_REGION)
        {
            spRegionAttachment* attachment = (spRegionAttachment*)slot->attachment;
            
            float verticesPos[8];
            
            spRegionAttachment_computeWorldVertices(attachment, slot->skeleton->x, slot->skeleton->y, slot->bone, verticesPos);
            
            float regionMinX, regionMinY, regionMaxX, regionMaxY;
            regionMinX = min(verticesPos[0], min(verticesPos[2], min(verticesPos[4], verticesPos[6])));
            regionMinY = min(verticesPos[1], min(verticesPos[3], min(verticesPos[5], verticesPos[7])));
            regionMaxX = max(verticesPos[0], max(verticesPos[2], max(verticesPos[4], verticesPos[6])));
            regionMaxY = max(verticesPos[1], max(verticesPos[3], max(verticesPos[5], verticesPos[7])));
            
            if (first)
            {
                minX = regionMinX;
                minY = regionMinY;
                maxX = regionMaxX;
                maxY = regionMaxY;
                first = false;
            }
            else
            {
                minX = min(minX, regionMinX);
                minY = min(minY, regionMinY);
                maxX = max(maxX, regionMaxX);
                maxY = max(maxY, regionMaxY);
            }
        }
        else if (slot->attachment->type == ATTACHMENT_MESH)
        {
            spMeshAttachment * attachment = (spMeshAttachment*)slot->attachment;
            
            unsigned int verticesCount = attachment->verticesLength / 2;
            if (verticesCount == 0) continue;
            spMeshAttachment_computeWorldVertices(attachment, slot->skeleton->x, slot->skeleton->y, slot->bone);
            
            V3F_C4B_T2F * vertices = (V3F_C4B_T2F *)alloca(sizeof(V3F_C4B_T2F) * verticesCount);
            
            spMeshAttachment_updateVertices(attachment, slot, vertices, premultipliedAlpha);
            
            float meshMinX = vertices[0].vertices.x
            , meshMinY = vertices[0].vertices.y
            , meshMaxX = vertices[0].vertices.x
            , meshMaxY = vertices[0].vertices.y;
            
            for (int j = 1; j < verticesCount; j++)
            {
                meshMinX = min(meshMinX, vertices[j].vertices.x);
                meshMinY = min(meshMinY, vertices[j].vertices.y);
                meshMaxX = max(meshMaxX, vertices[j].vertices.x);
                meshMaxY = max(meshMaxY, vertices[j].vertices.y);
            }
            
            if (first)
            {
                minX = meshMinX;
                minY = meshMinY;
                maxX = meshMaxX;
                maxY = meshMaxY;
                first = false;
            }
            else
            {
                minX = min(minX, meshMinX);
                minY = min(minY, meshMinY);
                maxX = max(maxX, meshMaxX);
                maxY = max(maxY, meshMaxY);
            }
        }
	}
    
    return Rect(minX, minY, maxX-minX, maxY-minY);
}
    
// --- Convenience methods for Skeleton_* functions.

void Skeleton::updateWorldTransform () {
	spSkeleton_updateWorldTransform(skeleton);
}

void Skeleton::setToSetupPose () {
	spSkeleton_setToSetupPose(skeleton);
}
void Skeleton::setBonesToSetupPose () {
	spSkeleton_setBonesToSetupPose(skeleton);
}
void Skeleton::setSlotsToSetupPose () {
	spSkeleton_setSlotsToSetupPose(skeleton);
}

spBone* Skeleton::findBone (const char* boneName) const {
	return spSkeleton_findBone(skeleton, boneName);
}

spSlot* Skeleton::findSlot (const char* slotName) const {
	return spSkeleton_findSlot(skeleton, slotName);
}

bool Skeleton::setSkin (const char* skinName) {
	return spSkeleton_setSkinByName(skeleton, skinName) ? true : false;
}

spAttachment* Skeleton::getAttachment (const char* slotName, const char* attachmentName) const {
	return spSkeleton_getAttachmentForSlotName(skeleton, slotName, attachmentName);
}
bool Skeleton::setAttachment (const char* slotName, const char* attachmentName) {
	return spSkeleton_setAttachment(skeleton, slotName, attachmentName) ? true : false;
}

// --- CCBlendProtocol

const cocos2d::BlendFunc& Skeleton::getBlendFunc () const {
    return blendFunc;
}

void Skeleton::setBlendFunc (const cocos2d::BlendFunc& aBlendFunc) {
    this->blendFunc = aBlendFunc;
}
    
void Skeleton::setFittedBlendingFunc(bool isPremultipliedAlpha, bool additive)
{
    if(isPremultipliedAlpha)
    {
        GL::blendFunc(BlendFunc::ALPHA_PREMULTIPLIED.src, additive ? GL_ONE : BlendFunc::ALPHA_PREMULTIPLIED.dst);
    }
    else
    {
        GL::blendFunc(BlendFunc::ALPHA_NON_PREMULTIPLIED.src, additive ? GL_ONE : BlendFunc::ALPHA_NON_PREMULTIPLIED.dst);
    }
}
    
void Skeleton::drawAndClear(TriangleTextureAtlas *atlas)
{
    atlas->drawTriangles();
    atlas->removeAllVertices();
    atlas->removeAllTriangles();
    atlas->setCurrentTriangles(0);
}
}
