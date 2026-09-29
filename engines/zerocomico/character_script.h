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

struct CharacterAnimSet {
	Common::String name;
	Common::String bodyName;
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
	const ScriptProgram &program() const { return _program; }

	Common::String playerName;
	Common::String initialAnimSet;
	Common::String initialBodyName;
	Common::Array<CharacterAnimSet> animSets;
	uint32 combineStart;
	uint32 combineEnd;

private:
	ScriptProgram _program;
};

} // namespace ZeroComico

#endif
