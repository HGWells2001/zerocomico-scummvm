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
			event.speechIndex = 0;
			if (!parseFrame(inst.args[0], event.frame))
				continue;
			for (uint32 arg = 1; arg < inst.args.size(); ++arg)
				event.args.push_back(inst.args[arg]);

			// SpeechEnumeration in the retail runtime numbers each speaker's
			// lines in Videos.isc globally and resolves files such as
			// Aldo0000.mp3 and giovanni0003.mp3 by convention.
			if (type == kCutsceneText && !event.args.empty()) {
				for (uint32 timelineIndex = 0; timelineIndex < timelines.size(); ++timelineIndex) {
					const CutsceneTimeline &previousTimeline = timelines[timelineIndex];
					for (uint32 eventIndex = 0; eventIndex < previousTimeline.events.size(); ++eventIndex) {
						const CutsceneEvent &previous = previousTimeline.events[eventIndex];
						if (previous.type == kCutsceneText && !previous.args.empty() &&
						    previous.args[0].equalsIgnoreCase(event.args[0]))
							++event.speechIndex;
					}
				}
			}

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
