/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: scene text-table definitions
 */

#include "zerocomico/text_table_script.h"
#include "zerocomico/script.h"

#include "common/tokenizer.h"

namespace ZeroComico {

namespace {

struct TextTableMacro {
	Common::String name;
	TextTableDefinition body;
};

static Common::String trimLine(const Common::String &value) {
	uint32 first = 0;
	while (first < value.size() &&
	       (value[first] == ' ' || value[first] == '\t' ||
	        value[first] == '\r' || value[first] == '\n'))
		++first;
	uint32 last = value.size();
	while (last > first &&
	       (value[last - 1] == ' ' || value[last - 1] == '\t' ||
	        value[last - 1] == '\r' || value[last - 1] == '\n'))
		--last;
	return value.substr(first, last - first);
}

static bool readQuoted(const Common::String &line, Common::String &out) {
	const uint32 first = line.find('"');
	if (first == Common::String::npos)
		return false;

	uint32 last = Common::String::npos;
	for (uint32 i = first + 1; i < line.size(); ++i) {
		if (line[i] == '"')
			last = i;
	}
	if (last == Common::String::npos || last <= first)
		return false;

	out = line.substr(first + 1, last - first - 1);
	return true;
}

static const TextTableMacro *findMacro(const Common::Array<TextTableMacro> &macros,
                                       const Common::String &name) {
	for (uint32 i = 0; i < macros.size(); ++i)
		if (macros[i].name.equalsIgnoreCase(name))
			return &macros[i];
	return nullptr;
}

} // namespace

bool TextTableScript::load(const Common::Path &path) {
	ScriptText source;
	if (!source.load(path))
		return false;
	return parse(source.text());
}

bool TextTableScript::parse(const Common::String &text) {
	tables.clear();
	Common::Array<TextTableMacro> macros;

	bool inMacro = false;
	bool collecting = false;
	TextTableMacro macro;
	TextTableDefinition table;

	uint32 start = 0;
	while (start <= text.size()) {
		uint32 end = start;
		while (end < text.size() && text[end] != '\r' && text[end] != '\n')
			++end;

		Common::String line = trimLine(text.substr(start, end - start));
		const uint32 comment = line.find("//");
		if (comment != Common::String::npos)
			line = trimLine(line.substr(0, comment));

		if (!line.empty()) {
			if (line.hasPrefixIgnoreCase("%Macro")) {
				Common::StringTokenizer tokens(line);
				(void)tokens.nextToken();
				if (!tokens.empty()) {
					macro = TextTableMacro();
					macro.name = tokens.nextToken();
					inMacro = true;
					collecting = false;
				}
			} else if (line.equalsIgnoreCase("%end")) {
				if (inMacro && !macro.name.empty() && !macro.body.lines.empty())
					macros.push_back(macro);
				inMacro = false;
				collecting = false;
			} else if (line.hasPrefixIgnoreCase("%call")) {
				Common::StringTokenizer tokens(line);
				(void)tokens.nextToken();
				if (!tokens.empty()) {
					const Common::String macroName = tokens.nextToken();
					if (!tokens.empty()) {
						const Common::String tableName = tokens.nextToken();
						const Common::String speaker = tokens.empty()
							? Common::String() : tokens.nextToken();
						const TextTableMacro *sourceMacro = findMacro(macros, macroName);
						if (sourceMacro) {
							TextTableDefinition instance = sourceMacro->body;
							instance.name = tableName;
							instance.speaker = speaker;
							tables.push_back(instance);
						}
					}
				}
			} else if (line.hasPrefixIgnoreCase("texttable")) {
				Common::StringTokenizer tokens(line);
				(void)tokens.nextToken();
				TextTableDefinition fresh;
				if (!tokens.empty())
					fresh.name = tokens.nextToken();
				if (!tokens.empty())
					fresh.speaker = tokens.nextToken();

				if (inMacro)
					macro.body = fresh;
				else
					table = fresh;
				collecting = true;
			} else if (collecting) {
				if (line.equalsIgnoreCase("NULL")) {
					if (!inMacro && !table.name.empty())
						tables.push_back(table);
					collecting = false;
				} else {
					Common::String value;
					if (readQuoted(line, value)) {
						if (inMacro)
							macro.body.lines.push_back(value);
						else
							table.lines.push_back(value);
					}
				}
			}
		}

		if (end >= text.size())
			break;
		if (text[end] == '\r' && end + 1 < text.size() && text[end + 1] == '\n')
			++end;
		start = end + 1;
	}

	return !tables.empty();
}

const TextTableDefinition *TextTableScript::findTable(const Common::String &name) const {
	for (uint32 i = 0; i < tables.size(); ++i)
		if (tables[i].name.equalsIgnoreCase(name))
			return &tables[i];
	return nullptr;
}

const Common::String *TextTableScript::findLine(const Common::String &name, int32 index) const {
	const TextTableDefinition *table = findTable(name);
	if (!table || index < 0 || (uint32)index >= table->lines.size())
		return nullptr;
	return &table->lines[(uint32)index];
}

} // namespace ZeroComico
