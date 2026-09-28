/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: decoded text script helpers
 */

#include "zerocomico/script.h"
#include "zerocomico/resource.h"

#include "common/array.h"

namespace ZeroComico {

static Common::String trim(const Common::String &s) {
	uint first = 0;
	while (first < s.size() && (s[first] == ' ' || s[first] == '\t' || s[first] == '\r' || s[first] == '\n'))
		++first;
	uint last = s.size();
	while (last > first && (s[last - 1] == ' ' || s[last - 1] == '\t' || s[last - 1] == '\r' || s[last - 1] == '\n'))
		--last;
	return Common::String(s.c_str() + first, s.c_str() + last);
}

bool ScriptText::load(const Common::Path &path) {
	Common::Array<byte> decoded;
	if (!ResourceReader::decodeJfxFile(path, decoded))
		return false;
	_text = Common::String(reinterpret_cast<const char *>(decoded.data()), decoded.size());
	return true;
}

Common::String ScriptText::valueAfter(const Common::String &key) const {
	const uint32 pos = _text.find(key);
	if (pos == Common::String::npos)
		return Common::String();
	uint32 begin = pos + key.size();
	while (begin < _text.size() && (_text[begin] == ':' || _text[begin] == ' ' || _text[begin] == '\t'))
		++begin;
	uint32 end = begin;
	while (end < _text.size() && _text[end] != '\r' && _text[end] != '\n')
		++end;
	return trim(Common::String(_text.c_str() + begin, _text.c_str() + end));
}

Common::String ScriptText::firstFilm() const {
	const Common::String opcode("play_CD_film");
	const uint32 pos = _text.find(opcode);
	if (pos == Common::String::npos)
		return Common::String();
	uint32 begin = pos + opcode.size();
	while (begin < _text.size() && (_text[begin] == ' ' || _text[begin] == '\t'))
		++begin;
	uint32 end = begin;
	while (end < _text.size() && _text[end] != ' ' && _text[end] != '\t' && _text[end] != '\r' && _text[end] != '\n')
		++end;
	return trim(Common::String(_text.c_str() + begin, _text.c_str() + end));
}

} // namespace ZeroComico
