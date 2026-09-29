/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: puzzle/object definitions
 */

#ifndef ZEROCOMICO_PUZZLE_SCRIPT_H
#define ZEROCOMICO_PUZZLE_SCRIPT_H

#include "common/array.h"
#include "common/path.h"
#include "common/str.h"

#include "zerocomico/script_program.h"

namespace ZeroComico {

struct PuzzleObject {
	Common::String name;
	Common::String entity;
	Common::String polygon;
	Common::String examineText;
	float range;
	float size;
	bool enabled;
	bool examinable;
	bool pickable;
	bool operated;
	bool examinated;
	bool autoCamera;
	bool randomPos;
	bool combined;
	bool assigned;
	bool inside;
	bool collision;
	bool soundState;
	uint32 operateStart;
	uint32 operateEnd;
};

class PuzzleScript {
public:
	bool load(const Common::Path &path);
	bool parse();

	PuzzleObject *findObject(const Common::String &name);
	const PuzzleObject *findObject(const Common::String &name) const;
	PuzzleObject *findByEntity(const Common::String &entity);
	const PuzzleObject *findByEntity(const Common::String &entity) const;
	const ScriptProgram &program() const { return _program; }

	Common::Array<PuzzleObject> objects;

private:
	ScriptProgram _program;
};

} // namespace ZeroComico

#endif
