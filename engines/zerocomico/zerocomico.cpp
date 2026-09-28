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
	: Engine(syst), _gameDescription(desc), _scriptVM(this),
	  _interfaceDisabled(false), _3dEnabled(true) {
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
	// intro. The source of truth is the shipped room.isc program.
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
	uint32 runtimeStart = instructions.size();
	uint32 runtimeEnd = instructions.size();

	for (uint32 i = 0; i < instructions.size(); ++i) {
		if (instructions[i].opcode.equalsIgnoreCase("runtime")) {
			runtimeStart = i + 1;
			break;
		}
	}
	if (runtimeStart >= instructions.size())
		return false;

	for (uint32 i = runtimeStart; i < instructions.size(); ++i) {
		if (instructions[i].opcode.equalsIgnoreCase("end_thread")) {
			runtimeEnd = i + 1;
			break;
		}
	}

	_scriptVM.reset();
	if (!_scriptVM.run(program, runtimeStart, runtimeEnd, 4096))
		return false;

	int32 menuFlag = 0;
	if (_scriptVM.getVariable("if_MenuIface", menuFlag))
		debug(1, "Zero Comico: room startup selected menu state %d", menuFlag);

	return true;
}

bool ZeroComicoEngine::executeScriptOpcode(const ScriptInstruction &instruction) {
	const Common::String &op = instruction.opcode;

	if (op.equalsIgnoreCase("play_CD_film")) {
		if (instruction.args.empty())
			return false;
		playFilmIfPresent(Common::Path(instruction.args[0]));
		return true;
	}

	// Film playback is synchronous for now, so this wait is already fulfilled.
	if (op.equalsIgnoreCase("wait_last_film"))
		return true;

	if (op.equalsIgnoreCase("disable3d")) {
		_3dEnabled = false;
		return true;
	}
	if (op.equalsIgnoreCase("enable3d")) {
		_3dEnabled = true;
		return true;
	}

	if (op.equalsIgnoreCase("DisableInterface")) {
		if (instruction.args.empty())
			return false;
		int32 disabled = 0;
		if (!_scriptVM.resolveValue(instruction.args[0], disabled))
			return false;
		_interfaceDisabled = disabled != 0;
		return true;
	}

	// The master-volume stack is preserved as an opcode boundary. Audio class
	// routing will be connected when the sound runtime is added.
	if (op.equalsIgnoreCase("mch_push_master_volume") ||
	    op.equalsIgnoreCase("mch_pop_master_volume"))
		return true;

	return false;
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
