/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: playable-character script metadata
 */

#ifndef ZEROCOMICO_CHARACTER_SCRIPT_H
#define ZEROCOMICO_CHARACTER_SCRIPT_H

#include "common/array.h"
#include "common/path.h"
#include "common/str.h"

#include "zerocomico/script_program.h"

namespace ZeroComico {

struct CharacterSample {
	int32 id;
	Common::String fileName;
};

struct CharacterStepEvent {
	Common::String animation;
	int32 frame;
	int32 sampleId;
};

struct CharacterAnimSet {
	Common::String name;
	Common::String bodyName;
	Common::String takeLowAnimation;
	Common::String takeMidAnimation;
	Common::String takeHighAnimation;
	int32 takeLowEventFrame;
	int32 takeMidEventFrame;
	int32 takeHighEventFrame;
	Common::Array<CharacterStepEvent> stepEvents;
};

struct CharacterDefinition {
	Common::String name;
	bool mainPlayer;
	bool cpuPlayer;
	bool castShadows;
	Common::String initialAnimSet;
	Common::String initialBodyName;
	Common::String cloneSourceBodyName;
	Common::String cloneTargetBodyName;
	Common::String initialEntity;
	Common::String initialVector;
	Common::String mainPlace;
	Common::String roomName;
	bool breakLifeOnInitialize;
	uint32 initializeStart;
	uint32 initializeEnd;
	uint32 controlStart;
	uint32 controlEnd;
	uint32 hidingStart;
	uint32 hidingEnd;
	Common::Array<CharacterSample> samples;
	Common::Array<CharacterAnimSet> animSets;
};

class CharacterScript {
public:
	CharacterScript();

	bool load(const Common::Path &path);
	bool parse();

	bool hasCombineBlock() const {
		return combineStart != 0xffffffffU && combineEnd != 0xffffffffU &&
		       combineStart < combineEnd;
	}
	const CharacterAnimSet *findAnimSet(const Common::String &name) const;
	const CharacterDefinition *findCharacter(const Common::String &name) const;
	const ScriptProgram &program() const { return _program; }

	Common::String playerName;
	Common::String initialAnimSet;
	Common::String initialBodyName;
	Common::Array<CharacterAnimSet> animSets;
	Common::Array<CharacterDefinition> characters;
	uint32 combineStart;
	uint32 combineEnd;

private:
	ScriptProgram _program;
};

} // namespace ZeroComico

#endif
