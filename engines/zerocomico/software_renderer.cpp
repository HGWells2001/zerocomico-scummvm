/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: minimal software renderer for JapoTek 3D scenes
 */

#include "zerocomico/software_renderer.h"
#include "zerocomico/resource.h"

#include "graphics/pixelformat.h"

#include <cmath>

namespace ZeroComico {

namespace {

struct ProjectedVertex {
	float x;
	float y;
	float z;
};

static float dot3(const Vec3f &a, const Vec3f &b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

static Vec3f sub3(const Vec3f &a, const Vec3f &b) {
	Vec3f r = { a.x - b.x, a.y - b.y, a.z - b.z };
	return r;
}

static Vec3f cross3(const Vec3f &a, const Vec3f &b) {
	Vec3f r = {
		a.y * b.z - a.z * b.y,
		a.z * b.x - a.x * b.z,
		a.x * b.y - a.y * b.x
	};
	return r;
}

static bool normalize3(Vec3f &v) {
	const float length2 = dot3(v, v);
	if (length2 <= 1.0e-12f)
		return false;
	const float invLength = 1.0f / std::sqrt(length2);
	v.x *= invLength;
	v.y *= invLength;
	v.z *= invLength;
	return true;
}

static float edge(float ax, float ay, float bx, float by, float px, float py) {
	return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

static float clamp01(float v) {
	if (v < 0.0f)
		return 0.0f;
	if (v > 1.0f)
		return 1.0f;
	return v;
}

static Common::String lowerAscii(const Common::String &value) {
	Common::String out = value;
	out.toLowercase();
	return out;
}

static bool isVisible(const Common::String &name, const Common::Array<Common::String> &visibleMeshes) {
	if (visibleMeshes.empty())
		return true;
	for (uint32 i = 0; i < visibleMeshes.size(); ++i) {
		if (name.equalsIgnoreCase(visibleMeshes[i]))
			return true;
	}
	return false;
}

static Vec3f transformVertex(const Vec3f &stored, const ObjectTransform &transform) {
	// MeshData stores the retail loader's intermediate vertex value:
	// filePosition + translation - pivot. Undo the translation component,
	// apply object scale + transposed on-disk 3x3 matrix, then translate.
	Vec3f local = {
		(stored.x - transform.translation.x) * transform.scale.x,
		(stored.y - transform.translation.y) * transform.scale.y,
		(stored.z - transform.translation.z) * transform.scale.z
	};

	Vec3f world = {
		transform.matrix[0] * local.x + transform.matrix[1] * local.y + transform.matrix[2] * local.z + transform.translation.x,
		transform.matrix[3] * local.x + transform.matrix[4] * local.y + transform.matrix[5] * local.z + transform.translation.y,
		transform.matrix[6] * local.x + transform.matrix[7] * local.y + transform.matrix[8] * local.z + transform.translation.z
	};
	return world;
}

static Vec3f applyInstanceTransform(const Vec3f &world, const RenderTransform *transform) {
	if (!transform)
		return world;

	Vec3f local = {
		world.x * transform->localScale.x,
		world.y * transform->localScale.y,
		world.z * transform->localScale.z
	};

	// JACS transform channels store quaternion components as x, y, z, w.
	const float qx = transform->localRotation[0];
	const float qy = transform->localRotation[1];
	const float qz = transform->localRotation[2];
	const float qw = transform->localRotation[3];
	const float qLength2 = qx * qx + qy * qy + qz * qz + qw * qw;
	if (qLength2 > 1.0e-12f) {
		const float invLength = 1.0f / std::sqrt(qLength2);
		const float x = qx * invLength;
		const float y = qy * invLength;
		const float z = qz * invLength;
		const float w = qw * invLength;

		const float xx = x * x;
		const float yy = y * y;
		const float zz = z * z;
		const float xy = x * y;
		const float xz = x * z;
		const float yz = y * z;
		const float wx = w * x;
		const float wy = w * y;
		const float wz = w * z;

		Vec3f rotated = {
			(1.0f - 2.0f * (yy + zz)) * local.x + 2.0f * (xy - wz) * local.y + 2.0f * (xz + wy) * local.z,
			2.0f * (xy + wz) * local.x + (1.0f - 2.0f * (xx + zz)) * local.y + 2.0f * (yz - wx) * local.z,
			2.0f * (xz - wy) * local.x + 2.0f * (yz + wx) * local.y + (1.0f - 2.0f * (xx + yy)) * local.z
		};
		local = rotated;
	}

	local.x += transform->localTranslation.x;
	local.y += transform->localTranslation.y;
	local.z += transform->localTranslation.z;

	const float c = std::cos(transform->yawRadians);
	const float s = std::sin(transform->yawRadians);
	Vec3f out = {
		local.x * c - local.z * s + transform->translation.x,
		local.y + transform->translation.y,
		local.x * s + local.z * c + transform->translation.z
	};
	return out;
}

static bool projectVertex(const Vec3f &world, const RenderCamera &camera,
                          const Vec3f &right, const Vec3f &up, const Vec3f &forward,
                          float focalPixels, int width, int height, ProjectedVertex &out) {
	const Vec3f relative = sub3(world, camera.position);
	const float depth = dot3(relative, forward);
	if (depth <= 0.001f)
		return false;

	out.x = width * 0.5f + dot3(relative, right) * focalPixels / depth;
	out.y = height * 0.5f - dot3(relative, up) * focalPixels / depth;
	out.z = depth;
	return true;
}

static bool loadTexture(const MaterialData &material, const Common::Path &directory,
                        Graphics::ManagedSurface &texture) {
	if (!material.hasTexture || material.textureName.empty())
		return false;

	Common::String fileName = lowerAscii(material.textureName);

	// Mpx/bodies/interfaccia/interfaccia.mat replaces VETRO.TGA with the
	// shipped int_vetro.tga. Keeping this one known override here reproduces
	// the retail main-menu glass until the .mat evaluator is connected.
	if (fileName.equalsIgnoreCase("vetro.tga"))
		fileName = "int_vetro.tga";

	if (ResourceReader::decodeJgfFile(directory.appendComponent(fileName), texture))
		return true;

	// Preserve compatibility with installations whose filesystem kept the
	// uppercase editor spelling from the model.
	return ResourceReader::decodeJgfFile(directory.appendComponent(material.textureName), texture);
}

static void clearTarget(Graphics::ManagedSurface &target) {
	for (int y = 0; y < target.h; ++y) {
		byte *row = static_cast<byte *>(target.getBasePtr(0, y));
		for (int x = 0; x < target.w; ++x) {
			row[x * 4 + 0] = 0;
			row[x * 4 + 1] = 0;
			row[x * 4 + 2] = 0;
			row[x * 4 + 3] = 0xff;
		}
	}
}

static byte floatColor(float v) {
	return (byte)(clamp01(v) * 255.0f + 0.5f);
}

static void drawTriangle(Graphics::ManagedSurface &target, Common::Array<float> &zBuffer,
                         const ProjectedVertex pv[3], const Vec2f uv[3], bool haveUv,
                         const Graphics::ManagedSurface *texture, const MaterialData *material) {
	const float area = edge(pv[0].x, pv[0].y, pv[1].x, pv[1].y, pv[2].x, pv[2].y);
	if (std::fabs(area) < 1.0e-6f)
		return;

	float minXf = pv[0].x;
	float maxXf = pv[0].x;
	float minYf = pv[0].y;
	float maxYf = pv[0].y;
	for (int i = 1; i < 3; ++i) {
		if (pv[i].x < minXf) minXf = pv[i].x;
		if (pv[i].x > maxXf) maxXf = pv[i].x;
		if (pv[i].y < minYf) minYf = pv[i].y;
		if (pv[i].y > maxYf) maxYf = pv[i].y;
	}

	int minX = (int)std::floor(minXf);
	int maxX = (int)std::ceil(maxXf);
	int minY = (int)std::floor(minYf);
	int maxY = (int)std::ceil(maxYf);
	if (minX < 0) minX = 0;
	if (minY < 0) minY = 0;
	if (maxX >= target.w) maxX = target.w - 1;
	if (maxY >= target.h) maxY = target.h - 1;
	if (minX > maxX || minY > maxY)
		return;

	const float invZ0 = 1.0f / pv[0].z;
	const float invZ1 = 1.0f / pv[1].z;
	const float invZ2 = 1.0f / pv[2].z;

	for (int y = minY; y <= maxY; ++y) {
		for (int x = minX; x <= maxX; ++x) {
			const float px = x + 0.5f;
			const float py = y + 0.5f;
			const float w0 = edge(pv[1].x, pv[1].y, pv[2].x, pv[2].y, px, py) / area;
			const float w1 = edge(pv[2].x, pv[2].y, pv[0].x, pv[0].y, px, py) / area;
			const float w2 = 1.0f - w0 - w1;
			if (w0 < -1.0e-5f || w1 < -1.0e-5f || w2 < -1.0e-5f)
				continue;

			const float invDepth = w0 * invZ0 + w1 * invZ1 + w2 * invZ2;
			if (invDepth <= 0.0f)
				continue;
			const float depth = 1.0f / invDepth;
			const uint32 zIndex = (uint32)y * (uint32)target.w + (uint32)x;
			if (depth >= zBuffer[zIndex])
				continue;

			byte b = 0xff;
			byte g = 0xff;
			byte r = 0xff;
			byte a = 0xff;

			if (texture && haveUv && !texture->empty()) {
				float u = (w0 * uv[0].x * invZ0 + w1 * uv[1].x * invZ1 + w2 * uv[2].x * invZ2) / invDepth;
				float v = (w0 * uv[0].y * invZ0 + w1 * uv[1].y * invZ1 + w2 * uv[2].y * invZ2) / invDepth;

				// JapoTek's texture U axis is opposite the decoded JGF pixel axis.
				u = 1.0f - u;
				u = clamp01(u);
				v = clamp01(v);

				const int tx = (int)(u * (texture->w - 1) + 0.5f);
				const int ty = (int)(v * (texture->h - 1) + 0.5f);
				const byte *src = static_cast<const byte *>(texture->getBasePtr(tx, ty));
				b = src[0];
				g = src[1];
				r = src[2];
				a = src[3];
			} else if (material) {
				// color1 corresponds to the diffuse RGB triplet in the retail .mat
				// terminology and is a useful fallback for untextured geometry.
				r = floatColor(material->color1.x);
				g = floatColor(material->color1.y);
				b = floatColor(material->color1.z);
			}

			byte *dst = static_cast<byte *>(target.getBasePtr(x, y));
			dst[0] = b;
			dst[1] = g;
			dst[2] = r;
			dst[3] = a;
			zBuffer[zIndex] = depth;
		}
	}
}

static void renderFaceRange(const SceneModel &scene, const MeshData &mesh,
                            const MeshMaterialRange *range, const Common::Path &textureDirectory,
                            const RenderCamera &camera, const Vec3f &right, const Vec3f &up,
                            const Vec3f &forward, float focalPixels,
                            const RenderTransform *instanceTransform,
                            Graphics::ManagedSurface &target, Common::Array<float> &zBuffer) {
	uint32 firstFace = 0;
	uint32 faceCount = mesh.faceCount;
	const MaterialData *material = nullptr;
	Graphics::ManagedSurface texture;
	Graphics::ManagedSurface *texturePtr = nullptr;

	if (range) {
		firstFace = range->firstFace;
		faceCount = range->faceCount;
		const NamedMaterial *named = scene.findMaterial(range->name);
		if (named) {
			material = &named->data;
			if (loadTexture(named->data, textureDirectory, texture))
				texturePtr = &texture;
		}
	}

	for (uint32 face = firstFace; face < firstFace + faceCount; ++face) {
		const uint32 corner = face * 3;
		if (corner + 2 >= mesh.indices.size())
			break;

		ProjectedVertex projected[3];
		Vec2f uv[3];
		bool valid = true;
		for (uint32 i = 0; i < 3; ++i) {
			const uint32 vertexIndex = mesh.indices[corner + i];
			if (vertexIndex >= mesh.vertices.size()) {
				valid = false;
				break;
			}
			const Vec3f meshWorld = transformVertex(mesh.vertices[vertexIndex], mesh.transform);
			const Vec3f world = applyInstanceTransform(meshWorld, instanceTransform);
			if (!projectVertex(world, camera, right, up, forward, focalPixels,
			                   target.w, target.h, projected[i])) {
				valid = false;
				break;
			}
		}
		if (!valid)
			continue;

		const bool haveUv = mesh.hasTexcoords() && corner + 2 < mesh.texcoords.size();
		if (haveUv) {
			uv[0] = mesh.texcoords[corner + 0];
			uv[1] = mesh.texcoords[corner + 1];
			uv[2] = mesh.texcoords[corner + 2];
		} else {
			for (int i = 0; i < 3; ++i) {
				uv[i].x = 0.0f;
				uv[i].y = 0.0f;
			}
		}

		drawTriangle(target, zBuffer, projected, uv, haveUv, texturePtr, material);
	}
}

static bool renderScene(const SceneModel &scene, const Common::Path &textureDirectory,
                        const Common::Array<Common::String> &visibleMeshes,
                        const RenderCamera &camera, const Vec3f &right, const Vec3f &up,
                        const Vec3f &forward, float focalPixels,
                        const RenderTransform *instanceTransform,
                        Graphics::ManagedSurface &target, Common::Array<float> &zBuffer) {
	bool renderedAny = false;
	for (uint32 meshIndex = 0; meshIndex < scene.meshes.size(); ++meshIndex) {
		const NamedMesh &namedMesh = scene.meshes[meshIndex];
		const MeshData &mesh = namedMesh.data;
		if (!isVisible(namedMesh.name, visibleMeshes) || mesh.isFlesh() || mesh.vertices.empty())
			continue;

		if (mesh.materials.empty()) {
			renderFaceRange(scene, mesh, nullptr, textureDirectory, camera, right, up, forward,
			                focalPixels, instanceTransform, target, zBuffer);
			renderedAny = true;
			continue;
		}

		for (uint32 materialIndex = 0; materialIndex < mesh.materials.size(); ++materialIndex) {
			renderFaceRange(scene, mesh, &mesh.materials[materialIndex], textureDirectory,
			                camera, right, up, forward, focalPixels, instanceTransform,
			                target, zBuffer);
			renderedAny = true;
		}
	}
	return renderedAny;
}

} // namespace

bool SoftwareRenderer::render(const SceneModel &scene, const Common::String &cameraName,
                              const Common::Path &textureDirectory,
                              const Common::Array<Common::String> &visibleMeshes,
                              Graphics::ManagedSurface &target, int width, int height) const {
	if (width <= 0 || height <= 0 || scene.cameras.empty())
		return false;

	const NamedCamera *namedCamera = scene.findCamera(cameraName);
	if (!namedCamera)
		namedCamera = &scene.cameras[0];

	// P3D camera records store a 35 mm focal length rather than an angular
	// field of view. A 36 mm horizontal film gate reproduces the retail
	// interface framing at 800x600.
	if (namedCamera->data.fov <= 0.0f)
		return false;

	RenderCamera camera;
	camera.position = namedCamera->data.position;
	camera.target = namedCamera->data.target;
	camera.focalPixels = namedCamera->data.fov * width / 36.0f;
	return render(scene, camera, textureDirectory, visibleMeshes, target, width, height);
}

bool SoftwareRenderer::render(const SceneModel &scene, const RenderCamera &camera,
                              const Common::Path &textureDirectory,
                              const Common::Array<Common::String> &visibleMeshes,
                              Graphics::ManagedSurface &target, int width, int height) const {
	if (width <= 0 || height <= 0 || camera.focalPixels <= 0.0f)
		return false;

	Vec3f forward = sub3(camera.target, camera.position);
	if (!normalize3(forward))
		return false;

	const Vec3f worldUp = { 0.0f, 1.0f, 0.0f };
	Vec3f right = cross3(forward, worldUp);
	if (!normalize3(right)) {
		const Vec3f alternateUp = { 0.0f, 0.0f, 1.0f };
		right = cross3(forward, alternateUp);
		if (!normalize3(right))
			return false;
	}
	Vec3f up = cross3(right, forward);
	if (!normalize3(up))
		return false;

	const float focalPixels = camera.focalPixels;

	target.free();
	target.create((int16)width, (int16)height, Graphics::PixelFormat::createFormatBGRA32());
	clearTarget(target);

	Common::Array<float> zBuffer;
	zBuffer.resize((uint32)width * (uint32)height);
	for (uint32 i = 0; i < zBuffer.size(); ++i)
		zBuffer[i] = 1.0e30f;

	return renderScene(scene, textureDirectory, visibleMeshes, camera, right, up, forward,
	                   focalPixels, nullptr, target, zBuffer);
}

bool SoftwareRenderer::renderWithActor(const SceneModel &scene, const RenderCamera &camera,
                                       const Common::Path &textureDirectory,
                                       const Common::Array<Common::String> &visibleMeshes,
                                       const SceneModel &actor,
                                       const Common::Path &actorTextureDirectory,
                                       const Common::Array<Common::String> &actorVisibleMeshes,
                                       const RenderTransform &actorTransform,
                                       Graphics::ManagedSurface &target, int width, int height) const {
	if (width <= 0 || height <= 0 || camera.focalPixels <= 0.0f)
		return false;

	Vec3f forward = sub3(camera.target, camera.position);
	if (!normalize3(forward))
		return false;

	const Vec3f worldUp = { 0.0f, 1.0f, 0.0f };
	Vec3f right = cross3(forward, worldUp);
	if (!normalize3(right)) {
		const Vec3f alternateUp = { 0.0f, 0.0f, 1.0f };
		right = cross3(forward, alternateUp);
		if (!normalize3(right))
			return false;
	}
	Vec3f up = cross3(right, forward);
	if (!normalize3(up))
		return false;

	target.free();
	target.create((int16)width, (int16)height, Graphics::PixelFormat::createFormatBGRA32());
	clearTarget(target);

	Common::Array<float> zBuffer;
	zBuffer.resize((uint32)width * (uint32)height);
	for (uint32 i = 0; i < zBuffer.size(); ++i)
		zBuffer[i] = 1.0e30f;

	const bool renderedRoom = renderScene(scene, textureDirectory, visibleMeshes, camera,
	                                      right, up, forward, camera.focalPixels,
	                                      nullptr, target, zBuffer);
	const bool renderedActor = renderScene(actor, actorTextureDirectory, actorVisibleMeshes, camera,
	                                       right, up, forward, camera.focalPixels,
	                                       &actorTransform, target, zBuffer);
	return renderedRoom || renderedActor;
}

} // namespace ZeroComico
