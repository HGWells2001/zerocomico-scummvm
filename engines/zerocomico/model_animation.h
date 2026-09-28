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

} // namespace ZeroComico

#endif
