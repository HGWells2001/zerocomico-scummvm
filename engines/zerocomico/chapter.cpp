/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: room/main-place definitions
 */

#include "zerocomico/chapter.h"
#include "zerocomico/script_program.h"

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
		} else if (inst.opcode.equalsIgnoreCase("map") && !inst.args.empty()) {
			room.maps.push_back(inst.args[0]);
		} else if (inst.opcode.equalsIgnoreCase("cameramap") && !inst.args.empty()) {
			room.cameraMaps.push_back(inst.args[0]);
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
