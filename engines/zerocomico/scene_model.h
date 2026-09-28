/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: decoded 3D asset set
 */

#ifndef ZEROCOMICO_SCENE_MODEL_H
#define ZEROCOMICO_SCENE_MODEL_H

#include "common/array.h"
#include "common/path.h"
#include "common/str.h"

#include "zerocomico/model_animation.h"
#include "zerocomico/model_data.h"

namespace ZeroComico {

struct NamedMaterial {
	Common::String name;
	MaterialData data;
};

struct NamedCamera {
	Common::String name;
	CameraData data;
};

struct NamedLight {
	Common::String name;
	LightData data;
};

struct NamedMesh {
	Common::String name;
	MeshData data;
};

struct NamedHierarchy {
	uint16 recordType;
	Common::String name;
	HierarchyData data;
};

struct NamedAnimationClip {
	Common::String name;
	AnimationClip data;
};

class SceneModel {
public:
	void clear();

	bool loadGeometry(const Common::Path &p3dPath);
	bool loadAnimation(const Common::Path &anjPath);
	bool loadPair(const Common::Path &p3dPath, const Common::Path &anjPath);

	const NamedMaterial *findMaterial(const Common::String &name) const;
	const NamedCamera *findCamera(const Common::String &name) const;
	const NamedLight *findLight(const Common::String &name) const;
	const NamedMesh *findMesh(const Common::String &name) const;
	const NamedAnimationClip *findClip(const Common::String &name) const;

	Common::Array<NamedMaterial> materials;
	Common::Array<NamedCamera> cameras;
	Common::Array<NamedLight> lights;
	Common::Array<NamedMesh> meshes;
	Common::Array<NamedHierarchy> hierarchies;
	Common::Array<NamedAnimationClip> clips;
};

} // namespace ZeroComico

#endif
