/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: decoded text script helpers
 */

#ifndef ZEROCOMICO_SCRIPT_H
#define ZEROCOMICO_SCRIPT_H

#include "common/path.h"
#include "common/str.h"

namespace ZeroComico {

class ScriptText {
public:
	bool load(const Common::Path &path);
	const Common::String &text() const { return _text; }
	Common::String valueAfter(const Common::String &key) const;
	Common::String firstFilm() const;

private:
	Common::String _text;
};

} // namespace ZeroComico

#endif
