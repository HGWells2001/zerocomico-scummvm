/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: decoded 3D asset set
 */

#include "zerocomico/scene_model.h"

#include "zerocomico/model.h"

#include <cmath>

namespace ZeroComico {

namespace {

struct PoseMatrix {
	float m[16];
};

static PoseMatrix identityMatrix() {
	PoseMatrix out;
	for (int i = 0; i < 16; ++i)
		out.m[i] = 0.0f;
	out.m[0] = out.m[5] = out.m[10] = out.m[15] = 1.0f;
	return out;
}

static PoseMatrix multiplyMatrix(const PoseMatrix &a, const PoseMatrix &b) {
	PoseMatrix out;
	for (int row = 0; row < 4; ++row) {
		for (int col = 0; col < 4; ++col) {
			float value = 0.0f;
			for (int k = 0; k < 4; ++k)
				value += a.m[row * 4 + k] * b.m[k * 4 + col];
			out.m[row * 4 + col] = value;
		}
	}
	return out;
}

static PoseMatrix transformMatrix(const float translation[3], const float scale[3],
                                  const float rotation[4]) {
	PoseMatrix out = identityMatrix();

	float x = rotation[0];
	float y = rotation[1];
	float z = rotation[2];
	const float angle = rotation[3];
	const float length2 = x * x + y * y + z * z;
	if (length2 > 1.0e-12f && angle != 0.0f) {
		const float invLength = 1.0f / std::sqrt(length2);
		x *= invLength;
		y *= invLength;
		z *= invLength;
		const float c = std::cos(angle);
		const float s = std::sin(angle);
		const float t = 1.0f - c;

		out.m[0] = (t * x * x + c) * scale[0];
		out.m[1] = (t * x * y - s * z) * scale[1];
		out.m[2] = (t * x * z + s * y) * scale[2];
		out.m[4] = (t * x * y + s * z) * scale[0];
		out.m[5] = (t * y * y + c) * scale[1];
		out.m[6] = (t * y * z - s * x) * scale[2];
		out.m[8] = (t * x * z - s * y) * scale[0];
		out.m[9] = (t * y * z + s * x) * scale[1];
		out.m[10] = (t * z * z + c) * scale[2];
	} else {
		out.m[0] = scale[0];
		out.m[5] = scale[1];
		out.m[10] = scale[2];
	}

	out.m[3] = translation[0];
	out.m[7] = translation[1];
	out.m[11] = translation[2];
	return out;
}

static bool invertAffine(const PoseMatrix &in, PoseMatrix &out) {
	const float a = in.m[0], b = in.m[1], c = in.m[2];
	const float d = in.m[4], e = in.m[5], f = in.m[6];
	const float g = in.m[8], h = in.m[9], i = in.m[10];
	const float det = a * (e * i - f * h) -
	                  b * (d * i - f * g) +
	                  c * (d * h - e * g);
	if (std::fabs(det) <= 1.0e-12f)
		return false;

	const float invDet = 1.0f / det;
	out = identityMatrix();
	out.m[0] = (e * i - f * h) * invDet;
	out.m[1] = (c * h - b * i) * invDet;
	out.m[2] = (b * f - c * e) * invDet;
	out.m[4] = (f * g - d * i) * invDet;
	out.m[5] = (a * i - c * g) * invDet;
	out.m[6] = (c * d - a * f) * invDet;
	out.m[8] = (d * h - e * g) * invDet;
	out.m[9] = (b * g - a * h) * invDet;
	out.m[10] = (a * e - b * d) * invDet;

	const float tx = in.m[3];
	const float ty = in.m[7];
	const float tz = in.m[11];
	out.m[3] = -(out.m[0] * tx + out.m[1] * ty + out.m[2] * tz);
	out.m[7] = -(out.m[4] * tx + out.m[5] * ty + out.m[6] * tz);
	out.m[11] = -(out.m[8] * tx + out.m[9] * ty + out.m[10] * tz);
	return true;
}

static Vec3f transformPoint(const PoseMatrix &m, const Vec3f &point) {
	Vec3f out = {
		m.m[0] * point.x + m.m[1] * point.y + m.m[2] * point.z + m.m[3],
		m.m[4] * point.x + m.m[5] * point.y + m.m[6] * point.z + m.m[7],
		m.m[8] * point.x + m.m[9] * point.y + m.m[10] * point.z + m.m[11]
	};
	return out;
}

static Vec3f transformBindVertex(const Vec3f &stored, const ObjectTransform &transform) {
	Vec3f local = {
		(stored.x - transform.translation.x) * transform.scale.x,
		(stored.y - transform.translation.y) * transform.scale.y,
		(stored.z - transform.translation.z) * transform.scale.z
	};

	Vec3f out = {
		transform.matrix[0] * local.x + transform.matrix[1] * local.y + transform.matrix[2] * local.z + transform.translation.x,
		transform.matrix[3] * local.x + transform.matrix[4] * local.y + transform.matrix[5] * local.z + transform.translation.y,
		transform.matrix[6] * local.x + transform.matrix[7] * local.y + transform.matrix[8] * local.z + transform.translation.z
	};
	return out;
}

static const HierarchyData *findHierarchy(const SceneModel &scene, const Common::String &rootName) {
	for (uint32 i = 0; i < scene.hierarchies.size(); ++i)
		if (scene.hierarchies[i].name.equalsIgnoreCase(rootName))
			return &scene.hierarchies[i].data;
	return nullptr;
}

static const Common::String *findParentName(const HierarchyData &hierarchy, const Common::String &name) {
	for (uint32 i = 0; i < hierarchy.entries.size(); ++i)
		if (hierarchy.entries[i].name.equalsIgnoreCase(name))
			return &hierarchy.entries[i].parent;
	return nullptr;
}

static bool hierarchyContains(const HierarchyData &hierarchy,
                              const Common::String &rootName,
                              const Common::String &name) {
	Common::String current = name;
	for (uint32 depth = 0; depth <= hierarchy.entries.size(); ++depth) {
		if (current.equalsIgnoreCase(rootName))
			return true;
		const Common::String *parent = findParentName(hierarchy, current);
		if (!parent || parent->empty() || parent->equalsIgnoreCase("NULL"))
			return false;
		current = *parent;
	}
	return false;
}

static bool sampleLocalMatrix(const AnimationClip &clip, const Common::String &name,
                              float frame, PoseMatrix &matrix) {
	float translation[3];
	float scale[3];
	float rotation[4];
	if (!AnimationSampler::sampleTransform(clip, name, frame, translation, scale, rotation))
		return false;
	matrix = transformMatrix(translation, scale, rotation);
	return true;
}

static bool buildGlobalMatrix(const AnimationClip &clip, const HierarchyData &hierarchy,
                              const Common::String &rootName, const Common::String &name,
                              float frame, bool ignoreRootMotion,
                              PoseMatrix &matrix, uint32 depth = 0) {
	if (depth > hierarchy.entries.size())
		return false;

	if (name.equalsIgnoreCase(rootName) && ignoreRootMotion) {
		matrix = identityMatrix();
		return true;
	}

	PoseMatrix local;
	if (!sampleLocalMatrix(clip, name, frame, local))
		return false;

	const Common::String *parentName = findParentName(hierarchy, name);
	if (name.equalsIgnoreCase(rootName) ||
	    !parentName || parentName->empty() || parentName->equalsIgnoreCase("NULL")) {
		matrix = local;
		return true;
	}

	PoseMatrix parent;
	if (!buildGlobalMatrix(clip, hierarchy, rootName, *parentName, frame,
	                       ignoreRootMotion, parent, depth + 1))
		return false;
	matrix = multiplyMatrix(parent, local);
	return true;
}

} // namespace

void SceneModel::clear() {
	materials.clear();
	cameras.clear();
	lights.clear();
	meshes.clear();
	hierarchies.clear();
	clips.clear();
}

bool SceneModel::loadGeometry(const Common::Path &p3dPath) {
	ModelArchive archive;
	if (!archive.load(p3dPath))
		return false;

	for (uint32 i = 0; i < archive.records().size(); ++i) {
		const ModelRecord &record = archive.records()[i];
		if (record.group)
			continue;

		switch (record.type) {
		case 0xf000: {
			NamedMaterial value;
			value.name = record.name;
			value.sourceDirectory = p3dPath.getParent();
			if (!ModelDataDecoder::decodeMaterial(archive, record, value.data))
				return false;
			materials.push_back(value);
			break;
		}
		case 0xf001: {
			NamedCamera value;
			value.name = record.name;
			if (!ModelDataDecoder::decodeCamera(archive, record, value.data))
				return false;
			cameras.push_back(value);
			break;
		}
		case 0xf002: {
			NamedLight value;
			value.name = record.name;
			if (!ModelDataDecoder::decodeLight(archive, record, value.data))
				return false;
			lights.push_back(value);
			break;
		}
		case 0xf003: {
			NamedMesh value;
			value.name = record.name;
			if (!ModelDataDecoder::decodeMesh(archive, record, value.data))
				return false;
			meshes.push_back(value);
			break;
		}
		default:
			// Geometry files in the retail data use F000..F003. Keep the
			// loader forward compatible by ignoring an unknown record here.
			break;
		}
	}

	return resolveSkinnedGeometry();
}

bool SceneModel::loadAnimation(const Common::Path &anjPath) {
	ModelArchive archive;
	if (!archive.load(anjPath))
		return false;

	for (uint32 i = 0; i < archive.records().size(); ++i) {
		const ModelRecord &record = archive.records()[i];
		if (record.group)
			continue;

		if (record.type == 0xf007) {
			NamedAnimationClip clip;
			clip.name = record.name;
			if (!AnimationDecoder::decodeClip(archive, record, clip.data))
				return false;
			clips.push_back(clip);
			continue;
		}

		// JACS hierarchy records reuse several of the same type numbers as
		// P3D objects. The body shape, rather than the type alone, identifies
		// these ANJ records.
		HierarchyData hierarchy;
		if (ModelDataDecoder::decodeHierarchy(archive, record, hierarchy)) {
			NamedHierarchy value;
			value.recordType = record.type;
			value.name = record.name;
			value.data = hierarchy;
			hierarchies.push_back(value);
			continue;
		}

		return false;
	}

	return true;
}

bool SceneModel::loadPair(const Common::Path &p3dPath, const Common::Path &anjPath) {
	clear();
	if (!loadGeometry(p3dPath))
		return false;
	if (!loadAnimation(anjPath)) {
		clear();
		return false;
	}
	return true;
}


bool SceneModel::resolveSkinnedGeometry() {
	for (uint32 meshIndex = 0; meshIndex < meshes.size(); ++meshIndex)
		meshes[meshIndex].posedVertices.clear();

	for (uint32 parentIndex = 0; parentIndex < meshes.size(); ++parentIndex) {
		MeshData &parent = meshes[parentIndex].data;
		if (!parent.isSkinnedParent())
			continue;

		parent.vertices.clear();
		parent.vertices.resize(parent.vertexCount);
		Common::Array<float> weights;
		weights.resize(parent.vertexCount);

		for (uint32 i = 0; i < parent.vertexCount; ++i) {
			parent.vertices[i].x = 0.0f;
			parent.vertices[i].y = 0.0f;
			parent.vertices[i].z = 0.0f;
			weights[i] = 0.0f;
		}

		for (uint32 fleshIndex = 0; fleshIndex < meshes.size(); ++fleshIndex) {
			const MeshData &flesh = meshes[fleshIndex].data;
			if (!flesh.isFlesh() ||
			    !flesh.parentMesh.equalsIgnoreCase(meshes[parentIndex].name) ||
			    flesh.vertices.size() != flesh.influences.size())
				continue;

			for (uint32 i = 0; i < flesh.vertices.size(); ++i) {
				const SkinInfluence &influence = flesh.influences[i];
				if (influence.parentVertexIndex >= parent.vertexCount ||
				    influence.weight <= 0.0f)
					continue;

				Vec3f &dst = parent.vertices[influence.parentVertexIndex];
				dst.x += flesh.vertices[i].x * influence.weight;
				dst.y += flesh.vertices[i].y * influence.weight;
				dst.z += flesh.vertices[i].z * influence.weight;
				weights[influence.parentVertexIndex] += influence.weight;
			}
		}

		for (uint32 i = 0; i < parent.vertexCount; ++i) {
			if (weights[i] <= 0.000001f)
				return false;

			// Retail flesh weights normally sum to 1.0. Normalize anyway so tiny
			// floating-point export differences cannot distort the bind pose.
			const float invWeight = 1.0f / weights[i];
			parent.vertices[i].x *= invWeight;
			parent.vertices[i].y *= invWeight;
			parent.vertices[i].z *= invWeight;
		}
	}

	return true;
}


bool SceneModel::poseSkinnedGeometry(const Common::String &rootName,
                                     const Common::String &bindSource,
                                     const Common::String &sourceName,
                                     float frame) {
	const NamedAnimationClip *bindClip = findClipBySource(rootName, bindSource);
	const NamedAnimationClip *poseClip = findClipBySource(rootName, sourceName);
	const HierarchyData *hierarchy = findHierarchy(*this, rootName);
	if (!bindClip || !poseClip || !hierarchy)
		return false;

	for (uint32 parentIndex = 0; parentIndex < meshes.size(); ++parentIndex) {
		MeshData &parent = meshes[parentIndex].data;
		if (!parent.isSkinnedParent())
			continue;

		Common::Array<Vec3f> posed;
		Common::Array<float> weights;
		posed.resize(parent.vertexCount);
		weights.resize(parent.vertexCount);
		for (uint32 i = 0; i < parent.vertexCount; ++i) {
			posed[i].x = posed[i].y = posed[i].z = 0.0f;
			weights[i] = 0.0f;
		}

		for (uint32 fleshIndex = 0; fleshIndex < meshes.size(); ++fleshIndex) {
			const NamedMesh &namedFlesh = meshes[fleshIndex];
			const MeshData &flesh = namedFlesh.data;
			if (!flesh.isFlesh() ||
			    !flesh.parentMesh.equalsIgnoreCase(meshes[parentIndex].name) ||
			    flesh.vertices.size() != flesh.influences.size())
				continue;

			PoseMatrix bindGlobal;
			PoseMatrix poseGlobal;
			PoseMatrix inverseBind;
			if (!buildGlobalMatrix(bindClip->data, *hierarchy, rootName, namedFlesh.name,
			                       (float)bindClip->data.startFrame, true, bindGlobal) ||
			    !buildGlobalMatrix(poseClip->data, *hierarchy, rootName, namedFlesh.name,
			                       frame, true, poseGlobal) ||
			    !invertAffine(bindGlobal, inverseBind))
				return false;

			const PoseMatrix delta = multiplyMatrix(poseGlobal, inverseBind);
			for (uint32 i = 0; i < flesh.vertices.size(); ++i) {
				const SkinInfluence &influence = flesh.influences[i];
				if (influence.parentVertexIndex >= parent.vertexCount ||
				    influence.weight <= 0.0f)
					continue;

				const Vec3f transformed = transformPoint(delta, flesh.vertices[i]);
				Vec3f &dst = posed[influence.parentVertexIndex];
				dst.x += transformed.x * influence.weight;
				dst.y += transformed.y * influence.weight;
				dst.z += transformed.z * influence.weight;
				weights[influence.parentVertexIndex] += influence.weight;
			}
		}

		meshes[parentIndex].posedVertices.resize(parent.vertexCount);
		for (uint32 i = 0; i < parent.vertexCount; ++i) {
			if (weights[i] <= 0.000001f)
				return false;
			const float invWeight = 1.0f / weights[i];
			meshes[parentIndex].posedVertices[i].x = posed[i].x * invWeight;
			meshes[parentIndex].posedVertices[i].y = posed[i].y * invWeight;
			meshes[parentIndex].posedVertices[i].z = posed[i].z * invWeight;
		}
	}

	// Rigid child meshes such as Giovanni's head and hat also participate in
	// the JACS hierarchy. Move their bind-space geometry with the same bone
	// delta so they stay attached to the animated skinned body.
	for (uint32 meshIndex = 0; meshIndex < meshes.size(); ++meshIndex) {
		NamedMesh &namedMesh = meshes[meshIndex];
		MeshData &mesh = namedMesh.data;
		if (mesh.isFlesh() || mesh.isSkinnedParent() || mesh.vertices.empty())
			continue;

		PoseMatrix bindGlobal;
		PoseMatrix poseGlobal;
		PoseMatrix inverseBind;
		if (!buildGlobalMatrix(bindClip->data, *hierarchy, rootName, namedMesh.name,
		                       (float)bindClip->data.startFrame, true, bindGlobal) ||
		    !buildGlobalMatrix(poseClip->data, *hierarchy, rootName, namedMesh.name,
		                       frame, true, poseGlobal) ||
		    !invertAffine(bindGlobal, inverseBind))
			continue;

		const PoseMatrix delta = multiplyMatrix(poseGlobal, inverseBind);
		namedMesh.posedVertices.resize(mesh.vertices.size());
		for (uint32 i = 0; i < mesh.vertices.size(); ++i) {
			const Vec3f bindPoint = transformBindVertex(mesh.vertices[i], mesh.transform);
			namedMesh.posedVertices[i] = transformPoint(delta, bindPoint);
		}
	}

	return true;
}


bool SceneModel::sampleHierarchyPoint(const Common::String &rootName,
                                      const Common::String &nodeName,
                                      const Common::String &sourceName,
                                      float frame, Vec3f &point) const {
	const NamedAnimationClip *clip = findClipBySource(rootName, sourceName);
	const HierarchyData *hierarchy = findHierarchy(*this, rootName);
	if (!clip || !hierarchy || !hierarchyContains(*hierarchy, rootName, nodeName))
		return false;

	PoseMatrix global;
	if (!buildGlobalMatrix(clip->data, *hierarchy, rootName, nodeName,
	                       frame, true, global))
		return false;

	const Vec3f origin = { 0.0f, 0.0f, 0.0f };
	point = transformPoint(global, origin);
	return true;
}

bool SceneModel::poseCutsceneGeometry(const Common::String &sourceName, float frame) {
	for (uint32 meshIndex = 0; meshIndex < meshes.size(); ++meshIndex)
		meshes[meshIndex].posedVertices.clear();

	// Character hierarchies carry their own animated root in cutscenes. Build
	// each hierarchy from frame zero/source start to the requested frame, so
	// both bone deformation and root translation/rotation are preserved.
	for (uint32 hierarchyIndex = 0; hierarchyIndex < hierarchies.size(); ++hierarchyIndex) {
		const NamedHierarchy &namedHierarchy = hierarchies[hierarchyIndex];
		const NamedAnimationClip *clip = findClipBySource(namedHierarchy.name, sourceName);
		if (!clip)
			continue;

		const HierarchyData &hierarchy = namedHierarchy.data;
		for (uint32 parentIndex = 0; parentIndex < meshes.size(); ++parentIndex) {
			MeshData &parent = meshes[parentIndex].data;
			if (!parent.isSkinnedParent())
				continue;

			Common::Array<Vec3f> posed;
			Common::Array<float> weights;
			posed.resize(parent.vertexCount);
			weights.resize(parent.vertexCount);
			for (uint32 i = 0; i < parent.vertexCount; ++i) {
				posed[i].x = posed[i].y = posed[i].z = 0.0f;
				weights[i] = 0.0f;
			}

			bool contributed = false;
			for (uint32 fleshIndex = 0; fleshIndex < meshes.size(); ++fleshIndex) {
				const NamedMesh &namedFlesh = meshes[fleshIndex];
				const MeshData &flesh = namedFlesh.data;
				if (!flesh.isFlesh() ||
				    !flesh.parentMesh.equalsIgnoreCase(meshes[parentIndex].name) ||
				    flesh.vertices.size() != flesh.influences.size() ||
				    !findParentName(hierarchy, namedFlesh.name))
					continue;

				PoseMatrix bindGlobal;
				PoseMatrix poseGlobal;
				PoseMatrix inverseBind;
				if (!buildGlobalMatrix(clip->data, hierarchy, namedHierarchy.name, namedFlesh.name,
				                       (float)clip->data.startFrame, false, bindGlobal) ||
				    !buildGlobalMatrix(clip->data, hierarchy, namedHierarchy.name, namedFlesh.name,
				                       frame, false, poseGlobal) ||
				    !invertAffine(bindGlobal, inverseBind))
					return false;

				const PoseMatrix delta = multiplyMatrix(poseGlobal, inverseBind);
				for (uint32 i = 0; i < flesh.vertices.size(); ++i) {
					const SkinInfluence &influence = flesh.influences[i];
					if (influence.parentVertexIndex >= parent.vertexCount ||
					    influence.weight <= 0.0f)
						continue;
					const Vec3f transformed = transformPoint(delta, flesh.vertices[i]);
					Vec3f &dst = posed[influence.parentVertexIndex];
					dst.x += transformed.x * influence.weight;
					dst.y += transformed.y * influence.weight;
					dst.z += transformed.z * influence.weight;
					weights[influence.parentVertexIndex] += influence.weight;
					contributed = true;
				}
			}

			if (!contributed)
				continue;

			meshes[parentIndex].posedVertices.resize(parent.vertexCount);
			for (uint32 i = 0; i < parent.vertexCount; ++i) {
				if (weights[i] <= 0.000001f)
					return false;
				const float invWeight = 1.0f / weights[i];
				meshes[parentIndex].posedVertices[i].x = posed[i].x * invWeight;
				meshes[parentIndex].posedVertices[i].y = posed[i].y * invWeight;
				meshes[parentIndex].posedVertices[i].z = posed[i].z * invWeight;
			}
		}

		for (uint32 meshIndex = 0; meshIndex < meshes.size(); ++meshIndex) {
			NamedMesh &namedMesh = meshes[meshIndex];
			MeshData &mesh = namedMesh.data;
			if (mesh.isFlesh() || mesh.isSkinnedParent() || mesh.vertices.empty() ||
			    !findParentName(hierarchy, namedMesh.name))
				continue;

			PoseMatrix bindGlobal;
			PoseMatrix poseGlobal;
			PoseMatrix inverseBind;
			if (!buildGlobalMatrix(clip->data, hierarchy, namedHierarchy.name, namedMesh.name,
			                       (float)clip->data.startFrame, false, bindGlobal) ||
			    !buildGlobalMatrix(clip->data, hierarchy, namedHierarchy.name, namedMesh.name,
			                       frame, false, poseGlobal) ||
			    !invertAffine(bindGlobal, inverseBind))
				continue;

			const PoseMatrix delta = multiplyMatrix(poseGlobal, inverseBind);
			namedMesh.posedVertices.resize(mesh.vertices.size());
			for (uint32 i = 0; i < mesh.vertices.size(); ++i) {
				const Vec3f bindPoint = transformBindVertex(mesh.vertices[i], mesh.transform);
				namedMesh.posedVertices[i] = transformPoint(delta, bindPoint);
			}
		}
	}

	// Rigid cutscene objects that are not members of a character hierarchy
	// have a direct F007 track named after the mesh itself.
	for (uint32 meshIndex = 0; meshIndex < meshes.size(); ++meshIndex) {
		NamedMesh &namedMesh = meshes[meshIndex];
		MeshData &mesh = namedMesh.data;
		if (mesh.isFlesh() || mesh.isSkinnedParent() || mesh.vertices.empty() ||
		    !namedMesh.posedVertices.empty())
			continue;

		const NamedAnimationClip *clip = findClipBySource(namedMesh.name, sourceName);
		if (!clip)
			continue;

		PoseMatrix bindMatrix;
		PoseMatrix poseMatrix;
		PoseMatrix inverseBind;
		if (!sampleLocalMatrix(clip->data, namedMesh.name, (float)clip->data.startFrame, bindMatrix) ||
		    !sampleLocalMatrix(clip->data, namedMesh.name, frame, poseMatrix) ||
		    !invertAffine(bindMatrix, inverseBind))
			continue;

		const PoseMatrix delta = multiplyMatrix(poseMatrix, inverseBind);
		namedMesh.posedVertices.resize(mesh.vertices.size());
		for (uint32 i = 0; i < mesh.vertices.size(); ++i) {
			const Vec3f bindPoint = transformBindVertex(mesh.vertices[i], mesh.transform);
			namedMesh.posedVertices[i] = transformPoint(delta, bindPoint);
		}
	}

	return true;
}


void SceneModel::visibleMeshesForSource(const Common::String &sourceName, float frame,
                                        Common::Array<Common::String> &visible) const {
	visible.clear();

	for (uint32 meshIndex = 0; meshIndex < meshes.size(); ++meshIndex) {
		const NamedMesh &mesh = meshes[meshIndex];
		if (mesh.data.isFlesh())
			continue;

		const AnimationTrack *visibilityTrack = nullptr;
		for (uint32 clipIndex = 0; clipIndex < clips.size() && !visibilityTrack; ++clipIndex) {
			const NamedAnimationClip &clip = clips[clipIndex];
			if (!clip.data.sourceName.equalsIgnoreCase(sourceName))
				continue;

			for (uint32 trackIndex = 0; trackIndex < clip.data.tracks.size(); ++trackIndex) {
				const AnimationTrack &track = clip.data.tracks[trackIndex];
				if (track.kind == kAnimTransform &&
				    track.targetName.equalsIgnoreCase(mesh.name) &&
				    track.visibilityEnabled) {
					visibilityTrack = &track;
					break;
				}
			}
		}

		if (!visibilityTrack) {
			visible.push_back(mesh.name);
			continue;
		}

		uint32 toggles = 0;
		for (uint32 eventIndex = 0; eventIndex < visibilityTrack->visibilityFrames.size(); ++eventIndex) {
			if ((float)visibilityTrack->visibilityFrames[eventIndex] <= frame)
				++toggles;
			else
				break;
		}
		if ((toggles & 1U) != 0)
			visible.push_back(mesh.name);
	}
}


bool SceneModel::poseRigidAnimation(const Common::String &targetName,
                                    const Common::String &sourceName,
                                    float frame) {
	NamedMesh *targetMesh = findMesh(targetName);
	if (targetMesh && !targetMesh->data.isFlesh() &&
	    !targetMesh->data.isSkinnedParent() && !targetMesh->data.vertices.empty()) {
		const NamedAnimationClip *clip = findClipBySource(targetName, sourceName);
		if (!clip)
			return false;

		PoseMatrix bindMatrix;
		PoseMatrix poseMatrix;
		PoseMatrix inverseBind;
		if (!sampleLocalMatrix(clip->data, targetName, (float)clip->data.startFrame, bindMatrix) ||
		    !sampleLocalMatrix(clip->data, targetName, frame, poseMatrix) ||
		    !invertAffine(bindMatrix, inverseBind))
			return false;

		const PoseMatrix delta = multiplyMatrix(poseMatrix, inverseBind);
		targetMesh->posedVertices.resize(targetMesh->data.vertices.size());
		for (uint32 i = 0; i < targetMesh->data.vertices.size(); ++i) {
			const Vec3f bindPoint = transformBindVertex(targetMesh->data.vertices[i],
			                                           targetMesh->data.transform);
			targetMesh->posedVertices[i] = transformPoint(delta, bindPoint);
		}
		return true;
	}

	// Setp controllers such as Mp2's r23_dummyossa are hierarchy roots rather
	// than visible meshes. Their playl animation must pose every rigid mesh
	// below that root instead of failing the direct-mesh lookup above.
	const HierarchyData *hierarchy = findHierarchy(*this, targetName);
	const NamedAnimationClip *clip = findClipBySource(targetName, sourceName);
	if (!hierarchy || !clip)
		return false;

	bool posedAny = false;
	for (uint32 meshIndex = 0; meshIndex < meshes.size(); ++meshIndex) {
		NamedMesh &namedMesh = meshes[meshIndex];
		MeshData &mesh = namedMesh.data;
		if (mesh.isFlesh() || mesh.isSkinnedParent() || mesh.vertices.empty() ||
		    !hierarchyContains(*hierarchy, targetName, namedMesh.name))
			continue;

		PoseMatrix bindGlobal;
		PoseMatrix poseGlobal;
		PoseMatrix inverseBind;
		if (!buildGlobalMatrix(clip->data, *hierarchy, targetName, namedMesh.name,
		                       (float)clip->data.startFrame, false, bindGlobal) ||
		    !buildGlobalMatrix(clip->data, *hierarchy, targetName, namedMesh.name,
		                       frame, false, poseGlobal) ||
		    !invertAffine(bindGlobal, inverseBind))
			continue;

		const PoseMatrix delta = multiplyMatrix(poseGlobal, inverseBind);
		namedMesh.posedVertices.resize(mesh.vertices.size());
		for (uint32 i = 0; i < mesh.vertices.size(); ++i) {
			const Vec3f bindPoint = transformBindVertex(mesh.vertices[i], mesh.transform);
			namedMesh.posedVertices[i] = transformPoint(delta, bindPoint);
		}
		posedAny = true;
	}
	return posedAny;
}

const NamedMaterial *SceneModel::findMaterial(const Common::String &name) const {
	for (uint32 i = 0; i < materials.size(); ++i)
		if (materials[i].name.equalsIgnoreCase(name))
			return &materials[i];
	return nullptr;
}

const NamedCamera *SceneModel::findCamera(const Common::String &name) const {
	for (uint32 i = 0; i < cameras.size(); ++i)
		if (cameras[i].name.equalsIgnoreCase(name))
			return &cameras[i];
	return nullptr;
}

const NamedLight *SceneModel::findLight(const Common::String &name) const {
	for (uint32 i = 0; i < lights.size(); ++i)
		if (lights[i].name.equalsIgnoreCase(name))
			return &lights[i];
	return nullptr;
}

NamedMesh *SceneModel::findMesh(const Common::String &name) {
	for (uint32 i = 0; i < meshes.size(); ++i)
		if (meshes[i].name.equalsIgnoreCase(name))
			return &meshes[i];
	return nullptr;
}

const NamedMesh *SceneModel::findMesh(const Common::String &name) const {
	for (uint32 i = 0; i < meshes.size(); ++i)
		if (meshes[i].name.equalsIgnoreCase(name))
			return &meshes[i];
	return nullptr;
}

bool SceneModel::hasHierarchy(const Common::String &name) const {
	return findHierarchy(*this, name) != nullptr;
}

bool SceneModel::translateHierarchy(const Common::String &name, const Vec3f &delta) {
	const HierarchyData *hierarchy = findHierarchy(*this, name);
	if (!hierarchy)
		return false;

	bool moved = false;
	for (uint32 i = 0; i < meshes.size(); ++i) {
		NamedMesh &namedMesh = meshes[i];
		if (!hierarchyContains(*hierarchy, name, namedMesh.name))
			continue;

		MeshData &mesh = namedMesh.data;
		for (uint32 v = 0; v < mesh.vertices.size(); ++v) {
			mesh.vertices[v].x += delta.x;
			mesh.vertices[v].y += delta.y;
			mesh.vertices[v].z += delta.z;
		}
		mesh.transform.translation.x += delta.x;
		mesh.transform.translation.y += delta.y;
		mesh.transform.translation.z += delta.z;
		namedMesh.posedVertices.clear();
		moved = true;
	}
	return moved;
}

void SceneModel::mergeFrom(const SceneModel &other) {
	for (uint32 i = 0; i < other.materials.size(); ++i) {
		if (!findMaterial(other.materials[i].name))
			materials.push_back(other.materials[i]);
	}
	for (uint32 i = 0; i < other.cameras.size(); ++i) {
		if (!findCamera(other.cameras[i].name))
			cameras.push_back(other.cameras[i]);
	}
	for (uint32 i = 0; i < other.lights.size(); ++i) {
		if (!findLight(other.lights[i].name))
			lights.push_back(other.lights[i]);
	}
	for (uint32 i = 0; i < other.meshes.size(); ++i) {
		if (!findMesh(other.meshes[i].name))
			meshes.push_back(other.meshes[i]);
	}
	for (uint32 i = 0; i < other.hierarchies.size(); ++i) {
		bool duplicate = false;
		for (uint32 j = 0; j < hierarchies.size(); ++j) {
			if (hierarchies[j].recordType == other.hierarchies[i].recordType &&
			    hierarchies[j].name.equalsIgnoreCase(other.hierarchies[i].name)) {
				duplicate = true;
				break;
			}
		}
		if (!duplicate)
			hierarchies.push_back(other.hierarchies[i]);
	}
	for (uint32 i = 0; i < other.clips.size(); ++i) {
		if (!findClipBySource(other.clips[i].name, other.clips[i].data.sourceName))
			clips.push_back(other.clips[i]);
	}
}

const NamedAnimationClip *SceneModel::findClip(const Common::String &name) const {
	for (uint32 i = 0; i < clips.size(); ++i)
		if (clips[i].name.equalsIgnoreCase(name))
			return &clips[i];
	return nullptr;
}

const NamedAnimationClip *SceneModel::findClipBySource(const Common::String &targetName,
                                                       const Common::String &sourceName) const {
	for (uint32 i = 0; i < clips.size(); ++i) {
		if (clips[i].name.equalsIgnoreCase(targetName) &&
		    clips[i].data.sourceName.equalsIgnoreCase(sourceName))
			return &clips[i];
	}
	return nullptr;
}

} // namespace ZeroComico
