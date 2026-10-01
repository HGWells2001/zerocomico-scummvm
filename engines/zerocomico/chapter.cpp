/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: room/main-place definitions
 */

#include "zerocomico/chapter.h"
#include "zerocomico/script_program.h"

#include <cstdlib>

namespace ZeroComico {

bool ChapterDefinition::load(const Common::Path &roomScript) {
	ScriptProgram program;
	if (!program.load(roomScript))
		return false;
	return parse(program);
}

bool ChapterDefinition::parse(const ScriptProgram &program) {
	startRoom.clear();
	startMarker.clear();
	rooms.clear();

	const Common::Array<ScriptInstruction> &instructions = program.instructions();
	int currentRoom = -1;
	int roomDepth = -1;

	for (uint32 i = 0; i < instructions.size(); ++i) {
		const ScriptInstruction &inst = instructions[i];

		if (inst.opcode.equalsIgnoreCase("StartPlace") && !inst.args.empty()) {
			startRoom = inst.args[0];
			if (inst.args.size() >= 2)
				startMarker = inst.args[1];
			continue;
		}

		if (inst.opcode.equalsIgnoreCase("Room") && !inst.args.empty()) {
			RoomDefinition room;
			room.name = inst.args[0];
			room.musicVolume = 100.0f;
			room.codeStart = room.codeEnd = 0xffffffffU;
			rooms.push_back(room);
			currentRoom = (int)rooms.size() - 1;
			roomDepth = inst.depth;
			continue;
		}

		if (currentRoom < 0)
			continue;

		// A new instruction at or above the Room declaration's depth is no
		// longer part of that Room block.
		if (inst.depth <= roomDepth) {
			currentRoom = -1;
			roomDepth = -1;
			continue;
		}

		RoomDefinition &room = rooms[(uint32)currentRoom];
		if (inst.opcode.equalsIgnoreCase("Prefix") && !inst.args.empty()) {
			room.prefix = inst.args[0];
		} else if (inst.opcode.equalsIgnoreCase("backgrd") && !inst.args.empty()) {
			room.backgroundPattern = inst.args[0];
		} else if (inst.opcode.equalsIgnoreCase("camera") && !inst.args.empty()) {
			room.camera = inst.args[0];
		} else if (inst.opcode.equalsIgnoreCase("cameraspot") && !inst.args.empty()) {
			room.cameraSpot = inst.args[0];
		} else if (inst.opcode.equalsIgnoreCase("Music") && !inst.args.empty()) {
			room.music = inst.args[0];
			if (inst.args.size() >= 2) {
				char *end = nullptr;
				const double parsed = strtod(inst.args[1].c_str(), &end);
				if (end && *end == 0 && parsed >= 0.0)
					room.musicVolume = (float)parsed;
			}
		} else if (inst.opcode.equalsIgnoreCase("map") && !inst.args.empty()) {
			room.maps.push_back(inst.args[0]);
		} else if (inst.opcode.equalsIgnoreCase("cameramap") && !inst.args.empty()) {
			room.cameraMaps.push_back(inst.args[0]);
		} else if (inst.opcode.equalsIgnoreCase("chsound") && inst.args.size() >= 3) {
			char *end = nullptr;
			const long parsed = strtol(inst.args[1].c_str(), &end, 10);
			if (end && *end == 0) {
				RoomCharacterSound sound;
				sound.character = inst.args[0];
				sound.sampleId = (int32)parsed;
				sound.fileName = inst.args[2];
				room.characterSounds.push_back(sound);
			}
		} else if (inst.opcode.equalsIgnoreCase("code") &&
		           room.codeStart == 0xffffffffU) {
			room.codeStart = i + 1;
			for (uint32 j = room.codeStart; j < instructions.size(); ++j) {
				if (instructions[j].opcode.equalsIgnoreCase("end") &&
				    instructions[j].depth <= inst.depth) {
					room.codeEnd = j;
					break;
				}
				if (instructions[j].opcode.equalsIgnoreCase("Room") &&
				    instructions[j].depth <= roomDepth)
					break;
			}
		} else if (inst.opcode.equalsIgnoreCase("portal") && inst.args.size() >= 2) {
			RoomPortal portal;
			portal.name = inst.args[0];
			portal.destinationRoom = inst.args[1];
			if (inst.args.size() >= 3)
				portal.marker = inst.args[2];
			if (inst.args.size() >= 4)
				portal.backgroundPattern = inst.args[3];
			if (inst.args.size() >= 5)
				portal.destinationPortal = inst.args[4];
			room.portals.push_back(portal);
		}
	}

	return !startRoom.empty() && findRoom(startRoom) != nullptr;
}

const RoomDefinition *ChapterDefinition::findRoom(const Common::String &name) const {
	for (uint32 i = 0; i < rooms.size(); ++i) {
		if (rooms[i].name.equalsIgnoreCase(name))
			return &rooms[i];
	}
	return nullptr;
}

} // namespace ZeroComico
