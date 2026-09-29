/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: named shape/position markers
 */

#include "zerocomico/shape_script.h"
#include "zerocomico/script_program.h"

#include <cmath>
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

bool ShapeScript::parsePolygonVec3(const ScriptInstruction &instruction, Vec3f &value) {
	return instruction.args.size() >= 2 &&
	       parseFloat(instruction.opcode, value.x) &&
	       parseFloat(instruction.args[0], value.y) &&
	       parseFloat(instruction.args[1], value.z);
}

bool ShapeScript::load(const Common::Path &path) {
	ScriptProgram program;
	if (!program.load(path))
		return false;
	return parse(program);
}

bool ShapeScript::parse(const ScriptProgram &program) {
	_shapes.clear();
	_polygons.clear();
	const Common::Array<ScriptInstruction> &instructions = program.instructions();

	for (uint32 i = 0; i < instructions.size(); ++i) {
		const ScriptInstruction &header = instructions[i];
		if (header.opcode.equalsIgnoreCase("ge_Shape") && !header.args.empty()) {
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
			continue;
		}

		if (!header.opcode.equalsIgnoreCase("ge_Polygon") || header.args.size() < 2)
			continue;

		char *end = nullptr;
		const long declaredCount = strtol(header.args[1].c_str(), &end, 10);
		if (end == header.args[1].c_str() || *end != 0 || declaredCount < 3)
			continue;

		ShapePolygon polygon;
		polygon.name = header.args[0];
		for (uint32 j = i + 1; j < instructions.size() &&
		     polygon.vertices.size() < (uint32)declaredCount; ++j) {
			const ScriptInstruction &inst = instructions[j];
			if (inst.depth <= header.depth)
				break;
			Vec3f vertex;
			if (parsePolygonVec3(inst, vertex))
				polygon.vertices.push_back(vertex);
		}
		if (polygon.vertices.size() == (uint32)declaredCount)
			_polygons.push_back(polygon);
	}

	return !_shapes.empty() || !_polygons.empty();
}

const ShapeMarker *ShapeScript::find(const Common::String &name) const {
	for (uint32 i = 0; i < _shapes.size(); ++i) {
		if (_shapes[i].name.equalsIgnoreCase(name))
			return &_shapes[i];
	}
	return nullptr;
}

const ShapePolygon *ShapeScript::findPolygon(const Common::String &name) const {
	for (uint32 i = 0; i < _polygons.size(); ++i) {
		if (_polygons[i].name.equalsIgnoreCase(name))
			return &_polygons[i];
	}
	return nullptr;
}

bool ShapeScript::containsRegion(const Common::String &name, float x, float z) const {
	const ShapePolygon *polygon = findPolygon(name);
	if (polygon && polygon->vertices.size() >= 3) {
		bool inside = false;
		for (uint32 i = 0, j = polygon->vertices.size() - 1;
		     i < polygon->vertices.size(); j = i++) {
			const Vec3f &a = polygon->vertices[i];
			const Vec3f &b = polygon->vertices[j];
			const float edgeX = b.x - a.x;
			const float edgeZ = b.z - a.z;
			const float pointX = x - a.x;
			const float pointZ = z - a.z;
			const float cross = edgeX * pointZ - edgeZ * pointX;
			const float dot = pointX * edgeX + pointZ * edgeZ;
			const float edgeLength2 = edgeX * edgeX + edgeZ * edgeZ;
			if (std::fabs(cross) <= 0.001f && dot >= -0.001f &&
			    dot <= edgeLength2 + 0.001f)
				return true;

			const bool crosses = ((a.z > z) != (b.z > z)) &&
				(x < (b.x - a.x) * (z - a.z) / (b.z - a.z) + a.x);
			if (crosses)
				inside = !inside;
		}
		return inside;
	}

	const ShapeMarker *range = find(name);
	if (!range || !range->kind.equalsIgnoreCase("Range"))
		return false;

	const float minX = range->a.x < range->b.x ? range->a.x : range->b.x;
	const float maxX = range->a.x > range->b.x ? range->a.x : range->b.x;
	const float minZ = range->a.z < range->b.z ? range->a.z : range->b.z;
	const float maxZ = range->a.z > range->b.z ? range->a.z : range->b.z;
	return x >= minX && x <= maxX && z >= minZ && z <= maxZ;
}

} // namespace ZeroComico
