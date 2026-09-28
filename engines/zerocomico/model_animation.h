/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: JACS animation data
 */

#ifndef ZEROCOMICO_MODEL_ANIMATION_H
#define ZEROCOMICO_MODEL_ANIMATION_H

#include "common/array.h"
#include "common/scummsys.h"
#include "common/str.h"

namespace ZeroComico {

class ModelArchive;
struct ModelRecord;

enum AnimationTrackKind {
	kAnimTransform,
	kAnimLight,
	kAnimCamera,
	kAnimTarget,
	kAnimExtendedLight
};

struct AnimationKey {
	float frame;
	float tension;
	float continuity;
	float bias;
	float value[4];
};

struct AnimationChannel {
	bool enabled;
	uint32 components;
	Common::Array<AnimationKey> keys;
};

struct AnimationTrack {
	uint32 tag;
	Common::String targetName;
	AnimationTrackKind kind;
	Common::Array<AnimationChannel> channels;

	// Mesh/object tracks have a separate visibility event stream. The retail
	// engine toggles visibility at these integer frame positions.
	bool visibilityEnabled;
	Common::Array<uint32> visibilityFrames;
};

struct AnimationClip {
	Common::String sourceName;
	uint32 startFrame;
	uint32 endFrame;
	Common::Array<AnimationTrack> tracks;
};

class AnimationDecoder {
public:
	static bool decodeClip(const ModelArchive &archive, const ModelRecord &record, AnimationClip &out);
};

class AnimationSampler {
public:
	// Samples the decoded key stream at an arbitrary animation frame using the
	// stored Kochanek-Bartels tension/continuity/bias values. Endpoint tangents
	// fall back to the adjacent segment slope, matching the single-sided case.
	static bool sampleChannel(const AnimationChannel &channel, float frame, float out[4]);

	// Finds a transform track by target name and samples translation, scale and
	// axis-angle rotation. Missing individual channels keep their identity values.
	static bool sampleTransform(const AnimationClip &clip, const Common::String &targetName,
	                            float frame, float translation[3], float scale[3],
	                            float rotation[4]);

	// Camera clips carry position, angular FOV and roll channels, while the
	// companion *.target record carries the animated look-at point.
	static bool sampleCamera(const AnimationClip &clip, const Common::String &targetName,
	                         float frame, float position[3], float &fovDegrees, float &roll);
	static bool sampleTarget(const AnimationClip &clip, const Common::String &targetName,
	                         float frame, float target[3]);
};

} // namespace ZeroComico

#endif
