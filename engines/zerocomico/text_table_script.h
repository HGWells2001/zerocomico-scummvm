/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: scene text-table definitions
 */

#ifndef ZEROCOMICO_TEXT_TABLE_SCRIPT_H
#define ZEROCOMICO_TEXT_TABLE_SCRIPT_H

#include "common/array.h"
#include "common/path.h"
#include "common/str.h"

namespace ZeroComico {

struct TextTableDefinition {
	Common::String name;
	Common::String speaker;
	Common::Array<Common::String> lines;
};

class TextTableScript {
public:
	bool load(const Common::Path &path);
	bool parse(const Common::String &text);

	const TextTableDefinition *findTable(const Common::String &name) const;
	const Common::String *findLine(const Common::String &name, int32 index) const;

	Common::Array<TextTableDefinition> tables;
};

} // namespace ZeroComico

#endif
