/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: line-oriented script parser
 */

#ifndef ZEROCOMICO_SCRIPT_PROGRAM_H
#define ZEROCOMICO_SCRIPT_PROGRAM_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/path.h"
#include "common/str.h"

namespace ZeroComico {

struct ScriptInstruction {
	uint32 lineNumber;
	int depth;
	bool opensBlock;
	bool closesBlock;
	Common::String raw;
	Common::String opcode;
	Common::Array<Common::String> args;
};

class ScriptProgram {
public:
	bool load(const Common::Path &path);
	bool parse(const Common::String &text);

	const Common::Array<ScriptInstruction> &instructions() const { return _instructions; }

	bool hasLabel(const Common::String &name) const;
	int labelIndex(const Common::String &name) const;

private:
	static bool tokenize(const Common::String &line, Common::Array<Common::String> &tokens);
	static Common::String stripComment(const Common::String &line);
	void indexLabels();

	Common::Array<ScriptInstruction> _instructions;
	Common::HashMap<Common::String, uint32, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> _labels;
};

} // namespace ZeroComico

#endif
