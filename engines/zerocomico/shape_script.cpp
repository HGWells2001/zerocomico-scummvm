/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: named shape/position markers
 */

#include "zerocomico/shape_script.h"
#include "zerocomico/script_program.h"

#include <cstdlib>

namespace ZeroComico {

bool ShapeScript::parseFloat(const Common::String &token, float &value) {
	if (token.empty())
		return false;

	char *end = nullptr;
	const double parsed = strtod(token.c_str(), &end);
	if (end == token.c_str() || *end != 0)
		return false;

	value = (float)parsed;
	return true;
}

bool ShapeScript::parseVec3(const ScriptInstruction &instruction, Vec3f &value) {
	return instruction.args.size() >= 3 &&
	       parseFloat(instruction.args[0], value.x) &&
	       parseFloat(instruction.args[1], value.y) &&
	       parseFloat(instruction.args[2], value.z);
}

bool ShapeScript::load(const Common::Path &path) {
	ScriptProgram program;
	if (!program.load(path))
		return false;
	return parse(program);
}

bool ShapeScript::parse(const ScriptProgram &program) {
	_shapes.clear();
	const Common::Array<ScriptInstruction> &instructions = program.instructions();

	for (uint32 i = 0; i < instructions.size(); ++i) {
		const ScriptInstruction &header = instructions[i];
		if (!header.opcode.equalsIgnoreCase("ge_Shape") || header.args.empty())
			continue;

		ShapeMarker shape;
		shape.name = header.args[0];
		shape.kind = header.args.size() >= 2 ? header.args[1] : Common::String();
		shape.a.x = shape.a.y = shape.a.z = 0.0f;
		shape.b.x = shape.b.y = shape.b.z = 0.0f;

		bool haveA = false;
		bool haveB = false;
		for (uint32 j = i + 1; j < instructions.size(); ++j) {
			const ScriptInstruction &inst = instructions[j];
			if (inst.depth <= header.depth)
				break;
			if (inst.opcode.equalsIgnoreCase("A"))
				haveA = parseVec3(inst, shape.a);
			else if (inst.opcode.equalsIgnoreCase("B"))
				haveB = parseVec3(inst, shape.b);
		}

		if (haveA && haveB)
			_shapes.push_back(shape);
	}

	return !_shapes.empty();
}

const ShapeMarker *ShapeScript::find(const Common::String &name) const {
	for (uint32 i = 0; i < _shapes.size(); ++i) {
		if (_shapes[i].name.equalsIgnoreCase(name))
			return &_shapes[i];
	}
	return nullptr;
}

} // namespace ZeroComico
