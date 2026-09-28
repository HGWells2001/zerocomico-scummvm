/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: JACS animation data
 */

#include "zerocomico/model_animation.h"
#include "zerocomico/model.h"

#include "common/endian.h"

namespace ZeroComico {

namespace {

static const uint32 kRangeTag = 0xf0f01234;

class BodyView {
public:
	BodyView(const ModelArchive &archive, const ModelRecord &record)
		: data(nullptr), size(0) {
		if (!record.group && record.bodyOffset <= archive.data().size() &&
		    record.bodySize <= archive.data().size() - record.bodyOffset) {
			data = archive.data().data() + record.bodyOffset;
			size = record.bodySize;
		}
	}

	bool valid() const { return data != nullptr; }

	const byte *data;
	uint32 size;
};

static bool readU32(const BodyView &v, uint32 &pos, uint32 &out) {
	if (pos > v.size || v.size - pos < 4)
		return false;
	out = READ_LE_UINT32(v.data + pos);
	pos += 4;
	return true;
}

static bool readFloat(const BodyView &v, uint32 &pos, float &out) {
	if (pos > v.size || v.size - pos < 4)
		return false;
	out = READ_LE_FLOAT32(v.data + pos);
	pos += 4;
	return true;
}

static bool validNameByte(byte c) {
	return c >= 0x20 && c < 0x7f;
}

static bool readName32(const BodyView &v, uint32 &pos, Common::String &out) {
	if (pos > v.size || v.size - pos < 32)
		return false;

	const byte *p = v.data + pos;
	uint32 length = 0;
	while (length < 32 && p[length] != 0) {
		if (!validNameByte(p[length]))
			return false;
		++length;
	}
	if (length == 0)
		return false;

	// The original editor fills unused name bytes with ASCII '0'.
	if (length < 32) {
		for (uint32 i = length + 1; i < 32; ++i) {
			if (p[i] != 0 && p[i] != '0')
				return false;
		}
	}

	out = Common::String(reinterpret_cast<const char *>(p), length);
	pos += 32;
	return true;
}

static bool parseChannel(const BodyView &v, uint32 &pos, uint32 components, AnimationChannel &out) {
	uint32 enabled = 0;
	uint32 count = 0;
	if (!readU32(v, pos, enabled) || !readU32(v, pos, count))
		return false;
	if (enabled > 1 || components == 0 || components > 4)
		return false;

	const uint32 floatsPerKey = 4 + components;
	if (count > (v.size - pos) / (floatsPerKey * 4))
		return false;

	out.enabled = enabled != 0;
	out.components = components;
	out.keys.clear();
	out.keys.resize(count);

	for (uint32 i = 0; i < count; ++i) {
		AnimationKey &key = out.keys[i];
		for (int k = 0; k < 4; ++k)
			key.value[k] = 0.0f;
		if (!readFloat(v, pos, key.frame) ||
		    !readFloat(v, pos, key.tension) ||
		    !readFloat(v, pos, key.continuity) ||
		    !readFloat(v, pos, key.bias))
			return false;
		for (uint32 c = 0; c < components; ++c) {
			if (!readFloat(v, pos, key.value[c]))
				return false;
		}
	}

	return true;
}

static bool parseTrackPayload(const BodyView &v, uint32 payloadPos, AnimationTrackKind kind,
                              AnimationTrack &track, uint32 &endPos) {
	uint32 pos = payloadPos;
	track.kind = kind;
	track.channels.clear();
	track.visibilityEnabled = false;
	track.visibilityFrames.clear();

	uint32 dims[5];
	uint32 channelCount = 0;
	switch (kind) {
	case kAnimTransform:
		dims[0] = 3; dims[1] = 3; dims[2] = 4; channelCount = 3;
		break;
	case kAnimLight:
		dims[0] = 3; dims[1] = 3; channelCount = 2;
		break;
	case kAnimCamera:
		dims[0] = 3; dims[1] = 1; dims[2] = 1; channelCount = 3;
		break;
	case kAnimTarget:
		dims[0] = 3; channelCount = 1;
		break;
	case kAnimExtendedLight:
		dims[0] = 3; dims[1] = 3; dims[2] = 1; channelCount = 3;
		break;
	}

	for (uint32 i = 0; i < channelCount; ++i) {
		AnimationChannel ch;
		if (!parseChannel(v, pos, dims[i], ch))
			return false;
		track.channels.push_back(ch);
	}

	if (kind == kAnimTransform) {
		uint32 enabled = 0;
		if (!readU32(v, pos, enabled) || enabled > 1)
			return false;
		track.visibilityEnabled = enabled != 0;
		if (track.visibilityEnabled) {
			uint32 count = 0;
			if (!readU32(v, pos, count) || count > (v.size - pos) / 4)
				return false;
			track.visibilityFrames.resize(count);
			for (uint32 i = 0; i < count; ++i) {
				if (!readU32(v, pos, track.visibilityFrames[i]))
					return false;
			}
		}
	} else if (kind == kAnimExtendedLight) {
		uint32 tag = 0;
		if (!readU32(v, pos, tag) || tag != kRangeTag)
			return false;

		for (int i = 0; i < 2; ++i) {
			AnimationChannel ch;
			if (!parseChannel(v, pos, 1, ch))
				return false;
			track.channels.push_back(ch);
		}
	}

	endPos = pos;
	return true;
}

static bool parseTracks(const BodyView &v, uint32 pos, Common::Array<AnimationTrack> &tracks) {
	// The three-byte outer record terminator follows this byte. Together they
	// form the dword 00 ED FF FF consumed by the retail F007 loader.
	if (pos + 1 == v.size && v.data[pos] == 0)
		return true;

	if (pos > v.size || v.size - pos < 36)
		return false;

	uint32 tag = 0;
	if (!readU32(v, pos, tag))
		return false;

	Common::String targetName;
	if (!readName32(v, pos, targetName))
		return false;

	AnimationTrackKind order[5];
	if (targetName.hasSuffixIgnoreCase(".target")) {
		order[0] = kAnimTarget;
		order[1] = kAnimCamera;
		order[2] = kAnimLight;
		order[3] = kAnimExtendedLight;
		order[4] = kAnimTransform;
	} else {
		order[0] = kAnimTransform;
		order[1] = kAnimLight;
		order[2] = kAnimCamera;
		order[3] = kAnimExtendedLight;
		order[4] = kAnimTarget;
	}

	for (int i = 0; i < 5; ++i) {
		AnimationTrack candidate;
		candidate.tag = tag;
		candidate.targetName = targetName;
		uint32 endPos = 0;
		if (!parseTrackPayload(v, pos, order[i], candidate, endPos))
			continue;

		const uint32 keep = tracks.size();
		tracks.push_back(candidate);
		if (parseTracks(v, endPos, tracks))
			return true;
		tracks.resize(keep);
	}

	return false;
}

} // namespace

bool AnimationDecoder::decodeClip(const ModelArchive &archive, const ModelRecord &record, AnimationClip &out) {
	if (record.group || record.type != 0xf007)
		return false;

	const BodyView v(archive, record);
	if (!v.valid() || v.size < 37)
		return false;

	uint32 pos = 0;
	if (!readName32(v, pos, out.sourceName))
		return false;

	uint32 defaultEnd = 0;
	if (!readU32(v, pos, defaultEnd))
		return false;
	out.startFrame = 0;
	out.endFrame = defaultEnd;

	if (pos <= v.size && v.size - pos >= 4 && READ_LE_UINT32(v.data + pos) == kRangeTag) {
		uint32 tag = 0;
		if (!readU32(v, pos, tag) || !readU32(v, pos, out.startFrame) || !readU32(v, pos, out.endFrame))
			return false;
	}

	out.tracks.clear();
	return parseTracks(v, pos, out.tracks);
}

bool AnimationSampler::sampleChannel(const AnimationChannel &channel, float frame, float out[4]) {
	for (int i = 0; i < 4; ++i)
		out[i] = 0.0f;

	if (!channel.enabled || channel.keys.empty() || channel.components == 0 || channel.components > 4)
		return false;

	if (channel.keys.size() == 1 || frame <= channel.keys[0].frame) {
		for (uint32 component = 0; component < channel.components; ++component)
			out[component] = channel.keys[0].value[component];
		return true;
	}

	const uint32 last = channel.keys.size() - 1;
	if (frame >= channel.keys[last].frame) {
		for (uint32 component = 0; component < channel.components; ++component)
			out[component] = channel.keys[last].value[component];
		return true;
	}

	uint32 rightIndex = 1;
	while (rightIndex < channel.keys.size() && frame > channel.keys[rightIndex].frame)
		++rightIndex;
	if (rightIndex >= channel.keys.size())
		rightIndex = last;

	const uint32 leftIndex = rightIndex - 1;
	const AnimationKey &left = channel.keys[leftIndex];
	const AnimationKey &right = channel.keys[rightIndex];
	const AnimationKey &previous = leftIndex > 0 ? channel.keys[leftIndex - 1] : left;
	const AnimationKey &next = rightIndex < last ? channel.keys[rightIndex + 1] : right;

	const float segmentFrames = right.frame - left.frame;
	if (segmentFrames <= 0.0f) {
		for (uint32 component = 0; component < channel.components; ++component)
			out[component] = left.value[component];
		return true;
	}

	float t = (frame - left.frame) / segmentFrames;
	if (t < 0.0f)
		t = 0.0f;
	if (t > 1.0f)
		t = 1.0f;

	const float t2 = t * t;
	const float t3 = t2 * t;
	const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
	const float h10 = t3 - 2.0f * t2 + t;
	const float h01 = -2.0f * t3 + 3.0f * t2;
	const float h11 = t3 - t2;

	for (uint32 component = 0; component < channel.components; ++component) {
		const float segmentSlope = (right.value[component] - left.value[component]) / segmentFrames;

		float previousSlope = segmentSlope;
		if (leftIndex > 0) {
			const float previousFrames = left.frame - previous.frame;
			if (previousFrames > 0.0f)
				previousSlope = (left.value[component] - previous.value[component]) / previousFrames;
		}

		float nextSlope = segmentSlope;
		if (rightIndex < last) {
			const float nextFrames = next.frame - right.frame;
			if (nextFrames > 0.0f)
				nextSlope = (next.value[component] - right.value[component]) / nextFrames;
		}

		const float leftFactor = 0.5f * (1.0f - left.tension);
		const float leftOutgoing =
			leftFactor * ((1.0f + left.continuity) * (1.0f + left.bias) * previousSlope +
			              (1.0f - left.continuity) * (1.0f - left.bias) * segmentSlope);

		const float rightFactor = 0.5f * (1.0f - right.tension);
		const float rightIncoming =
			rightFactor * ((1.0f - right.continuity) * (1.0f + right.bias) * segmentSlope +
			               (1.0f + right.continuity) * (1.0f - right.bias) * nextSlope);

		const float leftTangent = leftOutgoing * segmentFrames;
		const float rightTangent = rightIncoming * segmentFrames;
		out[component] = h00 * left.value[component] +
		                 h10 * leftTangent +
		                 h01 * right.value[component] +
		                 h11 * rightTangent;
	}

	return true;
}

bool AnimationSampler::sampleTransform(const AnimationClip &clip, const Common::String &targetName,
                                       float frame, float translation[3], float scale[3],
                                       float rotation[4]) {
	translation[0] = translation[1] = translation[2] = 0.0f;
	scale[0] = scale[1] = scale[2] = 1.0f;
	rotation[0] = 1.0f;
	rotation[1] = rotation[2] = rotation[3] = 0.0f;

	for (uint32 i = 0; i < clip.tracks.size(); ++i) {
		const AnimationTrack &track = clip.tracks[i];
		if (track.kind != kAnimTransform || !track.targetName.equalsIgnoreCase(targetName) ||
		    track.channels.size() < 3)
			continue;

		float value[4];
		if (sampleChannel(track.channels[0], frame, value)) {
			translation[0] = value[0];
			translation[1] = value[1];
			translation[2] = value[2];
		}
		if (sampleChannel(track.channels[1], frame, value)) {
			scale[0] = value[0];
			scale[1] = value[1];
			scale[2] = value[2];
		}
		if (sampleChannel(track.channels[2], frame, value)) {
			for (int component = 0; component < 4; ++component)
				rotation[component] = value[component];
		}
		return true;
	}

	return false;
}

} // namespace ZeroComico
