/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: dialogue definitions
 */

#ifndef ZEROCOMICO_DIALOG_SCRIPT_H
#define ZEROCOMICO_DIALOG_SCRIPT_H

#include "common/array.h"
#include "common/path.h"
#include "common/str.h"

#include "zerocomico/script_program.h"

namespace ZeroComico {

struct DialogSpeaker {
	Common::String name;
	Common::String key;
	uint32 red;
	uint32 green;
	uint32 blue;
	float speed;
};

struct DialogLine {
	Common::String speakerKey;
	Common::String text;
};

struct DialogChoice {
	Common::String text;
	Common::String targetDialog;
	bool enabled;
};

struct DialogDefinition {
	Common::String name;
	Common::Array<DialogLine> lines;
	Common::Array<DialogChoice> choices;
	uint32 doStart;
	uint32 doEnd;
};

class DialogScript {
public:
	bool load(const Common::Path &path);
	bool parse(const ScriptProgram &program);

	const ScriptProgram &program() const { return _program; }
	const DialogDefinition *findDialog(const Common::String &name) const;
	DialogDefinition *findDialogMutable(const Common::String &name);
	const DialogSpeaker *findSpeakerByKey(const Common::String &key) const;

	Common::Array<DialogSpeaker> speakers;
	Common::Array<DialogDefinition> dialogs;

private:
	ScriptProgram _program;
};

} // namespace ZeroComico

#endif
