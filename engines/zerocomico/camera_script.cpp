/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: scripted gameplay cameras
 */

#include "zerocomico/camera_script.h"
#include "zerocomico/script_program.h"

#include <cstdlib>

namespace ZeroComico {

bool CameraScript::parseFloat(const Common::String &token, float &value) {
	if (token.empty())
		return false;

	char *end = nullptr;
	const double parsed = strtod(token.c_str(), &end);
	if (end == token.c_str() || *end != 0)
		return false;

	value = (float)parsed;
	return true;
}

bool CameraScript::parseVec3(const ScriptInstruction &instruction, Vec3f &value) {
	return instruction.args.size() >= 3 &&
	       parseFloat(instruction.args[0], value.x) &&
	       parseFloat(instruction.args[1], value.y) &&
	       parseFloat(instruction.args[2], value.z);
}

bool CameraScript::load(const Common::Path &path) {
	ScriptProgram program;
	if (!program.load(path))
		return false;
	return parse(program);
}

bool CameraScript::parse(const ScriptProgram &program) {
	_cameras.clear();
	const Common::Array<ScriptInstruction> &instructions = program.instructions();

	for (uint32 i = 0; i < instructions.size(); ++i) {
		const ScriptInstruction &header = instructions[i];
		if (!header.opcode.equalsIgnoreCase("Bodies_are_absolute"))
			continue;

		int cameraArg = -1;
		for (uint32 arg = 0; arg < header.args.size(); ++arg) {
			if (header.args[arg].equalsIgnoreCase("camera")) {
				cameraArg = (int)arg;
				break;
			}
		}
		if (cameraArg < 0 || (uint32)cameraArg + 1 >= header.args.size())
			continue;

		ScriptCamera camera;
		camera.name = header.args[(uint32)cameraArg + 1];
		camera.source.x = camera.source.y = camera.source.z = 0.0f;
		camera.target.x = camera.target.y = camera.target.z = 0.0f;
		camera.horizontalFovDegrees = 0.0f;

		for (uint32 arg = (uint32)cameraArg + 2; arg + 1 < header.args.size(); ++arg) {
			if (header.args[arg].equalsIgnoreCase("FOV")) {
				parseFloat(header.args[arg + 1], camera.horizontalFovDegrees);
				break;
			}
		}

		bool haveSource = false;
		bool haveTarget = false;
		for (uint32 j = i + 1; j < instructions.size(); ++j) {
			const ScriptInstruction &inst = instructions[j];
			if (inst.depth <= header.depth)
				break;
			if (inst.opcode.equalsIgnoreCase("Source"))
				haveSource = parseVec3(inst, camera.source);
			else if (inst.opcode.equalsIgnoreCase("Target"))
				haveTarget = parseVec3(inst, camera.target);
		}

		if (haveSource && haveTarget && camera.horizontalFovDegrees > 0.0f)
			_cameras.push_back(camera);
	}

	return !_cameras.empty();
}

const ScriptCamera *CameraScript::findCamera(const Common::String &name) const {
	for (uint32 i = 0; i < _cameras.size(); ++i) {
		if (_cameras[i].name.equalsIgnoreCase(name))
			return &_cameras[i];
	}
	return nullptr;
}

} // namespace ZeroComico
