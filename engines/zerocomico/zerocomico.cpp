/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine
 */

#include "zerocomico/zerocomico.h"
#include "zerocomico/chapter.h"
#include "zerocomico/camera_script.h"
#include "zerocomico/resource.h"
#include "zerocomico/script.h"
#include "zerocomico/script_program.h"
#include "zerocomico/shape_script.h"
#include "zerocomico/software_renderer.h"

#include "common/events.h"
#include "common/file.h"
#include "common/system.h"
#include "engines/advancedDetector.h"
#include "engines/util.h"
#include "graphics/font.h"
#include "graphics/fontman.h"
#include "graphics/managed_surface.h"
#include "graphics/pixelformat.h"
#include "graphics/surface.h"

#include "audio/mixer.h"
#ifdef USE_MAD
#include "audio/decoders/mp3.h"
#endif
#include "video/avi_decoder.h"

#include <cmath>
#include <cstdlib>

namespace ZeroComico {

namespace {

static const char *const kMenuButtons[] = {
	"nuova",
	"help",
	"crediti",
	"abbandon",
	"carica"
};

static const int kMenuButtonCount = 5;

static bool containsIgnoreCase(const Common::Array<Common::String> &values,
                               const Common::String &value) {
	for (uint32 i = 0; i < values.size(); ++i)
		if (values[i].equalsIgnoreCase(value))
			return true;
	return false;
}

static bool removeIgnoreCase(Common::Array<Common::String> &values,
                             const Common::String &value) {
	for (uint32 i = 0; i < values.size(); ++i) {
		if (!values[i].equalsIgnoreCase(value))
			continue;
		values.remove_at(i);
		return true;
	}
	return false;
}

static Common::String resolveSceneEntity(const SceneModel &scene,
                                         const Common::String &entity,
                                         const Common::String &roomPrefix) {
	if (scene.findMesh(entity))
		return entity;

	const uint32 separator = entity.find('_');
	if (!roomPrefix.empty() && entity.size() > 1 &&
	    (entity[0] == 'c' || entity[0] == 'C') &&
	    separator != Common::String::npos && separator + 1 < entity.size()) {
		Common::String alias = roomPrefix;
		alias += entity.substr(separator + 1);
		if (scene.findMesh(alias))
			return alias;
	}

	return entity;
}

static PuzzleObject *findPuzzleObjectForMesh(PuzzleScript &puzzle,
                                             const SceneModel &scene,
                                             const Common::String &roomPrefix,
                                             const Common::String &meshName) {
	for (uint32 i = 0; i < puzzle.objects.size(); ++i) {
		PuzzleObject &object = puzzle.objects[i];
		if (resolveSceneEntity(scene, object.entity, roomPrefix).equalsIgnoreCase(meshName))
			return &object;
	}
	return nullptr;
}

static float dotVec3(const Vec3f &a, const Vec3f &b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

static Vec3f subtractVec3(const Vec3f &a, const Vec3f &b) {
	Vec3f out = { a.x - b.x, a.y - b.y, a.z - b.z };
	return out;
}

static Vec3f crossVec3(const Vec3f &a, const Vec3f &b) {
	Vec3f out = {
		a.y * b.z - a.z * b.y,
		a.z * b.x - a.x * b.z,
		a.x * b.y - a.y * b.x
	};
	return out;
}

static bool normalizeVec3(Vec3f &v) {
	const float length2 = dotVec3(v, v);
	if (length2 <= 1.0e-12f)
		return false;
	const float invLength = 1.0f / std::sqrt(length2);
	v.x *= invLength;
	v.y *= invLength;
	v.z *= invLength;
	return true;
}

static bool screenPointToGround(const RenderCamera &camera, int screenX, int screenY,
                                int width, int height, Vec3f &ground) {
	if (camera.focalPixels <= 0.0f)
		return false;

	Vec3f forward = subtractVec3(camera.target, camera.position);
	if (!normalizeVec3(forward))
		return false;

	const Vec3f worldUp = { 0.0f, 1.0f, 0.0f };
	Vec3f right = crossVec3(forward, worldUp);
	if (!normalizeVec3(right))
		return false;
	Vec3f up = crossVec3(right, forward);
	if (!normalizeVec3(up))
		return false;

	const float cameraX = (screenX - width * 0.5f) / camera.focalPixels;
	const float cameraY = -(screenY - height * 0.5f) / camera.focalPixels;
	Vec3f ray = {
		forward.x + right.x * cameraX + up.x * cameraY,
		forward.y + right.y * cameraX + up.y * cameraY,
		forward.z + right.z * cameraX + up.z * cameraY
	};
	if (!normalizeVec3(ray) || std::fabs(ray.y) <= 1.0e-6f)
		return false;

	const float distance = -camera.position.y / ray.y;
	if (distance <= 0.0f)
		return false;

	ground.x = camera.position.x + ray.x * distance;
	ground.y = 0.0f;
	ground.z = camera.position.z + ray.z * distance;
	return true;
}

static bool sampleRootTransform(const SceneModel &scene, const Common::String &targetName,
                                const Common::String &sourceName, float frame,
                                RenderTransform &transform) {
	transform.localTranslation.x = transform.localTranslation.y = transform.localTranslation.z = 0.0f;
	transform.localScale.x = transform.localScale.y = transform.localScale.z = 1.0f;
	transform.localRotation[0] = 1.0f;
	transform.localRotation[1] = transform.localRotation[2] = transform.localRotation[3] = 0.0f;

	const NamedAnimationClip *clip = scene.findClipBySource(targetName, sourceName);
	if (!clip)
		return false;

	float translation[3];
	float scale[3];
	float rotation[4];
	if (!AnimationSampler::sampleTransform(clip->data, targetName, frame, translation, scale, rotation))
		return false;

	// Gameplay movement owns horizontal root motion. Retain the sampled height,
	// scale and orientation but anchor X/Z to the first frame so a walk clip
	// does not translate the actor a second time on top of the BSP path.
	float baseTranslation[3];
	float baseScale[3];
	float baseRotation[4];
	if (!AnimationSampler::sampleTransform(clip->data, targetName, (float)clip->data.startFrame,
	                                      baseTranslation, baseScale, baseRotation))
		return false;

	transform.localTranslation.x = baseTranslation[0];
	transform.localTranslation.y = translation[1];
	transform.localTranslation.z = baseTranslation[2];
	transform.localScale.x = scale[0];
	transform.localScale.y = scale[1];
	transform.localScale.z = scale[2];
	for (int component = 0; component < 4; ++component)
		transform.localRotation[component] = rotation[component];
	return true;
}

static bool animationClipRange(const SceneModel &scene, const Common::String &targetName,
                               const Common::String &sourceName, float &startFrame, float &endFrame) {
	const NamedAnimationClip *clip = scene.findClipBySource(targetName, sourceName);
	if (!clip)
		return false;

	startFrame = (float)clip->data.startFrame;
	endFrame = (float)clip->data.endFrame;
	if (endFrame < startFrame)
		endFrame = startFrame;
	return true;
}

static float animationHorizontalSpeed(const SceneModel &scene, const Common::String &targetName,
                                      const Common::String &sourceName, float frameRate) {
	const NamedAnimationClip *clip = scene.findClipBySource(targetName, sourceName);
	if (!clip || clip->data.endFrame <= clip->data.startFrame || frameRate <= 0.0f)
		return 0.0f;

	float startTranslation[3];
	float startScale[3];
	float startRotation[4];
	float endTranslation[3];
	float endScale[3];
	float endRotation[4];
	if (!AnimationSampler::sampleTransform(clip->data, targetName, (float)clip->data.startFrame,
	                                      startTranslation, startScale, startRotation) ||
	    !AnimationSampler::sampleTransform(clip->data, targetName, (float)clip->data.endFrame,
	                                      endTranslation, endScale, endRotation))
		return 0.0f;

	const float dx = endTranslation[0] - startTranslation[0];
	const float dz = endTranslation[2] - startTranslation[2];
	const float distance = std::sqrt(dx * dx + dz * dz);
	const float durationSeconds =
		((float)clip->data.endFrame - (float)clip->data.startFrame) / frameRate;
	if (durationSeconds <= 0.0f)
		return 0.0f;
	return distance / durationSeconds;
}

static void drawCutsceneSubtitle(Graphics::ManagedSurface &surface,
                                 const Common::String &speaker,
                                 const Common::String &text) {
	if (text.empty())
		return;

	const Graphics::Font *font = FontMan.getFontByUsage(Graphics::FontManager::kBigGUIFont);
	if (!font)
		font = FontMan.getFontByUsage(Graphics::FontManager::kGUIFont);
	if (!font)
		return;

	uint32 color = surface.format.RGBToColor(255, 255, 255);
	if (speaker.equalsIgnoreCase("Aldo"))
		color = surface.format.RGBToColor(0, 255, 0);
	else if (speaker.equalsIgnoreCase("Giovanni"))
		color = surface.format.RGBToColor(255, 255, 0);
	else if (speaker.equalsIgnoreCase("Giacomo"))
		color = surface.format.RGBToColor(0, 255, 255);

	const uint32 shadow = surface.format.RGBToColor(0, 0, 0);
	Common::Array<Common::String> lines;
	font->wordWrapText(text, 720, lines);
	const int lineHeight = font->getFontHeight() + 2;
	const int totalHeight = lineHeight * (int)lines.size();
	int y = 570 - totalHeight;
	if (y < 450)
		y = 450;

	for (uint32 i = 0; i < lines.size(); ++i) {
		font->drawString(&surface, lines[i], 41, y + 1, 720, shadow, Graphics::kTextAlignCenter);
		font->drawString(&surface, lines[i], 40, y, 720, color, Graphics::kTextAlignCenter);
		y += lineHeight;
	}
}

static void drawInventoryOverlay(Graphics::ManagedSurface &surface,
                                 const Common::Array<Common::String> &inventory,
                                 const Common::String &selected,
                                 const Common::String &combineFirst) {
	if (inventory.empty())
		return;

	const Graphics::Font *font = FontMan.getFontByUsage(Graphics::FontManager::kGUIFont);
	if (!font)
		return;

	Common::String text("Inventario [TAB]: ");
	if (selected.empty())
		text += "(nessun oggetto selezionato)";
	else
		text += selected;
	if (!combineFirst.empty()) {
		text += "   Combina [C]: ";
		text += combineFirst;
		text += " + ...";
	}

	const uint32 shadow = surface.format.RGBToColor(0, 0, 0);
	const uint32 color = surface.format.RGBToColor(255, 255, 255);
	font->drawString(&surface, text, 11, 11, 778, shadow, Graphics::kTextAlignLeft);
	font->drawString(&surface, text, 10, 10, 778, color, Graphics::kTextAlignLeft);
}

static void drawDialogueChoices(Graphics::ManagedSurface &surface,
                                const Common::Array<DialogChoice> &choices,
                                uint32 selected) {
	const Graphics::Font *font = FontMan.getFontByUsage(Graphics::FontManager::kGUIFont);
	if (!font)
		return;

	const uint32 normal = surface.format.RGBToColor(255, 255, 255);
	const uint32 active = surface.format.RGBToColor(255, 255, 0);
	const uint32 shadow = surface.format.RGBToColor(0, 0, 0);
	const int lineHeight = font->getFontHeight() + 5;
	int y = 600 - (int)choices.size() * lineHeight - 22;
	if (y < 330)
		y = 330;

	for (uint32 i = 0; i < choices.size(); ++i) {
		const Common::String text = Common::String::format("%u. %s",
			(uint)(i + 1), choices[i].text.c_str());
		font->drawString(&surface, text, 31, y + 1, 738, shadow, Graphics::kTextAlignLeft);
		font->drawString(&surface, text, 30, y, 738,
		                 i == selected ? active : normal, Graphics::kTextAlignLeft);
		y += lineHeight;
	}
}

static void applyFadeToBlack(Graphics::ManagedSurface &surface, float amount) {
	if (amount <= 0.0f)
		return;
	if (amount > 1.0f)
		amount = 1.0f;

	const float keep = 1.0f - amount;
	for (int y = 0; y < surface.h; ++y) {
		byte *row = static_cast<byte *>(surface.getBasePtr(0, y));
		for (int x = 0; x < surface.w; ++x) {
			row[x * 4 + 0] = (byte)(row[x * 4 + 0] * keep);
			row[x * 4 + 1] = (byte)(row[x * 4 + 1] * keep);
			row[x * 4 + 2] = (byte)(row[x * 4 + 2] * keep);
		}
	}
}



} // namespace

ZeroComicoEngine::ZeroComicoEngine(OSystem *syst, const ADGameDescription *desc)
	: Engine(syst), _gameDescription(desc), _havePlayerStart(false), _playerHatVisible(true),
	  _playerNavNode(-1), _scriptVM(this), _interfaceDisabled(false), _3dEnabled(true) {
	_playerPosition.x = _playerPosition.y = _playerPosition.z = 0.0f;
	_playerFacingTarget.x = _playerFacingTarget.y = _playerFacingTarget.z = 0.0f;
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

	// The retail startup leaves if_MenuIface at 1. Decode and render the real
	// P3D menu scene with that initial visibility state. If any part of the 3D
	// path fails, retain the old decoded-texture bootstrap as a safe fallback.
	if (loadMenuScene() && renderMenuFrame(0))
		runMenu();
	else {
		showBootstrapScreen();
		waitForExit();
	}

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

bool ZeroComicoEngine::runMainPlaceRuntime(const ScriptProgram &program) {
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
		if (instructions[i].opcode.equalsIgnoreCase("end")) {
			runtimeEnd = i;
			break;
		}
	}

	return _scriptVM.run(program, runtimeStart, runtimeEnd, 8192);
}

bool ZeroComicoEngine::playCutscene(const Common::String &name) {
	if (_currentMainPlace.empty() || name.empty())
		return false;

	Common::String assetStem = name;
	const Common::Path videoDirectory(_currentMainPlace + "/videos");
	SceneModel scene;
	bool loadedScene = scene.loadPair(videoDirectory.appendComponent(assetStem + ".p3d"),
	                                  videoDirectory.appendComponent(assetStem + ".anj"));

	// Retail Mp1 mixes c121/c131 lowercase filenames with C111/C112/... uppercase
	// filenames. Try the editor-style uppercase C form only when the literal
	// script spelling did not resolve.
	if (!loadedScene && !assetStem.empty() && assetStem[0] == 'c') {
		assetStem = Common::String("C") + assetStem.substr(1);
		loadedScene = scene.loadPair(videoDirectory.appendComponent(assetStem + ".p3d"),
		                             videoDirectory.appendComponent(assetStem + ".anj"));
	}

	if (!loadedScene) {
		warning("Zero Comico: cannot load cutscene %s", name.c_str());
		return false;
	}

	CutsceneScript timelineScript;
	const CutsceneTimeline *timeline = nullptr;
	if (timelineScript.load(videoDirectory.appendComponent("Videos.isc")))
		timeline = timelineScript.findTimeline(name);

	const NamedAnimationClip *cameraClip = nullptr;
	const NamedAnimationClip *targetClip = nullptr;
	Common::String cameraName;
	float startFrame = 0.0f;
	float endFrame = 0.0f;
	bool haveRange = false;

	for (uint32 clipIndex = 0; clipIndex < scene.clips.size(); ++clipIndex) {
		const NamedAnimationClip &clip = scene.clips[clipIndex];
		if (!clip.data.sourceName.equalsIgnoreCase(assetStem))
			continue;

		if (!haveRange) {
			startFrame = (float)clip.data.startFrame;
			endFrame = (float)clip.data.endFrame;
			haveRange = true;
		} else {
			if ((float)clip.data.startFrame < startFrame)
				startFrame = (float)clip.data.startFrame;
			if ((float)clip.data.endFrame > endFrame)
				endFrame = (float)clip.data.endFrame;
		}

		for (uint32 trackIndex = 0; trackIndex < clip.data.tracks.size(); ++trackIndex) {
			if (clip.data.tracks[trackIndex].kind == kAnimCamera && !cameraClip) {
				cameraClip = &clip;
				cameraName = clip.data.tracks[trackIndex].targetName;
			}
		}
	}

	if (!haveRange)
		return false;

	if (!cameraName.empty())
		targetClip = scene.findClipBySource(cameraName + ".target", assetStem);

	const NamedCamera *embeddedCamera = cameraName.empty() ? nullptr : scene.findCamera(cameraName);
	if (!embeddedCamera && !scene.cameras.empty())
		embeddedCamera = &scene.cameras[0];

	SoftwareRenderer renderer;
	Graphics::ManagedSurface frameSurface;
	Common::Array<Common::String> visibleMeshes;
	uint32 nextEvent = 0;
	Common::String subtitleSpeaker;
	Common::String subtitleText;
	Audio::SoundHandle cutsceneSfxHandle;
	Audio::SoundHandle cutsceneSpeechHandle;
	int32 fadeStartFrame = -1;
	uint32 fadeDurationFrames = 0;
	bool skip = false;

	for (float frame = startFrame; frame <= endFrame && !shouldQuit() && !skip; frame += 1.0f) {
		if (!scene.poseCutsceneGeometry(assetStem, frame))
			warning("Zero Comico: cutscene %s pose failed at frame %.0f", name.c_str(), frame);
		scene.visibleMeshesForSource(assetStem, frame, visibleMeshes);

		RenderCamera renderCamera;
		bool haveCamera = false;
		if (cameraClip && targetClip && !cameraName.empty()) {
			float position[3];
			float target[3];
			float focalLength = 0.0f;
			float roll = 0.0f;
			if (AnimationSampler::sampleCamera(cameraClip->data, cameraName, frame,
			                                  position, focalLength, roll) &&
			    AnimationSampler::sampleTarget(targetClip->data, cameraName + ".target", frame, target) &&
			    focalLength > 0.0f) {
				renderCamera.position = { position[0], position[1], position[2] };
				renderCamera.target = { target[0], target[1], target[2] };
				// The animated camera channel uses the same focal-length convention
				// as the P3D camera record. Roll is parsed but the software camera
				// basis does not apply it yet.
				renderCamera.focalPixels = focalLength * 800.0f / 36.0f;
				haveCamera = true;
			}
		}

		if (!haveCamera && embeddedCamera && embeddedCamera->data.fov > 0.0f) {
			renderCamera.position = embeddedCamera->data.position;
			renderCamera.target = embeddedCamera->data.target;
			renderCamera.focalPixels = embeddedCamera->data.fov * 800.0f / 36.0f;
			haveCamera = true;
		}

		if (!haveCamera ||
		    !renderer.render(scene, renderCamera, videoDirectory, visibleMeshes,
		                     frameSurface, 800, 600)) {
			warning("Zero Comico: cannot render cutscene %s frame %.0f", name.c_str(), frame);
			return false;
		}

		if (timeline) {
			while (nextEvent < timeline->events.size() &&
			       timeline->events[nextEvent].frame <= (uint32)frame) {
				const CutsceneEvent &event = timeline->events[nextEvent++];
				switch (event.type) {
				case kCutsceneSample:
					if (!event.args.empty()) {
						debug(1, "Zero Comico: cutscene %s sample %s at frame %u",
						      name.c_str(), event.args[0].c_str(), event.frame);
#ifdef USE_MAD
						Common::File *sampleFile = new Common::File();
						const Common::Path samplePath(
							Common::String("Sound/") + event.args[0] + ".mp3");
						if (sampleFile->open(samplePath)) {
							Audio::SeekableAudioStream *stream =
								Audio::makeMP3Stream(sampleFile, DisposeAfterUse::YES);
							if (stream)
								_mixer->playStream(Audio::Mixer::kSFXSoundType,
								                   &cutsceneSfxHandle, stream);
							else
								delete sampleFile;
						} else {
							delete sampleFile;
						}
#endif
					}
					break;
				case kCutsceneText:
					if (event.args.size() >= 2) {
						subtitleSpeaker = event.args[0];
						subtitleText = event.args[1];
						debug(1, "Zero Comico: cutscene %s subtitle %s[%u]: %s",
						      name.c_str(), subtitleSpeaker.c_str(), event.speechIndex,
						      subtitleText.c_str());
#ifdef USE_MAD
						if (_mixer->isSoundHandleActive(cutsceneSpeechHandle))
							_mixer->stopHandle(cutsceneSpeechHandle);

						const Common::String speechDirectory =
							_currentMainPlace.equalsIgnoreCase("Mp1")
								? Common::String("Speech/MP1")
								: Common::String("Speech/") + _currentMainPlace;
						Common::String speechFileName = Common::String::format(
							"%s%04u.mp3", subtitleSpeaker.c_str(), event.speechIndex);
						Common::Path speechPath =
							Common::Path(speechDirectory).appendComponent(speechFileName);

						Common::File *speechFile = new Common::File();
						if (!speechFile->open(speechPath)) {
							delete speechFile;
							speechFile = new Common::File();
							speechFileName.toLowercase();
							speechPath = Common::Path(speechDirectory).appendComponent(speechFileName);
							if (!speechFile->open(speechPath)) {
								delete speechFile;
								speechFile = nullptr;
							}
						}

						if (speechFile) {
							Audio::SeekableAudioStream *stream =
								Audio::makeMP3Stream(speechFile, DisposeAfterUse::YES);
							if (stream)
								_mixer->playStream(Audio::Mixer::kSpeechSoundType,
								                   &cutsceneSpeechHandle, stream);
							else
								delete speechFile;
						}
#endif
					}
					break;
				case kCutsceneStopText:
					subtitleSpeaker.clear();
					subtitleText.clear();
					debug(1, "Zero Comico: cutscene %s subtitle stop at frame %u",
					      name.c_str(), event.frame);
					break;
				case kCutsceneFadeOut:
					fadeStartFrame = (int32)event.frame;
					fadeDurationFrames = 1;
					if (!event.args.empty()) {
						const long duration = strtol(event.args[0].c_str(), nullptr, 10);
						if (duration > 0)
							fadeDurationFrames = (uint32)duration;
					}
					debug(1, "Zero Comico: cutscene %s fade-out at frame %u over %u frames",
					      name.c_str(), event.frame, fadeDurationFrames);
					break;
				case kCutsceneSetEnvSound:
					debug(1, "Zero Comico: cutscene %s environment-sound event at frame %u",
					      name.c_str(), event.frame);
					break;
				}
			}
		}

		drawCutsceneSubtitle(frameSurface, subtitleSpeaker, subtitleText);
		if (fadeStartFrame >= 0 && frame >= (float)fadeStartFrame) {
			const float fadeProgress =
				(frame - (float)fadeStartFrame) / (float)fadeDurationFrames;
			applyFadeToBlack(frameSurface, fadeProgress);
		}

		_system->copyRectToScreen(frameSurface.getPixels(), frameSurface.pitch,
		                          0, 0, frameSurface.w, frameSurface.h);
		_system->updateScreen();

		Common::Event input;
		while (_system->getEventManager()->pollEvent(input)) {
			if (input.type == Common::EVENT_QUIT || input.type == Common::EVENT_RETURN_TO_LAUNCHER) {
				quitGame();
				break;
			}
			if (input.type == Common::EVENT_KEYDOWN && input.kbd.keycode == Common::KEYCODE_ESCAPE) {
				skip = true;
				break;
			}
		}

		// Retail cutscene timelines are frame-indexed at 25 fps.
		_system->delayMillis(40);
	}

	if (_mixer->isSoundHandleActive(cutsceneSfxHandle))
		_mixer->stopHandle(cutsceneSfxHandle);
	if (_mixer->isSoundHandleActive(cutsceneSpeechHandle))
		_mixer->stopHandle(cutsceneSpeechHandle);
	return !shouldQuit();
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

	if (op.equalsIgnoreCase("play_cut") || op.equalsIgnoreCase("play_open_cut")) {
		if (instruction.args.empty())
			return false;
		return playCutscene(instruction.args[0]);
	}

	// Cutscene playback is synchronous in this first runtime.
	if (op.equalsIgnoreCase("wait_cut"))
		return true;

	if (op.equalsIgnoreCase("playl")) {
		if (instruction.args.size() < 2)
			return false;

		for (uint32 i = 0; i < _sceneLoopTargets.size(); ++i) {
			if (_sceneLoopTargets[i].equalsIgnoreCase(instruction.args[0])) {
				_sceneLoopSources[i] = instruction.args[1];
				_sceneLoopStartMillis[i] = _system->getMillis();
				return true;
			}
		}

		_sceneLoopTargets.push_back(instruction.args[0]);
		_sceneLoopSources.push_back(instruction.args[1]);
		_sceneLoopStartMillis.push_back(_system->getMillis());
		return true;
	}

	if (op.equalsIgnoreCase("hide_subobj") || op.equalsIgnoreCase("unhide_subobj")) {
		if (instruction.args.size() >= 2 &&
		    instruction.args[0].equalsIgnoreCase("gio_giovanni") &&
		    instruction.args[1].equalsIgnoreCase("gio_cappello"))
			_playerHatVisible = op.equalsIgnoreCase("unhide_subobj");
		return true;
	}

	if (op.equalsIgnoreCase("SetCharPos_Vector")) {
		if (instruction.args.size() < 2)
			return false;
		const ShapeMarker *marker = _activeShapes.find(instruction.args[1]);
		if (!marker)
			return false;
		_playerPosition = marker->a;
		_playerFacingTarget = marker->b;
		_havePlayerStart = true;
		_playerNavNode = -1;
		return true;
	}

	if (op.equalsIgnoreCase("chplace")) {
		if (instruction.args.empty())
			return false;
		_pendingRoomName = instruction.args[0];
		_pendingRoomCutscene.clear();
		if (instruction.args.size() >= 2)
			_pendingRoomCutscene = instruction.args[1];
		return true;
	}

	if (op.equalsIgnoreCase("setmap")) {
		if (instruction.args.size() < 2 || _currentMainPlace.empty())
			return false;

		const Common::Path mapPath =
			Common::Path(_currentMainPlace + "/gameplay").appendComponent(instruction.args[1]);
		BspMap replacement;
		if (!replacement.load(mapPath))
			return false;
		_activeWalkMap = replacement;
		_playerNavNode = _activeWalkMap.graph.empty()
			? -1 : _activeWalkMap.nearestGraphNode(_playerPosition.x, _playerPosition.z);
		debug(1, "Zero Comico: switched walk map to %s at node %d",
		      instruction.args[1].c_str(), _playerNavNode);
		return true;
	}

	if (op.equalsIgnoreCase("ChangeMainplace")) {
		if (instruction.args.empty())
			return false;
		_pendingMainPlace = instruction.args[0];
		_pendingRoomName.clear();
		_pendingRoomCutscene.clear();
		debug(1, "Zero Comico: requested main-place transition to %s",
		      _pendingMainPlace.c_str());
		return true;
	}

	if (op.equalsIgnoreCase("SetPlace")) {
		if (instruction.args.empty())
			return false;
		_pendingRoomName = instruction.args[0];
		_pendingRoomCutscene.clear();
		return true;
	}

	if (op.equalsIgnoreCase("CSetPlace")) {
		// Character-specific SetPlace. The retail runtime uses this during the
		// Mp5 bootstrap as "CSetPlace Mainplayer Room5_1".
		if (instruction.args.size() < 2)
			return false;
		_pendingRoomName = instruction.args[1];
		_pendingRoomCutscene.clear();
		return true;
	}

	// Scene-construction/controller boundaries used by later main places. The
	// decoded room/cutscene assets are already loaded independently by this
	// engine, so these can advance the retail startup script without inventing
	// state we do not render yet. Clone placement is deliberately left as a
	// boundary until dynamic scene instances are represented natively.
	if (op.equalsIgnoreCase("GiveLifeToChar") ||
	    op.equalsIgnoreCase("Setp") ||
	    op.equalsIgnoreCase("setpos_x") ||
	    op.equalsIgnoreCase("setpos_y") ||
	    op.equalsIgnoreCase("setpos_z") ||
	    op.equalsIgnoreCase("CloneEntity") ||
	    op.equalsIgnoreCase("SetEntityPos_Vector") ||
	    op.equalsIgnoreCase("InsertInBackground") ||
	    op.equalsIgnoreCase("swap_entity_pos_byindex") ||
	    op.equalsIgnoreCase("SetFocus") ||
	    op.equalsIgnoreCase("portals_off") ||
	    op.equalsIgnoreCase("portals_on") ||
	    op.equalsIgnoreCase("dcue_all"))
		return true;

	if (op.equalsIgnoreCase("say")) {
		if (!instruction.args.empty()) {
			_pendingSaySpeaker = "Giovanni";
			_pendingSayText = instruction.args[0];
		}
		return true;
	}

	if (op.equalsIgnoreCase("start_dialog")) {
		if (instruction.args.size() < 2)
			return false;
		_pendingDialogName = instruction.args[1];
		return true;
	}

	if (op.equalsIgnoreCase("wait_last_dialog"))
		return true;

	if (op.equalsIgnoreCase("take")) {
		if (instruction.args.size() < 2)
			return false;
		const Common::String &inventoryObject = instruction.args[1];
		if (!containsIgnoreCase(_inventoryObjects, inventoryObject))
			_inventoryObjects.push_back(inventoryObject);
		_selectedInventoryObject = inventoryObject;
		debug(1, "Zero Comico: inventory acquired %s", inventoryObject.c_str());
		return true;
	}

	if (op.equalsIgnoreCase("wait_take"))
		return true;

	if (op.equalsIgnoreCase("subobjininv")) {
		if (instruction.args.size() < 2)
			return false;
		const Common::String &inventoryObject = instruction.args[1];
		removeIgnoreCase(_inventoryObjects, inventoryObject);
		if (_selectedInventoryObject.equalsIgnoreCase(inventoryObject))
			_selectedInventoryObject.clear();
		debug(1, "Zero Comico: inventory removed %s", inventoryObject.c_str());
		return true;
	}

	if (op.equalsIgnoreCase("addobjininv")) {
		if (instruction.args.size() < 2)
			return false;
		const Common::String &inventoryObject = instruction.args[1];
		if (!containsIgnoreCase(_inventoryObjects, inventoryObject))
			_inventoryObjects.push_back(inventoryObject);
		debug(1, "Zero Comico: inventory added %s", inventoryObject.c_str());
		return true;
	}

	if (op.equalsIgnoreCase("SelectObjInInv")) {
		if (instruction.args.size() < 2)
			return false;
		const Common::String &inventoryObject = instruction.args[1];
		if (!containsIgnoreCase(_inventoryObjects, inventoryObject))
			return false;
		_selectedInventoryObject = inventoryObject;
		return true;
	}

	if (op.equalsIgnoreCase("UpdateInventory") ||
	    op.equalsIgnoreCase("ShutupAll"))
		return true;

	if (op.equalsIgnoreCase("hide") || op.equalsIgnoreCase("unhide")) {
		if (instruction.args.empty())
			return false;
		const Common::String entity = resolveSceneEntity(
			_activeScene, instruction.args[0], _activeRoomPrefix);
		if (op.equalsIgnoreCase("hide")) {
			if (!containsIgnoreCase(_hiddenSceneMeshes, entity))
				_hiddenSceneMeshes.push_back(entity);
		} else {
			removeIgnoreCase(_hiddenSceneMeshes, entity);
		}
		return true;
	}

	if (op.equalsIgnoreCase("setobj")) {
		if (instruction.args.size() < 2)
			return false;
		PuzzleObject *object = _activePuzzle.findObject(instruction.args[0]);
		if (!object)
			return false;

		Common::String state = instruction.args[1];
		state.toLowercase();
		if (state.find("examinable true") != Common::String::npos)
			object->examinable = true;
		if (state.find("examinable false") != Common::String::npos)
			object->examinable = false;
		if (state.find("enabled true") != Common::String::npos)
			object->enabled = true;
		if (state.find("enabled false") != Common::String::npos)
			object->enabled = false;
		return true;
	}

	if (op.equalsIgnoreCase("play") ||
	    op.equalsIgnoreCase("wait_say") ||
	    op.equalsIgnoreCase("SetDialogCameras") ||
	    op.equalsIgnoreCase("e3d_hide") ||
	    op.equalsIgnoreCase("e3d_unhide") ||
	    op.equalsIgnoreCase("BreakLifeToChar"))
		return true;

	if (op.equalsIgnoreCase("e3d_Parse") || op.equalsIgnoreCase("setcameramode"))
		return true;

	if (op.equalsIgnoreCase("csay")) {
		if (instruction.args.size() >= 2) {
			_pendingSaySpeaker = instruction.args[0];
			_pendingSayText = instruction.args[1];
			debug(1, "Zero Comico: %s says: %s",
			      _pendingSaySpeaker.c_str(), _pendingSayText.c_str());
		}
		return true;
	}

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

bool ZeroComicoEngine::evaluateScriptCondition(const ScriptInstruction &instruction,
                                                    bool &result) const {
	if (instruction.opcode.equalsIgnoreCase("ifallobjnoselected")) {
		result = _selectedInventoryObject.empty();
		return true;
	}

	if (instruction.opcode.equalsIgnoreCase("ifobjselected")) {
		if (instruction.args.size() < 2)
			return false;
		result = _selectedInventoryObject.equalsIgnoreCase(instruction.args[1]);
		return true;
	}

	if (instruction.opcode.equalsIgnoreCase("ifcombine")) {
		if (instruction.args.size() < 2)
			return false;
		const bool direct =
			_combineInventoryFirst.equalsIgnoreCase(instruction.args[0]) &&
			_combineInventorySecond.equalsIgnoreCase(instruction.args[1]);
		const bool reverse =
			_combineInventoryFirst.equalsIgnoreCase(instruction.args[1]) &&
			_combineInventorySecond.equalsIgnoreCase(instruction.args[0]);
		result = direct || reverse;
		return true;
	}

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

bool ZeroComicoEngine::renderMenuFrame(int selection) {
	if (selection < 0 || selection >= kMenuButtonCount)
		return false;

	Common::Array<Common::String> visible;
	visible.push_back("int_iface");
	visible.push_back("int_vetro");
	visible.push_back("int_main");
	visible.push_back("int_sarac_des");
	visible.push_back("int_sarac_sin");

	for (int i = 0; i < kMenuButtonCount; ++i) {
		Common::String meshName("int_");
		meshName += (i == selection) ? "a_" : "s_";
		meshName += kMenuButtons[i];
		visible.push_back(meshName);
	}

	SoftwareRenderer renderer;
	Graphics::ManagedSurface frame;
	if (!renderer.render(_menuScene, "int_Camera01",
	                     Common::Path("Mpx/bodies/interfaccia"), visible, frame, 800, 600)) {
		warning("Zero Comico: could not render the decoded 3D menu scene");
		return false;
	}

	_system->copyRectToScreen(frame.getPixels(), frame.pitch, 0, 0, frame.w, frame.h);
	_system->updateScreen();
	return true;
}

bool ZeroComicoEngine::renderGameplayFrame(const RenderCamera &camera,
                                                const Common::Path &sceneDirectory,
                                                const Common::Path &playerDirectory,
                                                const Common::String &animationSource,
                                                float animationFrame,
                                                Graphics::ManagedSurface &frame) {
	const uint32 now = _system->getMillis();
	for (uint32 loopIndex = 0; loopIndex < _sceneLoopTargets.size(); ++loopIndex) {
		const NamedAnimationClip *clip = _activeScene.findClipBySource(
			_sceneLoopTargets[loopIndex], _sceneLoopSources[loopIndex]);
		if (!clip)
			continue;

		const float firstFrame = (float)clip->data.startFrame;
		const float lastFrame = (float)clip->data.endFrame;
		const float frameCount = lastFrame >= firstFrame
			? lastFrame - firstFrame + 1.0f : 1.0f;
		const float elapsedFrames =
			(float)(now - _sceneLoopStartMillis[loopIndex]) * 25.0f / 1000.0f;
		const float loopFrame = firstFrame + std::fmod(elapsedFrames, frameCount);
		_activeScene.poseRigidAnimation(_sceneLoopTargets[loopIndex],
		                                _sceneLoopSources[loopIndex], loopFrame);
	}

	Common::Array<Common::String> visibleMeshes;
	if (!_hiddenSceneMeshes.empty()) {
		for (uint32 meshIndex = 0; meshIndex < _activeScene.meshes.size(); ++meshIndex) {
			const NamedMesh &mesh = _activeScene.meshes[meshIndex];
			if (mesh.data.isFlesh() || containsIgnoreCase(_hiddenSceneMeshes, mesh.name))
				continue;
			visibleMeshes.push_back(mesh.name);
		}
		if (visibleMeshes.empty())
			visibleMeshes.push_back("__zerocomico_no_visible_room_meshes__");
	}
	bool rendered = false;

	if (!_playerScene.meshes.empty() && _havePlayerStart) {
		const Common::String playerRoot = !_playerSequences.bodyName.empty()
			? _playerSequences.bodyName : _playerCharacterScript.initialBodyName;
		if (!_playerScene.poseSkinnedGeometry(playerRoot, "Stay", animationSource, animationFrame))
			warning("Zero Comico: could not evaluate player skeletal pose %s at %.2f",
			        animationSource.c_str(), animationFrame);

		Common::Array<Common::String> playerVisible;
		for (uint32 meshIndex = 0; meshIndex < _playerScene.meshes.size(); ++meshIndex) {
			const NamedMesh &mesh = _playerScene.meshes[meshIndex];
			if (mesh.data.isFlesh())
				continue;
			if (mesh.data.isSkinnedParent()) {
				playerVisible.push_back(mesh.name);
				continue;
			}

			Common::String lowerName = mesh.name;
			lowerName.toLowercase();
			if (lowerName.hasSuffix("01") || lowerName.hasSuffix("02"))
				continue;

			const bool isHead = lowerName.find("testa") != Common::String::npos;
			const bool isHat = lowerName.find("cappello") != Common::String::npos;
			if (isHead || (isHat && _playerHatVisible))
				playerVisible.push_back(mesh.name);
		}

		RenderTransform playerTransform;
		playerTransform.translation = _playerPosition;
		const float faceX = _playerFacingTarget.x - _playerPosition.x;
		const float faceZ = _playerFacingTarget.z - _playerPosition.z;
		playerTransform.yawRadians = std::atan2(faceX, faceZ);
		if (!sampleRootTransform(_playerScene, playerRoot, animationSource, animationFrame, playerTransform))
			warning("Zero Comico: player %s root transform missing; using identity root pose",
			        animationSource.c_str());

		rendered = _gameplayRenderer.renderWithActor(_activeScene, camera, sceneDirectory, visibleMeshes,
		                                    _playerScene, playerDirectory, playerVisible,
		                                    playerTransform, frame, 800, 600);
	} else {
		rendered = _gameplayRenderer.render(_activeScene, camera, sceneDirectory, visibleMeshes,
		                           frame, 800, 600);
	}

	if (!rendered)
		return false;

	drawInventoryOverlay(frame, _inventoryObjects, _selectedInventoryObject,
	                     _combineInventoryFirst);
	_system->copyRectToScreen(frame.getPixels(), frame.pitch, 0, 0, frame.w, frame.h);
	_system->updateScreen();
	return true;
}

bool ZeroComicoEngine::playDialogue(const Common::String &name,
                                      const RenderCamera &camera,
                                      const Common::Path &sceneDirectory,
                                      const Common::Path &playerDirectory,
                                      Graphics::ManagedSurface &frame,
                                      uint32 depth) {
	if (depth > 8)
		return false;

	const DialogDefinition *dialog = _activeDialog.findDialog(name);
	if (!dialog) {
		warning("Zero Comico: dialogue %s is not declared", name.c_str());
		return false;
	}

	for (uint32 lineIndex = 0; lineIndex < dialog->lines.size() && !shouldQuit(); ++lineIndex) {
		const DialogLine &line = dialog->lines[lineIndex];
		const DialogSpeaker *speaker = _activeDialog.findSpeakerByKey(line.speakerKey);
		const Common::String speakerName = speaker ? speaker->name : line.speakerKey;

		if (!renderGameplayFrame(camera, sceneDirectory, playerDirectory, "Stay", 0.0f, frame))
			return false;
		drawCutsceneSubtitle(frame, speakerName, line.text);
		_system->copyRectToScreen(frame.getPixels(), frame.pitch, 0, 0, frame.w, frame.h);
		_system->updateScreen();

		uint32 duration = (uint32)line.text.size() * 65U;
		if (duration < 1200U)
			duration = 1200U;
		if (duration > 8000U)
			duration = 8000U;

		const uint32 started = _system->getMillis();
		bool advance = false;
		while (!shouldQuit() && !advance && _system->getMillis() - started < duration) {
			Common::Event event;
			while (_system->getEventManager()->pollEvent(event)) {
				if (event.type == Common::EVENT_QUIT ||
				    event.type == Common::EVENT_RETURN_TO_LAUNCHER) {
					quitGame();
					break;
				}
				if (event.type == Common::EVENT_KEYDOWN ||
				    event.type == Common::EVENT_LBUTTONDOWN ||
				    event.type == Common::EVENT_RBUTTONDOWN) {
					advance = true;
					break;
				}
			}
			_system->delayMillis(10);
		}
	}

	if (shouldQuit() || dialog->choices.empty())
		return !shouldQuit();

	uint32 selected = 0;
	bool chosen = false;
	while (!shouldQuit() && !chosen) {
		if (!renderGameplayFrame(camera, sceneDirectory, playerDirectory, "Stay", 0.0f, frame))
			return false;
		drawDialogueChoices(frame, dialog->choices, selected);
		_system->copyRectToScreen(frame.getPixels(), frame.pitch, 0, 0, frame.w, frame.h);
		_system->updateScreen();

		Common::Event event;
		while (_system->getEventManager()->pollEvent(event)) {
			if (event.type == Common::EVENT_QUIT ||
			    event.type == Common::EVENT_RETURN_TO_LAUNCHER) {
				quitGame();
				break;
			}
			if (event.type != Common::EVENT_KEYDOWN)
				continue;

			if (event.kbd.keycode == Common::KEYCODE_UP) {
				selected = (selected + dialog->choices.size() - 1) % dialog->choices.size();
				break;
			}
			if (event.kbd.keycode == Common::KEYCODE_DOWN) {
				selected = (selected + 1) % dialog->choices.size();
				break;
			}
			if (event.kbd.keycode >= Common::KEYCODE_1 &&
			    event.kbd.keycode <= Common::KEYCODE_9) {
				const uint32 direct = (uint32)(event.kbd.keycode - Common::KEYCODE_1);
				if (direct < dialog->choices.size()) {
					selected = direct;
					chosen = true;
				}
				break;
			}
			if (event.kbd.keycode == Common::KEYCODE_RETURN ||
			    event.kbd.keycode == Common::KEYCODE_KP_ENTER ||
			    event.kbd.keycode == Common::KEYCODE_SPACE) {
				chosen = true;
				break;
			}
			if (event.kbd.keycode == Common::KEYCODE_ESCAPE)
				return true;
		}
		_system->delayMillis(10);
	}

	if (shouldQuit())
		return false;
	return playDialogue(dialog->choices[selected].targetDialog, camera,
	                    sceneDirectory, playerDirectory, frame, depth + 1);
}

bool ZeroComicoEngine::runMainPlacePreview(const Common::String &mainPlace) {
	if (mainPlace.empty())
		return false;

	Common::String level = mainPlace;
	if (level.size() >= 2 &&
	    (level[0] == 'm' || level[0] == 'M') &&
	    (level[1] == 'p' || level[1] == 'P'))
		level = Common::String("Mp") + level.substr(2);

	_currentMainPlace = level;
	_playerHatVisible = true;
	_pendingSaySpeaker.clear();
	_pendingSayText.clear();
	_pendingDialogName.clear();
	_pendingRoomName.clear();
	_pendingRoomCutscene.clear();
	_selectedInventoryObject.clear();
	_combineInventoryFirst.clear();
	_combineInventorySecond.clear();
	_inventoryObjects.clear();
	_hiddenSceneMeshes.clear();
	_sceneLoopTargets.clear();
	_sceneLoopSources.clear();
	_sceneLoopStartMillis.clear();

	const Common::Path roomScript(level + "/gameplay/room.isc");
	ScriptProgram roomProgram;
	if (!roomProgram.load(roomScript)) {
		warning("Zero Comico: cannot parse main-place room script %s", roomScript.toString().c_str());
		return false;
	}

	ChapterDefinition chapter;
	if (!chapter.parse(roomProgram)) {
		warning("Zero Comico: cannot decode main-place room definitions %s", roomScript.toString().c_str());
		return false;
	}

	// Initialize the retail main-place variables before entering the room. Mp1's
	// startup block is intentionally side-effect free beyond scalar declarations,
	// so executing it now gives later puzzle/character scripts the same base state.
	uint32 startupStart = roomProgram.instructions().size();
	uint32 startupEnd = roomProgram.instructions().size();
	for (uint32 i = 0; i < roomProgram.instructions().size(); ++i) {
		if (!roomProgram.instructions()[i].opcode.equalsIgnoreCase("startup"))
			continue;
		startupStart = i + 1;
		for (uint32 j = startupStart; j < roomProgram.instructions().size(); ++j) {
			if (roomProgram.instructions()[j].opcode.equalsIgnoreCase("end")) {
				startupEnd = j;
				break;
			}
		}
		break;
	}
	if (startupStart < startupEnd) {
		_scriptVM.reset();
		if (!_scriptVM.run(roomProgram, startupStart, startupEnd, 4096)) {
			warning("Zero Comico: failed to execute %s startup state", level.c_str());
			return false;
		}
	}

	const RoomDefinition *room = chapter.findRoom(chapter.startRoom);
	if (!room) {
		warning("Zero Comico: start room %s is not declared", chapter.startRoom.c_str());
		return false;
	}

	_activeRoomPrefix = room->prefix;

	_havePlayerStart = false;
	_activeShapes = ShapeScript();
	const Common::Path shapePath(level + "/gameplay/Shape.shp");
	if (_activeShapes.load(shapePath)) {
		const ShapeMarker *startMarker = _activeShapes.find(chapter.startMarker);
		if (startMarker) {
			_playerPosition = startMarker->a;
			_playerFacingTarget = startMarker->b;
			_havePlayerStart = true;
			debug(1, "Zero Comico: player marker %s at %.3f %.3f %.3f facing %.3f %.3f %.3f",
			      chapter.startMarker.c_str(), _playerPosition.x, _playerPosition.y, _playerPosition.z,
			      _playerFacingTarget.x, _playerFacingTarget.y, _playerFacingTarget.z);
		} else {
			warning("Zero Comico: start marker %s is missing from %s",
			        chapter.startMarker.c_str(), shapePath.toString().c_str());
		}
	} else {
		warning("Zero Comico: cannot parse gameplay markers %s", shapePath.toString().c_str());
	}

	// Retail filenames use lower-case room stems even though room declarations
	// are often capitalized. Keep the logical room name untouched and only
	// normalize the filesystem stem.
	Common::String roomStem = room->name;
	roomStem.toLowercase();

	const Common::Path sceneDirectory(level + "/backgrd");
	const Common::Path p3dPath = sceneDirectory.appendComponent(roomStem + ".p3d");
	const Common::Path anjPath = sceneDirectory.appendComponent(roomStem + ".anj");

	_activeScene.clear();
	if (!_activeScene.loadPair(p3dPath, anjPath)) {
		warning("Zero Comico: cannot decode start-room scene %s", roomStem.c_str());
		return false;
	}

	const Common::Path characterPath(level + "/gameplay/char.isc");
	Common::String playerAssetStem("Giovanni");
	if (!_playerCharacterScript.load(characterPath)) {
		warning("Zero Comico: cannot parse playable character metadata %s",
		        characterPath.toString().c_str());
	} else {
		const uint32 separator = _playerCharacterScript.initialBodyName.find('_');
		if (separator != Common::String::npos &&
		    separator + 1 < _playerCharacterScript.initialBodyName.size())
			playerAssetStem = _playerCharacterScript.initialBodyName.substr(separator + 1);

		debug(1, "Zero Comico: main player %s uses %s (%s), combine block=%s",
		      _playerCharacterScript.playerName.c_str(),
		      _playerCharacterScript.initialBodyName.c_str(), playerAssetStem.c_str(),
		      _playerCharacterScript.hasCombineBlock() ? "yes" : "no");
	}

	Common::Path playerDirectory =
		Common::Path("Mpx/bodies").appendComponent(playerAssetStem);
	_playerScene.clear();
	bool havePlayerScene = _playerScene.loadPair(
		playerDirectory.appendComponent(playerAssetStem + ".p3d"),
		playerDirectory.appendComponent(playerAssetStem + ".anj"));

	// The retail tree mixes Giovanni with title-case directory spelling and
	// aldo/giacomo with lower-case folders. Retry the folder only, keeping the
	// actual asset filename stem from the character script.
	if (!havePlayerScene) {
		Common::String lowerFolder = playerAssetStem;
		lowerFolder.toLowercase();
		playerDirectory = Common::Path("Mpx/bodies").appendComponent(lowerFolder);
		havePlayerScene = _playerScene.loadPair(
			playerDirectory.appendComponent(playerAssetStem + ".p3d"),
			playerDirectory.appendComponent(playerAssetStem + ".anj"));
	}

	if (!havePlayerScene)
		warning("Zero Comico: cannot decode main-player P3D/ANJ scene %s",
		        playerAssetStem.c_str());
	else
		debug(1, "Zero Comico: player %s decoded: %u materials, %u meshes, %u clips",
		      playerAssetStem.c_str(), (uint)_playerScene.materials.size(),
		      (uint)_playerScene.meshes.size(), (uint)_playerScene.clips.size());

	if (!_playerSequences.load(playerDirectory.appendComponent(playerAssetStem + ".seq"))) {
		warning("Zero Comico: cannot parse player sequence file %s.seq",
		        playerAssetStem.c_str());
	} else {
		const AnimationSequence *walkSequence = _playerSequences.findSequence("cammina");
		const AnimationSequence *runSequence = _playerSequences.findSequence("corsa");
		debug(1, "Zero Comico: JACS body %s exposes %u sequences (walk=%s, run=%s)",
		      _playerSequences.bodyName.c_str(), (uint)_playerSequences.sequences.size(),
		      walkSequence ? "yes" : "no", runSequence ? "yes" : "no");
	}

	const Common::Path puzzlePath(level + "/gameplay/puzzle.isc");
	if (!_activePuzzle.load(puzzlePath))
		warning("Zero Comico: cannot parse puzzle objects %s", puzzlePath.toString().c_str());
	else
		debug(1, "Zero Comico: loaded %u puzzle objects", (uint)_activePuzzle.objects.size());

	const Common::Path dialogPath(level + "/gameplay/dialog.isc");
	if (!_activeDialog.load(dialogPath))
		warning("Zero Comico: cannot parse dialogue file %s", dialogPath.toString().c_str());
	else
		debug(1, "Zero Comico: loaded %u dialogues and %u speakers",
		      (uint)_activeDialog.dialogs.size(), (uint)_activeDialog.speakers.size());

	// Load both navigation layers declared by room.isc. The ordinary map
	// carries the walkable floor/path graph; cameramap is the camera-control
	// partition used by the original runtime.
	_activeWalkMap = BspMap();
	_activeCameraMap = BspMap();
	if (!room->maps.empty()) {
		const Common::Path mapPath = Common::Path(level + "/gameplay").appendComponent(room->maps[0]);
		if (!_activeWalkMap.load(mapPath))
			warning("Zero Comico: cannot load walk map %s", mapPath.toString().c_str());
	}
	if (!room->cameraMaps.empty()) {
		const Common::Path cameraMapPath = Common::Path(level + "/gameplay").appendComponent(room->cameraMaps[0]);
		if (!_activeCameraMap.load(cameraMapPath))
			warning("Zero Comico: cannot load camera map %s", cameraMapPath.toString().c_str());
	}

	_playerNavNode = -1;
	if (_havePlayerStart && !_activeWalkMap.graph.empty()) {
		_playerNavNode = _activeWalkMap.nearestGraphNode(_playerPosition.x, _playerPosition.z);
		if (_playerNavNode >= 0)
			debug(1, "Zero Comico: player start mapped to navigation node %d", _playerNavNode);
	}

	Common::String cameraName = room->camera;
	RenderCamera renderCamera;
	bool haveRenderCamera = false;

	// Gameplay rooms do not use the editor camera embedded in room*.p3d.
	// room.isc points at an alias whose source/target/FOV live in Camera.scr.
	// Using that script camera fixes the start-room viewpoint instead of
	// falling back to the unrelated exported editor camera.
	CameraScript cameraScript;
	const Common::Path cameraScriptPath(level + "/gameplay/Camera.scr");
	if (cameraScript.load(cameraScriptPath)) {
		const ScriptCamera *scriptCamera = cameraScript.findCamera(cameraName);
		if (scriptCamera) {
			const float radians = scriptCamera->horizontalFovDegrees * 3.14159265358979323846f / 180.0f;
			const float halfTan = std::tan(radians * 0.5f);
			if (halfTan > 0.0001f) {
				renderCamera.position = scriptCamera->source;
				renderCamera.target = scriptCamera->target;
				renderCamera.focalPixels = 400.0f / halfTan;
				haveRenderCamera = true;
			}
		}
	}

	if (!haveRenderCamera) {
		const NamedCamera *embedded = _activeScene.findCamera(cameraName);
		if (!embedded && !_activeScene.cameras.empty())
			embedded = &_activeScene.cameras[0];
		if (!embedded || embedded->data.fov <= 0.0f) {
			warning("Zero Comico: start-room scene has no usable camera");
			return false;
		}
		cameraName = embedded->name;
		renderCamera.position = embedded->data.position;
		renderCamera.target = embedded->data.target;
		renderCamera.focalPixels = embedded->data.fov * 800.0f / 36.0f;
	}

	if (!runMainPlaceRuntime(roomProgram))
		warning("Zero Comico: main-place runtime block did not complete cleanly");
	if (!_pendingMainPlace.empty())
		return true;

	Graphics::ManagedSurface frame;
	if (!renderGameplayFrame(renderCamera, sceneDirectory, playerDirectory, "Stay", 0.0f, frame)) {
		warning("Zero Comico: could not render start room %s", room->name.c_str());
		return false;
	}

	// csay is synchronous in the retail room runtime. The runtime stores the
	// line while the cutscene owns the screen; once C111 returns, show it over
	// the first gameplay frame before handing control to the player.
	if (!_pendingSayText.empty()) {
		drawCutsceneSubtitle(frame, _pendingSaySpeaker, _pendingSayText);
		_system->copyRectToScreen(frame.getPixels(), frame.pitch, 0, 0, frame.w, frame.h);
		_system->updateScreen();

		uint32 sayDuration = (uint32)_pendingSayText.size() * 70U;
		if (sayDuration < 1200U)
			sayDuration = 1200U;
		if (sayDuration > 5000U)
			sayDuration = 5000U;

		const uint32 sayStart = _system->getMillis();
		bool dismissSay = false;
		while (!shouldQuit() && !dismissSay &&
		       _system->getMillis() - sayStart < sayDuration) {
			Common::Event sayEvent;
			while (_system->getEventManager()->pollEvent(sayEvent)) {
				if (sayEvent.type == Common::EVENT_QUIT ||
				    sayEvent.type == Common::EVENT_RETURN_TO_LAUNCHER) {
					quitGame();
					dismissSay = true;
					break;
				}
				if (sayEvent.type == Common::EVENT_KEYDOWN ||
				    sayEvent.type == Common::EVENT_LBUTTONDOWN) {
					dismissSay = true;
					break;
				}
			}
			_system->delayMillis(10);
		}

		_pendingSaySpeaker.clear();
		_pendingSayText.clear();
		if (!shouldQuit())
			renderGameplayFrame(renderCamera, sceneDirectory, playerDirectory, "Stay", 0.0f, frame);
	}

	debug(1, "Zero Comico: main place %s start room %s marker %s, camera %s, %u meshes, %u nav nodes%s",
	      level.c_str(), room->name.c_str(), chapter.startMarker.c_str(),
	      cameraName.c_str(), (uint)_activeScene.meshes.size(), (uint)_activeWalkMap.graph.size(),
	      _havePlayerStart ? ", player start resolved" : ", player start unresolved");
	if (_playerNavNode >= 0)
		debug(1, "Zero Comico: navigation runtime ready at node %d", _playerNavNode);

	// Gameplay prototype: clicks are projected onto the floor plane, snapped to
	// the retail BSP graph and routed with Dijkstra. The character traverses
	// that route continuously while the 0>1, 1>1 and 1>0 JACS sequence states
	// drive start, looping walk and stop animation clips. Idle rendering keeps
	// the Stay pose and playl room-object loops alive at 25 fps.
	Common::Array<Common::String> examineMeshes;
	Common::Array<Common::String> operateMeshes;
	for (uint32 objectIndex = 0; objectIndex < _activePuzzle.objects.size(); ++objectIndex) {
		const PuzzleObject &object = _activePuzzle.objects[objectIndex];
		if (!object.enabled || object.entity.empty())
			continue;
		const Common::String sceneEntity =
			resolveSceneEntity(_activeScene, object.entity, _activeRoomPrefix);
		if (!_activeScene.findMesh(sceneEntity) ||
		    containsIgnoreCase(_hiddenSceneMeshes, sceneEntity))
			continue;
		if (object.examinable)
			examineMeshes.push_back(sceneEntity);
		if (object.operateStart != 0xffffffffU && object.operateEnd != 0xffffffffU &&
		    object.operateStart < object.operateEnd)
			operateMeshes.push_back(sceneEntity);
	}

	bool done = false;
	uint32 lastIdleRender = _system->getMillis();
	const uint32 idleAnimationStart = lastIdleRender;
	while (!shouldQuit() && !done) {
		Common::Event event;
		while (_system->getEventManager()->pollEvent(event)) {
			if (event.type == Common::EVENT_QUIT || event.type == Common::EVENT_RETURN_TO_LAUNCHER) {
				quitGame();
				break;
			}
			if (event.type == Common::EVENT_KEYDOWN && event.kbd.keycode == Common::KEYCODE_ESCAPE) {
				done = true;
				break;
			}
			if (event.type == Common::EVENT_KEYDOWN && event.kbd.keycode == Common::KEYCODE_TAB) {
				if (!_inventoryObjects.empty()) {
					int selectedIndex = -1;
					for (uint32 i = 0; i < _inventoryObjects.size(); ++i) {
						if (_inventoryObjects[i].equalsIgnoreCase(_selectedInventoryObject)) {
							selectedIndex = (int)i;
							break;
						}
					}
					selectedIndex = (selectedIndex + 1) % (int)_inventoryObjects.size();
					_selectedInventoryObject = _inventoryObjects[(uint32)selectedIndex];
					debug(1, "Zero Comico: inventory selected %s", _selectedInventoryObject.c_str());
					renderGameplayFrame(renderCamera, sceneDirectory, playerDirectory,
					                    "Stay", 0.0f, frame);
				}
				continue;
			}
			if (event.type == Common::EVENT_KEYDOWN && event.kbd.keycode == Common::KEYCODE_BACKSPACE) {
				_selectedInventoryObject.clear();
				_combineInventoryFirst.clear();
				_combineInventorySecond.clear();
				renderGameplayFrame(renderCamera, sceneDirectory, playerDirectory,
				                    "Stay", 0.0f, frame);
				continue;
			}
			if (event.type == Common::EVENT_KEYDOWN && event.kbd.keycode == Common::KEYCODE_c) {
				if (_selectedInventoryObject.empty() || !_playerCharacterScript.hasCombineBlock())
					continue;

				if (_combineInventoryFirst.empty()) {
					_combineInventoryFirst = _selectedInventoryObject;
					_combineInventorySecond.clear();
					debug(1, "Zero Comico: inventory combine armed with %s",
					      _combineInventoryFirst.c_str());
					renderGameplayFrame(renderCamera, sceneDirectory, playerDirectory,
					                    "Stay", 0.0f, frame);
					continue;
				}

				if (_combineInventoryFirst.equalsIgnoreCase(_selectedInventoryObject)) {
					_combineInventoryFirst.clear();
					_combineInventorySecond.clear();
					renderGameplayFrame(renderCamera, sceneDirectory, playerDirectory,
					                    "Stay", 0.0f, frame);
					continue;
				}

				_combineInventorySecond = _selectedInventoryObject;
				_pendingSaySpeaker.clear();
				_pendingSayText.clear();
				debug(1, "Zero Comico: trying inventory combine %s + %s",
				      _combineInventoryFirst.c_str(), _combineInventorySecond.c_str());

				if (!_scriptVM.run(_playerCharacterScript.program(),
				                   _playerCharacterScript.combineStart,
				                   _playerCharacterScript.combineEnd, 4096)) {
					warning("Zero Comico: inventory combine block stopped on an unsupported opcode");
				}

				_combineInventoryFirst.clear();
				_combineInventorySecond.clear();
				renderGameplayFrame(renderCamera, sceneDirectory, playerDirectory,
				                    "Stay", 0.0f, frame);

				if (!_pendingSayText.empty() && !shouldQuit()) {
					drawCutsceneSubtitle(frame,
					                     _pendingSaySpeaker.empty() ? Common::String("Giovanni") : _pendingSaySpeaker,
					                     _pendingSayText);
					_system->copyRectToScreen(frame.getPixels(), frame.pitch,
					                          0, 0, frame.w, frame.h);
					_system->updateScreen();

					uint32 sayDuration = (uint32)_pendingSayText.size() * 60U;
					if (sayDuration < 1000U)
						sayDuration = 1000U;
					if (sayDuration > 5000U)
						sayDuration = 5000U;

					const uint32 sayStart = _system->getMillis();
					bool dismissSay = false;
					while (!shouldQuit() && !dismissSay &&
					       _system->getMillis() - sayStart < sayDuration) {
						Common::Event sayEvent;
						while (_system->getEventManager()->pollEvent(sayEvent)) {
							if (sayEvent.type == Common::EVENT_QUIT ||
							    sayEvent.type == Common::EVENT_RETURN_TO_LAUNCHER) {
								quitGame();
								dismissSay = true;
								break;
							}
							if (sayEvent.type == Common::EVENT_KEYDOWN ||
							    sayEvent.type == Common::EVENT_LBUTTONDOWN ||
							    sayEvent.type == Common::EVENT_RBUTTONDOWN) {
								dismissSay = true;
								break;
							}
						}
						_system->delayMillis(10);
					}
					_pendingSaySpeaker.clear();
					_pendingSayText.clear();
					if (!shouldQuit())
						renderGameplayFrame(renderCamera, sceneDirectory, playerDirectory,
						                    "Stay", 0.0f, frame);
				}
				continue;
			}
			if (event.type == Common::EVENT_RBUTTONDOWN) {
				Common::String pickedEntity;
				if (_gameplayRenderer.pickMesh(_activeScene, renderCamera,
				                               event.mouse.x, event.mouse.y,
				                               examineMeshes, pickedEntity)) {
					const PuzzleObject *object = findPuzzleObjectForMesh(
						_activePuzzle, _activeScene, _activeRoomPrefix, pickedEntity);
					if (object && !object->examineText.empty()) {
						drawCutsceneSubtitle(frame, "Giovanni", object->examineText);
						_system->copyRectToScreen(frame.getPixels(), frame.pitch,
						                          0, 0, frame.w, frame.h);
						_system->updateScreen();

						uint32 examineDuration = (uint32)object->examineText.size() * 55U;
						if (examineDuration < 1200U)
							examineDuration = 1200U;
						if (examineDuration > 5000U)
							examineDuration = 5000U;

						const uint32 examineStart = _system->getMillis();
						bool dismissExamine = false;
						while (!shouldQuit() && !dismissExamine &&
						       _system->getMillis() - examineStart < examineDuration) {
							Common::Event examineEvent;
							while (_system->getEventManager()->pollEvent(examineEvent)) {
								if (examineEvent.type == Common::EVENT_QUIT ||
								    examineEvent.type == Common::EVENT_RETURN_TO_LAUNCHER) {
									quitGame();
									dismissExamine = true;
									break;
								}
								if (examineEvent.type == Common::EVENT_KEYDOWN ||
								    examineEvent.type == Common::EVENT_LBUTTONDOWN ||
								    examineEvent.type == Common::EVENT_RBUTTONDOWN) {
									dismissExamine = true;
									break;
								}
							}
							_system->delayMillis(10);
						}

						if (!shouldQuit()) {
							float stayStart = 0.0f;
							float stayEnd = 0.0f;
							animationClipRange(_playerScene, _playerSequences.bodyName, "Stay",
							                   stayStart, stayEnd);
							renderGameplayFrame(renderCamera, sceneDirectory, playerDirectory,
							                    "Stay", stayStart, frame);
						}
					}
				}
				continue;
			}

			if (event.type != Common::EVENT_LBUTTONDOWN || _playerNavNode < 0)
				continue;

			Common::String operatedEntity;
			if (_gameplayRenderer.pickMesh(_activeScene, renderCamera,
			                               event.mouse.x, event.mouse.y,
			                               operateMeshes, operatedEntity)) {
				const PuzzleObject *object = findPuzzleObjectForMesh(
					_activePuzzle, _activeScene, _activeRoomPrefix, operatedEntity);
				if (object && object->operateStart < object->operateEnd) {
					_pendingRoomName.clear();
					_pendingRoomCutscene.clear();
					_pendingSaySpeaker.clear();
					_pendingSayText.clear();
					_pendingDialogName.clear();

					if (!_scriptVM.run(_activePuzzle.program(), object->operateStart,
					                   object->operateEnd, 4096)) {
						warning("Zero Comico: object operation %s stopped on an unsupported opcode",
						        object->name.c_str());
					}

					if (!_pendingMainPlace.empty()) {
						done = true;
						break;
					}

					examineMeshes.clear();
					operateMeshes.clear();
					for (uint32 objectIndex = 0; objectIndex < _activePuzzle.objects.size(); ++objectIndex) {
						const PuzzleObject &updatedObject = _activePuzzle.objects[objectIndex];
						if (!updatedObject.enabled || updatedObject.entity.empty())
							continue;
						const Common::String updatedEntity = resolveSceneEntity(
							_activeScene, updatedObject.entity, _activeRoomPrefix);
						if (!_activeScene.findMesh(updatedEntity) ||
						    containsIgnoreCase(_hiddenSceneMeshes, updatedEntity))
							continue;
						if (updatedObject.examinable)
							examineMeshes.push_back(updatedEntity);
						if (updatedObject.operateStart != 0xffffffffU &&
						    updatedObject.operateEnd != 0xffffffffU &&
						    updatedObject.operateStart < updatedObject.operateEnd)
							operateMeshes.push_back(updatedEntity);
					}

					if (!_pendingDialogName.empty() && !done && !shouldQuit()) {
						playDialogue(_pendingDialogName, renderCamera, sceneDirectory,
						             playerDirectory, frame);
						_pendingDialogName.clear();
					}

					if (!_pendingRoomName.empty()) {
						if (_pendingRoomCutscene.equalsIgnoreCase("d101"))
							playCutscene("d101_dor");

						const RoomDefinition *nextRoom = chapter.findRoom(_pendingRoomName);
						if (nextRoom) {
							room = nextRoom;
							_activeRoomPrefix = room->prefix;
							Common::String nextStem = room->name;
							nextStem.toLowercase();

							_activeScene.clear();
							if (!_activeScene.loadPair(
									sceneDirectory.appendComponent(nextStem + ".p3d"),
									sceneDirectory.appendComponent(nextStem + ".anj"))) {
								warning("Zero Comico: cannot load destination room %s",
								        room->name.c_str());
								done = true;
								break;
							}

							_activeWalkMap = BspMap();
							_activeCameraMap = BspMap();
							if (!room->maps.empty()) {
								const Common::Path nextMap = Common::Path(level + "/gameplay")
									.appendComponent(room->maps[0]);
								if (!_activeWalkMap.load(nextMap))
									warning("Zero Comico: cannot load destination walk map %s",
									        nextMap.toString().c_str());
							}
							if (!room->cameraMaps.empty()) {
								const Common::Path nextCameraMap = Common::Path(level + "/gameplay")
									.appendComponent(room->cameraMaps[0]);
								if (!_activeCameraMap.load(nextCameraMap))
									warning("Zero Comico: cannot load destination camera map %s",
									        nextCameraMap.toString().c_str());
							}

							_playerNavNode = -1;
							if (_havePlayerStart && !_activeWalkMap.graph.empty())
								_playerNavNode = _activeWalkMap.nearestGraphNode(
									_playerPosition.x, _playerPosition.z);

							cameraName = room->camera;
							bool nextCameraReady = false;
							const ScriptCamera *nextScriptCamera = cameraScript.findCamera(cameraName);
							if (nextScriptCamera) {
								const float radians = nextScriptCamera->horizontalFovDegrees *
									3.14159265358979323846f / 180.0f;
								const float halfTan = std::tan(radians * 0.5f);
								if (halfTan > 0.0001f) {
									renderCamera.position = nextScriptCamera->source;
									renderCamera.target = nextScriptCamera->target;
									renderCamera.focalPixels = 400.0f / halfTan;
									nextCameraReady = true;
								}
							}
							if (!nextCameraReady) {
								const NamedCamera *embedded = _activeScene.findCamera(cameraName);
								if (!embedded && !_activeScene.cameras.empty())
									embedded = &_activeScene.cameras[0];
								if (embedded && embedded->data.fov > 0.0f) {
									cameraName = embedded->name;
									renderCamera.position = embedded->data.position;
									renderCamera.target = embedded->data.target;
									renderCamera.focalPixels = embedded->data.fov * 800.0f / 36.0f;
									nextCameraReady = true;
								}
							}
							if (!nextCameraReady) {
								warning("Zero Comico: destination room %s has no usable camera",
								        room->name.c_str());
								done = true;
								break;
							}

							examineMeshes.clear();
							operateMeshes.clear();
							for (uint32 objectIndex = 0; objectIndex < _activePuzzle.objects.size(); ++objectIndex) {
								const PuzzleObject &nextObject = _activePuzzle.objects[objectIndex];
								if (!nextObject.enabled || nextObject.entity.empty())
									continue;
								const Common::String nextEntity = resolveSceneEntity(
									_activeScene, nextObject.entity, _activeRoomPrefix);
								if (!_activeScene.findMesh(nextEntity) ||
								    containsIgnoreCase(_hiddenSceneMeshes, nextEntity))
									continue;
								if (nextObject.examinable)
									examineMeshes.push_back(nextEntity);
								if (nextObject.operateStart != 0xffffffffU &&
								    nextObject.operateEnd != 0xffffffffU &&
								    nextObject.operateStart < nextObject.operateEnd)
									operateMeshes.push_back(nextEntity);
							}

							debug(1, "Zero Comico: changed place to %s at nav node %d",
							      room->name.c_str(), _playerNavNode);
							renderGameplayFrame(renderCamera, sceneDirectory, playerDirectory,
							                    "Stay", 0.0f, frame);
						} else {
							warning("Zero Comico: destination room %s is not declared",
							        _pendingRoomName.c_str());
						}
						_pendingRoomName.clear();
						_pendingRoomCutscene.clear();
					}

					if (!_pendingSayText.empty() && !done && !shouldQuit()) {
						drawCutsceneSubtitle(frame, _pendingSaySpeaker, _pendingSayText);
						_system->copyRectToScreen(frame.getPixels(), frame.pitch,
						                          0, 0, frame.w, frame.h);
						_system->updateScreen();

						uint32 sayDuration = (uint32)_pendingSayText.size() * 70U;
						if (sayDuration < 1200U)
							sayDuration = 1200U;
						if (sayDuration > 5000U)
							sayDuration = 5000U;
						const uint32 sayStart = _system->getMillis();
						bool dismissSay = false;
						while (!shouldQuit() && !dismissSay &&
						       _system->getMillis() - sayStart < sayDuration) {
							Common::Event sayEvent;
							while (_system->getEventManager()->pollEvent(sayEvent)) {
								if (sayEvent.type == Common::EVENT_QUIT ||
								    sayEvent.type == Common::EVENT_RETURN_TO_LAUNCHER) {
									quitGame();
									dismissSay = true;
									break;
								}
								if (sayEvent.type == Common::EVENT_KEYDOWN ||
								    sayEvent.type == Common::EVENT_LBUTTONDOWN ||
								    sayEvent.type == Common::EVENT_RBUTTONDOWN) {
									dismissSay = true;
									break;
								}
							}
							_system->delayMillis(10);
						}

						_pendingSaySpeaker.clear();
						_pendingSayText.clear();
						if (!shouldQuit())
							renderGameplayFrame(renderCamera, sceneDirectory, playerDirectory,
							                    "Stay", 0.0f, frame);
					}
				}
				continue;
			}

			Vec3f ground;
			if (!screenPointToGround(renderCamera, event.mouse.x, event.mouse.y, 800, 600, ground))
				continue;

			const int destinationNode = _activeWalkMap.nearestGraphNode(ground.x, ground.z);
			Common::Array<int> route;
			if (destinationNode < 0 ||
			    !_activeWalkMap.shortestPath(_playerNavNode, destinationNode, route) || route.empty())
				continue;

			debug(1, "Zero Comico: click navigation selected node %d through %u path nodes",
			      destinationNode, (uint)route.size());

			Common::Array<Common::String> startClips;
			Common::Array<Common::String> loopClips;
			Common::Array<Common::String> stopClips;

			const SequenceTransition *startTransition = _playerSequences.findTransition("cammina", "0>1");
			const SequenceTransition *loopTransition = _playerSequences.findTransition("cammina", "1>1");
			const SequenceTransition *stopTransition = _playerSequences.findTransition("cammina", "1>0");
			if (startTransition)
				startClips = startTransition->clips;
			if (loopTransition)
				loopClips = loopTransition->clips;
			if (stopTransition)
				stopClips = stopTransition->clips;

			if (startClips.empty())
				startClips.push_back("Start1");
			if (loopClips.empty())
				loopClips.push_back("Camm1");
			if (stopClips.empty())
				stopClips.push_back("Alt1");

			const float frameRate = 25.0f;
			const float tickSeconds = 0.02f;
			float walkSpeed = 0.0f;
			uint32 measuredWalkClips = 0;
			for (uint32 speedIndex = 0; speedIndex < loopClips.size(); ++speedIndex) {
				const float clipSpeed = animationHorizontalSpeed(
					_playerScene, _playerSequences.bodyName, loopClips[speedIndex], frameRate);
				if (clipSpeed > 0.01f) {
					walkSpeed += clipSpeed;
					++measuredWalkClips;
				}
			}
			if (measuredWalkClips > 0)
				walkSpeed /= measuredWalkClips;
			else
				walkSpeed = 45.0f;
			const float stepDistance = walkSpeed * tickSeconds;
			debug(1, "Zero Comico: Giovanni walk speed %.3f units/s from %u JACS loop clips",
			      walkSpeed, (uint)measuredWalkClips);

			bool starting = true;
			uint32 animationClipIndex = 0;
			Common::String animationSource = startClips[0];
			float animationFrame = 0.0f;
			float animationEnd = 0.0f;
			if (!animationClipRange(_playerScene, _playerSequences.bodyName, animationSource,
			                        animationFrame, animationEnd)) {
				starting = false;
				animationSource = loopClips[0];
				animationClipRange(_playerScene, _playerSequences.bodyName, animationSource,
				                   animationFrame, animationEnd);
			}

			for (uint32 routeIndex = 1; routeIndex < route.size() && !done && !shouldQuit(); ++routeIndex) {
				const NavNode &targetNode = _activeWalkMap.graph[(uint32)route[routeIndex]];
				Vec3f target = { targetNode.pos.x, 0.0f, targetNode.pos.y };
				float dx = target.x - _playerPosition.x;
				float dz = target.z - _playerPosition.z;
				float distance = std::sqrt(dx * dx + dz * dz);

				if (distance > 0.0001f) {
					_playerFacingTarget = target;
					dx /= distance;
					dz /= distance;
				}

				while (distance > 0.0001f && !done && !shouldQuit()) {
					const float advance = distance < stepDistance ? distance : stepDistance;
					_playerPosition.x += dx * advance;
					_playerPosition.z += dz * advance;
					distance -= advance;

					if (!renderGameplayFrame(renderCamera, sceneDirectory, playerDirectory,
					                         animationSource, animationFrame, frame)) {
						done = true;
						break;
					}

					animationFrame += frameRate * tickSeconds;
					if (animationFrame > animationEnd) {
						if (starting) {
							starting = false;
							animationClipIndex = 0;
						} else {
							animationClipIndex = (animationClipIndex + 1) % loopClips.size();
						}
						animationSource = loopClips[animationClipIndex];
						if (!animationClipRange(_playerScene, _playerSequences.bodyName, animationSource,
						                        animationFrame, animationEnd)) {
							animationFrame = 0.0f;
							animationEnd = 30.0f;
						}
					}

					Common::Event moveEvent;
					while (_system->getEventManager()->pollEvent(moveEvent)) {
						if (moveEvent.type == Common::EVENT_QUIT ||
						    moveEvent.type == Common::EVENT_RETURN_TO_LAUNCHER) {
							quitGame();
							done = true;
							break;
						}
						if (moveEvent.type == Common::EVENT_KEYDOWN &&
						    moveEvent.kbd.keycode == Common::KEYCODE_ESCAPE) {
							done = true;
							break;
						}
					}
					_system->delayMillis(20);
				}

				_playerPosition = target;
				_playerNavNode = route[routeIndex];
			}

			// Complete the 1>0 transition at the destination instead of snapping
			// straight from the looping walk cycle to the idle pose.
			if (!done && !shouldQuit()) {
				const Common::String &stopSource =
					stopClips[animationClipIndex % stopClips.size()];
				float stopFrame = 0.0f;
				float stopEnd = 0.0f;
				if (animationClipRange(_playerScene, _playerSequences.bodyName, stopSource,
				                       stopFrame, stopEnd)) {
					while (stopFrame <= stopEnd && !done && !shouldQuit()) {
						if (!renderGameplayFrame(renderCamera, sceneDirectory, playerDirectory,
						                         stopSource, stopFrame, frame)) {
							done = true;
							break;
						}
						stopFrame += frameRate * tickSeconds;
						_system->delayMillis(20);
					}
				}

				if (!done && !shouldQuit()) {
					float stayFrame = 0.0f;
					float stayEnd = 0.0f;
					animationClipRange(_playerScene, _playerSequences.bodyName, "Stay", stayFrame, stayEnd);
					renderGameplayFrame(renderCamera, sceneDirectory, playerDirectory,
					                    "Stay", stayFrame, frame);
				}
			}
		}

		const uint32 idleNow = _system->getMillis();
		if (!done && !shouldQuit() && idleNow - lastIdleRender >= 40U) {
			float stayStart = 0.0f;
			float stayEnd = 0.0f;
			float stayFrame = 0.0f;
			if (animationClipRange(_playerScene, _playerSequences.bodyName, "Stay", stayStart, stayEnd)) {
				const float stayCount = stayEnd >= stayStart
					? stayEnd - stayStart + 1.0f : 1.0f;
				const float elapsedFrames =
					(float)(idleNow - idleAnimationStart) * 25.0f / 1000.0f;
				stayFrame = stayStart + std::fmod(elapsedFrames, stayCount);
			}
			renderGameplayFrame(renderCamera, sceneDirectory, playerDirectory,
			                    "Stay", stayFrame, frame);
			lastIdleRender = idleNow;
		}
		_system->delayMillis(10);
	}

	return !shouldQuit();
}

void ZeroComicoEngine::showImageModal(const Common::Path &path) {
	Graphics::ManagedSurface image;
	if (!ResourceReader::decodeJgfFile(path, image)) {
		warning("Zero Comico: cannot decode image %s", path.toString().c_str());
		return;
	}

	const int x = (800 - image.w) / 2;
	const int y = (600 - image.h) / 2;
	_system->copyRectToScreen(image.getPixels(), image.pitch, x, y, image.w, image.h);
	_system->updateScreen();

	bool dismiss = false;
	while (!shouldQuit() && !dismiss) {
		Common::Event event;
		while (_system->getEventManager()->pollEvent(event)) {
			if (event.type == Common::EVENT_QUIT || event.type == Common::EVENT_RETURN_TO_LAUNCHER) {
				quitGame();
				break;
			}
			if (event.type == Common::EVENT_KEYDOWN || event.type == Common::EVENT_LBUTTONDOWN) {
				dismiss = true;
				break;
			}
		}
		_system->delayMillis(10);
	}
}

void ZeroComicoEngine::runMenu() {
	int selection = 0;

	while (!shouldQuit()) {
		Common::Event event;
		while (_system->getEventManager()->pollEvent(event)) {
			if (event.type == Common::EVENT_QUIT || event.type == Common::EVENT_RETURN_TO_LAUNCHER) {
				quitGame();
				break;
			}
			if (event.type != Common::EVENT_KEYDOWN)
				continue;

			const Common::KeyCode key = event.kbd.keycode;
			if (key == Common::KEYCODE_ESCAPE) {
				quitGame();
				break;
			}

			if (key == Common::KEYCODE_LEFT || key == Common::KEYCODE_UP) {
				selection = (selection + kMenuButtonCount - 1) % kMenuButtonCount;
				renderMenuFrame(selection);
				continue;
			}
			if (key == Common::KEYCODE_RIGHT || key == Common::KEYCODE_DOWN) {
				selection = (selection + 1) % kMenuButtonCount;
				renderMenuFrame(selection);
				continue;
			}

			if (key != Common::KEYCODE_RETURN && key != Common::KEYCODE_KP_ENTER && key != Common::KEYCODE_SPACE)
				continue;

			switch (selection) {
			case 0: { // NUOVO -> ChangeMainPlace mp1 in Interface.isc
				Common::String nextMainPlace("Mp1");
				while (!nextMainPlace.empty() && !shouldQuit()) {
					_pendingMainPlace.clear();
					if (!runMainPlacePreview(nextMainPlace))
						break;
					nextMainPlace = _pendingMainPlace;
				}
				if (!shouldQuit())
					renderMenuFrame(selection);
				break;
			}
			case 1: // AIUTI
				showImageModal(Common::Path("images/help.tga"));
				if (!shouldQuit())
					renderMenuFrame(selection);
				break;
			case 2: // CREDITS
				playFilmIfPresent(Common::Path("Data/crediti.avi"));
				if (!shouldQuit())
					renderMenuFrame(selection);
				break;
			case 3: // ESCI
				quitGame();
				break;
			case 4: // CARICA
				debug(1, "Zero Comico: CARICA selected; save/load UI is not implemented yet");
				break;
			default:
				break;
			}
		}
		_system->delayMillis(10);
	}
}

void ZeroComicoEngine::showBootstrapScreen() {
	Graphics::ManagedSurface image;

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
