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

class SoftwareRenderer {
public:
	// Renders a static SceneModel with the retail camera convention. The
	// optional mesh list is useful for JACS scenes where visibility is normally
	// driven by animation/script state; an empty list means render every mesh.
	bool render(const SceneModel &scene, const Common::String &cameraName,
	            const Common::Path &textureDirectory,
	            const Common::Array<Common::String> &visibleMeshes,
	            Graphics::ManagedSurface &target, int width = 800, int height = 600) const;
};

} // namespace ZeroComico

#endif
