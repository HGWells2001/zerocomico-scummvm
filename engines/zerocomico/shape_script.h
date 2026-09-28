/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: named shape/position markers
 */

#ifndef ZEROCOMICO_SHAPE_SCRIPT_H
#define ZEROCOMICO_SHAPE_SCRIPT_H

#include "common/array.h"
#include "common/path.h"
#include "common/str.h"

#include "zerocomico/model_data.h"

namespace ZeroComico {

class ScriptProgram;
struct ScriptInstruction;

struct ShapeMarker {
	Common::String name;
	Common::String kind;
	Vec3f a;
	Vec3f b;
};

class ShapeScript {
public:
	bool load(const Common::Path &path);
	bool parse(const ScriptProgram &program);

	const ShapeMarker *find(const Common::String &name) const;
	const Common::Array<ShapeMarker> &shapes() const { return _shapes; }

private:
	static bool parseFloat(const Common::String &token, float &value);
	static bool parseVec3(const ScriptInstruction &instruction, Vec3f &value);

	Common::Array<ShapeMarker> _shapes;
};

} // namespace ZeroComico

#endif
