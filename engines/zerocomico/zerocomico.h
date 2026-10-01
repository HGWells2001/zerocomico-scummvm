/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine
 */

#ifndef ZEROCOMICO_H
#define ZEROCOMICO_H

#include "engines/engine.h"
#include "audio/mixer.h"
#include "common/events.h"

#include "zerocomico/bsp.h"
#include "zerocomico/camera_script.h"
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

struct DynamicSceneEntity {
	Common::String name;
	Common::String roomName;
	NamedMesh mesh;
	Common::Array<NamedMaterial> materials;
};

struct MixerVolumeSnapshot {
	int plain;
	int sfx;
	int music;
	int speech;
};

struct SamplePlaybackParams {
	Common::String name;
	float farVolume;
	float nearVolume;
	float minRange;
	float maxRange;
};

struct CpuCharacterRuntime {
	Common::String name;
	Common::String roomName;
	Common::String bodyRoot;
	Common::String initialEntity;
	Common::String initialVector;
	Common::Path assetDirectory;
	SceneModel scene;
	Vec3f position;
	Vec3f facing;
	bool alive;
	bool lifeBroken;
	bool positioned;
	bool haveFacing;
	int32 waitState;
	uint32 idleAnimationStartMillis;
	Common::String lastEventSource;
	int32 lastEventFrame;
	bool haveEventFrame;
};

class ZeroComicoEngine : public Engine, public ScriptVMHost {
public:
	ZeroComicoEngine(OSystem *syst, const ADGameDescription *desc);
	~ZeroComicoEngine() override = default;

	Common::Error run() override;
	bool hasFeature(EngineFeature f) const override;

	bool executeScriptOpcode(const ScriptInstruction &instruction) override;
	bool evaluateScriptCondition(const ScriptInstruction &instruction,
	                           bool &result) const override;
	bool yieldScriptExecution() override;

private:
	bool runStartupScript(const Common::String &mainPlace);
	bool runMainPlaceRuntime(const ScriptProgram &program);
	bool runScriptWithAudioClass(const ScriptProgram &program, uint32 startIndex,
	                             uint32 endIndex, uint32 maxSteps, int audioClass);
	bool playCutscene(const Common::String &name);
	bool startLoopCutscene(const Common::String &name);
	bool renderLoopCutsceneFrame(const Common::String &name);
	bool stopLoopCutscene(const Common::String &name);
	void playFilmIfPresent(const Common::Path &path);
	float retailSampleVolume(const Common::String &sampleName,
	                         float sourceDistance = 0.0f) const;
	byte retailChannelVolume(int soundClass, float sourceVolume,
	                         const Common::String &sampleName,
	                         float sourceDistance = 0.0f) const;
	void startRoomMusic(const Common::String &fileName, float volume);
	void setEnvironmentSound(const Common::String &fileName, bool enabled);
	void applyMasterColor(Graphics::ManagedSurface &surface);
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
	void updateScriptKeyState(const Common::Event &event);
	void synchronizePersistentState(Common::Serializer &s);
	DynamicSceneEntity *findDynamicSceneEntity(const Common::String &name);
	const DynamicSceneEntity *findDynamicSceneEntity(const Common::String &name) const;
	bool cloneSceneEntity(const Common::String &sourceName, const Common::String &cloneName);
	void installDynamicBackgroundForRoom(const Common::String &roomName);
	bool setSceneEntityTranslation(const Common::String &name, const Vec3f &position);
	bool setSceneEntityVectorTransform(const Common::String &name, const ShapeMarker &marker);
	CpuCharacterRuntime *findCpuCharacter(const Common::String &name);
	bool instantiateCpuCharacter(const Common::String &name);
	bool giveLifeToCharacter(const Common::String &name);
	void ensureCpuCharactersForRoom(const Common::String &roomName);
	void installCpuCharactersForRoom(const Common::String &roomName);

	const ADGameDescription *_gameDescription;
	SceneModel _menuScene;
	SceneModel _activeScene;
	SceneModel _playerScene;
	SceneModel _loopCutScene;
	SequenceScript _playerSequences;
	Common::Path _playerAssetDirectory;
	Common::String _playerAnimSetName;
	CharacterScript _playerCharacterScript;
	PuzzleScript _activePuzzle;
	PuzzleScript _activeCameraTriggers;
	CameraScript _activeCameraScript;
	DialogScript _activeDialog;
	TextTableScript _activeTextTables;
	ShapeScript _activeShapes;
	ShapeScript _activeCameraShapes;
	SoftwareRenderer _gameplayRenderer;
	BspMap _activeWalkMap;
	BspMap _activeCameraMap;
	Vec3f _playerPosition;
	Vec3f _playerFacingTarget;
	bool _havePlayerStart;
	bool _playerHatVisible;
	int _playerNavNode;
	Common::String _currentMainPlace;
	Common::String _activeRoomName;
	Common::String _activeRoomPrefix;
	Common::Array<Common::String> _activeRoomMaps;
	Common::Array<Common::String> _activeRoomCameraMaps;
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
	Common::String _pendingRoomMapRoomName;
	Common::String _pendingRoomMapName;
	Common::String _pendingCameraName;
	Common::String _defaultRoomCameraName;
	Common::String _dialogCameraFirstName;
	Common::String _dialogCameraSecondName;
	Common::String _lastDialogCameraName;
	Common::String _activeAutoCameraTrigger;
	Common::String _currentMusicName;
	Audio::SoundHandle _musicHandle;
	Common::String _environmentSoundName;
	Audio::SoundHandle _environmentSoundHandle;
	Common::Array<Common::String> _environmentStateRooms;
	Common::Array<Common::String> _environmentStateNames;
	Common::Array<bool> _environmentStateEnabled;
	Common::Array<Common::String> _lightStateNames;
	Common::Array<bool> _lightStateEnabled;
	Common::Array<Common::String> _userEffectStateNames;
	Common::Array<int32> _userEffectStates;
	Common::Array<uint32> _userEffectElapsedMs;
	Common::Array<uint32> _userEffectStateChangedMillis;
	Common::Array<MixerVolumeSnapshot> _masterVolumeStack;
	float _globalMasterVolume;
	float _soundClassVolumes[6];
	float _sampleDefaultFarVolume;
	float _sampleDefaultNearVolume;
	float _sampleDefaultMinRange;
	float _sampleDefaultMaxRange;
	Common::Array<SamplePlaybackParams> _samplePlaybackParams;
	Common::String _loopCutName;
	Common::String _loopCutAssetStem;
	float _loopCutStartFrame;
	float _loopCutEndFrame;
	uint32 _loopCutStartMillis;
	bool _loopCutActive;
	Common::String _pendingTakeAnimation;
	Common::String _pendingTakeInventoryObject;
	int32 _pendingTakeEventFrame;
	bool _pendingTakeActive;
	bool _pendingTakeInventoryAdded;
	Common::String _selectedInventoryObject;
	Common::String _combineInventoryFirst;
	Common::String _combineInventorySecond;
	Common::Array<Common::String> _inventoryObjects;
	Common::Array<Common::String> _hiddenSceneMeshes;
	Common::Array<Common::String> _sceneLoopTargets;
	Common::Array<Common::String> _sceneLoopSources;
	Common::Array<uint32> _sceneLoopStartMillis;
	Common::Array<Common::String> _sceneOneShotTargets;
	Common::Array<Common::String> _sceneOneShotSources;
	Common::Array<uint32> _sceneOneShotStartMillis;
	Common::Array<Common::String> _loadedSetpAssets;
	Common::Array<SceneModel> _loadedSetpScenes;
	Common::Array<Common::String> _setpControllerNames;
	Common::Array<Vec3f> _setpControllerPositions;
	Common::Array<DynamicSceneEntity> _dynamicSceneEntities;
	Common::Array<CpuCharacterRuntime> _cpuCharacters;
	Common::Array<Common::String> _deferredBrokenCpuCharacters;
	Common::Array<RenderActor> _activeRenderActors;
	ScriptVM _scriptVM;
	bool _interfaceDisabled;
	bool _3dEnabled;
	bool _portalsEnabled;
	int _cameraMode;
	bool _cameraModeLocked;
	bool _playerNoCameraReset;
	bool _depthCueEnabled;
	float _depthCueStart;
	float _depthCueEnd;
	float _masterColor[4];
	float _masterColorFrom[4];
	float _masterColorTo[4];
	float _masterColorFadeSteps;
	uint32 _masterColorFadeStartMillis;
	bool _masterColorFadeActive;
	float _spotHeight;
	float _spotMaxDeltaY;
	float _spotDistance;
	float _spotMinDistance;
	float _spotSmooth;
	bool _spotCameraInitialized;
	Vec3f _spotCameraPosition;
	bool _dynamicCameraInitialized;
	Vec3f _dynamicCameraPosition;
	uint32 _scriptKeyMask;
	int _scriptAudioClass;
};

} // namespace ZeroComico

#endif
