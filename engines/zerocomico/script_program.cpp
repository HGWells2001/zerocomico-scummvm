/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: line-oriented script parser
 */

#include "zerocomico/script_program.h"
#include "zerocomico/script.h"

namespace ZeroComico {

namespace {

static Common::String stripBlockComments(const Common::String &text) {
	Common::String out;
	bool blockComment = false;
	bool quoted = false;

	for (uint32 i = 0; i < text.size(); ++i) {
		const char ch = text[i];
		const char next = i + 1 < text.size() ? text[i + 1] : 0;

		if (blockComment) {
			if (ch == '*' && next == '/') {
				out += ' ';
				out += ' ';
				++i;
				blockComment = false;
			} else if (ch == '\n' || ch == '\r') {
				out += ch;
			} else {
				out += ' ';
			}
			continue;
		}

		if (ch == '"' && (i == 0 || text[i - 1] != '\\')) {
			quoted = !quoted;
			out += ch;
			continue;
		}

		if (!quoted && ch == '/' && next == '*') {
			out += ' ';
			out += ' ';
			++i;
			blockComment = true;
			continue;
		}

		out += ch;
	}

	return out;
}

} // namespace

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

	const Common::String cleanText = stripBlockComments(text);
	int depth = 0;
	uint32 lineNumber = 1;
	uint32 start = 0;

	while (start <= cleanText.size()) {
		uint32 end = start;
		while (end < cleanText.size() && cleanText[end] != '\n' && cleanText[end] != '\r')
			++end;

		Common::String raw = stripComment(cleanText.substr(start, end - start));
		raw.trim();

		if (!raw.empty()) {
			Common::Array<Common::String> tokens;
			if (!tokenize(raw, tokens))
				return false;

			if (!tokens.empty()) {
				ScriptInstruction inst;
				inst.lineNumber = lineNumber;
				inst.depth = depth;
				inst.opensBlock = false;
				inst.closesBlock = false;
				inst.raw = raw;

				int lineDepth = depth;
				int structuralDepth = depth;
				bool haveOpcode = false;

				// Braces are significant in token order. Real game data commonly
				// uses balanced inline blocks, for example:
				//   array if_BMap { 0 1 2 3 4 5 6 }
				// and also "} else {"-style transitions. Counting only whether a
				// line contains a brace loses this information and underflows at
				// top level. Walk every token and preserve the net nesting depth.
				for (uint32 i = 0; i < tokens.size(); ++i) {
					if (tokens[i] == "{") {
						inst.opensBlock = true;
						++structuralDepth;
						continue;
					}
					if (tokens[i] == "}") {
						inst.closesBlock = true;
						// A shipped material file contains one surplus closing brace.
						// The original parser accepts it, so keep the structural view
						// tolerant instead of rejecting otherwise valid retail data.
						if (structuralDepth > 0)
							--structuralDepth;
						if (!haveOpcode)
							lineDepth = structuralDepth;
						continue;
					}

					if (!haveOpcode) {
						haveOpcode = true;
						lineDepth = structuralDepth;
						inst.opcode = tokens[i];
					} else {
						inst.args.push_back(tokens[i]);
					}
				}

				inst.depth = lineDepth;
				depth = structuralDepth;
				if (haveOpcode)
					_instructions.push_back(inst);
			}
		}

		if (end >= cleanText.size())
			break;
		if (cleanText[end] == '\r' && end + 1 < cleanText.size() && cleanText[end + 1] == '\n')
			++end;
		start = end + 1;
		++lineNumber;
	}

	// Some generated retail files use End. as an implicit final closure and
	// therefore finish with a positive brace depth. Do not reject them: the
	// nesting information remains useful for all explicit blocks we saw.
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
