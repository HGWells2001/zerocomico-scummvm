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
	characters.clear();
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
		for (uint32 j = i + 1; j < instructions.size(); ++j) {
			const ScriptInstruction &child = instructions[j];
			if ((child.opcode.equalsIgnoreCase("ge_Character") &&
			     child.depth <= characterDepth) ||
			    child.depth < characterDepth) {
				end = j;
				break;
			}
		}

		CharacterDefinition character;
		character.name = header.args[0];
		character.mainPlayer = false;
		character.cpuPlayer = false;
		character.castShadows = true;
		character.breakLifeOnInitialize = false;

		for (uint32 j = i + 1; j < end; ++j) {
			const ScriptInstruction &inst = instructions[j];

			if (inst.opcode.equalsIgnoreCase("mainplayer")) {
				character.mainPlayer = true;
				continue;
			}
			if (inst.opcode.equalsIgnoreCase("cpuplayer")) {
				character.cpuPlayer = true;
				continue;
			}
			if (inst.opcode.equalsIgnoreCase("CastShadows") && !inst.args.empty()) {
				character.castShadows =
					inst.args[0].equalsIgnoreCase("TRUE") || inst.args[0] == "1";
				continue;
			}
			if (inst.opcode.equalsIgnoreCase("AnimSet") && inst.args.size() >= 2) {
				CharacterAnimSet animSet;
				animSet.name = inst.args[0];
				animSet.bodyName = inst.args[1];
				character.animSets.push_back(animSet);
				continue;
			}
			if (inst.opcode.equalsIgnoreCase("InitialAnimSet") && !inst.args.empty()) {
				character.initialAnimSet = inst.args[0];
				continue;
			}
			if (inst.opcode.equalsIgnoreCase("SetCharPos_Entity") &&
			    inst.args.size() >= 2 &&
			    inst.args[0].equalsIgnoreCase(character.name)) {
				character.initialEntity = inst.args[1];
				continue;
			}
			if (inst.opcode.equalsIgnoreCase("BreakLifeToChar") &&
			    !inst.args.empty() &&
			    inst.args[0].equalsIgnoreCase(character.name)) {
				character.breakLifeOnInitialize = true;
				continue;
			}
			if (inst.opcode.equalsIgnoreCase("place") && inst.args.size() >= 2) {
				character.mainPlace = inst.args[0];
				character.roomName = inst.args[1];
				continue;
			}
		}

		for (uint32 a = 0; a < character.animSets.size(); ++a) {
			if (character.animSets[a].name.equalsIgnoreCase(character.initialAnimSet)) {
				character.initialBodyName = character.animSets[a].bodyName;
				break;
			}
		}

		characters.push_back(character);

		if (character.mainPlayer && playerHeader == 0xffffffffU) {
			playerHeader = i;
			playerEnd = end;
			playerName = character.name;
			initialAnimSet = character.initialAnimSet;
			initialBodyName = character.initialBodyName;
			animSets = character.animSets;
		}
	}

	if (playerHeader == 0xffffffffU)
		return false;

	for (uint32 i = playerHeader + 1; i < playerEnd; ++i) {
		const ScriptInstruction &inst = instructions[i];
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

	return !initialBodyName.empty();
}

const CharacterAnimSet *CharacterScript::findAnimSet(const Common::String &name) const {
	for (uint32 i = 0; i < animSets.size(); ++i)
		if (animSets[i].name.equalsIgnoreCase(name))
			return &animSets[i];
	return nullptr;
}

const CharacterDefinition *CharacterScript::findCharacter(const Common::String &name) const {
	for (uint32 i = 0; i < characters.size(); ++i)
		if (characters[i].name.equalsIgnoreCase(name))
			return &characters[i];
	return nullptr;
}

} // namespace ZeroComico
