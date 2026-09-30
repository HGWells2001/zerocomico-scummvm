/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: minimal software renderer for JapoTek 3D scenes
 */

#ifndef ZEROCOMICO_SOFTWARE_RENDERER_H
#define ZEROCOMICO_SOFTWARE_RENDERER_H

#include "common/array.h"
#include "common/path.h"
#include "common/str.h"
#include "graphics/managed_surface.h"

#include "zerocomico/scene_model.h"

namespace ZeroComico {

struct RenderCamera {
	Vec3f position;
	Vec3f target;
	float focalPixels;
	float rollRadians = 0.0f;
	bool depthCueEnabled = false;
	float depthCueStart = 0.0f;
	float depthCueEnd = 0.0f;
};

struct AnimatedTextureCacheEntry {
	Common::String key;
	uint32 frameDelayMs;
	Common::Array<Graphics::ManagedSurface *> frames;
};

struct RenderTransform {
	// World placement supplied by the gameplay marker.
	Vec3f translation;
	float yawRadians;

	// Local JACS root transform sampled from the active animation.
	Vec3f localTranslation;
	Vec3f localScale;
	float localRotation[4]; // axis x, y, z followed by angle in radians
};

struct RenderActor {
	const SceneModel *scene;
	Common::Path textureDirectory;
	Common::Array<Common::String> visibleMeshes;
	RenderTransform transform;
};

class SoftwareRenderer {
public:
	SoftwareRenderer();
	~SoftwareRenderer();

	// Renders a static SceneModel with the retail camera convention. The
	// optional mesh list is useful for JACS scenes where visibility is normally
	// driven by animation/script state; an empty list means render every mesh.
	bool render(const SceneModel &scene, const Common::String &cameraName,
	            const Common::Path &textureDirectory,
	            const Common::Array<Common::String> &visibleMeshes,
	            Graphics::ManagedSurface &target, int width = 800, int height = 600) const;

	// Gameplay cameras defined by Camera.scr already carry their source/target
	// and use an angular FOV. The caller converts that FOV to focal pixels and
	// feeds the same rasterizer through this overload.
	bool render(const SceneModel &scene, const RenderCamera &camera,
	            const Common::Path &textureDirectory,
	            const Common::Array<Common::String> &visibleMeshes,
	            Graphics::ManagedSurface &target, int width = 800, int height = 600) const;

	// Draws a gameplay room and one independently transformed actor into the
	// same target/z buffer. This is the first runtime bridge between the room
	// scene and character P3D data.
	bool renderWithActor(const SceneModel &scene, const RenderCamera &camera,
	                     const Common::Path &textureDirectory,
	                     const Common::Array<Common::String> &visibleMeshes,
	                     const SceneModel &actor,
	                     const Common::Path &actorTextureDirectory,
	                     const Common::Array<Common::String> &actorVisibleMeshes,
	                     const RenderTransform &actorTransform,
	                     Graphics::ManagedSurface &target, int width = 800, int height = 600) const;

	// Generalized gameplay compositor for the player plus any number of CPU
	// characters. Every actor keeps its own source scene, texture directory and
	// instance transform while sharing the room z buffer.
	bool renderWithActors(const SceneModel &scene, const RenderCamera &camera,
	                      const Common::Path &textureDirectory,
	                      const Common::Array<Common::String> &visibleMeshes,
	                      const Common::Array<RenderActor> &actors,
	                      Graphics::ManagedSurface &target, int width = 800, int height = 600) const;

	// Projects candidate room meshes through the same camera convention as the
	// renderer and returns the nearest screen-space hit.
	bool pickMesh(const SceneModel &scene, const RenderCamera &camera,
	              int screenX, int screenY,
	              const Common::Array<Common::String> &candidates,
	              Common::String &pickedName, int width = 800, int height = 600) const;

	bool pickMeshWithActors(const SceneModel &scene, const RenderCamera &camera,
	                        int screenX, int screenY,
	                        const Common::Array<Common::String> &candidates,
	                        const Common::Array<RenderActor> &actors,
	                        Common::String &pickedName, int width = 800, int height = 600) const;

private:
	// JGF decoding is expensive and cutscenes redraw the same materials every
	// frame. Keep decoded texture surfaces for the lifetime of this renderer.
	mutable Common::Array<Common::String> _textureCacheKeys;
	mutable Common::Array<Graphics::ManagedSurface *> _textureCache;
	mutable Common::Array<AnimatedTextureCacheEntry> _animatedTextureCache;
};

} // namespace ZeroComico

#endif
