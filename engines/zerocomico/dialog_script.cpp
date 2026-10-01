/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: dialogue definitions
 */

#include "zerocomico/dialog_script.h"
#include "zerocomico/script_program.h"

#include <cstdlib>

namespace ZeroComico {

namespace {

static uint32 parseUnsigned(const Common::String &value, uint32 fallback) {
	char *end = nullptr;
	const long parsed = strtol(value.c_str(), &end, 10);
	if (!end || end == value.c_str() || *end != 0 || parsed < 0)
		return fallback;
	return (uint32)parsed;
}

static float parseFloat(const Common::String &value, float fallback) {
	char *end = nullptr;
	const double parsed = strtod(value.c_str(), &end);
	if (!end || end == value.c_str() || *end != 0)
		return fallback;
	return (float)parsed;
}

} // namespace

bool DialogScript::load(const Common::Path &path) {
	if (!_program.load(path))
		return false;
	return parse(_program);
}

bool DialogScript::parse(const ScriptProgram &program) {
	speakers.clear();
	dialogs.clear();

	const Common::Array<ScriptInstruction> &instructions = program.instructions();
	for (uint32 i = 0; i < instructions.size(); ++i) {
		const ScriptInstruction &inst = instructions[i];

		if (inst.opcode.equalsIgnoreCase("speaker") && inst.args.size() >= 6) {
			DialogSpeaker speaker;
			speaker.name = inst.args[0];
			speaker.key = inst.args[1];
			speaker.red = parseUnsigned(inst.args[2], 255);
			speaker.green = parseUnsigned(inst.args[3], 255);
			speaker.blue = parseUnsigned(inst.args[4], 255);
			speaker.speed = parseFloat(inst.args[5], 0.07f);
			speakers.push_back(speaker);
			continue;
		}

		if (!inst.opcode.equalsIgnoreCase("Dialog") || inst.args.empty())
			continue;

		DialogDefinition dialog;
		dialog.name = inst.args[0];
		dialog.doStart = dialog.doEnd = 0xffffffffU;
		const int dialogDepth = inst.depth;
		bool inChoices = false;

		for (uint32 j = i + 1; j < instructions.size(); ++j) {
			const ScriptInstruction &child = instructions[j];
			if (child.opcode.equalsIgnoreCase("Dialog") && child.depth <= dialogDepth)
				break;
			if (child.depth < dialogDepth)
				break;

			if (child.opcode.equalsIgnoreCase("do")) {
				dialog.doStart = j + 1;
				for (uint32 k = j + 1; k < instructions.size(); ++k) {
					if (instructions[k].opcode.equalsIgnoreCase("end")) {
						dialog.doEnd = k;
						break;
					}
					if (instructions[k].opcode.equalsIgnoreCase("Dialog") &&
					    instructions[k].depth <= dialogDepth)
						break;
				}
				continue;
			}

			if (child.opcode.equalsIgnoreCase("BEGIN")) {
				inChoices = true;
				continue;
			}
			if (child.opcode.equalsIgnoreCase("END")) {
				inChoices = false;
				continue;
			}

			if (inChoices) {
				DialogChoice choice;
				choice.text = child.opcode;
				choice.enabled = true;
				for (uint32 arg = 0; arg < child.args.size(); ++arg) {
					if (!child.args[arg].empty() && child.args[arg][0] == '@') {
						choice.targetDialog = child.args[arg].substr(1);
						break;
					}
				}
				// #Fix/#Exit choices used by the insult minigames intentionally
				// have no @Dialog target. Preserve them so the runtime can return
				// the selected choice index to GetLastChoisePos.
				if (!choice.text.empty())
					dialog.choices.push_back(choice);
				continue;
			}

			if (child.opcode.size() == 1 && !child.args.empty()) {
				DialogLine line;
				line.speakerKey = child.opcode;
				line.text = child.args[0];
				dialog.lines.push_back(line);
			}
		}

		dialogs.push_back(dialog);
	}

	return !dialogs.empty();
}

const DialogDefinition *DialogScript::findDialog(const Common::String &name) const {
	for (uint32 i = 0; i < dialogs.size(); ++i)
		if (dialogs[i].name.equalsIgnoreCase(name))
			return &dialogs[i];
	return nullptr;
}

DialogDefinition *DialogScript::findDialogMutable(const Common::String &name) {
	for (uint32 i = 0; i < dialogs.size(); ++i)
		if (dialogs[i].name.equalsIgnoreCase(name))
			return &dialogs[i];
	return nullptr;
}

const DialogSpeaker *DialogScript::findSpeakerByKey(const Common::String &key) const {
	for (uint32 i = 0; i < speakers.size(); ++i)
		if (speakers[i].key.equalsIgnoreCase(key))
			return &speakers[i];
	return nullptr;
}

const DialogSpeaker *DialogScript::findSpeakerByName(const Common::String &name) const {
	for (uint32 i = 0; i < speakers.size(); ++i)
		if (speakers[i].name.equalsIgnoreCase(name))
			return &speakers[i];
	return nullptr;
}

void DialogScript::synchronizeState(Common::Serializer &s) {
	uint32 dialogCount = s.isSaving() ? (uint32)dialogs.size() : 0;
	s.syncAsUint32LE(dialogCount);

	for (uint32 dialogIndex = 0; dialogIndex < dialogCount; ++dialogIndex) {
		Common::String dialogName;
		uint32 lineCount = 0;
		uint32 choiceCount = 0;

		if (s.isSaving()) {
			dialogName = dialogs[dialogIndex].name;
			lineCount = (uint32)dialogs[dialogIndex].lines.size();
			choiceCount = (uint32)dialogs[dialogIndex].choices.size();
		}

		s.syncString(dialogName);
		s.syncAsUint32LE(lineCount);
		DialogDefinition *dialog = s.isLoading() ? findDialogMutable(dialogName) : &dialogs[dialogIndex];

		for (uint32 lineIndex = 0; lineIndex < lineCount; ++lineIndex) {
			Common::String text;
			if (s.isSaving())
				text = dialog->lines[lineIndex].text;
			s.syncString(text);
			if (s.isLoading() && dialog && lineIndex < dialog->lines.size())
				dialog->lines[lineIndex].text = text;
		}

		s.syncAsUint32LE(choiceCount);
		for (uint32 choiceIndex = 0; choiceIndex < choiceCount; ++choiceIndex) {
			byte enabled = 0;
			if (s.isSaving())
				enabled = dialog->choices[choiceIndex].enabled ? 1 : 0;
			s.syncAsByte(enabled);
			if (s.isLoading() && dialog && choiceIndex < dialog->choices.size())
				dialog->choices[choiceIndex].enabled = enabled != 0;
		}
	}
}

} // namespace ZeroComico
