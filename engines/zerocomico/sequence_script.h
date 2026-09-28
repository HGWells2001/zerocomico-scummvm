/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: JACS sequence state tables
 */

#ifndef ZEROCOMICO_SEQUENCE_SCRIPT_H
#define ZEROCOMICO_SEQUENCE_SCRIPT_H

#include "common/array.h"
#include "common/path.h"
#include "common/str.h"

namespace ZeroComico {

class ScriptProgram;

struct SequenceTransition {
	Common::String state;
	Common::Array<Common::String> clips;
};

struct SequenceTableRow {
	Common::Array<Common::String> clips;
};

struct AnimationSequence {
	Common::String name;
	Common::Array<SequenceTransition> transitions;
	Common::Array<Common::String> endClips;
	Common::Array<SequenceTableRow> table;
};

class SequenceScript {
public:
	bool load(const Common::Path &path);
	bool parse(const ScriptProgram &program);

	const AnimationSequence *findSequence(const Common::String &name) const;
	const SequenceTransition *findTransition(const Common::String &sequenceName,
	                                         const Common::String &state) const;

	Common::String bodyName;
	Common::Array<AnimationSequence> sequences;
};

} // namespace ZeroComico

#endif
