/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: room/main-place definitions
 */

#ifndef ZEROCOMICO_CHAPTER_H
#define ZEROCOMICO_CHAPTER_H

#include "common/array.h"
#include "common/path.h"
#include "common/str.h"

namespace ZeroComico {

class ScriptProgram;

struct RoomPortal {
	Common::String name;
	Common::String destinationRoom;
	Common::String marker;
	Common::String backgroundPattern;
	Common::String destinationPortal;
};

struct RoomCharacterSound {
	Common::String character;
	int32 sampleId;
	Common::String fileName;
};

struct RoomEnvironmentSound {
	Common::String name;
	Common::String fileName;
	Common::String entity;
	bool emitter;
	bool enabled;
	float farVolume;
	float nearVolume;
	float minRange;
	float maxRange;
};

struct RoomDefinition {
	Common::String name;
	Common::String prefix;
	Common::String backgroundPattern;
	Common::String camera;
	Common::String cameraSpot;
	Common::String music;
	float musicVolume;
	Common::Array<Common::String> maps;
	Common::Array<Common::String> cameraMaps;
	Common::Array<RoomPortal> portals;
	Common::Array<RoomCharacterSound> characterSounds;
	Common::Array<RoomEnvironmentSound> environmentSounds;
	uint32 codeStart;
	uint32 codeEnd;
};

class ChapterDefinition {
public:
	bool load(const Common::Path &roomScript);
	bool parse(const ScriptProgram &program);

	const RoomDefinition *findRoom(const Common::String &name) const;

	Common::String startRoom;
	Common::String startMarker;
	Common::Array<RoomDefinition> rooms;
};

} // namespace ZeroComico

#endif
