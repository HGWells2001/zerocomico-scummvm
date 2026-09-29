/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: puzzle/object definitions
 */

#include "zerocomico/puzzle_script.h"

#include <cstdlib>

namespace ZeroComico {

namespace {

static bool scriptBool(const Common::String &value, bool fallback) {
	if (value.equalsIgnoreCase("TRUE") || value == "1")
		return true;
	if (value.equalsIgnoreCase("FALSE") || value == "0")
		return false;
	return fallback;
}

static float scriptFloat(const Common::String &value, float fallback) {
	char *end = nullptr;
	const double parsed = strtod(value.c_str(), &end);
	if (!end || end == value.c_str() || *end != 0)
		return fallback;
	return (float)parsed;
}

} // namespace

bool PuzzleScript::load(const Common::Path &path) {
	if (!_program.load(path))
		return false;
	return parse();
}

bool PuzzleScript::parse() {
	objects.clear();
	const Common::Array<ScriptInstruction> &instructions = _program.instructions();

	for (uint32 i = 0; i < instructions.size(); ++i) {
		const ScriptInstruction &header = instructions[i];
		if (!header.opcode.equalsIgnoreCase("Object") || header.args.empty())
			continue;

		PuzzleObject object;
		object.name = header.args[0];
		object.range = 0.0f;
		object.size = 1.0f;
		object.enabled = true;
		object.examinable = false;
		object.pickable = false;
		object.operated = false;
		object.examinated = false;
		object.autoCamera = false;
		object.randomPos = false;
		object.combined = false;
		object.assigned = false;
		object.inside = false;
		object.collision = false;
		object.soundState = false;
		object.operateStart = object.operateEnd = 0xffffffffU;
		object.enterStart = object.enterEnd = 0xffffffffU;

		const int objectDepth = header.depth;
		for (uint32 j = i + 1; j < instructions.size(); ++j) {
			const ScriptInstruction &inst = instructions[j];
			if (inst.opcode.equalsIgnoreCase("Object") && inst.depth <= objectDepth)
				break;
			if (inst.depth < objectDepth)
				break;

			if (inst.opcode.equalsIgnoreCase("entity") && !inst.args.empty()) {
				object.entity = inst.args[0];
			} else if (inst.opcode.equalsIgnoreCase("polygon") && !inst.args.empty()) {
				object.polygon = inst.args[0];
			} else if (inst.opcode.equalsIgnoreCase("rangeshape") && !inst.args.empty()) {
				object.rangeShape = inst.args[0];
			} else if (inst.opcode.equalsIgnoreCase("roomscope") && !inst.args.empty()) {
				object.roomScope = inst.args[0];
			} else if (inst.opcode.equalsIgnoreCase("range") && !inst.args.empty()) {
				object.range = scriptFloat(inst.args[0], object.range);
			} else if (inst.opcode.equalsIgnoreCase("size") && !inst.args.empty()) {
				object.size = scriptFloat(inst.args[0], object.size);
			} else if (inst.opcode.equalsIgnoreCase("ENABLED") && !inst.args.empty()) {
				object.enabled = scriptBool(inst.args[0], object.enabled);
			} else if (inst.opcode.equalsIgnoreCase("EXAMINABLE") && !inst.args.empty()) {
				object.examinable = scriptBool(inst.args[0], object.examinable);
			} else if (inst.opcode.equalsIgnoreCase("PICKABLE") && !inst.args.empty()) {
				object.pickable = scriptBool(inst.args[0], object.pickable);
			} else if (inst.opcode.equalsIgnoreCase("OPERATED") && !inst.args.empty()) {
				object.operated = scriptBool(inst.args[0], object.operated);
			} else if (inst.opcode.equalsIgnoreCase("EXAMINATED") && !inst.args.empty()) {
				object.examinated = scriptBool(inst.args[0], object.examinated);
			} else if (inst.opcode.equalsIgnoreCase("AUTOCAMERA") && !inst.args.empty()) {
				object.autoCamera = scriptBool(inst.args[0], object.autoCamera);
			} else if (inst.opcode.equalsIgnoreCase("RANDOMPOS") && !inst.args.empty()) {
				object.randomPos = scriptBool(inst.args[0], object.randomPos);
			} else if (inst.opcode.equalsIgnoreCase("COMBINED") && !inst.args.empty()) {
				object.combined = scriptBool(inst.args[0], object.combined);
			} else if (inst.opcode.equalsIgnoreCase("ASSIGNED") && !inst.args.empty()) {
				object.assigned = scriptBool(inst.args[0], object.assigned);
			} else if (inst.opcode.equalsIgnoreCase("INSIDE") && !inst.args.empty()) {
				object.inside = scriptBool(inst.args[0], object.inside);
			} else if (inst.opcode.equalsIgnoreCase("COLLISION") && !inst.args.empty()) {
				object.collision = scriptBool(inst.args[0], object.collision);
			} else if (inst.opcode.equalsIgnoreCase("SOUNDSTATE") && !inst.args.empty()) {
				object.soundState = scriptBool(inst.args[0], object.soundState);
			} else if (inst.opcode.equalsIgnoreCase("in")) {
				object.enterStart = j + 1;
				for (uint32 k = j + 1; k < instructions.size(); ++k) {
					if (instructions[k].opcode.equalsIgnoreCase("end")) {
						object.enterEnd = k;
						break;
					}
					if (instructions[k].opcode.equalsIgnoreCase("Object"))
						break;
				}
			} else if (inst.opcode.equalsIgnoreCase("operate")) {
				object.operateStart = j + 1;
				for (uint32 k = j + 1; k < instructions.size(); ++k) {
					if (instructions[k].opcode.equalsIgnoreCase("end")) {
						object.operateEnd = k;
						break;
					}
					if (instructions[k].opcode.equalsIgnoreCase("examine_text") ||
					    instructions[k].opcode.equalsIgnoreCase("Object"))
						break;
				}
			} else if (inst.opcode.equalsIgnoreCase("examine_text")) {
				for (uint32 k = j + 1; k < instructions.size(); ++k) {
					const ScriptInstruction &text = instructions[k];
					if (text.opcode.equalsIgnoreCase("NULL") ||
					    text.opcode.equalsIgnoreCase("sound") ||
					    text.opcode.equalsIgnoreCase("Object"))
						break;
					if (!text.opcode.empty()) {
						object.examineText = text.opcode;
						for (uint32 a = 0; a < text.args.size(); ++a) {
							object.examineText += " ";
							object.examineText += text.args[a];
						}
						break;
					}
				}
			}
		}

		objects.push_back(object);
	}

	return !objects.empty();
}

PuzzleObject *PuzzleScript::findObject(const Common::String &name) {
	for (uint32 i = 0; i < objects.size(); ++i)
		if (objects[i].name.equalsIgnoreCase(name))
			return &objects[i];
	return nullptr;
}

const PuzzleObject *PuzzleScript::findObject(const Common::String &name) const {
	for (uint32 i = 0; i < objects.size(); ++i)
		if (objects[i].name.equalsIgnoreCase(name))
			return &objects[i];
	return nullptr;
}

PuzzleObject *PuzzleScript::findByEntity(const Common::String &entity) {
	for (uint32 i = 0; i < objects.size(); ++i)
		if (objects[i].entity.equalsIgnoreCase(entity))
			return &objects[i];
	return nullptr;
}

const PuzzleObject *PuzzleScript::findByEntity(const Common::String &entity) const {
	for (uint32 i = 0; i < objects.size(); ++i)
		if (objects[i].entity.equalsIgnoreCase(entity))
			return &objects[i];
	return nullptr;
}

} // namespace ZeroComico
