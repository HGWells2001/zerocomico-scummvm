/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine
 */

#ifndef ZEROCOMICO_H
#define ZEROCOMICO_H

#include "engines/engine.h"

#include "zerocomico/bsp.h"
#include "zerocomico/cutscene_script.h"
#include "zerocomico/puzzle_script.h"
#include "zerocomico/scene_model.h"
#include "zerocomico/script_vm.h"
#include "zerocomico/sequence_script.h"
#include "zerocomico/shape_script.h"
#include "zerocomico/software_renderer.h"
#include "graphics/managed_surface.h"

struct ADGameDescription;

namespace ZeroComico {

class ZeroComicoEngine : public Engine, public ScriptVMHost {
public:
	ZeroComicoEngine(OSystem *syst, const ADGameDescription *desc);
	~ZeroComicoEngine() override = default;

	Common::Error run() override;
	bool hasFeature(EngineFeature f) const override;

	bool executeScriptOpcode(const ScriptInstruction &instruction) override;

private:
	bool runStartupScript(const Common::String &mainPlace);
	bool runMainPlaceRuntime(const ScriptProgram &program);
	bool playCutscene(const Common::String &name);
	void playFilmIfPresent(const Common::Path &path);
	bool loadMenuScene();
	bool renderMenuFrame(int selection);
	bool runMainPlacePreview(const Common::String &mainPlace);
	bool renderGameplayFrame(const RenderCamera &camera,
	                         const Common::Path &sceneDirectory,
	                         const Common::Path &playerDirectory,
	                         const Common::String &animationSource,
	                         float animationFrame,
	                         Graphics::ManagedSurface &frame);
	void runMenu();
	void showImageModal(const Common::Path &path);
	void showBootstrapScreen();
	void waitForExit();

	const ADGameDescription *_gameDescription;
	SceneModel _menuScene;
	SceneModel _activeScene;
	SceneModel _playerScene;
	SequenceScript _playerSequences;
	PuzzleScript _activePuzzle;
	ShapeScript _activeShapes;
	SoftwareRenderer _gameplayRenderer;
	BspMap _activeWalkMap;
	BspMap _activeCameraMap;
	Vec3f _playerPosition;
	Vec3f _playerFacingTarget;
	bool _havePlayerStart;
	bool _playerHatVisible;
	int _playerNavNode;
	Common::String _currentMainPlace;
	Common::String _pendingSaySpeaker;
	Common::String _pendingSayText;
	Common::String _pendingRoomName;
	Common::String _pendingRoomCutscene;
	Common::Array<Common::String> _sceneLoopTargets;
	Common::Array<Common::String> _sceneLoopSources;
	Common::Array<uint32> _sceneLoopStartMillis;
	ScriptVM _scriptVM;
	bool _interfaceDisabled;
	bool _3dEnabled;
};

} // namespace ZeroComico

#endif
