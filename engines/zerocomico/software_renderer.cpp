/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: minimal software renderer for JapoTek 3D scenes
 */

#include "zerocomico/software_renderer.h"
#include "zerocomico/resource.h"
#include "zerocomico/wrapped_flic.h"

#include "graphics/pixelformat.h"

#include <cmath>
#include <cstring>

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

static bool buildCameraBasis(const RenderCamera &camera, Vec3f &right, Vec3f &up, Vec3f &forward) {
	forward = sub3(camera.target, camera.position);
	if (!normalize3(forward))
		return false;

	const Vec3f worldUp = { 0.0f, 1.0f, 0.0f };
	right = cross3(forward, worldUp);
	if (!normalize3(right)) {
		const Vec3f alternateUp = { 0.0f, 0.0f, 1.0f };
		right = cross3(forward, alternateUp);
		if (!normalize3(right))
			return false;
	}
	up = cross3(right, forward);
	if (!normalize3(up))
		return false;

	if (std::fabs(camera.rollRadians) > 1.0e-7f) {
		const float c = std::cos(camera.rollRadians);
		const float s = std::sin(camera.rollRadians);
		const Vec3f unrolledRight = right;
		const Vec3f unrolledUp = up;
		right.x = unrolledRight.x * c + unrolledUp.x * s;
		right.y = unrolledRight.y * c + unrolledUp.y * s;
		right.z = unrolledRight.z * c + unrolledUp.z * s;
		up.x = unrolledUp.x * c - unrolledRight.x * s;
		up.y = unrolledUp.y * c - unrolledRight.y * s;
		up.z = unrolledUp.z * c - unrolledRight.z * s;
	}

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

static bool containsIgnoreCase(const Common::Array<Common::String> &values,
                               const Common::String &value) {
	for (uint32 i = 0; i < values.size(); ++i)
		if (values[i].equalsIgnoreCase(value))
			return true;
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

	// The 4-component JACS rotation channel is axis-angle: xyz is the
	// rotation axis and the fourth value is the angle in radians. The retail
	// Stay root, for example, is approximately (-1, 0, 0, 0), i.e. identity.
	Vec3f axis = {
		transform->localRotation[0],
		transform->localRotation[1],
		transform->localRotation[2]
	};
	const float angle = transform->localRotation[3];
	if (std::fabs(angle) > 1.0e-7f && normalize3(axis)) {
		const float c = std::cos(angle);
		const float s = std::sin(angle);
		const float oneMinusC = 1.0f - c;
		const float projection = dot3(axis, local);

		Vec3f cross = cross3(axis, local);
		Vec3f rotated = {
			local.x * c + cross.x * s + axis.x * projection * oneMinusC,
			local.y * c + cross.y * s + axis.y * projection * oneMinusC,
			local.z * c + cross.z * s + axis.z * projection * oneMinusC
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


struct NearClipVertex {
	Vec3f world;
	Vec2f uv;
};

static NearClipVertex interpolateNearClipVertex(const NearClipVertex &a,
                                                 const NearClipVertex &b,
                                                 float t) {
	NearClipVertex out;
	out.world.x = a.world.x + (b.world.x - a.world.x) * t;
	out.world.y = a.world.y + (b.world.y - a.world.y) * t;
	out.world.z = a.world.z + (b.world.z - a.world.z) * t;
	out.uv.x = a.uv.x + (b.uv.x - a.uv.x) * t;
	out.uv.y = a.uv.y + (b.uv.y - a.uv.y) * t;
	return out;
}

// Clip one triangle against the camera near plane instead of dropping the
// complete face when only one vertex is behind the camera. Large indoor room
// triangles frequently straddle this plane.
static uint32 clipTriangleToNearPlane(const NearClipVertex input[3],
                                      const RenderCamera &camera,
                                      const Vec3f &forward,
                                      NearClipVertex output[4]) {
	const float nearDepth = 0.001f;
	NearClipVertex polygonA[4];
	NearClipVertex polygonB[4];
	for (uint32 i = 0; i < 3; ++i)
		polygonA[i] = input[i];
	uint32 count = 3;

	uint32 outCount = 0;
	for (uint32 i = 0; i < count; ++i) {
		const NearClipVertex &current = polygonA[i];
		const NearClipVertex &previous = polygonA[(i + count - 1) % count];
		const float currentDepth = dot3(sub3(current.world, camera.position), forward);
		const float previousDepth = dot3(sub3(previous.world, camera.position), forward);
		const bool currentInside = currentDepth > nearDepth;
		const bool previousInside = previousDepth > nearDepth;

		if (currentInside != previousInside) {
			const float denominator = currentDepth - previousDepth;
			if (std::fabs(denominator) > 1.0e-12f) {
				const float t = (nearDepth - previousDepth) / denominator;
				polygonB[outCount++] = interpolateNearClipVertex(previous, current, t);
			}
		}
		if (currentInside)
			polygonB[outCount++] = current;
	}

	for (uint32 i = 0; i < outCount; ++i)
		output[i] = polygonB[i];
	return outCount;
}

static void freeAnimatedFrames(AnimatedTextureCacheEntry &entry) {
	for (uint32 i = 0; i < entry.frames.size(); ++i) {
		if (entry.frames[i]) {
			entry.frames[i]->free();
			delete entry.frames[i];
		}
	}
	entry.frames.clear();
}

static bool loadWrappedFlicFrames(const Common::Path &path,
                                  const Common::String &key,
                                  AnimatedTextureCacheEntry &entry) {
	WrappedFlicDecoder decoder;
	if (!decoder.loadJfxFile(path))
		return false;
	entry.key = key;
	entry.frameDelayMs = 40;
	const uint32 count = decoder.getFrameCount();
	if (!count)
		return false;

	for (uint32 i = 0; i < count; ++i) {
		const Graphics::Surface *frame = decoder.decodeNextFrame();
		if (!frame) {
			freeAnimatedFrames(entry);
			return false;
		}
		if (i == 0 && decoder.getCurFrameDelay() > 0)
			entry.frameDelayMs = (uint32)decoder.getCurFrameDelay();

		Graphics::Surface *converted =
			frame->convertTo(Graphics::PixelFormat::createFormatBGRA32(), decoder.getPalette());
		if (!converted) {
			freeAnimatedFrames(entry);
			return false;
		}
		Graphics::ManagedSurface *stored = new Graphics::ManagedSurface();
		stored->create(converted->w, converted->h, converted->format);
		const uint32 rowBytes = (uint32)converted->w * (uint32)converted->format.bytesPerPixel;
		for (int y = 0; y < converted->h; ++y)
			memcpy(stored->getBasePtr(0, y), converted->getBasePtr(0, y), rowBytes);
		converted->free();
		delete converted;
		entry.frames.push_back(stored);
	}
	return true;
}

static const Graphics::ManagedSurface *loadTextureCached(
		const MaterialData &material, const Common::Path &directory,
		Common::Array<Common::String> &cacheKeys,
		Common::Array<Graphics::ManagedSurface *> &cache,
		Common::Array<AnimatedTextureCacheEntry> &animatedCache) {
	if (!material.hasTexture || material.textureName.empty())
		return nullptr;

	Common::String fileName = lowerAscii(material.textureName);
	if (fileName.hasSuffixIgnoreCase(".flc")) {
		const Common::String key = directory.toString() + "/" + fileName;
		AnimatedTextureCacheEntry *entry = nullptr;
		for (uint32 i = 0; i < animatedCache.size(); ++i) {
			if (animatedCache[i].key.equalsIgnoreCase(key)) {
				entry = &animatedCache[i];
				break;
			}
		}
		if (!entry) {
			AnimatedTextureCacheEntry decoded;
			bool loaded = loadWrappedFlicFrames(directory.appendComponent(fileName), key, decoded);
			if (!loaded && fileName != material.textureName)
				loaded = loadWrappedFlicFrames(directory.appendComponent(material.textureName), key, decoded);
			if (!loaded)
				return nullptr;
			animatedCache.push_back(decoded);
			entry = &animatedCache[animatedCache.size() - 1];
		}
		const uint32 delay = entry->frameDelayMs ? entry->frameDelayMs : 40;
		const uint32 frameIndex = (material.userEffectElapsedMs / delay) % entry->frames.size();
		return entry->frames[frameIndex];
	}

	if (fileName.equalsIgnoreCase("vetro.tga"))
		fileName = "int_vetro.tga";
	const Common::String key = directory.toString() + "/" + fileName;
	for (uint32 i = 0; i < cacheKeys.size(); ++i)
		if (cacheKeys[i].equalsIgnoreCase(key))
			return cache[i];

	Graphics::ManagedSurface *texture = new Graphics::ManagedSurface();
	bool loaded =
		ResourceReader::decodeJgfFile(directory.appendComponent(fileName), *texture) ||
		ResourceReader::decodeJgfFile(directory.appendComponent(material.textureName), *texture);
	if (!loaded) {
		const Common::String directoryName = directory.toString();
		const uint32 marker = directoryName.find("/backgrd");
		if (marker != Common::String::npos) {
			const Common::Path helper =
				Common::Path(directoryName.substr(0, marker)).appendComponent("bodies").appendComponent("helpers");
			loaded = ResourceReader::decodeJgfFile(helper.appendComponent(fileName), *texture) ||
			         ResourceReader::decodeJgfFile(helper.appendComponent(material.textureName), *texture);
		}
	}
	if (!loaded) {
		delete texture;
		return nullptr;
	}
	cacheKeys.push_back(key);
	cache.push_back(texture);
	return texture;
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

static bool lightTargetsMesh(const LightData &light, const Common::String &meshName) {
	if (light.linkedNames.empty())
		return true;
	for (uint32 i = 0; i < light.linkedNames.size(); ++i)
		if (light.linkedNames[i].equalsIgnoreCase(meshName))
			return true;
	return false;
}

static Vec3f evaluateFaceLighting(const SceneModel &lightingScene,
                                  const Common::String &meshName,
                                  const Vec3f world[3],
                                  const RenderCamera &camera) {
	Vec3f centroid = {
		(world[0].x + world[1].x + world[2].x) / 3.0f,
		(world[0].y + world[1].y + world[2].y) / 3.0f,
		(world[0].z + world[1].z + world[2].z) / 3.0f
	};

	const Vec3f edge0 = sub3(world[1], world[0]);
	const Vec3f edge1 = sub3(world[2], world[0]);
	Vec3f normal = cross3(edge0, edge1);
	if (!normalize3(normal)) {
		Vec3f unlit = {1.0f, 1.0f, 1.0f};
		return unlit;
	}

	// The software rasterizer is two-sided. Orient the geometric face normal
	// toward the visible side before evaluating diffuse lighting.
	const Vec3f toCamera = sub3(camera.position, centroid);
	if (dot3(normal, toCamera) < 0.0f) {
		normal.x = -normal.x;
		normal.y = -normal.y;
		normal.z = -normal.z;
	}

	Vec3f accumulated = {0.0f, 0.0f, 0.0f};
	bool contributed = false;

	for (uint32 i = 0; i < lightingScene.lights.size(); ++i) {
		const NamedLight &namedLight = lightingScene.lights[i];
		const LightData &light = namedLight.data;

		bool enabled = true;
		if (camera.lightStateNames && camera.lightStateEnabled) {
			const uint32 overrideCount =
				camera.lightStateNames->size() < camera.lightStateEnabled->size()
					? camera.lightStateNames->size() : camera.lightStateEnabled->size();
			for (uint32 stateIndex = 0; stateIndex < overrideCount; ++stateIndex) {
				if ((*camera.lightStateNames)[stateIndex].equalsIgnoreCase(namedLight.name)) {
					enabled = (*camera.lightStateEnabled)[stateIndex];
					break;
				}
			}
		}
		if (!enabled || !lightTargetsMesh(light, meshName))
			continue;

		const float intensity = light.params[0];
		const float innerRange = light.params[1];
		const float outerRange = light.params[2];
		if (intensity <= 0.0f || outerRange <= 0.0f)
			continue;

		Vec3f toLight = sub3(light.position, centroid);
		const float distance2 = dot3(toLight, toLight);
		if (distance2 <= 1.0e-10f)
			continue;
		const float distance = std::sqrt(distance2);
		if (distance >= outerRange)
			continue;
		if (!normalize3(toLight))
			continue;

		float attenuation = 1.0f;
		if (distance > innerRange && outerRange > innerRange)
			attenuation = (outerRange - distance) / (outerRange - innerRange);
		attenuation = clamp01(attenuation);

		const float diffuse = dot3(normal, toLight);
		if (diffuse <= 0.0f)
			continue;

		const float factor = intensity * attenuation * diffuse;
		accumulated.x += light.color.x * factor;
		accumulated.y += light.color.y * factor;
		accumulated.z += light.color.z * factor;
		contributed = true;
	}

	// Conservative fallback: geometry that receives no decoded light keeps the
	// previous unshaded appearance instead of becoming black due to an
	// incomplete light-model interpretation.
	if (!contributed) {
		accumulated.x = accumulated.y = accumulated.z = 1.0f;
	} else {
		accumulated.x = clamp01(accumulated.x);
		accumulated.y = clamp01(accumulated.y);
		accumulated.z = clamp01(accumulated.z);
	}
	return accumulated;
}

static void drawTriangle(Graphics::ManagedSurface &target, Common::Array<float> &zBuffer,
                         const ProjectedVertex pv[3], const Vec2f uv[3], bool haveUv,
                         const Graphics::ManagedSurface *texture, const MaterialData *material,
                         const RenderCamera &camera, const Vec3f &shade) {
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

			if (camera.shadeEnabled) {
				r = (byte)((float)r * shade.x + 0.5f);
				g = (byte)((float)g * shade.y + 0.5f);
				b = (byte)((float)b * shade.z + 0.5f);
			}

			if (camera.depthCueEnabled && camera.depthCueEnd > camera.depthCueStart) {
				const float cue = clamp01(
					(depth - camera.depthCueStart) /
					(camera.depthCueEnd - camera.depthCueStart));
				const float keep = 1.0f - cue;
				b = (byte)(b * keep + 0.5f);
				g = (byte)(g * keep + 0.5f);
				r = (byte)(r * keep + 0.5f);
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

static void renderFaceRange(const SceneModel &scene, const SceneModel &lightingScene,
                            const Common::String &meshName, const MeshData &mesh,
                            const Common::Array<Vec3f> *posedVertices,
                            const MeshMaterialRange *range, const Common::Path &textureDirectory,
                            const RenderCamera &camera, const Vec3f &right, const Vec3f &up,
                            const Vec3f &forward, float focalPixels,
                            const RenderTransform *instanceTransform,
                            Common::Array<Common::String> &textureCacheKeys,
                            Common::Array<Graphics::ManagedSurface *> &textureCache,
                            Common::Array<AnimatedTextureCacheEntry> &animatedTextureCache,
                            Graphics::ManagedSurface &target, Common::Array<float> &zBuffer) {
	uint32 firstFace = 0;
	uint32 faceCount = mesh.faceCount;
	const MaterialData *material = nullptr;
	const Graphics::ManagedSurface *texturePtr = nullptr;

	if (range) {
		firstFace = range->firstFace;
		faceCount = range->faceCount;
		const NamedMaterial *named = scene.findMaterial(range->name);
		if (named) {
			material = &named->data;
			const Common::Path &materialDirectory =
				named->sourceDirectory.empty() ? textureDirectory : named->sourceDirectory;
			texturePtr = loadTextureCached(named->data, materialDirectory,
			                               textureCacheKeys, textureCache,
			                               animatedTextureCache);
		}
	}

	for (uint32 face = firstFace; face < firstFace + faceCount; ++face) {
		const uint32 corner = face * 3;
		if (corner + 2 >= mesh.indices.size())
			break;

		const bool haveUv = mesh.hasTexcoords() && corner + 2 < mesh.texcoords.size();
		NearClipVertex input[3];
		bool valid = true;
		for (uint32 i = 0; i < 3; ++i) {
			const uint32 vertexIndex = mesh.indices[corner + i];
			const uint32 availableVertices = posedVertices ? posedVertices->size() : mesh.vertices.size();
			if (vertexIndex >= availableVertices) {
				valid = false;
				break;
			}

			Vec3f meshWorld;
			if (posedVertices) {
				meshWorld = (*posedVertices)[vertexIndex];
			} else if (mesh.isSkinnedParent()) {
				// The reconstructed bind pose is already actor-local.
				meshWorld = mesh.vertices[vertexIndex];
			} else {
				meshWorld = transformVertex(mesh.vertices[vertexIndex], mesh.transform);
			}
			input[i].world = applyInstanceTransform(meshWorld, instanceTransform);
			if (haveUv) {
				input[i].uv = mesh.texcoords[corner + i];
			} else {
				input[i].uv.x = 0.0f;
				input[i].uv.y = 0.0f;
			}
		}
		if (!valid)
			continue;

		NearClipVertex clipped[4];
		const uint32 clippedCount = clipTriangleToNearPlane(input, camera, forward, clipped);
		if (clippedCount < 3)
			continue;

		// A triangle clipped by one plane is either a triangle or a quad.
		// Triangulate the clipped polygon as a fan while preserving UVs.
		for (uint32 triangle = 1; triangle + 1 < clippedCount; ++triangle) {
			const uint32 fanIndices[3] = { 0, triangle, triangle + 1 };
			ProjectedVertex projected[3];
			Vec3f worldVertices[3];
			Vec2f uv[3];
			bool projectedValid = true;
			for (uint32 i = 0; i < 3; ++i) {
				const NearClipVertex &vertex = clipped[fanIndices[i]];
				worldVertices[i] = vertex.world;
				uv[i] = vertex.uv;
				if (!projectVertex(vertex.world, camera, right, up, forward, focalPixels,
				                   target.w, target.h, projected[i])) {
					projectedValid = false;
					break;
				}
			}
			if (!projectedValid)
				continue;

			Vec3f shade = {1.0f, 1.0f, 1.0f};
			if (camera.shadeEnabled)
				shade = evaluateFaceLighting(lightingScene, meshName, worldVertices, camera);
			drawTriangle(target, zBuffer, projected, uv, haveUv, texturePtr, material, camera, shade);
		}
	}
}

static bool renderScene(const SceneModel &scene, const SceneModel &lightingScene,
                        const Common::Path &textureDirectory,
                        const Common::Array<Common::String> &visibleMeshes,
                        const RenderCamera &camera, const Vec3f &right, const Vec3f &up,
                        const Vec3f &forward, float focalPixels,
                        const RenderTransform *instanceTransform,
                        Common::Array<Common::String> &textureCacheKeys,
                        Common::Array<Graphics::ManagedSurface *> &textureCache,
                        Common::Array<AnimatedTextureCacheEntry> &animatedTextureCache,
                        Graphics::ManagedSurface &target, Common::Array<float> &zBuffer) {
	bool renderedAny = false;
	for (uint32 meshIndex = 0; meshIndex < scene.meshes.size(); ++meshIndex) {
		const NamedMesh &namedMesh = scene.meshes[meshIndex];
		const MeshData &mesh = namedMesh.data;
		if (!isVisible(namedMesh.name, visibleMeshes) || mesh.isFlesh() || mesh.vertices.empty())
			continue;

		const Common::Array<Vec3f> *posedVertices =
			namedMesh.posedVertices.empty() ? nullptr : &namedMesh.posedVertices;

		if (mesh.materials.empty()) {
			renderFaceRange(scene, lightingScene, namedMesh.name, mesh, posedVertices, nullptr, textureDirectory, camera, right, up, forward,
			                focalPixels, instanceTransform, textureCacheKeys, textureCache,
			                animatedTextureCache, target, zBuffer);
			renderedAny = true;
			continue;
		}

		for (uint32 materialIndex = 0; materialIndex < mesh.materials.size(); ++materialIndex) {
			renderFaceRange(scene, lightingScene, namedMesh.name, mesh, posedVertices, &mesh.materials[materialIndex], textureDirectory,
			                camera, right, up, forward, focalPixels, instanceTransform,
			                textureCacheKeys, textureCache, animatedTextureCache, target, zBuffer);
			renderedAny = true;
		}
	}
	return renderedAny;
}

} // namespace

SoftwareRenderer::SoftwareRenderer() {
}

SoftwareRenderer::~SoftwareRenderer() {
	for (uint32 i = 0; i < _textureCache.size(); ++i) {
		if (_textureCache[i]) {
			_textureCache[i]->free();
			delete _textureCache[i];
		}
	}
	_textureCache.clear();
	_textureCacheKeys.clear();
	for (uint32 i = 0; i < _animatedTextureCache.size(); ++i)
		freeAnimatedFrames(_animatedTextureCache[i]);
	_animatedTextureCache.clear();
}

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

	Vec3f right;
	Vec3f up;
	Vec3f forward;
	if (!buildCameraBasis(camera, right, up, forward))
		return false;

	const float focalPixels = camera.focalPixels;

	target.free();
	target.create((int16)width, (int16)height, Graphics::PixelFormat::createFormatBGRA32());
	clearTarget(target);

	Common::Array<float> zBuffer;
	zBuffer.resize((uint32)width * (uint32)height);
	for (uint32 i = 0; i < zBuffer.size(); ++i)
		zBuffer[i] = 1.0e30f;

	return renderScene(scene, scene, textureDirectory, visibleMeshes, camera, right, up, forward,
	                   focalPixels, nullptr, _textureCacheKeys, _textureCache,
	                   _animatedTextureCache, target, zBuffer);
}

bool SoftwareRenderer::renderWithActor(const SceneModel &scene, const RenderCamera &camera,
                                       const Common::Path &textureDirectory,
                                       const Common::Array<Common::String> &visibleMeshes,
                                       const SceneModel &actor,
                                       const Common::Path &actorTextureDirectory,
                                       const Common::Array<Common::String> &actorVisibleMeshes,
                                       const RenderTransform &actorTransform,
                                       Graphics::ManagedSurface &target, int width, int height) const {
	RenderActor renderActor;
	renderActor.scene = &actor;
	renderActor.textureDirectory = actorTextureDirectory;
	renderActor.visibleMeshes = actorVisibleMeshes;
	renderActor.transform = actorTransform;

	Common::Array<RenderActor> actors;
	actors.push_back(renderActor);
	return renderWithActors(scene, camera, textureDirectory, visibleMeshes,
	                        actors, target, width, height);
}

bool SoftwareRenderer::renderWithActors(const SceneModel &scene, const RenderCamera &camera,
                                        const Common::Path &textureDirectory,
                                        const Common::Array<Common::String> &visibleMeshes,
                                        const Common::Array<RenderActor> &actors,
                                        Graphics::ManagedSurface &target, int width, int height) const {
	if (width <= 0 || height <= 0 || camera.focalPixels <= 0.0f)
		return false;

	Vec3f right;
	Vec3f up;
	Vec3f forward;
	if (!buildCameraBasis(camera, right, up, forward))
		return false;

	target.free();
	target.create((int16)width, (int16)height, Graphics::PixelFormat::createFormatBGRA32());
	clearTarget(target);

	Common::Array<float> zBuffer;
	zBuffer.resize((uint32)width * (uint32)height);
	for (uint32 i = 0; i < zBuffer.size(); ++i)
		zBuffer[i] = 1.0e30f;

	bool rendered = renderScene(scene, scene, textureDirectory, visibleMeshes, camera,
	                            right, up, forward, camera.focalPixels,
	                            nullptr, _textureCacheKeys, _textureCache,
	                            _animatedTextureCache, target, zBuffer);

	for (uint32 actorIndex = 0; actorIndex < actors.size(); ++actorIndex) {
		const RenderActor &actor = actors[actorIndex];
		if (!actor.scene)
			continue;
		rendered = renderScene(*actor.scene, scene, actor.textureDirectory, actor.visibleMeshes,
		                       camera, right, up, forward, camera.focalPixels,
		                       &actor.transform, _textureCacheKeys, _textureCache,
		                       _animatedTextureCache, target, zBuffer) || rendered;
	}

	return rendered;
}

static void pickSceneMeshes(const SceneModel &scene, const RenderCamera &camera,
                            const Vec3f &right, const Vec3f &up, const Vec3f &forward,
                            int screenX, int screenY,
                            const Common::Array<Common::String> &candidates,
                            const RenderTransform *instanceTransform,
                            int width, int height, float &bestDepth,
                            Common::String &pickedName) {
	const float px = (float)screenX + 0.5f;
	const float py = (float)screenY + 0.5f;

	for (uint32 candidateIndex = 0; candidateIndex < candidates.size(); ++candidateIndex) {
		Common::Array<Common::String> candidateMeshes;
		if (scene.findMesh(candidates[candidateIndex])) {
			candidateMeshes.push_back(candidates[candidateIndex]);
		} else {
			// Retail puzzle objects may target a JACS hierarchy root rather than a
			// drawable mesh. Room1_4's r14_esplor is the progression-critical Mp1
			// example: its geometry lives in r14_espc*/esps*/espd* child meshes.
			scene.meshesForHierarchy(candidates[candidateIndex], candidateMeshes);
		}

		for (uint32 meshIndex = 0; meshIndex < candidateMeshes.size(); ++meshIndex) {
			const NamedMesh *namedMesh = scene.findMesh(candidateMeshes[meshIndex]);
			if (!namedMesh || namedMesh->data.isFlesh())
				continue;

			const MeshData &mesh = namedMesh->data;
			const Common::Array<Vec3f> *posed =
				namedMesh->posedVertices.empty() ? nullptr : &namedMesh->posedVertices;
			const uint32 vertexCount = posed ? posed->size() : mesh.vertices.size();
			if (vertexCount == 0 || mesh.indices.size() < 3)
				continue;

			for (uint32 corner = 0; corner + 2 < mesh.indices.size(); corner += 3) {
				ProjectedVertex projected[3];
				bool valid = true;
				for (uint32 i = 0; i < 3; ++i) {
					const uint32 vertexIndex = mesh.indices[corner + i];
					if (vertexIndex >= vertexCount) {
						valid = false;
						break;
					}

					Vec3f world;
					if (posed)
						world = (*posed)[vertexIndex];
					else if (mesh.isSkinnedParent())
						world = mesh.vertices[vertexIndex];
					else
						world = transformVertex(mesh.vertices[vertexIndex], mesh.transform);
					world = applyInstanceTransform(world, instanceTransform);

					if (!projectVertex(world, camera, right, up, forward, camera.focalPixels,
					                   width, height, projected[i])) {
						valid = false;
						break;
					}
				}
				if (!valid)
					continue;

				const float area = edge(projected[0].x, projected[0].y,
				                        projected[1].x, projected[1].y,
				                        projected[2].x, projected[2].y);
				if (std::fabs(area) < 1.0e-6f)
					continue;

				const float w0 = edge(projected[1].x, projected[1].y,
				                      projected[2].x, projected[2].y, px, py) / area;
				const float w1 = edge(projected[2].x, projected[2].y,
				                      projected[0].x, projected[0].y, px, py) / area;
				const float w2 = 1.0f - w0 - w1;
				if (w0 < -0.0025f || w1 < -0.0025f || w2 < -0.0025f)
					continue;

				const float invDepth =
					w0 / projected[0].z + w1 / projected[1].z + w2 / projected[2].z;
				if (invDepth <= 0.0f)
					continue;
				const float depth = 1.0f / invDepth;
				if (depth < bestDepth) {
					bestDepth = depth;
					pickedName = candidates[candidateIndex];
				}
			}
		}
	}
}

bool SoftwareRenderer::pickMesh(const SceneModel &scene, const RenderCamera &camera,
                                int screenX, int screenY,
                                const Common::Array<Common::String> &candidates,
                                Common::String &pickedName, int width, int height) const {
	pickedName.clear();
	if (width <= 0 || height <= 0 || camera.focalPixels <= 0.0f)
		return false;

	Vec3f right;
	Vec3f up;
	Vec3f forward;
	if (!buildCameraBasis(camera, right, up, forward))
		return false;

	float bestDepth = 1.0e30f;
	pickSceneMeshes(scene, camera, right, up, forward, screenX, screenY,
	                candidates, nullptr, width, height, bestDepth, pickedName);
	return !pickedName.empty();
}

bool SoftwareRenderer::pickMeshWithActors(const SceneModel &scene, const RenderCamera &camera,
                                          int screenX, int screenY,
                                          const Common::Array<Common::String> &candidates,
                                          const Common::Array<RenderActor> &actors,
                                          Common::String &pickedName,
                                          int width, int height) const {
	pickedName.clear();
	if (width <= 0 || height <= 0 || camera.focalPixels <= 0.0f)
		return false;

	Vec3f right;
	Vec3f up;
	Vec3f forward;
	if (!buildCameraBasis(camera, right, up, forward))
		return false;

	float bestDepth = 1.0e30f;
	pickSceneMeshes(scene, camera, right, up, forward, screenX, screenY,
	                candidates, nullptr, width, height, bestDepth, pickedName);
	for (uint32 actorIndex = 0; actorIndex < actors.size(); ++actorIndex) {
		const RenderActor &actor = actors[actorIndex];
		if (!actor.scene)
			continue;

		// Preserve direct sub-mesh picking for any caller that explicitly names
		// actor geometry.
		pickSceneMeshes(*actor.scene, camera, right, up, forward, screenX, screenY,
		                candidates, &actor.transform, width, height,
		                bestDepth, pickedName);

		// Puzzle scripts normally name a CPU character by its JACS hierarchy root,
		// not by one of the drawable child meshes. When that logical name is a
		// candidate, hit-test the actor's currently visible geometry and report the
		// hierarchy root back to the puzzle layer.
		if (actor.interactionName.empty() ||
		    !containsIgnoreCase(candidates, actor.interactionName))
			continue;

		Common::Array<Common::String> actorMeshes = actor.visibleMeshes;
		if (actorMeshes.empty()) {
			for (uint32 meshIndex = 0; meshIndex < actor.scene->meshes.size(); ++meshIndex) {
				if (!actor.scene->meshes[meshIndex].data.isFlesh())
					actorMeshes.push_back(actor.scene->meshes[meshIndex].name);
			}
		}

		const float previousBestDepth = bestDepth;
		pickSceneMeshes(*actor.scene, camera, right, up, forward, screenX, screenY,
		                actorMeshes, &actor.transform, width, height,
		                bestDepth, pickedName);
		if (bestDepth < previousBestDepth)
			pickedName = actor.interactionName;
	}
	return !pickedName.empty();
}

} // namespace ZeroComico
