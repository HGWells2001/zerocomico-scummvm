/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: playable-character script metadata
 */

#include "zerocomico/character_script.h"

namespace ZeroComico {

CharacterScript::CharacterScript()
	: combineStart(0xffffffffU), combineEnd(0xffffffffU) {
}

bool CharacterScript::load(const Common::Path &path) {
	if (!_program.load(path))
		return false;
	return parse();
}

bool CharacterScript::parse() {
	playerName.clear();
	initialAnimSet.clear();
	initialBodyName.clear();
	animSets.clear();
	combineStart = combineEnd = 0xffffffffU;

	const Common::Array<ScriptInstruction> &instructions = _program.instructions();
	uint32 playerHeader = 0xffffffffU;
	uint32 playerEnd = instructions.size();

	for (uint32 i = 0; i < instructions.size(); ++i) {
		const ScriptInstruction &header = instructions[i];
		if (!header.opcode.equalsIgnoreCase("ge_Character") || header.args.empty())
			continue;

		const int characterDepth = header.depth;
		uint32 end = instructions.size();
		bool mainPlayer = false;
		for (uint32 j = i + 1; j < instructions.size(); ++j) {
			const ScriptInstruction &child = instructions[j];
			if (child.opcode.equalsIgnoreCase("ge_Character") &&
			    child.depth <= characterDepth) {
				end = j;
				break;
			}
			if (child.depth < characterDepth) {
				end = j;
				break;
			}
			if (child.opcode.equalsIgnoreCase("mainplayer"))
				mainPlayer = true;
		}

		if (mainPlayer) {
			playerHeader = i;
			playerEnd = end;
			playerName = header.args[0];
			break;
		}
	}

	if (playerHeader == 0xffffffffU)
		return false;

	for (uint32 i = playerHeader + 1; i < playerEnd; ++i) {
		const ScriptInstruction &inst = instructions[i];

		if (inst.opcode.equalsIgnoreCase("AnimSet") && inst.args.size() >= 2) {
			CharacterAnimSet animSet;
			animSet.name = inst.args[0];
			animSet.bodyName = inst.args[1];
			animSets.push_back(animSet);
			continue;
		}

		if (inst.opcode.equalsIgnoreCase("InitialAnimSet") && !inst.args.empty()) {
			initialAnimSet = inst.args[0];
			continue;
		}

		if (inst.opcode.equalsIgnoreCase("combineobj") && combineStart == 0xffffffffU) {
			combineStart = i + 1;
			for (uint32 j = combineStart; j < playerEnd; ++j) {
				if (instructions[j].opcode.equalsIgnoreCase("end")) {
					combineEnd = j;
					break;
				}
			}
		}
	}

	for (uint32 i = 0; i < animSets.size(); ++i) {
		if (animSets[i].name.equalsIgnoreCase(initialAnimSet)) {
			initialBodyName = animSets[i].bodyName;
			break;
		}
	}

	return !initialBodyName.empty();
}

} // namespace ZeroComico
