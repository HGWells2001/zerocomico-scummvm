/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: scripted gameplay cameras
 */

#ifndef ZEROCOMICO_CAMERA_SCRIPT_H
#define ZEROCOMICO_CAMERA_SCRIPT_H

#include "common/array.h"
#include "common/path.h"
#include "common/str.h"

#include "zerocomico/model_data.h"

namespace ZeroComico {

class ScriptProgram;
struct ScriptInstruction;

struct ScriptCamera {
	Common::String name;
	Vec3f source;
	Vec3f target;
	float horizontalFovDegrees;
};

class CameraScript {
public:
	bool load(const Common::Path &path);
	bool parse(const ScriptProgram &program);

	const ScriptCamera *findCamera(const Common::String &name) const;
	const Common::Array<ScriptCamera> &cameras() const { return _cameras; }

private:
	static bool parseFloat(const Common::String &token, float &value);
	static bool parseVec3(const ScriptInstruction &instruction, Vec3f &value);

	Common::Array<ScriptCamera> _cameras;
};

} // namespace ZeroComico

#endif
