/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine
 */

#include "zerocomico/zerocomico.h"
#include "zerocomico/resource.h"
#include "zerocomico/script.h"
#include "zerocomico/script_program.h"

#include "common/events.h"
#include "common/file.h"
#include "common/system.h"
#include "engines/advancedDetector.h"
#include "engines/util.h"
#include "graphics/pixelformat.h"
#include "graphics/surface.h"
#include "video/avi_decoder.h"

namespace ZeroComico {

ZeroComicoEngine::ZeroComicoEngine(OSystem *syst, const ADGameDescription *desc)
	: Engine(syst), _gameDescription(desc) {
}

bool ZeroComicoEngine::hasFeature(EngineFeature f) const {
	return f == kSupportsReturnToLauncher;
}

Common::Error ZeroComicoEngine::run() {
	Graphics::PixelFormat format = Graphics::PixelFormat::createFormatBGRA32();
	initGraphics(800, 600, &format);

	ScriptText config;
	if (!config.load(Common::Path("Config.gsc")))
		return Common::kReadingFailed;

	const Common::String mainPlace = config.valueAfter("StartMainplace");
	if (mainPlace.empty()) {
		warning("Zero Comico: Config.gsc has no StartMainplace");
	} else {
		debug(1, "Zero Comico: StartMainplace = %s", mainPlace.c_str());
	}

	// Execute the actual Mp0 room startup sequence rather than hard-coding the
	// intro. The interpreter is intentionally small at this stage, but the
	// source of truth is already the shipped room.isc program.
	if (!runStartupScript(mainPlace)) {
		warning("Zero Comico: could not execute room startup script, using intro fallback");
		playFilmIfPresent(Common::Path("Data/Intro.avi"));
	}

	// Decode the real menu P3D/ANJ pair now, even though the renderer is not
	// wired yet. This makes bootstrap exercise geometry, hierarchy and JACS
	// animation decoding on the retail asset set.
	loadMenuScene();

	showBootstrapScreen();
	waitForExit();
	return Common::kNoError;
}

bool ZeroComicoEngine::runStartupScript(const Common::String &mainPlace) {
	if (mainPlace.empty())
		return false;

	Common::String level = mainPlace;
	if (level.size() >= 2 &&
	    (level[0] == 'm' || level[0] == 'M') &&
	    (level[1] == 'p' || level[1] == 'P'))
		level = Common::String("Mp") + level.substr(2);

	const Common::Path roomPath(level + "/gameplay/room.isc");
	ScriptProgram program;
	if (!program.load(roomPath)) {
		warning("Zero Comico: cannot parse %s", roomPath.toString().c_str());
		return false;
	}

	const Common::Array<ScriptInstruction> &instructions = program.instructions();
	bool inRuntime = false;
	bool inThread = false;
	bool sawRuntime = false;

	for (uint32 i = 0; i < instructions.size() && !shouldQuit(); ++i) {
		const ScriptInstruction &inst = instructions[i];

		if (inst.opcode.equalsIgnoreCase("runtime")) {
			inRuntime = true;
			sawRuntime = true;
			continue;
		}
		if (!inRuntime)
			continue;

		if (inst.opcode.equalsIgnoreCase("begin_thread")) {
			inThread = true;
			continue;
		}
		if (inst.opcode.equalsIgnoreCase("end_thread")) {
			if (inThread)
				return true;
			continue;
		}
		if (!inThread)
			continue;

		if (inst.opcode.equalsIgnoreCase("play_CD_film")) {
			if (!inst.args.empty())
				playFilmIfPresent(Common::Path(inst.args[0]));
			continue;
		}

		// Film playback above is synchronous in this implementation, so the
		// retail wait instruction has already been satisfied.
		if (inst.opcode.equalsIgnoreCase("wait_last_film"))
			continue;

		// These startup opcodes describe state that will become observable
		// once the 3D/menu runtime is active. Keep them in the execution path
		// now so room.isc, rather than C++, remains authoritative.
		if (inst.opcode.equalsIgnoreCase("disable3d") ||
		    inst.opcode.equalsIgnoreCase("enable3d") ||
		    inst.opcode.equalsIgnoreCase("DisableInterface") ||
		    inst.opcode.equalsIgnoreCase("mch_push_master_volume") ||
		    inst.opcode.equalsIgnoreCase("mch_pop_master_volume")) {
			debug(2, "Zero Comico: startup opcode %s", inst.opcode.c_str());
			continue;
		}

		if (inst.opcode.equalsIgnoreCase("mov") && inst.args.size() >= 2) {
			debug(2, "Zero Comico: startup mov %s = %s",
			      inst.args[0].c_str(), inst.args[1].c_str());
			continue;
		}
	}

	return sawRuntime;
}

void ZeroComicoEngine::playFilmIfPresent(const Common::Path &path) {
	if (!Common::File::exists(path))
		return;

	Video::AVIDecoder decoder;
	if (!decoder.loadFile(path)) {
		warning("Zero Comico: cannot decode %s", path.toString().c_str());
		return;
	}

	decoder.start();
	bool skip = false;
	while (!shouldQuit() && !skip && !decoder.endOfVideo()) {
		if (decoder.needsUpdate()) {
			const Graphics::Surface *frame = decoder.decodeNextFrame();
			if (frame) {
				Graphics::Surface *converted = frame->convertTo(_system->getScreenFormat());
				const int x = (800 - converted->w) / 2;
				const int y = (600 - converted->h) / 2;
				_system->copyRectToScreen(converted->getPixels(), converted->pitch,
				                          x, y, converted->w, converted->h);
				converted->free();
				delete converted;
				_system->updateScreen();
			}
		}

		Common::Event event;
		while (_system->getEventManager()->pollEvent(event)) {
			if (event.type == Common::EVENT_QUIT || event.type == Common::EVENT_RETURN_TO_LAUNCHER) {
				quitGame();
				break;
			}
			if (event.type == Common::EVENT_KEYDOWN && event.kbd.keycode == Common::KEYCODE_ESCAPE)
				skip = true;
		}
		_system->delayMillis(10);
	}
}

bool ZeroComicoEngine::loadMenuScene() {
	const Common::Path p3d("Mpx/bodies/interfaccia/interfaccia.p3d");
	const Common::Path anj("Mpx/bodies/interfaccia/interfaccia.anj");

	if (!_menuScene.loadPair(p3d, anj)) {
		warning("Zero Comico: failed to decode menu P3D/ANJ asset pair");
		return false;
	}

	debug(1, "Zero Comico: menu scene decoded: %u materials, %u cameras, %u lights, %u meshes, %u hierarchies, %u clips",
	      (uint)_menuScene.materials.size(), (uint)_menuScene.cameras.size(),
	      (uint)_menuScene.lights.size(), (uint)_menuScene.meshes.size(),
	      (uint)_menuScene.hierarchies.size(), (uint)_menuScene.clips.size());
	return true;
}

void ZeroComicoEngine::showBootstrapScreen() {
	Graphics::ManagedSurface image;

	// Until the decoded menu scene is rendered, use its original 512x512
	// interface texture as a visible bootstrap surface.
	if (!ResourceReader::decodeJgfFile(Common::Path("Mpx/bodies/interfaccia/interf.tga"), image)) {
		if (!ResourceReader::decodeJgfFile(Common::Path("images/CD.tga"), image))
			return;
	}

	const int x = (800 - image.w) / 2;
	const int y = (600 - image.h) / 2;
	_system->copyRectToScreen(image.getPixels(), image.pitch, x, y, image.w, image.h);
	_system->updateScreen();
}

void ZeroComicoEngine::waitForExit() {
	while (!shouldQuit()) {
		Common::Event event;
		while (_system->getEventManager()->pollEvent(event)) {
			if (event.type == Common::EVENT_QUIT || event.type == Common::EVENT_RETURN_TO_LAUNCHER) {
				quitGame();
				break;
			}
			if (event.type == Common::EVENT_KEYDOWN && event.kbd.keycode == Common::KEYCODE_ESCAPE) {
				quitGame();
				break;
			}
		}
		_system->delayMillis(10);
	}
}

} // namespace ZeroComico
