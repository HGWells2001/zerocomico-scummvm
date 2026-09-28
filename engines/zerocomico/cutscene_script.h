/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: cutscene timeline scripts
 */

#ifndef ZEROCOMICO_CUTSCENE_SCRIPT_H
#define ZEROCOMICO_CUTSCENE_SCRIPT_H

#include "common/array.h"
#include "common/path.h"
#include "common/str.h"

namespace ZeroComico {

class ScriptProgram;

enum CutsceneEventType {
	kCutsceneSample,
	kCutsceneText,
	kCutsceneStopText,
	kCutsceneFadeOut,
	kCutsceneSetEnvSound
};

struct CutsceneEvent {
	CutsceneEventType type;
	uint32 frame;
	Common::Array<Common::String> args;
};

struct CutsceneTimeline {
	Common::String name;
	Common::Array<CutsceneEvent> events;
};

class CutsceneScript {
public:
	bool load(const Common::Path &path);
	bool parse(const ScriptProgram &program);

	const CutsceneTimeline *findTimeline(const Common::String &name) const;

	Common::Array<CutsceneTimeline> timelines;
};

} // namespace ZeroComico

#endif
