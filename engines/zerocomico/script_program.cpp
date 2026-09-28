/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: line-oriented script parser
 */

#include "zerocomico/script_program.h"
#include "zerocomico/script.h"

namespace ZeroComico {

Common::String ScriptProgram::stripComment(const Common::String &line) {
	bool quoted = false;
	for (uint32 i = 0; i + 1 < line.size(); ++i) {
		if (line[i] == '"' && (i == 0 || line[i - 1] != '\\'))
			quoted = !quoted;
		if (!quoted && line[i] == '/' && line[i + 1] == '/')
			return line.substr(0, i);
	}
	return line;
}

bool ScriptProgram::tokenize(const Common::String &line, Common::Array<Common::String> &tokens) {
	tokens.clear();
	Common::String current;
	bool quoted = false;

	for (uint32 i = 0; i < line.size(); ++i) {
		const char c = line[i];
		if (c == '"' && (i == 0 || line[i - 1] != '\\')) {
			quoted = !quoted;
			continue;
		}

		if (!quoted && (c == ' ' || c == '\t' || c == ',' || c == ':' || c == '{' || c == '}')) {
			if (!current.empty()) {
				tokens.push_back(current);
				current.clear();
			}
			if (c == '{' || c == '}')
				tokens.push_back(Common::String(c));
			continue;
		}

		current += c;
	}

	if (quoted)
		return false;
	if (!current.empty())
		tokens.push_back(current);
	return true;
}

bool ScriptProgram::load(const Common::Path &path) {
	ScriptText script;
	if (!script.load(path))
		return false;
	return parse(script.text());
}

bool ScriptProgram::parse(const Common::String &text) {
	_instructions.clear();
	_labels.clear();

	int depth = 0;
	uint32 lineNumber = 1;
	uint32 start = 0;

	while (start <= text.size()) {
		uint32 end = start;
		while (end < text.size() && text[end] != '\n' && text[end] != '\r')
			++end;

		Common::String raw = stripComment(text.substr(start, end - start));
		raw.trim();

		if (!raw.empty()) {
			Common::Array<Common::String> tokens;
			if (!tokenize(raw, tokens))
				return false;

			if (!tokens.empty()) {
				bool closes = false;
				bool opens = false;

				for (uint32 i = 0; i < tokens.size(); ++i) {
					if (tokens[i] == "}")
						closes = true;
					else if (tokens[i] == "{")
						opens = true;
				}

				if (closes) {
					if (depth <= 0)
						return false;
					--depth;
				}

				ScriptInstruction inst;
				inst.lineNumber = lineNumber;
				inst.depth = depth;
				inst.opensBlock = opens;
				inst.closesBlock = closes;
				inst.raw = raw;

				for (uint32 i = 0; i < tokens.size(); ++i) {
					if (tokens[i] == "{" || tokens[i] == "}")
						continue;
					if (inst.opcode.empty())
						inst.opcode = tokens[i];
					else
						inst.args.push_back(tokens[i]);
				}

				if (!inst.opcode.empty())
					_instructions.push_back(inst);

				if (opens)
					++depth;
			}
		}

		if (end >= text.size())
			break;
		if (text[end] == '\r' && end + 1 < text.size() && text[end + 1] == '\n')
			++end;
		start = end + 1;
		++lineNumber;
	}

	if (depth != 0)
		return false;

	indexLabels();
	return true;
}

void ScriptProgram::indexLabels() {
	_labels.clear();
	for (uint32 i = 0; i < _instructions.size(); ++i) {
		const ScriptInstruction &inst = _instructions[i];
		if (inst.opcode.equalsIgnoreCase("label") && !inst.args.empty())
			_labels[inst.args[0]] = i;
	}
}

bool ScriptProgram::hasLabel(const Common::String &name) const {
	return _labels.contains(name);
}

int ScriptProgram::labelIndex(const Common::String &name) const {
	LabelMap::const_iterator it = _labels.find(name);
	if (it == _labels.end())
		return -1;
	return it->_value;
}

} // namespace ZeroComico
