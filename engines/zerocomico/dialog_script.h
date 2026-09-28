/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: dialogue definitions
 */

#ifndef ZEROCOMICO_DIALOG_SCRIPT_H
#define ZEROCOMICO_DIALOG_SCRIPT_H

#include "common/array.h"
#include "common/path.h"
#include "common/str.h"

namespace ZeroComico {

class ScriptProgram;

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
};

struct DialogDefinition {
	Common::String name;
	Common::Array<DialogLine> lines;
	Common::Array<DialogChoice> choices;
};

class DialogScript {
public:
	bool load(const Common::Path &path);
	bool parse(const ScriptProgram &program);

	const DialogDefinition *findDialog(const Common::String &name) const;
	const DialogSpeaker *findSpeakerByKey(const Common::String &key) const;

	Common::Array<DialogSpeaker> speakers;
	Common::Array<DialogDefinition> dialogs;
};

} // namespace ZeroComico

#endif
