/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine
 */

#ifndef ZEROCOMICO_H
#define ZEROCOMICO_H

#include "engines/engine.h"

#include "zerocomico/bsp.h"
#include "zerocomico/cutscene_script.h"
#include "zerocomico/character_script.h"
#include "zerocomico/dialog_script.h"
#include "zerocomico/puzzle_script.h"
#include "zerocomico/scene_model.h"
#include "zerocomico/script_vm.h"
#include "zerocomico/sequence_script.h"
#include "zerocomico/shape_script.h"
#include "zerocomico/software_renderer.h"
#include "zerocomico/text_table_script.h"
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
	bool evaluateScriptCondition(const ScriptInstruction &instruction,
	                           bool &result) const override;

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
	bool showScriptLine(const Common::String &speaker, const Common::String &text);
	bool playDialogue(const Common::String &name,
	                  const RenderCamera &camera,
	                  const Common::Path &sceneDirectory,
	                  const Common::Path &playerDirectory,
	                  Graphics::ManagedSurface &frame,
	                  uint32 depth = 0);
	void runMenu();
	void showImageModal(const Common::Path &path);
	void showBootstrapScreen();
	void waitForExit();

	const ADGameDescription *_gameDescription;
	SceneModel _menuScene;
	SceneModel _activeScene;
	SceneModel _playerScene;
	SequenceScript _playerSequences;
	Common::Path _playerAssetDirectory;
	CharacterScript _playerCharacterScript;
	PuzzleScript _activePuzzle;
	DialogScript _activeDialog;
	TextTableScript _activeTextTables;
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
	Common::String _activeRoomPrefix;
	Common::String _pendingSaySpeaker;
	Common::String _pendingSayText;
	Common::String _pendingDialogName;
	int32 _lastDialogueChoice;
	bool _scriptDialogueContextActive;
	RenderCamera _scriptDialogueCamera;
	Common::Path _scriptDialogueSceneDirectory;
	Common::Path _scriptDialoguePlayerDirectory;
	Graphics::ManagedSurface *_scriptDialogueFrame;
	Common::String _pendingMainPlace;
	Common::String _pendingRoomName;
	Common::String _pendingRoomCutscene;
	Common::String _pendingCameraName;
	Common::String _defaultRoomCameraName;
	Common::String _selectedInventoryObject;
	Common::String _combineInventoryFirst;
	Common::String _combineInventorySecond;
	Common::Array<Common::String> _inventoryObjects;
	Common::Array<Common::String> _hiddenSceneMeshes;
	Common::Array<Common::String> _sceneLoopTargets;
	Common::Array<Common::String> _sceneLoopSources;
	Common::Array<uint32> _sceneLoopStartMillis;
	ScriptVM _scriptVM;
	bool _interfaceDisabled;
	bool _3dEnabled;
};

} // namespace ZeroComico

#endif
