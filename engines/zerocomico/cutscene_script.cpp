/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: cutscene timeline scripts
 */

#include "zerocomico/cutscene_script.h"
#include "zerocomico/script_program.h"

#include <cstdlib>

namespace ZeroComico {

namespace {

static bool parseFrame(const Common::String &value, uint32 &frame) {
	char *end = nullptr;
	const long parsed = strtol(value.c_str(), &end, 10);
	if (!end || *end != 0 || parsed < 0)
		return false;
	frame = (uint32)parsed;
	return true;
}

static bool eventTypeForOpcode(const Common::String &opcode, CutsceneEventType &type) {
	if (opcode.equalsIgnoreCase("sample")) {
		type = kCutsceneSample;
		return true;
	}
	if (opcode.equalsIgnoreCase("text")) {
		type = kCutsceneText;
		return true;
	}
	if (opcode.equalsIgnoreCase("stop_text")) {
		type = kCutsceneStopText;
		return true;
	}
	if (opcode.equalsIgnoreCase("fade_out")) {
		type = kCutsceneFadeOut;
		return true;
	}
	if (opcode.equalsIgnoreCase("set_envsound")) {
		type = kCutsceneSetEnvSound;
		return true;
	}
	return false;
}

} // namespace

bool CutsceneScript::load(const Common::Path &path) {
	ScriptProgram program;
	if (!program.load(path))
		return false;
	return parse(program);
}

bool CutsceneScript::parse(const ScriptProgram &program) {
	timelines.clear();

	const Common::Array<ScriptInstruction> &instructions = program.instructions();
	CutsceneTimeline *current = nullptr;

	for (uint32 i = 0; i < instructions.size(); ++i) {
		const ScriptInstruction &inst = instructions[i];

		if (inst.opcode.equalsIgnoreCase("end.")) {
			current = nullptr;
			continue;
		}

		CutsceneEventType type;
		if (eventTypeForOpcode(inst.opcode, type)) {
			if (!current || inst.args.empty())
				continue;

			CutsceneEvent event;
			event.type = type;
			if (!parseFrame(inst.args[0], event.frame))
				continue;
			for (uint32 arg = 1; arg < inst.args.size(); ++arg)
				event.args.push_back(inst.args[arg]);
			current->events.push_back(event);
			continue;
		}

		// Timeline headers in Videos.isc are bare labels such as c111: and
		// d101_dor:. ScriptProgram removes the colon during tokenization.
		if (inst.args.empty() && !inst.opcode.empty()) {
			CutsceneTimeline timeline;
			timeline.name = inst.opcode;
			timelines.push_back(timeline);
			current = &timelines.back();
		}
	}

	return !timelines.empty();
}

const CutsceneTimeline *CutsceneScript::findTimeline(const Common::String &name) const {
	for (uint32 i = 0; i < timelines.size(); ++i)
		if (timelines[i].name.equalsIgnoreCase(name))
			return &timelines[i];
	return nullptr;
}

} // namespace ZeroComico
