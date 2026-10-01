/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: playable-character script metadata
 */

#include "zerocomico/character_script.h"

#include <cstdlib>

namespace ZeroComico {

namespace {

static int32 parseAnimInteger(const Common::String &value, int32 fallback) {
	char *end = nullptr;
	const long parsed = strtol(value.c_str(), &end, 10);
	if (!end || end == value.c_str() || *end != 0)
		return fallback;
	return (int32)parsed;
}

static bool parseStepEventToken(const Common::String &value, int32 &frame, int32 &sampleId) {
	const uint32 separator = value.find('#');
	if (separator == Common::String::npos || separator == 0 || separator + 1 >= value.size())
		return false;

	frame = parseAnimInteger(value.substr(0, separator), -1);
	sampleId = parseAnimInteger(value.substr(separator + 1), -1);
	return frame >= 0 && sampleId >= 0;
}

} // namespace

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
		character.initializeStart = character.initializeEnd = 0xffffffffU;
		character.controlStart = character.controlEnd = 0xffffffffU;
		character.hidingStart = character.hidingEnd = 0xffffffffU;

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
			if (inst.opcode.equalsIgnoreCase("Sample") && inst.args.size() >= 2) {
				const int32 sampleId = parseAnimInteger(inst.args[0], -1);
				if (sampleId >= 0) {
					CharacterSample sample;
					sample.id = sampleId;
					sample.fileName = inst.args[1];
					character.samples.push_back(sample);
				}
				continue;
			}
			if (inst.opcode.equalsIgnoreCase("AnimSet") && inst.args.size() >= 2) {
				CharacterAnimSet animSet;
				animSet.name = inst.args[0];
				animSet.bodyName = inst.args[1];

				// These are the retail engine defaults reconstructed from
				// Zero Comico.exe. The shipped playable AnimSets override low/mid
				// with GetDown/Get, while take_high remains at its default.
				animSet.takeLowAnimation = "pickdw";
				animSet.takeMidAnimation = "pickmd";
				animSet.takeHighAnimation = "pickup";
				animSet.takeLowEventFrame = 10;
				animSet.takeMidEventFrame = 10;
				animSet.takeHighEventFrame = 10;

				const int animSetDepth = inst.depth;
				for (uint32 k = j + 1; k < end; ++k) {
					const ScriptInstruction &property = instructions[k];
					if ((property.opcode.equalsIgnoreCase("AnimSet") &&
					     property.depth <= animSetDepth) ||
					    property.depth < animSetDepth)
						break;

					if (property.opcode.equalsIgnoreCase("step_events")) {
						const int stepDepth = property.depth;
						uint32 eventIndex = k + 1;
						for (; eventIndex < end && instructions[eventIndex].depth > stepDepth; ++eventIndex) {
							const ScriptInstruction &event = instructions[eventIndex];
							for (uint32 tokenIndex = 0; tokenIndex < event.args.size(); ++tokenIndex) {
								int32 frame = -1;
								int32 sampleId = -1;
								if (!parseStepEventToken(event.args[tokenIndex], frame, sampleId))
									continue;
								CharacterStepEvent stepEvent;
								stepEvent.animation = event.opcode;
								stepEvent.frame = frame;
								stepEvent.sampleId = sampleId;
								animSet.stepEvents.push_back(stepEvent);
							}
						}
						if (eventIndex > k + 1)
							k = eventIndex - 1;
						continue;
					}

					Common::String *animation = nullptr;
					int32 *eventFrame = nullptr;
					if (property.opcode.equalsIgnoreCase("take_low")) {
						animation = &animSet.takeLowAnimation;
						eventFrame = &animSet.takeLowEventFrame;
					} else if (property.opcode.equalsIgnoreCase("take_mid")) {
						animation = &animSet.takeMidAnimation;
						eventFrame = &animSet.takeMidEventFrame;
					} else if (property.opcode.equalsIgnoreCase("take_high")) {
						animation = &animSet.takeHighAnimation;
						eventFrame = &animSet.takeHighEventFrame;
					}

					if (!animation || property.args.empty())
						continue;
					*animation = property.args[0];
					if (property.args.size() >= 2)
						*eventFrame = parseAnimInteger(property.args[1], *eventFrame);
				}

				character.animSets.push_back(animSet);
				continue;
			}
			if (inst.opcode.equalsIgnoreCase("InitialAnimSet") && !inst.args.empty()) {
				character.initialAnimSet = inst.args[0];
				continue;
			}
			if (inst.opcode.equalsIgnoreCase("ControlCode") &&
			    character.controlStart == 0xffffffffU) {
				character.controlStart = j + 1;
				for (uint32 k = character.controlStart; k < end; ++k) {
					if (instructions[k].opcode.equalsIgnoreCase("end") &&
					    instructions[k].depth <= inst.depth) {
						character.controlEnd = k;
						break;
					}
				}
				continue;
			}
			if (inst.opcode.equalsIgnoreCase("HidingCode") &&
			    character.hidingStart == 0xffffffffU) {
				character.hidingStart = j + 1;
				for (uint32 k = character.hidingStart; k < end; ++k) {
					if (instructions[k].opcode.equalsIgnoreCase("end") &&
					    instructions[k].depth <= inst.depth) {
						character.hidingEnd = k;
						break;
					}
				}
				continue;
			}
			if (inst.opcode.equalsIgnoreCase("initialize") &&
			    character.initializeStart == 0xffffffffU) {
				character.initializeStart = j + 1;
				for (uint32 k = character.initializeStart; k < end; ++k) {
					if (instructions[k].opcode.equalsIgnoreCase("end") &&
					    instructions[k].depth <= inst.depth) {
						character.initializeEnd = k;
						break;
					}
				}
				continue;
			}
			if (inst.opcode.equalsIgnoreCase("SetCharPos_Entity") &&
			    inst.args.size() >= 2 &&
			    inst.args[0].equalsIgnoreCase(character.name)) {
				character.initialEntity = inst.args[1];
				continue;
			}
			if (inst.opcode.equalsIgnoreCase("SetCharPos_Vector") &&
			    inst.args.size() >= 2 &&
			    inst.args[0].equalsIgnoreCase(character.name)) {
				character.initialVector = inst.args[1];
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
