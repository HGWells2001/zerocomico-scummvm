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

#include "common/config-manager.h"
#include "common/events.h"
#include "common/file.h"
#include "common/serializer.h"
#include "common/memstream.h"
#include "common/system.h"
#include "engines/advancedDetector.h"
#include "engines/util.h"
#include "graphics/font.h"
#include "graphics/fontman.h"
#include "graphics/managed_surface.h"
#include "graphics/pixelformat.h"
#include "graphics/surface.h"

#include "audio/mixer.h"
#include "audio/audiostream.h"
#ifdef USE_MAD
#include "audio/decoders/mp3.h"
#endif
#include "video/avi_decoder.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

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

static const uint32 kSaveVersion = 5;
static const uint32 kMinimumSaveVersion = 4;

// Zero Comico.exe initializes the Subjective-camera fallback eye height to
// 0.52 metres. With the retail GlobalScaling value of 1, the engine's world
// conversion (value * 100 * GlobalScaling) produces 52 world units.
static const float kSubjectiveFallbackEyeHeight = 52.0f;

enum ScriptKeyMask {
	kScriptKeyLeft    = 1 << 0,
	kScriptKeyRight   = 1 << 1,
	kScriptKeyUp      = 1 << 2,
	kScriptKeyDown    = 1 << 3,
	kScriptKeyOperate = 1 << 4,
	kScriptKeySkip    = 1 << 5,
	kScriptKeyAbort   = 1 << 6,
	kScriptKeySpace   = 1 << 7
};

static uint32 scriptKeyMaskForName(const Common::String &name) {
	if (name.equalsIgnoreCase("LEFT"))
		return kScriptKeyLeft;
	if (name.equalsIgnoreCase("RIGHT"))
		return kScriptKeyRight;
	if (name.equalsIgnoreCase("UP"))
		return kScriptKeyUp;
	if (name.equalsIgnoreCase("DOWN"))
		return kScriptKeyDown;
	if (name.equalsIgnoreCase("OPERATE"))
		return kScriptKeyOperate;
	if (name.equalsIgnoreCase("SKIP"))
		return kScriptKeySkip;
	if (name.equalsIgnoreCase("ABORT"))
		return kScriptKeyAbort;
	if (name.equalsIgnoreCase("SPACE"))
		return kScriptKeySpace;
	return 0;
}

static uint32 scriptKeyMaskForEvent(const Common::Event &event) {
	if (event.type != Common::EVENT_KEYDOWN && event.type != Common::EVENT_KEYUP)
		return 0;

	switch (event.kbd.keycode) {
	case Common::KEYCODE_LEFT:
		return kScriptKeyLeft;
	case Common::KEYCODE_RIGHT:
		return kScriptKeyRight;
	case Common::KEYCODE_UP:
		return kScriptKeyUp;
	case Common::KEYCODE_DOWN:
		return kScriptKeyDown;
	case Common::KEYCODE_RETURN:
	case Common::KEYCODE_KP_ENTER:
		return kScriptKeyOperate;
	case Common::KEYCODE_DELETE:
		return kScriptKeySkip;
	case Common::KEYCODE_ESCAPE:
		return kScriptKeyAbort;
	case Common::KEYCODE_SPACE:
		return kScriptKeySpace;
	default:
		return 0;
	}
}

static bool containsIgnoreCase(const Common::Array<Common::String> &values,
                               const Common::String &value) {
	for (uint32 i = 0; i < values.size(); ++i)
		if (values[i].equalsIgnoreCase(value))
			return true;
	return false;
}

static bool startsWithIgnoreCase(const Common::String &value,
                                const Common::String &prefix) {
	return prefix.size() <= value.size() &&
	       value.substr(0, prefix.size()).equalsIgnoreCase(prefix);
}

static Common::String indexedSceneEntityName(const Common::String &prefix, int index) {
	return Common::String::format("%s%02d", prefix.c_str(), index);
}

static bool parseScriptFloat(const Common::String &token, float &value) {
	if (token.empty())
		return false;

	Common::String normalized = token;
	for (uint32 i = 0; i < normalized.size(); ++i)
		if (normalized[i] == ',')
			normalized.setChar('.', i);

	char *end = nullptr;
	const double parsed = strtod(normalized.c_str(), &end);
	if (!end || end == normalized.c_str() || *end != 0)
		return false;

	value = (float)parsed;
	return true;
}

static void splitE3dCommand(const Common::String &command,
                            Common::Array<Common::String> &tokens) {
	tokens.clear();
	Common::String current;
	for (uint32 i = 0; i <= command.size(); ++i) {
		const char ch = i < command.size() ? command[i] : ' ';
		if (ch == ' ' || ch == '\t' || ch == ',') {
			if (!current.empty()) {
				tokens.push_back(current);
				current.clear();
			}
			continue;
		}
		current += ch;
	}
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

static void markInventoryObjectAssigned(PuzzleScript &puzzle, const Common::String &name) {
	PuzzleObject *object = puzzle.findObject(name);
	if (!object)
		return;

	// Zero Comico.exe applies this exact state string in addobjininv:
	// "pickable FALSE examinable FALSE assigned TRUE".
	object->pickable = false;
	object->examinable = false;
	object->assigned = true;
}

static bool puzzleObjectMatchesState(const PuzzleObject &object, Common::String state) {
	state.toLowercase();

	if (state.find("pickable true") != Common::String::npos) return object.pickable;
	if (state.find("pickable false") != Common::String::npos) return !object.pickable;
	if (state.find("examinable true") != Common::String::npos) return object.examinable;
	if (state.find("examinable false") != Common::String::npos) return !object.examinable;
	if (state.find("operated true") != Common::String::npos) return object.operated;
	if (state.find("operated false") != Common::String::npos) return !object.operated;
	if (state.find("examinated true") != Common::String::npos) return object.examinated;
	if (state.find("examinated false") != Common::String::npos) return !object.examinated;
	if (state.find("autocamera true") != Common::String::npos) return object.autoCamera;
	if (state.find("autocamera false") != Common::String::npos) return !object.autoCamera;
	if (state.find("randompos true") != Common::String::npos) return object.randomPos;
	if (state.find("randompos false") != Common::String::npos) return !object.randomPos;
	if (state.find("combined true") != Common::String::npos) return object.combined;
	if (state.find("combined false") != Common::String::npos) return !object.combined;
	if (state.find("assigned true") != Common::String::npos) return object.assigned;
	if (state.find("assigned false") != Common::String::npos) return !object.assigned;
	if (state.find("enabled true") != Common::String::npos) return object.enabled;
	if (state.find("enabled false") != Common::String::npos) return !object.enabled;
	if (state.find("inside true") != Common::String::npos) return object.inside;
	if (state.find("inside false") != Common::String::npos) return !object.inside;
	if (state.find("collision true") != Common::String::npos) return object.collision;
	if (state.find("collision false") != Common::String::npos) return !object.collision;
	if (state.find("soundstate true") != Common::String::npos) return object.soundState;
	if (state.find("soundstate false") != Common::String::npos) return !object.soundState;

	return false;
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

static Common::String subjectiveHeadNodeName(const Common::String &rootName) {
	const uint32 separator = rootName.find('_');
	if (separator == Common::String::npos || separator == 0)
		return Common::String();

	const Common::String prefix = rootName.substr(0, separator);
	return prefix + "_" + prefix + "testa";
}

static Vec3f transformActorLocalPoint(const Vec3f &point,
                                      const RenderTransform &transform) {
	Vec3f local = {
		point.x * transform.localScale.x,
		point.y * transform.localScale.y,
		point.z * transform.localScale.z
	};

	Vec3f axis = {
		transform.localRotation[0],
		transform.localRotation[1],
		transform.localRotation[2]
	};
	const float angle = transform.localRotation[3];
	if (std::fabs(angle) > 1.0e-7f && normalizeVec3(axis)) {
		const float c = std::cos(angle);
		const float s = std::sin(angle);
		const float oneMinusC = 1.0f - c;
		const float projection = dotVec3(axis, local);
		const Vec3f cross = crossVec3(axis, local);
		Vec3f rotated = {
			local.x * c + cross.x * s + axis.x * projection * oneMinusC,
			local.y * c + cross.y * s + axis.y * projection * oneMinusC,
			local.z * c + cross.z * s + axis.z * projection * oneMinusC
		};
		local = rotated;
	}

	local.x += transform.localTranslation.x;
	local.y += transform.localTranslation.y;
	local.z += transform.localTranslation.z;

	const float c = std::cos(transform.yawRadians);
	const float s = std::sin(transform.yawRadians);
	Vec3f world = {
		local.x * c - local.z * s + transform.translation.x,
		local.y + transform.translation.y,
		local.x * s + local.z * c + transform.translation.z
	};
	return world;
}

static float cross2D(float ax, float az, float bx, float bz) {
	return ax * bz - az * bx;
}

static bool movementCrossesPortal(const Vec3f &from, const Vec3f &to,
                                  const ShapeMarker &portal) {
	const float rx = to.x - from.x;
	const float rz = to.z - from.z;
	const float sx = portal.b.x - portal.a.x;
	const float sz = portal.b.z - portal.a.z;
	const float denominator = cross2D(rx, rz, sx, sz);
	if (std::fabs(denominator) <= 1.0e-6f)
		return false;

	const float qpx = portal.a.x - from.x;
	const float qpz = portal.a.z - from.z;
	const float t = cross2D(qpx, qpz, sx, sz) / denominator;
	const float u = cross2D(qpx, qpz, rx, rz) / denominator;

	// The retail executable tests the character's previous-to-current movement
	// segment against the portal A/B segment and requires an interior crossing.
	// Keep movement endpoints exclusive to avoid bouncing on the same boundary
	// immediately after a room switch.
	return t > 1.0e-5f && t < 1.0f - 1.0e-5f &&
	       u >= -1.0e-5f && u <= 1.0f + 1.0e-5f;
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

static uint32 retailTextDurationMillis(const Common::String &text, float speakerSpeed) {
	if (speakerSpeed <= 0.0f)
		speakerSpeed = 0.07f;

	// Retail: trunc(strlen * speakerSpeed * 70), clamped to at least 210 ticks.
	uint32 ticks = (uint32)((float)text.size() * speakerSpeed * 70.0f);
	if (ticks < 210U)
		ticks = 210U;
	return (uint32)(((uint64)ticks * 1000U) / 70U);
}

static void drawCutsceneSubtitle(Graphics::ManagedSurface &surface,
                                 const Common::String &speaker,
                                 const Common::String &text,
                                 const DialogSpeaker *speakerInfo = nullptr) {
	if (text.empty())
		return;

	const Graphics::Font *font = FontMan.getFontByUsage(Graphics::FontManager::kBigGUIFont);
	if (!font)
		font = FontMan.getFontByUsage(Graphics::FontManager::kGUIFont);
	if (!font)
		return;

	uint32 color = surface.format.RGBToColor(255, 255, 255);
	if (speakerInfo) {
		const byte red = (byte)(speakerInfo->red > 255 ? 255 : speakerInfo->red);
		const byte green = (byte)(speakerInfo->green > 255 ? 255 : speakerInfo->green);
		const byte blue = (byte)(speakerInfo->blue > 255 ? 255 : speakerInfo->blue);
		color = surface.format.RGBToColor(red, green, blue);
	} else if (speaker.equalsIgnoreCase("Aldo")) {
		color = surface.format.RGBToColor(0, 255, 0);
	} else if (speaker.equalsIgnoreCase("Giovanni")) {
		color = surface.format.RGBToColor(255, 255, 0);
	} else if (speaker.equalsIgnoreCase("Giacomo")) {
		color = surface.format.RGBToColor(0, 255, 255);
	}

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

#ifdef USE_MAD
static bool playNamedMp3(Audio::Mixer *mixer, Audio::Mixer::SoundType type,
                         const Common::String &name, Audio::SoundHandle *handle = nullptr,
                         byte channelVolume = Audio::Mixer::kMaxChannelVolume) {
	if (!mixer || name.empty())
		return false;

	Common::String fileName = name;
	if (!fileName.hasSuffixIgnoreCase(".mp3"))
		fileName += ".mp3";

	const char *const directories[] = {
		"Sound",
		"Sound/Interface",
		"Sound/Inventory",
		"Sound/steps"
	};

	for (uint32 directoryIndex = 0; directoryIndex < ARRAYSIZE(directories); ++directoryIndex) {
		Common::File *file = new Common::File();
		const Common::Path path =
			Common::Path(directories[directoryIndex]).appendComponent(fileName);
		if (!file->open(path)) {
			delete file;
			continue;
		}

		Audio::SeekableAudioStream *stream =
			Audio::makeMP3Stream(file, DisposeAfterUse::YES);
		if (!stream) {
			delete file;
			return false;
		}

		mixer->playStream(type, handle, stream, -1, channelVolume);
		return true;
	}

	return false;
}

static bool playNamedMp3Looped(Audio::Mixer *mixer, Audio::Mixer::SoundType type,
                               const Common::String &name, Audio::SoundHandle &handle,
                               byte channelVolume = Audio::Mixer::kMaxChannelVolume) {
	if (!mixer || name.empty())
		return false;

	Common::String fileName = name;
	if (!fileName.hasSuffixIgnoreCase(".mp3"))
		fileName += ".mp3";

	const char *const directories[] = {
		"Sound",
		"Sound/Interface",
		"Sound/Inventory",
		"Sound/steps"
	};

	for (uint32 directoryIndex = 0; directoryIndex < ARRAYSIZE(directories); ++directoryIndex) {
		Common::File *file = new Common::File();
		const Common::Path path =
			Common::Path(directories[directoryIndex]).appendComponent(fileName);
		if (!file->open(path)) {
			delete file;
			continue;
		}

		Audio::SeekableAudioStream *stream =
			Audio::makeMP3Stream(file, DisposeAfterUse::YES);
		if (!stream) {
			delete file;
			return false;
		}

		Audio::AudioStream *loop = Audio::makeLoopingAudioStream(stream, 0, 0, 0);
		mixer->playStream(type, &handle, loop, -1, channelVolume);
		return true;
	}

	return false;
}
#endif



} // namespace

ZeroComicoEngine::ZeroComicoEngine(OSystem *syst, const ADGameDescription *desc)
	: Engine(syst), _gameDescription(desc), _havePlayerStart(false), _playerHatVisible(true),
	  _playerNavNode(-1), _lastDialogueChoice(-1), _scriptDialogueContextActive(false),
	  _scriptDialogueFrame(nullptr),
	  _loopCutStartFrame(0.0f), _loopCutEndFrame(0.0f), _loopCutStartMillis(0),
	  _loopCutActive(false), _pendingTakeEventFrame(0), _pendingTakeActive(false),
	  _pendingTakeInventoryAdded(false), _scriptVM(this),
	  _interfaceDisabled(false), _3dEnabled(true), _portalsEnabled(true),
	  _cameraMode(0), _cameraModeLocked(false), _playerNoCameraReset(false),
	  _depthCueEnabled(false), _shadeEnabled(false), _depthCueStart(0.0f), _depthCueEnd(0.0f),
	  _globalMasterVolume(100.0f), _sampleDefaultFarVolume(0.0f), _sampleDefaultNearVolume(100.0f),
	  _sampleDefaultMinRange(0.0f), _sampleDefaultMaxRange(500.0f),
	  _masterColorFadeSteps(0.0f), _masterColorFadeStartMillis(0),
	  _masterColorFadeActive(false), _spotHeight(85.0f), _spotMaxDeltaY(30.0f), _spotDistance(350.0f),
	  _spotMinDistance(25.0f), _spotSmooth(30.0f),
	  _spotCameraInitialized(false), _dynamicCameraInitialized(false), _scriptKeyMask(0),
	  _scriptAudioClass(3), _pendingLoadVersion(0), _pendingLoadActive(false),
	  _lastBackgroundScriptTick(0) {
	_environmentSoundActive = false;
	_playerPosition.x = _playerPosition.y = _playerPosition.z = 0.0f;
	for (int soundClass = 0; soundClass < 6; ++soundClass)
		_soundClassVolumes[soundClass] = 100.0f;
	for (int component = 0; component < 4; ++component) {
		_masterColor[component] = 1.0f;
		_masterColorFrom[component] = 1.0f;
		_masterColorTo[component] = 1.0f;
	}
	_spotCameraPosition.x = _spotCameraPosition.y = _spotCameraPosition.z = 0.0f;
	_dynamicCameraPosition.x = _dynamicCameraPosition.y = _dynamicCameraPosition.z = 0.0f;
	_playerFacingTarget.x = _playerFacingTarget.y = _playerFacingTarget.z = 0.0f;
}

bool ZeroComicoEngine::hasFeature(EngineFeature f) const {
	return f == kSupportsReturnToLauncher ||
	       f == kSupportsSavingDuringRuntime ||
	       f == kSupportsLoadingDuringRuntime;
}

bool ZeroComicoEngine::canSaveGameStateCurrently(Common::U32String *msg) {
	(void)msg;
	return !_currentMainPlace.empty() && !_activeRoomName.empty() &&
	       !_scriptDialogueContextActive && !_pendingTakeActive && !_loopCutActive;
}

bool ZeroComicoEngine::canLoadGameStateCurrently(Common::U32String *msg) {
	(void)msg;
	return true;
}

Common::Error ZeroComicoEngine::saveGameStream(Common::WriteStream *stream, bool isAutosave) {
	(void)isAutosave;
	if (!stream || _currentMainPlace.empty() || _activeRoomName.empty())
		return Common::kWritingFailed;

	Common::MemoryWriteStreamDynamic payload(DisposeAfterUse::YES);
	Common::Serializer serializer(nullptr, &payload);
	synchronizePersistentState(serializer, kSaveVersion);
	if (serializer.err())
		return Common::kWritingFailed;

	static const char kMagic[4] = {'Z', 'C', 'O', 'M'};
	stream->write(kMagic, sizeof(kMagic));
	stream->writeUint32LE(kSaveVersion);
	stream->writeUint32LE((uint32)payload.size());
	if (payload.size() != 0)
		stream->write(payload.getData(), payload.size());

	return stream->err() ? Common::kWritingFailed : Common::kNoError;
}

Common::Error ZeroComicoEngine::loadGameStream(Common::SeekableReadStream *stream) {
	if (!stream)
		return Common::kReadingFailed;

	char magic[4] = {0, 0, 0, 0};
	if (stream->read(magic, sizeof(magic)) != sizeof(magic) ||
	    memcmp(magic, "ZCOM", 4) != 0)
		return Common::kReadingFailed;

	const uint32 version = stream->readUint32LE();
	const uint32 payloadSize = stream->readUint32LE();
	if (stream->err() || version < kMinimumSaveVersion || version > kSaveVersion ||
	    payloadSize == 0 || payloadSize > 16U * 1024U * 1024U)
		return Common::kReadingFailed;

	_pendingLoadData.resize(payloadSize);
	if (stream->read(_pendingLoadData.data(), payloadSize) != payloadSize) {
		_pendingLoadData.clear();
		return Common::kReadingFailed;
	}

	Common::MemoryReadStream probe(_pendingLoadData.data(), _pendingLoadData.size());
	Common::Serializer header(&probe, nullptr);
	_pendingLoadMainPlace.clear();
	_pendingLoadRoomName.clear();
	header.syncString(_pendingLoadMainPlace);
	header.syncString(_pendingLoadRoomName);
	if (header.err() || _pendingLoadMainPlace.empty() || _pendingLoadRoomName.empty()) {
		_pendingLoadData.clear();
		_pendingLoadMainPlace.clear();
		_pendingLoadRoomName.clear();
		return Common::kReadingFailed;
	}

	_pendingLoadVersion = version;
	_pendingLoadActive = true;
	// Make loads requested by ScummVM's global menu behave like F9: the
	// interactive loop observes this transition request as soon as control
	// returns to gameplay and rebuilds the saved main place before applying
	// the staged payload.
	_pendingMainPlace = _pendingLoadMainPlace;
	debug(1, "Zero Comico: staged ZCOM v%u restore for %s/%s (%u bytes)",
	      (uint)_pendingLoadVersion, _pendingLoadMainPlace.c_str(),
	      _pendingLoadRoomName.c_str(), (uint)_pendingLoadData.size());
	return Common::kNoError;
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

	// ScummVM's launcher can request a specific save slot before run(). Engine
	// does not consume that setting automatically for every engine, so honor it
	// here and go straight to the staged main-place rebuild instead of replaying
	// the retail intro/menu first.
	if (ConfMan.hasKey("save_slot")) {
		const int slot = ConfMan.getInt("save_slot");
		const Common::Error loadError = loadGameState(slot);
		if (loadError.getCode() == Common::kNoError &&
		    _pendingLoadActive && !_pendingLoadMainPlace.empty()) {
			debug(1, "Zero Comico: launcher requested save slot %d", slot);
			Common::String nextMainPlace = _pendingLoadMainPlace;
			while (!nextMainPlace.empty() && !shouldQuit()) {
				_pendingMainPlace.clear();
				if (!runMainPlacePreview(nextMainPlace))
					break;
				nextMainPlace = _pendingMainPlace;
			}

			if (shouldQuit())
				return Common::kNoError;

			if (loadMenuScene() && renderMenuFrame(0))
				runMenu();
			else {
				showBootstrapScreen();
				waitForExit();
			}
			return Common::kNoError;
		}

		warning("Zero Comico: could not load launcher save slot %d; continuing normally", slot);
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

bool ZeroComicoEngine::runScriptWithAudioClass(const ScriptProgram &program,
                                                    uint32 startIndex, uint32 endIndex,
                                                    uint32 maxSteps, int audioClass) {
	const int previousClass = _scriptAudioClass;
	_scriptAudioClass = audioClass;
	const bool ok = _scriptVM.run(program, startIndex, endIndex, maxSteps);
	_scriptAudioClass = previousClass;
	return ok;
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
	if (!runScriptWithAudioClass(program, runtimeStart, runtimeEnd, 4096, 3))
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

	return runScriptWithAudioClass(program, runtimeStart, runtimeEnd, 8192, 3);
}

bool ZeroComicoEngine::playCutscene(const Common::String &name, bool leaveOpen) {
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
	const uint32 cutsceneStartMillis = _system->getMillis();
	uint32 presentedFrames = 0;

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
				renderCamera.rollRadians = roll;
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
						playNamedMp3(_mixer, Audio::Mixer::kSFXSoundType,
						             event.args[0], &cutsceneSfxHandle,
						             retailChannelVolume(3, 100.0f, event.args[0]));
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
								                   &cutsceneSpeechHandle, stream, -1,
								                   retailChannelVolume(1, 100.0f, Common::String()));
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
					if (event.args.empty()) {
						setEnvironmentSound(Common::String(), false);
					} else {
						const Common::String &soundName = event.args[0];
						const bool enable = !soundName.equalsIgnoreCase("none") &&
						                    !soundName.equalsIgnoreCase("off") &&
						                    soundName != "0";
						setEnvironmentSound(soundName, enable);
					}
					debug(1, "Zero Comico: cutscene %s environment-sound event at frame %u",
					      name.c_str(), event.frame);
					break;
				}
			}
		}

		applyMasterColor(frameSurface);
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

		// Retail cutscene timelines are frame-indexed at 25 fps. Sleep to the
		// absolute frame deadline rather than adding 40 ms after rendering, so
		// CPU render cost does not progressively slow audio/subtitle sync.
		++presentedFrames;
		const uint32 deadline = cutsceneStartMillis + presentedFrames * 40U;
		const uint32 now = _system->getMillis();
		if (now < deadline)
			_system->delayMillis(deadline - now);
	}

	if (_mixer->isSoundHandleActive(cutsceneSfxHandle))
		_mixer->stopHandle(cutsceneSfxHandle);
	if (_mixer->isSoundHandleActive(cutsceneSpeechHandle))
		_mixer->stopHandle(cutsceneSpeechHandle);

	if (leaveOpen && !shouldQuit()) {
		// play_open_cut differs from play_cut in shipped progression: its final
		// scene remains part of the room. Mp1 relies on this for c121_pallina,
		// c131_libro/cappello and later open-cut entities that do not exist in
		// the base room P3D.
		if (!scene.poseCutsceneGeometry(assetStem, endFrame))
			warning("Zero Comico: open cutscene %s final pose failed at %.0f",
			        name.c_str(), endFrame);

		Common::Array<Common::String> finalVisible;
		scene.visibleMeshesForSource(assetStem, endFrame, finalVisible);
		for (uint32 meshIndex = 0; meshIndex < scene.meshes.size(); ++meshIndex) {
			const NamedMesh &mesh = scene.meshes[meshIndex];
			if (mesh.data.isFlesh())
				continue;
			if (containsIgnoreCase(finalVisible, mesh.name))
				removeIgnoreCase(_hiddenSceneMeshes, mesh.name);
			else if (!containsIgnoreCase(_hiddenSceneMeshes, mesh.name))
				_hiddenSceneMeshes.push_back(mesh.name);
		}

		bool replaced = false;
		for (uint32 i = 0; i < _openCutScenes.size(); ++i) {
			OpenCutSceneRuntime &runtime = _openCutScenes[i];
			if (!runtime.roomName.equalsIgnoreCase(_activeRoomName) ||
			    !runtime.assetStem.equalsIgnoreCase(assetStem))
				continue;
			runtime.scene = scene;
			replaced = true;
			break;
		}
		if (!replaced) {
			OpenCutSceneRuntime runtime;
			runtime.roomName = _activeRoomName;
			runtime.assetStem = assetStem;
			runtime.scene = scene;
			_openCutScenes.push_back(runtime);
		}

		_activeScene.mergeFrom(scene);
		debug(1, "Zero Comico: kept open cutscene %s in room %s (%u meshes)",
		      name.c_str(), _activeRoomName.c_str(), (uint)scene.meshes.size());
	}

	return !shouldQuit();
}

bool ZeroComicoEngine::startLoopCutscene(const Common::String &name) {
	if (_currentMainPlace.empty() || name.empty())
		return false;

	_loopCutScene.clear();
	_loopCutName.clear();
	_loopCutAssetStem.clear();
	_loopCutActive = false;

	Common::String assetStem = name;
	const Common::Path videoDirectory(_currentMainPlace + "/videos");
	bool loaded = _loopCutScene.loadPair(
		videoDirectory.appendComponent(assetStem + ".p3d"),
		videoDirectory.appendComponent(assetStem + ".anj"));

	if (!loaded && !assetStem.empty() && assetStem[0] == 'c') {
		assetStem = Common::String("C") + assetStem.substr(1);
		loaded = _loopCutScene.loadPair(
			videoDirectory.appendComponent(assetStem + ".p3d"),
			videoDirectory.appendComponent(assetStem + ".anj"));
	}
	if (!loaded) {
		warning("Zero Comico: cannot load loop cutscene %s", name.c_str());
		return false;
	}

	bool haveRange = false;
	float startFrame = 0.0f;
	float endFrame = 0.0f;
	for (uint32 clipIndex = 0; clipIndex < _loopCutScene.clips.size(); ++clipIndex) {
		const NamedAnimationClip &clip = _loopCutScene.clips[clipIndex];
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
	}
	if (!haveRange) {
		warning("Zero Comico: loop cutscene %s has no animation range", name.c_str());
		_loopCutScene.clear();
		return false;
	}

	_loopCutName = name;
	_loopCutAssetStem = assetStem;
	_loopCutStartFrame = startFrame;
	_loopCutEndFrame = endFrame;
	_loopCutStartMillis = _system->getMillis();
	_loopCutActive = true;
	debug(1, "Zero Comico: started loop cutscene %s frames %.0f..%.0f",
	      name.c_str(), startFrame, endFrame);
	return true;
}

bool ZeroComicoEngine::renderLoopCutsceneFrame(const Common::String &name) {
	if (!_loopCutActive || !_loopCutName.equalsIgnoreCase(name))
		return false;

	const float frameCount = _loopCutEndFrame - _loopCutStartFrame + 1.0f;
	if (frameCount <= 0.0f)
		return false;

	const uint32 elapsedMillis = _system->getMillis() - _loopCutStartMillis;
	const float elapsedFrames = (float)elapsedMillis * 25.0f / 1000.0f;
	const float frame = _loopCutStartFrame + std::fmod(elapsedFrames, frameCount);

	if (!_loopCutScene.poseCutsceneGeometry(_loopCutAssetStem, frame))
		warning("Zero Comico: loop cutscene %s pose failed at %.2f", name.c_str(), frame);

	Common::Array<Common::String> visibleMeshes;
	_loopCutScene.visibleMeshesForSource(_loopCutAssetStem, frame, visibleMeshes);

	const NamedAnimationClip *cameraClip = nullptr;
	const NamedAnimationClip *targetClip = nullptr;
	Common::String cameraName;
	for (uint32 clipIndex = 0; clipIndex < _loopCutScene.clips.size() && !cameraClip; ++clipIndex) {
		const NamedAnimationClip &clip = _loopCutScene.clips[clipIndex];
		if (!clip.data.sourceName.equalsIgnoreCase(_loopCutAssetStem))
			continue;
		for (uint32 trackIndex = 0; trackIndex < clip.data.tracks.size(); ++trackIndex) {
			if (clip.data.tracks[trackIndex].kind == kAnimCamera) {
				cameraClip = &clip;
				cameraName = clip.data.tracks[trackIndex].targetName;
				break;
			}
		}
	}

	if (!cameraName.empty())
		targetClip = _loopCutScene.findClipBySource(cameraName + ".target", _loopCutAssetStem);

	RenderCamera renderCamera;
	bool haveCamera = false;
	if (cameraClip && targetClip && !cameraName.empty()) {
		float position[3];
		float target[3];
		float focalLength = 0.0f;
		float roll = 0.0f;
		if (AnimationSampler::sampleCamera(cameraClip->data, cameraName, frame,
		                                  position, focalLength, roll) &&
		    AnimationSampler::sampleTarget(targetClip->data, cameraName + ".target",
		                                  frame, target) &&
		    focalLength > 0.0f) {
			renderCamera.position = { position[0], position[1], position[2] };
			renderCamera.target = { target[0], target[1], target[2] };
			renderCamera.focalPixels = focalLength * 800.0f / 36.0f;
			renderCamera.rollRadians = roll;
			haveCamera = true;
		}
	}

	const NamedCamera *embeddedCamera =
		cameraName.empty() ? nullptr : _loopCutScene.findCamera(cameraName);
	if (!embeddedCamera && !_loopCutScene.cameras.empty())
		embeddedCamera = &_loopCutScene.cameras[0];
	if (!haveCamera && embeddedCamera && embeddedCamera->data.fov > 0.0f) {
		renderCamera.position = embeddedCamera->data.position;
		renderCamera.target = embeddedCamera->data.target;
		renderCamera.focalPixels = embeddedCamera->data.fov * 800.0f / 36.0f;
		renderCamera.rollRadians = 0.0f;
		haveCamera = true;
	}
	if (!haveCamera)
		return false;

	Graphics::ManagedSurface frameSurface;
	const Common::Path videoDirectory(_currentMainPlace + "/videos");
	if (!_gameplayRenderer.render(_loopCutScene, renderCamera, videoDirectory,
	                              visibleMeshes, frameSurface, 800, 600))
		return false;

	_system->copyRectToScreen(frameSurface.getPixels(), frameSurface.pitch,
	                          0, 0, frameSurface.w, frameSurface.h);
	_system->updateScreen();
	return true;
}

bool ZeroComicoEngine::stopLoopCutscene(const Common::String &name) {
	if (!_loopCutActive)
		return true;
	if (!name.empty() && !_loopCutName.equalsIgnoreCase(name))
		return false;

	debug(1, "Zero Comico: stopped loop cutscene %s", _loopCutName.c_str());
	_loopCutActive = false;
	_loopCutName.clear();
	_loopCutAssetStem.clear();
	_loopCutStartFrame = 0.0f;
	_loopCutEndFrame = 0.0f;
	_loopCutStartMillis = 0;
	_loopCutScene.clear();
	return true;
}

bool ZeroComicoEngine::rehydrateOpenCutScene(OpenCutSceneRuntime &runtime) {
	if (_currentMainPlace.empty() || runtime.assetStem.empty())
		return false;

	const Common::Path videoDirectory(_currentMainPlace + "/videos");
	SceneModel scene;
	if (!scene.loadPair(videoDirectory.appendComponent(runtime.assetStem + ".p3d"),
	                    videoDirectory.appendComponent(runtime.assetStem + ".anj"))) {
		warning("Zero Comico: cannot restore open cutscene asset %s",
		        runtime.assetStem.c_str());
		return false;
	}

	float endFrame = 0.0f;
	bool haveRange = false;
	for (uint32 clipIndex = 0; clipIndex < scene.clips.size(); ++clipIndex) {
		const NamedAnimationClip &clip = scene.clips[clipIndex];
		if (!clip.data.sourceName.equalsIgnoreCase(runtime.assetStem))
			continue;
		if (!haveRange || (float)clip.data.endFrame > endFrame)
			endFrame = (float)clip.data.endFrame;
		haveRange = true;
	}
	if (!haveRange) {
		warning("Zero Comico: restored open cutscene %s has no animation range",
		        runtime.assetStem.c_str());
		return false;
	}

	if (!scene.poseCutsceneGeometry(runtime.assetStem, endFrame))
		warning("Zero Comico: restored open cutscene %s final pose failed at %.0f",
		        runtime.assetStem.c_str(), endFrame);

	runtime.scene = scene;
	return true;
}

void ZeroComicoEngine::installOpenCutScenesForRoom(const Common::String &roomName) {
	for (uint32 i = 0; i < _openCutScenes.size(); ++i) {
		const OpenCutSceneRuntime &runtime = _openCutScenes[i];
		if (runtime.roomName.equalsIgnoreCase(roomName) && !runtime.scene.meshes.empty())
			_activeScene.mergeFrom(runtime.scene);
	}
}

DynamicSceneEntity *ZeroComicoEngine::findDynamicSceneEntity(const Common::String &name) {
	for (uint32 i = 0; i < _dynamicSceneEntities.size(); ++i)
		if (_dynamicSceneEntities[i].name.equalsIgnoreCase(name))
			return &_dynamicSceneEntities[i];
	return nullptr;
}

const DynamicSceneEntity *ZeroComicoEngine::findDynamicSceneEntity(const Common::String &name) const {
	for (uint32 i = 0; i < _dynamicSceneEntities.size(); ++i)
		if (_dynamicSceneEntities[i].name.equalsIgnoreCase(name))
			return &_dynamicSceneEntities[i];
	return nullptr;
}

static void translateRigidMesh(NamedMesh &mesh, const Vec3f &position) {
	const Vec3f delta = {
		position.x - mesh.data.transform.translation.x,
		position.y - mesh.data.transform.translation.y,
		position.z - mesh.data.transform.translation.z
	};

	// MeshData vertices retain the retail loader representation
	// filePosition + translation - pivot. Move that stored representation by
	// the same delta as the transform so transformVertex() keeps the object's
	// local geometry intact while changing its world position.
	for (uint32 i = 0; i < mesh.data.vertices.size(); ++i) {
		mesh.data.vertices[i].x += delta.x;
		mesh.data.vertices[i].y += delta.y;
		mesh.data.vertices[i].z += delta.z;
	}
	mesh.data.transform.translation = position;
}

bool ZeroComicoEngine::cloneSceneEntity(const Common::String &sourceName,
                                        const Common::String &cloneName) {
	if (findDynamicSceneEntity(cloneName) || _activeScene.findMesh(cloneName))
		return true;

	SceneModel helperScene;
	const SceneModel *sourceScene = &_activeScene;
	const NamedMesh *source = _activeScene.findMesh(sourceName);
	if (!source) {
		const DynamicSceneEntity *dynamicSource = findDynamicSceneEntity(sourceName);
		if (dynamicSource)
			source = &dynamicSource->mesh;
	}

	// Retail CloneEntity arguments of entity type are resolved before the
	// callback. Mp2's Star_Star template lives under bodies/helpers/star rather
	// than in the active room, so reproduce that resolver fallback explicitly.
	if (!source && !_currentMainPlace.empty()) {
		Common::String helperStem = sourceName;
		const uint32 separator = helperStem.find('_');
		if (separator != Common::String::npos)
			helperStem = helperStem.substr(0, separator);
		helperStem.toLowercase();

		const Common::Path helperDirectory(_currentMainPlace + "/bodies/helpers");
		if (helperScene.loadPair(
				helperDirectory.appendComponent(helperStem + ".p3d"),
				helperDirectory.appendComponent(helperStem + ".anj"))) {
			source = helperScene.findMesh(sourceName);
			if (source)
				sourceScene = &helperScene;
		}
	}

	if (!source) {
		warning("Zero Comico: CloneEntity source %s is not available", sourceName.c_str());
		return false;
	}

	DynamicSceneEntity entity;
	entity.sourceName = sourceName;
	entity.name = cloneName;
	entity.mesh = *source;
	entity.mesh.name = cloneName;

	for (uint32 i = 0; i < entity.mesh.data.materials.size(); ++i) {
		const Common::String &materialName = entity.mesh.data.materials[i].name;
		bool duplicate = false;
		for (uint32 j = 0; j < entity.materials.size(); ++j) {
			if (entity.materials[j].name.equalsIgnoreCase(materialName)) {
				duplicate = true;
				break;
			}
		}
		if (duplicate)
			continue;

		const NamedMaterial *material = sourceScene->findMaterial(materialName);
		if (!material)
			material = _activeScene.findMaterial(materialName);
		if (!material) {
			for (uint32 j = 0; j < _dynamicSceneEntities.size() && !material; ++j)
				for (uint32 k = 0; k < _dynamicSceneEntities[j].materials.size(); ++k)
					if (_dynamicSceneEntities[j].materials[k].name.equalsIgnoreCase(materialName)) {
						material = &_dynamicSceneEntities[j].materials[k];
						break;
					}
		}
		if (material)
			entity.materials.push_back(*material);
	}

	_dynamicSceneEntities.push_back(entity);
	debug(1, "Zero Comico: cloned scene entity %s -> %s",
	      sourceName.c_str(), cloneName.c_str());
	return true;
}

void ZeroComicoEngine::installDynamicBackgroundForRoom(const Common::String &roomName) {
	for (uint32 i = 0; i < _dynamicSceneEntities.size(); ++i) {
		DynamicSceneEntity &entity = _dynamicSceneEntities[i];
		if (!entity.roomName.equalsIgnoreCase(roomName))
			continue;

		for (uint32 m = 0; m < entity.materials.size(); ++m)
			if (!_activeScene.findMaterial(entity.materials[m].name))
				_activeScene.materials.push_back(entity.materials[m]);

		NamedMesh *existing = _activeScene.findMesh(entity.name);
		if (existing)
			*existing = entity.mesh;
		else
			_activeScene.meshes.push_back(entity.mesh);
	}
}

bool ZeroComicoEngine::setSceneEntityTranslation(const Common::String &name,
                                                  const Vec3f &position) {
	bool changed = false;
	DynamicSceneEntity *dynamic = findDynamicSceneEntity(name);
	if (dynamic) {
		translateRigidMesh(dynamic->mesh, position);
		changed = true;
	}

	NamedMesh *active = _activeScene.findMesh(name);
	if (active) {
		translateRigidMesh(*active, position);
		changed = true;
	}
	return changed;
}

bool ZeroComicoEngine::setSceneEntityVectorTransform(const Common::String &name,
                                                      const ShapeMarker &marker) {
	Vec3f direction = subtractVec3(marker.b, marker.a);
	direction.y = 0.0f;
	if (!normalizeVec3(direction))
		return false;

	const float yaw = std::atan2(direction.x, -direction.z);
	const float cs = std::cos(yaw);
	const float sn = std::sin(yaw);

	bool changed = false;
	DynamicSceneEntity *dynamic = findDynamicSceneEntity(name);
	NamedMesh *targets[2] = {
		dynamic ? &dynamic->mesh : nullptr,
		_activeScene.findMesh(name)
	};

	for (uint32 targetIndex = 0; targetIndex < ARRAYSIZE(targets); ++targetIndex) {
		NamedMesh *mesh = targets[targetIndex];
		if (!mesh)
			continue;
		if (targetIndex == 1 && targets[0] == targets[1])
			continue;

		translateRigidMesh(*mesh, marker.a);
		mesh->data.transform.matrix[0] = cs;
		mesh->data.transform.matrix[1] = 0.0f;
		mesh->data.transform.matrix[2] = -sn;
		mesh->data.transform.matrix[3] = 0.0f;
		mesh->data.transform.matrix[4] = 1.0f;
		mesh->data.transform.matrix[5] = 0.0f;
		mesh->data.transform.matrix[6] = sn;
		mesh->data.transform.matrix[7] = 0.0f;
		mesh->data.transform.matrix[8] = cs;
		changed = true;
	}
	return changed;
}

CpuCharacterRuntime *ZeroComicoEngine::findCpuCharacter(const Common::String &name) {
	for (uint32 i = 0; i < _cpuCharacters.size(); ++i)
		if (_cpuCharacters[i].name.equalsIgnoreCase(name))
			return &_cpuCharacters[i];
	return nullptr;
}

void ZeroComicoEngine::installCpuCharactersForRoom(const Common::String &roomName) {
	for (uint32 i = 0; i < _cpuCharacters.size(); ++i) {
		CpuCharacterRuntime &character = _cpuCharacters[i];
		if (!character.alive || !character.roomName.equalsIgnoreCase(roomName))
			continue;

		if (!character.positioned) {
			if (!character.initialEntity.empty()) {
				const NamedMesh *spawn = _activeScene.findMesh(character.initialEntity);
				if (spawn) {
					character.position = spawn->data.transform.translation;
					character.positioned = true;

					// SetCharPos_Entity spawn meshes carry the actor orientation in the
					// retail 3x3 transform. Local -Z is the character's forward vector.
					Vec3f facing = {
						-spawn->data.transform.matrix[2],
						0.0f,
						-spawn->data.transform.matrix[8]
					};
					if (normalizeVec3(facing)) {
						character.facing = facing;
						character.haveFacing = true;
					}
				}
			} else if (!character.initialVector.empty()) {
				const ShapeMarker *marker = _activeShapes.find(character.initialVector);
				if (marker) {
					character.position = marker->a;
					character.positioned = true;
					Vec3f facing = subtractVec3(marker->b, marker->a);
					facing.y = 0.0f;
					if (normalizeVec3(facing)) {
						character.facing = facing;
						character.haveFacing = true;
					}
				}
			}
		}
	}
}
bool ZeroComicoEngine::instantiateCpuCharacter(const Common::String &name) {
	if (findCpuCharacter(name))
		return true;

	const CharacterDefinition *definition = _playerCharacterScript.findCharacter(name);
	if (!definition || !definition->cpuPlayer || definition->initialBodyName.empty())
		return false;

	Common::String assetStem = definition->initialBodyName;
	const uint32 separator = assetStem.find('_');
	if (separator != Common::String::npos && separator + 1 < assetStem.size())
		assetStem = assetStem.substr(separator + 1);

	Common::String lowerStem = assetStem;
	lowerStem.toLowercase();

	Common::Array<Common::Path> directories;
	directories.push_back(
		Common::Path(_currentMainPlace + "/bodies").appendComponent(lowerStem));
	directories.push_back(Common::Path("Mpx/bodies").appendComponent(lowerStem));
	directories.push_back(Common::Path("Mpx/bodies").appendComponent(assetStem));
	directories.push_back(Common::Path("Mpx/bodies").appendComponent(name));

	Common::Array<Common::String> fileStems;
	fileStems.push_back(assetStem);
	if (!lowerStem.equalsIgnoreCase(assetStem) || lowerStem != assetStem)
		fileStems.push_back(lowerStem);
	if (!name.equalsIgnoreCase(assetStem))
		fileStems.push_back(name);

	SceneModel body;
	Common::Path bodyDirectory;
	bool loaded = false;
	for (uint32 directoryIndex = 0; directoryIndex < directories.size() && !loaded; ++directoryIndex) {
		for (uint32 stemIndex = 0; stemIndex < fileStems.size() && !loaded; ++stemIndex) {
			loaded = body.loadPair(
				directories[directoryIndex].appendComponent(fileStems[stemIndex] + ".p3d"),
				directories[directoryIndex].appendComponent(fileStems[stemIndex] + ".anj"));
			if (loaded)
				bodyDirectory = directories[directoryIndex];
		}
	}
	if (!loaded) {
		warning("Zero Comico: cannot load CPU body %s for %s",
		        definition->initialBodyName.c_str(), name.c_str());
		return false;
	}

	body.resolveSkinnedGeometry();

	CpuCharacterRuntime runtime;
	runtime.name = definition->name;
	runtime.roomName = definition->roomName;
	runtime.bodyRoot = definition->initialBodyName;
	runtime.initialEntity = definition->initialEntity;
	runtime.initialVector = definition->initialVector;
	runtime.assetDirectory = bodyDirectory;
	runtime.scene = body;
	runtime.position.x = runtime.position.y = runtime.position.z = 0.0f;
	runtime.facing.x = 0.0f;
	runtime.facing.y = 0.0f;
	runtime.facing.z = -1.0f;
	runtime.alive = true;
	runtime.lifeBroken =
		definition->breakLifeOnInitialize ||
		containsIgnoreCase(_deferredBrokenCpuCharacters, definition->name);
	runtime.positioned = false;
	runtime.haveFacing = false;
	runtime.waitState = runtime.lifeBroken ? 0x40 : 0;
	runtime.idleAnimationStartMillis = _system->getMillis();
	runtime.lastEventSource.clear();
	runtime.lastEventFrame = -1;
	runtime.haveEventFrame = false;
	_cpuCharacters.push_back(runtime);

	CpuCharacterRuntime *created = findCpuCharacter(definition->name);
	if (!created)
		return false;

	if (definition->initializeStart != 0xffffffffU &&
	    definition->initializeEnd != 0xffffffffU &&
	    definition->initializeStart < definition->initializeEnd) {
		if (!runScriptWithAudioClass(_playerCharacterScript.program(),
		                             definition->initializeStart, definition->initializeEnd,
		                             128, 3)) {
			warning("Zero Comico: CPU initialize block for %s stopped early",
			        definition->name.c_str());
		}
	}

	debug(1, "Zero Comico: instantiated CPU %s using %s in %s%s",
	      created->name.c_str(), created->bodyRoot.c_str(), created->roomName.c_str(),
	      created->lifeBroken ? " (life broken)" : "");
	return true;
}

bool ZeroComicoEngine::giveLifeToCharacter(const Common::String &name) {
	if (name.equalsIgnoreCase("MainPlayer") ||
	    name.equalsIgnoreCase(_playerCharacterScript.playerName))
		return true;

	if (!instantiateCpuCharacter(name)) {
		warning("Zero Comico: GiveLifeToChar cannot resolve CPU character %s",
		        name.c_str());
		return false;
	}

	CpuCharacterRuntime *character = findCpuCharacter(name);
	if (!character)
		return false;

	removeIgnoreCase(_deferredBrokenCpuCharacters, name);
	character->alive = true;
	character->lifeBroken = false;
	character->waitState = 0;
	if (_activeRoomName.equalsIgnoreCase(character->roomName))
		installCpuCharactersForRoom(character->roomName);

	debug(1, "Zero Comico: GiveLifeToChar activated %s", character->name.c_str());
	return true;
}

void ZeroComicoEngine::ensureCpuCharactersForRoom(const Common::String &roomName) {
	for (uint32 i = 0; i < _playerCharacterScript.characters.size(); ++i) {
		const CharacterDefinition &definition = _playerCharacterScript.characters[i];
		if (!definition.cpuPlayer ||
		    !definition.roomName.equalsIgnoreCase(roomName) ||
		    definition.initialBodyName.empty() ||
		    (definition.initialEntity.empty() && definition.initialVector.empty()))
			continue;
		if (!findCpuCharacter(definition.name) && !instantiateCpuCharacter(definition.name))
			warning("Zero Comico: could not instantiate CPU %s for %s",
			        definition.name.c_str(), roomName.c_str());
	}
	installCpuCharactersForRoom(roomName);
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

	if (op.equalsIgnoreCase("ResetMaterialFilm")) {
		if (instruction.args.empty())
			return false;

		// The retail callback resolves the named material-film object and resets
		// its animation cursor. Our synchronous cutscenes load a fresh P3D/ANJ
		// scene for every play_cut/play_open_cut, so the two shipped Mp4 uses
		// already start from pristine material state. Accepting the opcode here
		// preserves that equivalent behavior instead of aborting the script.
		debug(1, "Zero Comico: material film %s reset by fresh cutscene load",
		      instruction.args[0].c_str());
		return true;
	}

	if (op.equalsIgnoreCase("play_cut") || op.equalsIgnoreCase("play_open_cut")) {
		if (instruction.args.empty())
			return false;
		return playCutscene(instruction.args[0], op.equalsIgnoreCase("play_open_cut"));
	}

	// Ordinary play_cut is synchronous. The retail water puzzle additionally
	// uses loop_cut/run_cut/stop_cut as a persistent 25 fps cutscene object.
	if (op.equalsIgnoreCase("wait_cut"))
		return true;

	if (op.equalsIgnoreCase("loop_cut")) {
		if (instruction.args.empty())
			return false;
		return startLoopCutscene(instruction.args[0]);
	}

	if (op.equalsIgnoreCase("run_cut")) {
		if (instruction.args.empty())
			return false;
		return renderLoopCutsceneFrame(instruction.args[0]);
	}

	if (op.equalsIgnoreCase("stop_cut")) {
		if (instruction.args.empty())
			return false;
		return stopLoopCutscene(instruction.args[0]);
	}

	if (op.equalsIgnoreCase("play")) {
		if (instruction.args.size() < 2)
			return false;

		for (uint32 i = 0; i < _sceneOneShotTargets.size(); ++i) {
			if (_sceneOneShotTargets[i].equalsIgnoreCase(instruction.args[0])) {
				_sceneOneShotSources[i] = instruction.args[1];
				_sceneOneShotStartMillis[i] = _system->getMillis();
				return true;
			}
		}

		_sceneOneShotTargets.push_back(instruction.args[0]);
		_sceneOneShotSources.push_back(instruction.args[1]);
		_sceneOneShotStartMillis.push_back(_system->getMillis());
		return true;
	}

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
		    instruction.args[1].equalsIgnoreCase("gio_cappello")) {
			const CharacterAnimSet *activeAnimSet =
				_playerCharacterScript.findAnimSet(_playerAnimSetName);
			const Common::String &bodyName = activeAnimSet
				? activeAnimSet->bodyName : _playerCharacterScript.initialBodyName;
			if (bodyName.equalsIgnoreCase("gio_giovanni"))
				_playerHatVisible = op.equalsIgnoreCase("unhide_subobj");
		}
		return true;
	}

	if (op.equalsIgnoreCase("SetCharPos_Entity")) {
		if (instruction.args.size() < 2)
			return false;

		CpuCharacterRuntime *character = findCpuCharacter(instruction.args[0]);
		if (!character)
			return false;
		const NamedMesh *spawn = _activeScene.findMesh(instruction.args[1]);
		if (!spawn)
			return false;

		character->position = spawn->data.transform.translation;
		character->positioned = true;
		character->initialEntity = instruction.args[1];

		Vec3f facing = {
			-spawn->data.transform.matrix[2],
			0.0f,
			-spawn->data.transform.matrix[8]
		};
		if (normalizeVec3(facing)) {
			character->facing = facing;
			character->haveFacing = true;
		}
		return true;
	}

	if (op.equalsIgnoreCase("SetCharPos_Vector")) {
		if (instruction.args.size() < 2)
			return false;

		const ShapeMarker *marker = _activeShapes.find(instruction.args[1]);
		if (!marker)
			return false;

		const Common::String &characterName = instruction.args[0];
		const bool targetsPlayer =
			characterName.equalsIgnoreCase("MainPlayer") ||
			(!_playerCharacterScript.playerName.empty() &&
			 characterName.equalsIgnoreCase(_playerCharacterScript.playerName));
		if (targetsPlayer) {
			_playerPosition = marker->a;
			_playerFacingTarget = marker->b;
			_havePlayerStart = true;
			_playerNavNode = -1;
			return true;
		}

		CpuCharacterRuntime *character = findCpuCharacter(characterName);
		if (!character)
			return false;

		character->position = marker->a;
		character->positioned = true;
		character->initialVector = instruction.args[1];
		Vec3f facing = subtractVec3(marker->b, marker->a);
		facing.y = 0.0f;
		if (normalizeVec3(facing)) {
			character->facing = facing;
			character->haveFacing = true;
		}
		return true;
	}

	if (op.equalsIgnoreCase("chplace")) {
		if (instruction.args.empty())
			return false;
		_pendingRoomName = instruction.args[0];
		_pendingRoomCutscene.clear();
		_pendingRoomMapRoomName.clear();
		_pendingRoomMapName.clear();
		if (instruction.args.size() >= 2)
			_pendingRoomCutscene = instruction.args[1];
		return true;
	}

	if (op.equalsIgnoreCase("setmap")) {
		if (instruction.args.size() < 2 || _currentMainPlace.empty())
			return false;

		const Common::String &targetRoom = instruction.args[0];
		const Common::String &requestedMap = instruction.args[1];

		// Retail scripts commonly issue SetPlace <room> followed immediately by
		// SetMap <same room> <map>. The destination room is not active yet, so
		// defer that map selection until the room transition is committed.
		if (!targetRoom.equalsIgnoreCase(_activeRoomName)) {
			_pendingRoomMapRoomName = targetRoom;
			_pendingRoomMapName = requestedMap;
			debug(1, "Zero Comico: queued map %s for destination room %s",
			      requestedMap.c_str(), targetRoom.c_str());
			return true;
		}

		if (!_activeRoomMaps.empty() && !containsIgnoreCase(_activeRoomMaps, requestedMap)) {
			warning("Zero Comico: map %s is not declared for room %s",
			        requestedMap.c_str(), _activeRoomName.c_str());
			return false;
		}

		const Common::Path gameplayDirectory(_currentMainPlace + "/gameplay");
		const Common::Path mapPath = gameplayDirectory.appendComponent(requestedMap);
		BspMap replacement;
		if (!replacement.load(mapPath))
			return false;

		// SetMap changes only the character-navigation map. The room-declared
		// camera map remains active until a room transition replaces it.
		_activeWalkMap = replacement;
		_activeWalkMapName = requestedMap;
		_playerNavNode = _activeWalkMap.graph.empty()
			? -1 : _activeWalkMap.nearestGraphNode(_playerPosition.x, _playerPosition.z);
		debug(1, "Zero Comico: switched walk map to %s at node %d",
		      requestedMap.c_str(), _playerNavNode);
		return true;
	}

	if (op.equalsIgnoreCase("ChangeMainplace")) {
		if (instruction.args.empty())
			return false;
		_pendingMainPlace = instruction.args[0];
		_pendingRoomName.clear();
		_pendingRoomCutscene.clear();
		_pendingRoomMapRoomName.clear();
		_pendingRoomMapName.clear();
		debug(1, "Zero Comico: requested main-place transition to %s",
		      _pendingMainPlace.c_str());
		return true;
	}

	if (op.equalsIgnoreCase("SetPlace")) {
		if (instruction.args.empty())
			return false;

		// Setting the already active place is an in-place state assertion in the
		// shipped scripts, not a request to reload its P3D/BSP data. Mp2 does this
		// twice during bootstrap, including after permuting the 25 tube meshes. A
		// deferred same-room reload would later replace those live transforms with
		// the pristine Setp snapshot and desynchronize the physical puzzle from its
		// Tubi_IN array.
		if (instruction.args[0].equalsIgnoreCase(_activeRoomName)) {
			_pendingRoomName.clear();
			_pendingRoomCutscene.clear();
			_pendingRoomMapRoomName.clear();
			_pendingRoomMapName.clear();

			// A same-room SetPlace often follows SetCharPos_Vector. The latter
			// deliberately invalidates the navigation node because the character
			// has teleported. Since we keep the room/BSP in place here, resnap the
			// new position to that existing graph instead of leaving gameplay with
			// _playerNavNode == -1. Mp2's Fachiro/c241 sequence depends on this.
			if (_playerNavNode < 0 && !_activeWalkMap.graph.empty())
				_playerNavNode = _activeWalkMap.nearestGraphNode(
					_playerPosition.x, _playerPosition.z);

			debug(2, "Zero Comico: SetPlace %s keeps the current room in place at nav node %d",
			      _activeRoomName.c_str(), _playerNavNode);
			return true;
		}

		_pendingRoomName = instruction.args[0];
		_pendingRoomCutscene.clear();
		_pendingRoomMapRoomName.clear();
		_pendingRoomMapName.clear();
		return true;
	}

	if (op.equalsIgnoreCase("CSetPlace")) {
		// Character-specific SetPlace. The retail runtime uses this during the
		// Mp5 bootstrap as "CSetPlace Mainplayer Room5_1".
		if (instruction.args.size() < 2)
			return false;
		if (instruction.args[1].equalsIgnoreCase(_activeRoomName)) {
			_pendingRoomName.clear();
			_pendingRoomCutscene.clear();
			_pendingRoomMapRoomName.clear();
			_pendingRoomMapName.clear();
			return true;
		}
		_pendingRoomName = instruction.args[1];
		_pendingRoomCutscene.clear();
		_pendingRoomMapRoomName.clear();
		_pendingRoomMapName.clear();
		return true;
	}

	// Scene-construction commands. CloneEntity's retail callback is empty
	// because its typed entity argument resolver performs the clone before the
	// callback fires. Our VM has no such binder, so reproduce that side effect
	// explicitly here.
	if (op.equalsIgnoreCase("CloneEntity")) {
		if (instruction.args.size() < 2)
			return false;
		return cloneSceneEntity(instruction.args[0], instruction.args[1]);
	}

	if (op.equalsIgnoreCase("SetEntityPos_Vector")) {
		if (instruction.args.size() < 2)
			return false;
		const ShapeMarker *marker = _activeShapes.find(instruction.args[1]);
		if (!marker)
			return false;
		if (!setSceneEntityVectorTransform(instruction.args[0], *marker)) {
			warning("Zero Comico: SetEntityPos_Vector cannot resolve %s",
			        instruction.args[0].c_str());
			return false;
		}
		return true;
	}

	if (op.equalsIgnoreCase("InsertInBackground")) {
		if (instruction.args.size() < 2)
			return false;
		DynamicSceneEntity *entity = findDynamicSceneEntity(instruction.args[1]);
		if (!entity) {
			// Existing room entities can also be inserted/reinserted by retail
			// scripts; their base room scene already owns them.
			return _activeScene.findMesh(instruction.args[1]) != nullptr;
		}
		entity->roomName = instruction.args[0];
		if (_activeRoomName.equalsIgnoreCase(entity->roomName))
			installDynamicBackgroundForRoom(_activeRoomName);
		return true;
	}

	if (op.equalsIgnoreCase("setpos_x") ||
	    op.equalsIgnoreCase("setpos_y") ||
	    op.equalsIgnoreCase("setpos_z")) {
		if (instruction.args.size() < 2)
			return false;
		float value = 0.0f;
		int32 integerValue = 0;
		if (_scriptVM.resolveValue(instruction.args[1], integerValue))
			value = (float)integerValue;
		else if (!parseScriptFloat(instruction.args[1], value))
			return false;

		NamedMesh *active = _activeScene.findMesh(instruction.args[0]);
		DynamicSceneEntity *dynamic = findDynamicSceneEntity(instruction.args[0]);
		if (!active && !dynamic) {
			if (!_activeScene.hasHierarchy(instruction.args[0])) {
				debug(2, "Zero Comico: %s target %s is not a scene entity",
				      op.c_str(), instruction.args[0].c_str());
				return true;
			}

			int controllerIndex = -1;
			for (uint32 i = 0; i < _setpControllerNames.size(); ++i)
				if (_setpControllerNames[i].equalsIgnoreCase(instruction.args[0])) {
					controllerIndex = (int)i;
					break;
				}
			if (controllerIndex < 0) {
				_setpControllerNames.push_back(instruction.args[0]);
				Vec3f origin = { 0.0f, 0.0f, 0.0f };
				_setpControllerPositions.push_back(origin);
				controllerIndex = (int)_setpControllerPositions.size() - 1;
			}

			Vec3f previous = _setpControllerPositions[(uint32)controllerIndex];
			Vec3f next = previous;
			if (op.equalsIgnoreCase("setpos_x"))
				next.x = value;
			else if (op.equalsIgnoreCase("setpos_y"))
				next.y = value;
			else
				next.z = value;
			Vec3f delta = {
				next.x - previous.x,
				next.y - previous.y,
				next.z - previous.z
			};
			if (!_activeScene.translateHierarchy(instruction.args[0], delta))
				return false;
			_setpControllerPositions[(uint32)controllerIndex] = next;
			return true;
		}

		Vec3f position = active ? active->data.transform.translation
		                        : dynamic->mesh.data.transform.translation;
		if (op.equalsIgnoreCase("setpos_x"))
			position.x = value;
		else if (op.equalsIgnoreCase("setpos_y"))
			position.y = value;
		else
			position.z = value;
		return setSceneEntityTranslation(instruction.args[0], position);
	}

	if (op.equalsIgnoreCase("Setpos_z_by_index")) {
		if (instruction.args.size() < 3)
			return false;
		int32 index = 0;
		if (!_scriptVM.resolveValue(instruction.args[1], index))
			return false;
		float z = 0.0f;
		int32 integerValue = 0;
		if (_scriptVM.resolveValue(instruction.args[2], integerValue))
			z = (float)integerValue;
		else if (!parseScriptFloat(instruction.args[2], z))
			return false;

		const Common::String entityName = indexedSceneEntityName(instruction.args[0], (int)index);
		NamedMesh *mesh = _activeScene.findMesh(entityName);
		const DynamicSceneEntity *dynamic = findDynamicSceneEntity(entityName);
		if (!mesh && !dynamic)
			return false;
		Vec3f position = mesh ? mesh->data.transform.translation
		                    : dynamic->mesh.data.transform.translation;
		position.z = z;
		return setSceneEntityTranslation(entityName, position);
	}

	if (op.equalsIgnoreCase("hide_by_index")) {
		if (instruction.args.size() < 3)
			return false;
		int32 index = 0;
		int32 hidden = 0;
		if (!_scriptVM.resolveValue(instruction.args[1], index) ||
		    !_scriptVM.resolveValue(instruction.args[2], hidden))
			return false;

		const Common::String entityName = indexedSceneEntityName(instruction.args[0], (int)index);
		if (!_activeScene.findMesh(entityName))
			return false;
		if (hidden != 0) {
			if (!containsIgnoreCase(_hiddenSceneMeshes, entityName))
				_hiddenSceneMeshes.push_back(entityName);
		} else {
			removeIgnoreCase(_hiddenSceneMeshes, entityName);
		}
		return true;
	}

	if (op.equalsIgnoreCase("swap_entity_pos_byindex")) {
		if (instruction.args.size() < 3)
			return false;
		int32 firstIndex = 0;
		int32 secondIndex = 0;
		if (!_scriptVM.resolveValue(instruction.args[1], firstIndex) ||
		    !_scriptVM.resolveValue(instruction.args[2], secondIndex))
			return false;

		const Common::String firstName =
			indexedSceneEntityName(instruction.args[0], (int)firstIndex);
		const Common::String secondName =
			indexedSceneEntityName(instruction.args[0], (int)secondIndex);
		NamedMesh *first = _activeScene.findMesh(firstName);
		NamedMesh *second = _activeScene.findMesh(secondName);
		if (!first || !second)
			return false;

		const Vec3f firstPosition = first->data.transform.translation;
		const Vec3f secondPosition = second->data.transform.translation;
		return setSceneEntityTranslation(firstName, secondPosition) &&
		       setSceneEntityTranslation(secondName, firstPosition);
	}

	if (op.equalsIgnoreCase("setpos_on_entity_byindex")) {
		if (instruction.args.size() < 3)
			return false;
		int32 index = 0;
		if (!_scriptVM.resolveValue(instruction.args[2], index))
			return false;

		const Common::String sourceName =
			indexedSceneEntityName(instruction.args[1], (int)index);
		NamedMesh *target = _activeScene.findMesh(instruction.args[0]);
		const NamedMesh *source = _activeScene.findMesh(sourceName);
		if (!target || !source)
			return false;

		return setSceneEntityTranslation(
			instruction.args[0], source->data.transform.translation);
	}

	if (op.equalsIgnoreCase("Setp")) {
		if (instruction.args.size() < 2 || _currentMainPlace.empty())
			return false;

		const Common::String &entityName = instruction.args[0];
		const Common::String &assetStem = instruction.args[1];
		if (!containsIgnoreCase(_loadedSetpAssets, assetStem)) {
			const Common::Path assetDirectory(_currentMainPlace + "/backgrd");
			SceneModel asset;
			if (!asset.loadPair(assetDirectory.appendComponent(assetStem + ".p3d"),
			                    assetDirectory.appendComponent(assetStem + ".anj"))) {
				warning("Zero Comico: Setp cannot load asset %s for %s",
				        assetStem.c_str(), entityName.c_str());
				return false;
			}
			_activeScene.mergeFrom(asset);
			_loadedSetpAssets.push_back(assetStem);
			_loadedSetpScenes.push_back(asset);
			debug(1, "Zero Comico: Setp merged asset %s (%u meshes, %u clips)",
			      assetStem.c_str(), (uint)asset.meshes.size(), (uint)asset.clips.size());
		}

		// A Setp target can be a hierarchy/controller rather than a visible mesh
		// (r23_dummyossa in acqua.anj is one such retail example). Loading the
		// pair is therefore the meaningful success condition.
		return true;
	}

	if (op.equalsIgnoreCase("GiveLifeToChar")) {
		if (instruction.args.empty())
			return false;
		return giveLifeToCharacter(instruction.args[0]);
	}

	if (op.equalsIgnoreCase("dcue_all")) {
		if (instruction.args.size() < 3)
			return false;

		float enabled = 0.0f;
		float startMetres = 0.0f;
		float endMetres = 0.0f;
		if (!parseScriptFloat(instruction.args[0], enabled) ||
		    !parseScriptFloat(instruction.args[1], startMetres) ||
		    !parseScriptFloat(instruction.args[2], endMetres))
			return false;

		_depthCueEnabled = enabled != 0.0f;
		_depthCueStart = startMetres * 100.0f;
		_depthCueEnd = endMetres * 100.0f;
		debug(1, "Zero Comico: depth cue %s from %.1f to %.1f world units",
		      _depthCueEnabled ? "enabled" : "disabled",
		      _depthCueStart, _depthCueEnd);
		return true;
	}

	if (op.equalsIgnoreCase("portals_off")) {
		_portalsEnabled = false;
		debug(1, "Zero Comico: portal traversal disabled");
		return true;
	}

	if (op.equalsIgnoreCase("portals_on")) {
		_portalsEnabled = true;
		debug(1, "Zero Comico: portal traversal enabled");
		return true;
	}

	if (op.equalsIgnoreCase("SetDialogCameras")) {
		if (instruction.args.size() < 2)
			return false;
		_dialogCameraFirstName = instruction.args[0];
		_dialogCameraSecondName = instruction.args[1];
		debug(1, "Zero Comico: dialogue cameras %s / %s",
		      _dialogCameraFirstName.c_str(), _dialogCameraSecondName.c_str());
		return true;
	}

	if (op.equalsIgnoreCase("SetFocus") || op.equalsIgnoreCase("SetCamera")) {
		if (instruction.args.empty())
			return false;
		_pendingCameraName = instruction.args[0];
		debug(1, "Zero Comico: script requested camera %s",
		      _pendingCameraName.c_str());
		return true;
	}

	if (op.equalsIgnoreCase("ResetCamera")) {
		if (_defaultRoomCameraName.empty())
			return false;
		_pendingCameraName = _defaultRoomCameraName;
		debug(1, "Zero Comico: script reset camera to room default %s",
		      _defaultRoomCameraName.c_str());
		return true;
	}

	if (op.equalsIgnoreCase("say")) {
		if (instruction.args.empty())
			return false;
		const Common::String speaker = _playerCharacterScript.playerName.empty()
			? Common::String("MainPlayer") : _playerCharacterScript.playerName;
		return showScriptLine(speaker, instruction.args[0]);
	}

	if (op.equalsIgnoreCase("Sayn")) {
		if (instruction.args.size() < 2)
			return false;

		int32 value = 0;
		if (!_scriptVM.resolveValue(instruction.args[1], value))
			return false;

		Common::String text = instruction.args[0];
		const uint32 marker = text.find("%d");
		if (marker != Common::String::npos) {
			text = text.substr(0, marker) +
			       Common::String::format("%d", (int)value) +
			       text.substr(marker + 2);
		}

		const Common::String speaker = _playerCharacterScript.playerName.empty()
			? Common::String("MainPlayer") : _playerCharacterScript.playerName;
		return showScriptLine(speaker, text);
	}

	if (op.equalsIgnoreCase("start_dialog")) {
		if (instruction.args.size() < 2)
			return false;

		const Common::String &dialogName = instruction.args[1];
		if (_scriptDialogueContextActive && _scriptDialogueFrame) {
			_pendingDialogName.clear();
			_lastDialogueChoice = -1;
			return playDialogue(dialogName, _scriptDialogueCamera,
			                    _scriptDialogueSceneDirectory,
			                    _scriptDialoguePlayerDirectory,
			                    *_scriptDialogueFrame);
		}

		_pendingDialogName = dialogName;
		return true;
	}

	if (op.equalsIgnoreCase("wait_last_dialog"))
		return true;

	if (op.equalsIgnoreCase("take")) {
		if (instruction.args.size() < 2)
			return false;

		_pendingTakeAnimation.clear();
		_pendingTakeInventoryObject = instruction.args[1];
		_pendingTakeEventFrame = 0;
		_pendingTakeActive = false;
		_pendingTakeInventoryAdded = false;

		const PuzzleObject *pickupObject = _activePuzzle.findByEntity(instruction.args[0]);
		if (!pickupObject)
			pickupObject = _activePuzzle.findObject(instruction.args[0]);
		const CharacterAnimSet *animSet = _playerCharacterScript.findAnimSet(_playerAnimSetName);

		if (pickupObject && animSet) {
			switch (pickupObject->takeMode) {
			case kPuzzleTakeLow:
				_pendingTakeAnimation = animSet->takeLowAnimation;
				_pendingTakeEventFrame = animSet->takeLowEventFrame;
				break;
			case kPuzzleTakeMid:
				_pendingTakeAnimation = animSet->takeMidAnimation;
				_pendingTakeEventFrame = animSet->takeMidEventFrame;
				break;
			case kPuzzleTakeHigh:
				_pendingTakeAnimation = animSet->takeHighAnimation;
				_pendingTakeEventFrame = animSet->takeHighEventFrame;
				break;
			case kPuzzleTakeNone:
			default:
				break;
			}
		}

		if (!_pendingTakeAnimation.empty()) {
			_pendingTakeActive = true;
			debug(1, "Zero Comico: pickup %s uses %s, inventory event frame %d",
			      instruction.args[0].c_str(), _pendingTakeAnimation.c_str(),
			      (int)_pendingTakeEventFrame);
		} else {
			if (!containsIgnoreCase(_inventoryObjects, _pendingTakeInventoryObject))
				_inventoryObjects.push_back(_pendingTakeInventoryObject);
			markInventoryObjectAssigned(_activePuzzle, _pendingTakeInventoryObject);
			_selectedInventoryObject = _pendingTakeInventoryObject;
			_pendingTakeInventoryAdded = true;
			debug(1, "Zero Comico: pickup %s has no animation class; inventory acquired immediately",
			      instruction.args[0].c_str());
		}
		return true;
	}

	if (op.equalsIgnoreCase("wait_take")) {
		if (!_pendingTakeActive)
			return true;

		auto acquirePendingTake = [&]() {
			if (_pendingTakeInventoryAdded || _pendingTakeInventoryObject.empty())
				return;
			if (!containsIgnoreCase(_inventoryObjects, _pendingTakeInventoryObject))
				_inventoryObjects.push_back(_pendingTakeInventoryObject);
			markInventoryObjectAssigned(_activePuzzle, _pendingTakeInventoryObject);
			_selectedInventoryObject = _pendingTakeInventoryObject;
			_pendingTakeInventoryAdded = true;
			debug(1, "Zero Comico: inventory acquired %s at pickup event frame %d",
			      _pendingTakeInventoryObject.c_str(), (int)_pendingTakeEventFrame);
		};

		const Common::String playerRoot = !_playerSequences.bodyName.empty()
			? _playerSequences.bodyName : _playerCharacterScript.initialBodyName;
		float startFrame = 0.0f;
		float endFrame = 0.0f;
		if (!_scriptDialogueContextActive || !_scriptDialogueFrame ||
		    !animationClipRange(_playerScene, playerRoot, _pendingTakeAnimation,
		                        startFrame, endFrame)) {
			warning("Zero Comico: cannot play pickup animation %s; completing inventory transfer",
			        _pendingTakeAnimation.c_str());
			acquirePendingTake();
			_pendingTakeActive = false;
			_pendingTakeAnimation.clear();
			_pendingTakeInventoryObject.clear();
			return true;
		}

		const CharacterAnimSet *activeAnimSet =
			_playerCharacterScript.findAnimSet(_playerAnimSetName);
		const CharacterDefinition *playerDefinition =
			_playerCharacterScript.findCharacter(_playerCharacterScript.playerName);

		const float frameRate = 25.0f;
		float frameValue = startFrame;
		int32 relativeFrame = 0;
		while (!shouldQuit() && frameValue <= endFrame) {
			if (!_pendingTakeInventoryAdded && relativeFrame >= _pendingTakeEventFrame)
				acquirePendingTake();

#ifdef USE_MAD
			if (activeAnimSet && playerDefinition) {
				for (uint32 eventIndex = 0; eventIndex < activeAnimSet->stepEvents.size(); ++eventIndex) {
					const CharacterStepEvent &stepEvent = activeAnimSet->stepEvents[eventIndex];
					if (!stepEvent.animation.equalsIgnoreCase(_pendingTakeAnimation) ||
					    stepEvent.frame != relativeFrame)
						continue;

					for (uint32 sampleIndex = 0; sampleIndex < playerDefinition->samples.size(); ++sampleIndex) {
						const CharacterSample &sample = playerDefinition->samples[sampleIndex];
						if (sample.id != stepEvent.sampleId)
							continue;
						playNamedMp3(_mixer, Audio::Mixer::kSFXSoundType, sample.fileName, nullptr,
						             retailChannelVolume(2, 100.0f, sample.fileName));
						debug(2, "Zero Comico: pickup step event %s frame %d -> sample %d (%s)",
						      _pendingTakeAnimation.c_str(), (int)relativeFrame,
						      (int)sample.id, sample.fileName.c_str());
						break;
					}
				}
			}
#endif

			if (!renderGameplayFrame(_scriptDialogueCamera, _scriptDialogueSceneDirectory,
			                         _scriptDialoguePlayerDirectory, _pendingTakeAnimation,
			                         frameValue, *_scriptDialogueFrame))
				break;

			Common::Event event;
			while (_system->getEventManager()->pollEvent(event)) {
				updateScriptKeyState(event);
				if (event.type == Common::EVENT_QUIT ||
				    event.type == Common::EVENT_RETURN_TO_LAUNCHER) {
					quitGame();
					break;
				}
			}
			if (shouldQuit())
				break;

			frameValue += 1.0f;
			++relativeFrame;
			_system->delayMillis((uint32)(1000.0f / frameRate));
		}

		if (!_pendingTakeInventoryAdded)
			acquirePendingTake();

		_pendingTakeActive = false;
		_pendingTakeAnimation.clear();
		_pendingTakeInventoryObject.clear();

		if (!shouldQuit()) {
			float stayFrame = 0.0f;
			float stayEnd = 0.0f;
			animationClipRange(_playerScene, playerRoot, "Stay", stayFrame, stayEnd);
			renderGameplayFrame(_scriptDialogueCamera, _scriptDialogueSceneDirectory,
			                    _scriptDialoguePlayerDirectory, "Stay", stayFrame,
			                    *_scriptDialogueFrame);
		}
		return !shouldQuit();
	}

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
		markInventoryObjectAssigned(_activePuzzle, inventoryObject);
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

		Common::Array<Common::String> stateTokens;
		splitE3dCommand(instruction.args[1], stateTokens);
		if (stateTokens.empty() || (stateTokens.size() & 1U) != 0)
			return false;

		for (uint32 i = 0; i < stateTokens.size(); i += 2) {
			const Common::String &property = stateTokens[i];
			const Common::String &valueToken = stateTokens[i + 1];

			// The retail object-state parser has deliberately loose boolean
			// semantics: only the literal FALSE (case-insensitive) and "0"
			// clear a flag; every other successfully tokenized value sets it.
			// This preserves the shipped Mp5 typo "enabled flase", which the
			// original executable therefore treats as true.
			const bool value =
				!valueToken.equalsIgnoreCase("FALSE") && valueToken != "0";

			if (property.equalsIgnoreCase("pickable"))
				object->pickable = value;
			else if (property.equalsIgnoreCase("examinable"))
				object->examinable = value;
			else if (property.equalsIgnoreCase("operated"))
				object->operated = value;
			else if (property.equalsIgnoreCase("examinated"))
				object->examinated = value;
			else if (property.equalsIgnoreCase("autocamera"))
				object->autoCamera = value;
			else if (property.equalsIgnoreCase("randompos"))
				object->randomPos = value;
			else if (property.equalsIgnoreCase("combined"))
				object->combined = value;
			else if (property.equalsIgnoreCase("assigned"))
				object->assigned = value;
			else if (property.equalsIgnoreCase("enabled"))
				object->enabled = value;
			else if (property.equalsIgnoreCase("inside"))
				object->inside = value;
			else if (property.equalsIgnoreCase("collision"))
				object->collision = value;
			else if (property.equalsIgnoreCase("soundstate"))
				object->soundState = value;
			else
				return false;
		}
		return true;
	}

	if (op.equalsIgnoreCase("GetLastChoisePos")) {
		if (instruction.args.empty())
			return false;
		return _scriptVM.setVariable(instruction.args[0], _lastDialogueChoice);
	}

	if (op.equalsIgnoreCase("SetChoise")) {
		if (instruction.args.size() < 4)
			return false;
		DialogDefinition *dialog = _activeDialog.findDialogMutable(instruction.args[1]);
		if (!dialog)
			return false;

		int32 choiceIndex = 0;
		int32 enabled = 0;
		if (!_scriptVM.resolveValue(instruction.args[2], choiceIndex) ||
		    !_scriptVM.resolveValue(instruction.args[3], enabled) ||
		    choiceIndex < 0 || (uint32)choiceIndex >= dialog->choices.size())
			return false;

		dialog->choices[(uint32)choiceIndex].enabled = enabled != 0;
		return true;
	}

	if (op.equalsIgnoreCase("ModifySentence")) {
		if (instruction.args.size() < 5)
			return false;

		int32 sentenceIndex = 0;
		int32 tableIndex = 0;
		if (!_scriptVM.resolveValue(instruction.args[2], sentenceIndex) ||
		    !_scriptVM.resolveValue(instruction.args[4], tableIndex) ||
		    sentenceIndex < 0)
			return false;

		DialogDefinition *dialog = _activeDialog.findDialogMutable(instruction.args[1]);
		const Common::String *replacement =
			_activeTextTables.findLine(instruction.args[3], tableIndex);
		if (!dialog || !replacement || (uint32)sentenceIndex >= dialog->lines.size())
			return false;

		dialog->lines[(uint32)sentenceIndex].text = *replacement;
		debug(1, "Zero Comico: dialogue %s sentence %d <- %s[%d]",
		      instruction.args[1].c_str(), sentenceIndex,
		      instruction.args[3].c_str(), tableIndex);
		return true;
	}

	if (op.equalsIgnoreCase("csay_FromTextable") ||
	    op.equalsIgnoreCase("csay_Textable")) {
		if (instruction.args.size() < 3)
			return false;

		int32 tableIndex = 0;
		if (!_scriptVM.resolveValue(instruction.args[2], tableIndex))
			return false;
		const Common::String *line =
			_activeTextTables.findLine(instruction.args[1], tableIndex);
		if (!line)
			return false;
		return showScriptLine(instruction.args[0], *line);
	}

	if (op.equalsIgnoreCase("SetWaitState")) {
		if (instruction.args.size() < 2)
			return false;

		CpuCharacterRuntime *character = findCpuCharacter(instruction.args[0]);
		if (!character)
			return false;

		int32 enabled = 0;
		if (!_scriptVM.resolveValue(instruction.args[1], enabled))
			return false;

		// Zero Comico.exe stores 0 for disabled and 0x1f for enabled at
		// character offset +0x150. Preserve the exact retail value even though
		// the CPU-character autonomous state machine is not active yet.
		character->waitState = enabled == 0 ? 0 : 0x1f;
		debug(1, "Zero Comico: %s wait state = %d",
		      character->name.c_str(), (int)character->waitState);
		return true;
	}

	if (op.equalsIgnoreCase("SetAnimSet")) {
		if (instruction.args.size() < 2)
			return false;

		const CharacterAnimSet *animSet =
			_playerCharacterScript.findAnimSet(instruction.args[1]);
		if (!animSet)
			return false;

		Common::String assetStem = animSet->bodyName;
		const uint32 separator = assetStem.find('_');
		if (separator != Common::String::npos && separator + 1 < assetStem.size())
			assetStem = assetStem.substr(separator + 1);

		Common::Path directory =
			Common::Path("Mpx/bodies").appendComponent(assetStem);
		SceneModel replacementScene;
		bool loaded = replacementScene.loadPair(
			directory.appendComponent(assetStem + ".p3d"),
			directory.appendComponent(assetStem + ".anj"));
		if (!loaded) {
			Common::String lowerFolder = assetStem;
			lowerFolder.toLowercase();
			directory = Common::Path("Mpx/bodies").appendComponent(lowerFolder);
			loaded = replacementScene.loadPair(
				directory.appendComponent(assetStem + ".p3d"),
				directory.appendComponent(assetStem + ".anj"));
		}
		if (!loaded)
			return false;

		SequenceScript replacementSequences;
		if (!replacementSequences.load(directory.appendComponent(assetStem + ".seq")))
			return false;

		_playerScene = replacementScene;
		_playerSequences = replacementSequences;
		_playerAssetDirectory = directory;
		_playerAnimSetName = animSet->name;
		_playerHatVisible = true;
		debug(1, "Zero Comico: switched MainPlayer animation set to %s (%s)",
		      instruction.args[1].c_str(), animSet->bodyName.c_str());
		return true;
	}

	if (op.equalsIgnoreCase("e3d_hide") || op.equalsIgnoreCase("e3d_unhide")) {
		if (instruction.args.empty())
			return false;
		const Common::String entity = resolveSceneEntity(
			_activeScene, instruction.args[0], _activeRoomPrefix);
		if (op.equalsIgnoreCase("e3d_hide")) {
			if (!containsIgnoreCase(_hiddenSceneMeshes, entity))
				_hiddenSceneMeshes.push_back(entity);
		} else {
			removeIgnoreCase(_hiddenSceneMeshes, entity);
		}
		return true;
	}

	if (op.equalsIgnoreCase("setY_Vector")) {
		if (instruction.args.size() < 2)
			return false;
		const ShapeMarker *marker = _activeShapes.find(instruction.args[1]);
		if (!marker)
			return false;
		_playerPosition.y = marker->a.y;
		return true;
	}

	if (op.equalsIgnoreCase("hide3d")) {
		_3dEnabled = false;
		return true;
	}
	if (op.equalsIgnoreCase("show3d")) {
		_3dEnabled = true;
		return true;
	}

	if (op.equalsIgnoreCase("set_entity_pos")) {
		if (instruction.args.size() < 4)
			return false;

		auto resolveCoordinate = [&](const Common::String &token, float &value) -> bool {
			int32 integerValue = 0;
			if (_scriptVM.resolveValue(token, integerValue)) {
				value = (float)integerValue;
				return true;
			}
			return parseScriptFloat(token, value);
		};

		Vec3f position;
		if (!resolveCoordinate(instruction.args[1], position.x) ||
		    !resolveCoordinate(instruction.args[2], position.y) ||
		    !resolveCoordinate(instruction.args[3], position.z))
			return false;

		for (uint32 cpuIndex = 0; cpuIndex < _cpuCharacters.size(); ++cpuIndex) {
			CpuCharacterRuntime &character = _cpuCharacters[cpuIndex];
			if (!character.name.equalsIgnoreCase(instruction.args[0]) &&
			    !character.bodyRoot.equalsIgnoreCase(instruction.args[0]))
				continue;
			character.position = position;
			character.positioned = true;
			return true;
		}

		return setSceneEntityTranslation(instruction.args[0], position);
	}

	if (op.equalsIgnoreCase("ms_smp_default")) {
		if (instruction.args.size() < 5)
			return false;

		float farVolume = 0.0f;
		float nearVolume = 0.0f;
		float minRangeMetres = 0.0f;
		float maxRangeMetres = 0.0f;
		int32 applyExisting = 0;
		if (!parseScriptFloat(instruction.args[0], farVolume) ||
		    !parseScriptFloat(instruction.args[1], nearVolume) ||
		    !parseScriptFloat(instruction.args[2], minRangeMetres) ||
		    !parseScriptFloat(instruction.args[3], maxRangeMetres) ||
		    !_scriptVM.resolveValue(instruction.args[4], applyExisting))
			return false;

		_sampleDefaultFarVolume = farVolume;
		_sampleDefaultNearVolume = nearVolume;
		_sampleDefaultMinRange = minRangeMetres * 100.0f;
		_sampleDefaultMaxRange = maxRangeMetres * 100.0f;
		debug(1, "Zero Comico: sample defaults far %.1f%% near %.1f%% range %.1f..%.1f%s",
		      _sampleDefaultFarVolume, _sampleDefaultNearVolume,
		      _sampleDefaultMinRange, _sampleDefaultMaxRange,
		      applyExisting != 0 ? " (apply-active requested)" : "");
		return true;
	}

	if (op.equalsIgnoreCase("smp_param")) {
		if (instruction.args.size() < 5)
			return false;

		SamplePlaybackParams params;
		params.name = instruction.args[0];
		float minRangeMetres = 0.0f;
		float maxRangeMetres = 0.0f;
		if (!parseScriptFloat(instruction.args[1], params.farVolume) ||
		    !parseScriptFloat(instruction.args[2], params.nearVolume) ||
		    !parseScriptFloat(instruction.args[3], minRangeMetres) ||
		    !parseScriptFloat(instruction.args[4], maxRangeMetres))
			return false;
		params.minRange = minRangeMetres * 100.0f;
		params.maxRange = maxRangeMetres * 100.0f;

		bool found = false;
		for (uint32 i = 0; i < _samplePlaybackParams.size(); ++i) {
			if (!_samplePlaybackParams[i].name.equalsIgnoreCase(params.name))
				continue;
			_samplePlaybackParams[i] = params;
			found = true;
			break;
		}
		if (!found)
			_samplePlaybackParams.push_back(params);

		debug(1, "Zero Comico: sample params %s far %.1f%% near %.1f%% range %.1f..%.1f",
		      params.name.c_str(), params.farVolume, params.nearVolume,
		      params.minRange, params.maxRange);
		return true;
	}

	if (op.equalsIgnoreCase("setglobalmastervolume")) {
		if (instruction.args.empty())
			return false;
		float volume = 0.0f;
		if (!parseScriptFloat(instruction.args[0], volume))
			return false;
		if (volume < 0.0f)
			volume = 0.0f;
		else if (volume > 100.0f)
			volume = 100.0f;
		_globalMasterVolume = volume;
		debug(1, "Zero Comico: retail global master volume %.1f%%", _globalMasterVolume);
		return true;
	}

	if (op.equalsIgnoreCase("setclassvolume")) {
		if (instruction.args.size() < 2)
			return false;
		int32 soundClass = -1;
		float volume = 0.0f;
		if (!_scriptVM.resolveValue(instruction.args[0], soundClass) ||
		    soundClass < 0 || soundClass >= 6 ||
		    !parseScriptFloat(instruction.args[1], volume))
			return false;
		if (volume < 0.0f)
			volume = 0.0f;
		else if (volume > 100.0f)
			volume = 100.0f;
		_soundClassVolumes[soundClass] = volume;
		debug(1, "Zero Comico: retail sound class %d volume %.1f%%",
		      (int)soundClass, volume);
		return true;
	}

	if (op.equalsIgnoreCase("PlaySample")) {
		if (instruction.args.empty())
			return false;
#ifdef USE_MAD
		return playNamedMp3(_mixer, Audio::Mixer::kSFXSoundType,
		                    instruction.args[0], nullptr,
		                    retailChannelVolume(_scriptAudioClass, 100.0f,
		                                        instruction.args[0]));
#else
		return true;
#endif
	}

	if (op.equalsIgnoreCase("wait_frames")) {
		if (instruction.args.empty())
			return false;
		int32 frames = 0;
		if (!_scriptVM.resolveValue(instruction.args[0], frames) || frames < 0)
			return false;

		const uint32 duration = (uint32)frames * 40U;
		const uint32 start = _system->getMillis();
		while (!shouldQuit() && _system->getMillis() - start < duration) {
			Common::Event event;
			while (_system->getEventManager()->pollEvent(event)) {
				updateScriptKeyState(event);
				if (event.type == Common::EVENT_QUIT ||
				    event.type == Common::EVENT_RETURN_TO_LAUNCHER) {
					quitGame();
					break;
				}
			}
			_system->delayMillis(10);
		}
		return !shouldQuit();
	}

	if (op.equalsIgnoreCase("SetEnvSound") || op.equalsIgnoreCase("set_envsound")) {
		if (instruction.args.empty())
			return false;
		const Common::String &soundName = instruction.args[0];
		const bool enable = !soundName.equalsIgnoreCase("none") &&
		                    !soundName.equalsIgnoreCase("off") &&
		                    soundName != "0";
		setEnvironmentSound(soundName, enable);
		return true;
	}

	if (op.equalsIgnoreCase("StopEnvSound") || op.equalsIgnoreCase("stop_envsound")) {
		setEnvironmentSound(_environmentSoundName, false);
		return true;
	}

	if (op.equalsIgnoreCase("envsound_state")) {
		if (instruction.args.size() < 3)
			return false;

		int32 state = 0;
		if (!_scriptVM.resolveValue(instruction.args[2], state))
			return false;

		const Common::String &roomName = instruction.args[0];
		const Common::String &soundName = instruction.args[1];
		bool found = false;
		for (uint32 i = 0; i < _environmentStateNames.size(); ++i) {
			if (_environmentStateRooms[i].equalsIgnoreCase(roomName) &&
			    _environmentStateNames[i].equalsIgnoreCase(soundName)) {
				_environmentStateEnabled[i] = state != 0;
				found = true;
				break;
			}
		}
		if (!found) {
			_environmentStateRooms.push_back(roomName);
			_environmentStateNames.push_back(soundName);
			_environmentStateEnabled.push_back(state != 0);
		}

		// Retail signature is Room, EnvSound-object, state. These are per-emitter
		// flags (for example Star00..Star17 in Mp2), not the chapter's global
		// ambient sound handle.
		debug(1, "Zero Comico: environment emitter %s/%s = %d",
		      roomName.c_str(), soundName.c_str(), state != 0 ? 1 : 0);
		updateRoomEnvironmentSounds();
		return true;
	}

	if (op.equalsIgnoreCase("wait_say") ||
	    op.equalsIgnoreCase("cwait_say"))
		return true;

	if (op.equalsIgnoreCase("BreakLifeToChar")) {
		if (instruction.args.empty())
			return false;

		CpuCharacterRuntime *character = findCpuCharacter(instruction.args[0]);
		if (!character) {
			if (!containsIgnoreCase(_deferredBrokenCpuCharacters, instruction.args[0]))
				_deferredBrokenCpuCharacters.push_back(instruction.args[0]);
			debug(2, "Zero Comico: BreakLifeToChar deferred for %s",
			      instruction.args[0].c_str());
			return true;
		}

		// Retail callback 0x42b7b8 unregisters the life controller, clears its
		// active flag and writes 0x40 to character offset +0x150. The rendered
		// body remains present.
		character->lifeBroken = true;
		character->waitState = 0x40;
		debug(1, "Zero Comico: life broken for %s (wait state 0x40)",
		      character->name.c_str());
		return true;
	}

	if (op.equalsIgnoreCase("SetNoCameraReset")) {
		if (instruction.args.size() < 2 ||
		    !instruction.args[0].equalsIgnoreCase("MainPlayer"))
			return false;

		int32 value = 0;
		if (!_scriptVM.resolveValue(instruction.args[1], value))
			return false;
		_playerNoCameraReset = value != 0;
		debug(1, "Zero Comico: MainPlayer no-camera-reset %s",
		      _playerNoCameraReset ? "enabled" : "disabled");
		return true;
	}

	if (op.equalsIgnoreCase("SetCameraMode") || op.equalsIgnoreCase("setcameramode")) {
		if (instruction.args.empty())
			return false;
		int32 requested = 0;
		if (!_scriptVM.resolveValue(instruction.args[0], requested) ||
		    requested < 1 || requested > 3)
			return false;

		// The original callback maps script values 1/2/3 to internal camera
		// modes 0/1/2: Placed, Subjective and Spot respectively.
		// LockCameraMode is a separate global flag in the retail executable; the
		// SetCameraMode callback does not test it, so script-driven changes remain
		// authoritative even while manual camera-mode controls are locked.
		_cameraMode = (int)requested - 1;
		_dynamicCameraInitialized = false;
		_activeAutoCameraTrigger.clear();
		debug(1, "Zero Comico: camera mode request %d -> %s%s",
		      (int)requested,
		      _cameraMode == 0 ? "Placed" : (_cameraMode == 1 ? "Subjective" : "Spot"),
		      _cameraModeLocked ? " (manual switching locked)" : "");
		return true;
	}

	if (op.equalsIgnoreCase("LockCameraMode")) {
		_cameraModeLocked = true;
		debug(1, "Zero Comico: camera mode locked");
		return true;
	}

	if (op.equalsIgnoreCase("UnLockCameraMode")) {
		_cameraModeLocked = false;
		debug(1, "Zero Comico: camera mode unlocked");
		return true;
	}

	if (op.equalsIgnoreCase("SetSpotCameraParameters")) {
		if (instruction.args.size() < 5)
			return false;
		char *end = nullptr;
		const double p0 = strtod(instruction.args[0].c_str(), &end);
		if (!end || end == instruction.args[0].c_str() || *end != 0)
			return false;
		const double p1 = strtod(instruction.args[1].c_str(), &end);
		if (!end || end == instruction.args[1].c_str() || *end != 0)
			return false;
		const double p2 = strtod(instruction.args[2].c_str(), &end);
		if (!end || end == instruction.args[2].c_str() || *end != 0)
			return false;
		const double p3 = strtod(instruction.args[3].c_str(), &end);
		if (!end || end == instruction.args[3].c_str() || *end != 0)
			return false;
		const double p4 = strtod(instruction.args[4].c_str(), &end);
		if (!end || end == instruction.args[4].c_str() || *end != 0)
			return false;

		// Original Zero Comico.exe runs the first four values through its
		// world-unit conversion: value * 100 * GlobalScaling. Config.gsc for the
		// retail build declares GlobalScaling 1.
		_spotHeight = (float)(p0 * 100.0);
		_spotMaxDeltaY = (float)(p1 * 100.0);
		_spotDistance = (float)(p2 * 100.0);
		_spotMinDistance = (float)(p3 * 100.0);
		_spotSmooth = (float)p4;
		debug(1, "Zero Comico: spot camera parameters %.3f %.3f %.3f %.3f %.3f",
		      _spotHeight, _spotMaxDeltaY, _spotDistance,
		      _spotMinDistance, _spotSmooth);
		return true;
	}

	if (op.equalsIgnoreCase("SetLightState")) {
		if (instruction.args.size() < 2)
			return false;

		int32 enabled = 0;
		if (!_scriptVM.resolveValue(instruction.args[1], enabled))
			return false;

		bool found = false;
		for (uint32 i = 0; i < _lightStateNames.size(); ++i) {
			if (!_lightStateNames[i].equalsIgnoreCase(instruction.args[0]))
				continue;
			_lightStateEnabled[i] = enabled != 0;
			found = true;
			break;
		}
		if (!found) {
			_lightStateNames.push_back(instruction.args[0]);
			_lightStateEnabled.push_back(enabled != 0);
		}

		// Mpx/Interface.isc uses SetLightState to drive highlight lights. Runtime
		// render cameras now pass these overrides to the decoded-light evaluator.
		debug(1, "Zero Comico: light %s = %d",
		      instruction.args[0].c_str(), enabled != 0 ? 1 : 0);
		return true;
	}

	if (op.equalsIgnoreCase("e3d_Parse")) {
		if (instruction.args.empty())
			return false;

		Common::Array<Common::String> command;
		splitE3dCommand(instruction.args[0], command);
		if (command.empty())
			return false;

		if (command[0].equalsIgnoreCase("Master_Color")) {
			if (command.size() < 5)
				return false;
			for (int component = 0; component < 4; ++component) {
				float value = 0.0f;
				if (!parseScriptFloat(command[(uint32)component + 1], value))
					return false;
				if (value < 0.0f)
					value = 0.0f;
				else if (value > 255.0f)
					value = 255.0f;
				_masterColor[component] = value / 255.0f;
			}
			_masterColorFadeActive = false;
			debug(1, "Zero Comico: master color %.3f %.3f %.3f %.3f",
			      _masterColor[0], _masterColor[1],
			      _masterColor[2], _masterColor[3]);
			return true;
		}

		if (command[0].equalsIgnoreCase("Master_Color_Fade_In") ||
		    command[0].equalsIgnoreCase("Master_Color_Fade_Out")) {
			if (command.size() < 2)
				return false;
			float steps = 0.0f;
			if (!parseScriptFloat(command[1], steps))
				return false;

			for (int component = 0; component < 4; ++component)
				_masterColorFrom[component] = _masterColor[component];

			const bool fadeIn = command[0].equalsIgnoreCase("Master_Color_Fade_In");
			_masterColorTo[0] = fadeIn ? 1.0f : 0.0f;
			_masterColorTo[1] = fadeIn ? 1.0f : 0.0f;
			_masterColorTo[2] = fadeIn ? 1.0f : 0.0f;
			_masterColorTo[3] = 1.0f;
			_masterColorFadeSteps = steps;
			_masterColorFadeStartMillis = _system->getMillis();
			_masterColorFadeActive = steps > 0.0f;
			if (!_masterColorFadeActive)
				for (int component = 0; component < 4; ++component)
					_masterColor[component] = _masterColorTo[component];

			debug(1, "Zero Comico: master-color fade %s over %.1f 70-Hz ticks",
			      fadeIn ? "in" : "out", steps);
			return true;
		}

		if (command[0].equalsIgnoreCase("set_usereffect_state")) {
			if (command.size() < 3)
				return false;

			char *end = nullptr;
			const long parsed = strtol(command[2].c_str(), &end, 10);
			if (!end || end == command[2].c_str() || *end != 0)
				return false;
			const int32 state = (int32)parsed;

			const uint32 now = _system->getMillis();
			bool found = false;
			for (uint32 i = 0; i < _userEffectStateNames.size(); ++i) {
				if (!_userEffectStateNames[i].equalsIgnoreCase(command[1]))
					continue;
				if (_userEffectStates[i] != 0)
					_userEffectElapsedMs[i] += now - _userEffectStateChangedMillis[i];
				_userEffectStates[i] = state;
				_userEffectStateChangedMillis[i] = now;
				found = true;
				break;
			}
			if (!found) {
				_userEffectStateNames.push_back(command[1]);
				_userEffectStates.push_back(state);
				_userEffectElapsedMs.push_back(0);
				_userEffectStateChangedMillis.push_back(now);
			}

			debug(1, "Zero Comico: user effect %s state = %d",
			      command[1].c_str(), (int)state);
			return true;
		}

		if (command[0].equalsIgnoreCase("shade")) {
			_shadeEnabled = true;
			debug(1, "Zero Comico: retail room shading enabled");
			return true;
		}

		return true;
	}

	if (op.equalsIgnoreCase("csay")) {
		if (instruction.args.size() < 2)
			return false;
		debug(1, "Zero Comico: %s says: %s",
		      instruction.args[0].c_str(), instruction.args[1].c_str());
		return showScriptLine(instruction.args[0], instruction.args[1]);
	}

	if (op.equalsIgnoreCase("quit_game")) {
		quitGame();
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

	if (op.equalsIgnoreCase("mch_push_master_volume")) {
		if (instruction.args.empty() || !_mixer)
			return false;

		int32 requested = 0;
		if (!_scriptVM.resolveValue(instruction.args[0], requested))
			return false;

		MixerVolumeSnapshot snapshot;
		snapshot.plain = _mixer->getVolumeForSoundType(Audio::Mixer::kPlainSoundType);
		snapshot.sfx = _mixer->getVolumeForSoundType(Audio::Mixer::kSFXSoundType);
		snapshot.music = _mixer->getVolumeForSoundType(Audio::Mixer::kMusicSoundType);
		snapshot.speech = _mixer->getVolumeForSoundType(Audio::Mixer::kSpeechSoundType);
		_masterVolumeStack.push_back(snapshot);

		int volume = (int)requested;
		if (volume < 0)
			volume = 0;
		else if (volume > Audio::Mixer::kMaxMixerVolume)
			volume = Audio::Mixer::kMaxMixerVolume;

		_mixer->setVolumeForSoundType(Audio::Mixer::kPlainSoundType, volume);
		_mixer->setVolumeForSoundType(Audio::Mixer::kSFXSoundType, volume);
		_mixer->setVolumeForSoundType(Audio::Mixer::kMusicSoundType, volume);
		_mixer->setVolumeForSoundType(Audio::Mixer::kSpeechSoundType, volume);
		debug(1, "Zero Comico: pushed master volume and set mixer groups to %d", volume);
		return true;
	}

	if (op.equalsIgnoreCase("mch_pop_master_volume")) {
		if (!_mixer || _masterVolumeStack.empty())
			return false;

		const MixerVolumeSnapshot snapshot = _masterVolumeStack.back();
		_masterVolumeStack.pop_back();
		_mixer->setVolumeForSoundType(Audio::Mixer::kPlainSoundType, snapshot.plain);
		_mixer->setVolumeForSoundType(Audio::Mixer::kSFXSoundType, snapshot.sfx);
		_mixer->setVolumeForSoundType(Audio::Mixer::kMusicSoundType, snapshot.music);
		_mixer->setVolumeForSoundType(Audio::Mixer::kSpeechSoundType, snapshot.speech);
		debug(1, "Zero Comico: restored master volume snapshot");
		return true;
	}

	return false;
}

bool ZeroComicoEngine::evaluateScriptCondition(const ScriptInstruction &instruction,
                                                    bool &result) const {
	if (instruction.opcode.equalsIgnoreCase("if_key") ||
	    instruction.opcode.equalsIgnoreCase("jmp_if_key")) {
		if (instruction.args.empty())
			return false;
		const uint32 mask = scriptKeyMaskForName(instruction.args[0]);
		if (mask == 0)
			return false;
		result = (_scriptKeyMask & mask) != 0;
		return true;
	}

	if (instruction.opcode.equalsIgnoreCase("if_No_Key_Pressed")) {
		result = _scriptKeyMask == 0;
		return true;
	}

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

	if (instruction.opcode.equalsIgnoreCase("ifobjininv")) {
		if (instruction.args.size() < 2)
			return false;
		result = containsIgnoreCase(_inventoryObjects, instruction.args[1]);
		return true;
	}

	if (instruction.opcode.equalsIgnoreCase("ifcobjstate")) {
		if (instruction.args.size() < 3)
			return false;
		const PuzzleObject *object = _activePuzzle.findObject(instruction.args[1]);
		if (!object)
			return false;
		result = puzzleObjectMatchesState(*object, instruction.args[2]);
		return true;
	}

	if (instruction.opcode.equalsIgnoreCase("ifcplace")) {
		if (instruction.args.size() < 2)
			return false;

		const Common::String &characterName = instruction.args[0];
		const bool player =
			characterName.equalsIgnoreCase("MainPlayer") ||
			(!_playerCharacterScript.playerName.empty() &&
			 characterName.equalsIgnoreCase(_playerCharacterScript.playerName));
		if (player) {
			result = _activeRoomName.equalsIgnoreCase(instruction.args[1]);
			return true;
		}

		const CpuCharacterRuntime *character = nullptr;
		for (uint32 i = 0; i < _cpuCharacters.size(); ++i) {
			if (_cpuCharacters[i].name.equalsIgnoreCase(characterName)) {
				character = &_cpuCharacters[i];
				break;
			}
		}
		result = character && character->alive &&
		         character->roomName.equalsIgnoreCase(instruction.args[1]);
		return true;
	}

	if (instruction.opcode.equalsIgnoreCase("ifplace")) {
		if (instruction.args.empty())
			return false;
		result = _activeRoomName.equalsIgnoreCase(instruction.args[0]);
		return true;
	}

	if (instruction.opcode.equalsIgnoreCase("if_Char_InDialog")) {
		// Dialogue playback is synchronous in this runtime, so autonomous
		// ControlCode ticks cannot run while a dialogue owns the screen.
		result = false;
		return true;
	}

	if (instruction.opcode.equalsIgnoreCase("if_Is_OpenInterface")) {
		// The retail modal inventory/interface controller is not open while the
		// gameplay scheduler is running. DisableInterface is a separate gate.
		result = false;
		return true;
	}


	// Ordinary play_cut is synchronous, but loop_cut keeps a persistent retail
	// cut object alive between ScriptVM scheduler boundaries.
	if (instruction.opcode.equalsIgnoreCase("if_is_playingcut")) {
		result = _loopCutActive;
		return true;
	}
	if (instruction.opcode.equalsIgnoreCase("if_is_openmenuinterface")) {
		result = false;
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

void ZeroComicoEngine::updateScriptKeyState(const Common::Event &event) {
	const uint32 mask = scriptKeyMaskForEvent(event);
	if (mask == 0)
		return;
	if (event.type == Common::EVENT_KEYDOWN)
		_scriptKeyMask |= mask;
	else if (event.type == Common::EVENT_KEYUP)
		_scriptKeyMask &= ~mask;
}

bool ZeroComicoEngine::yieldScriptExecution() {
	Common::Event event;
	while (_system->getEventManager()->pollEvent(event)) {
		updateScriptKeyState(event);
		if (event.type == Common::EVENT_QUIT ||
		    event.type == Common::EVENT_RETURN_TO_LAUNCHER) {
			quitGame();
			return false;
		}
	}

	// Scripts use wjmp as their scheduler boundary. The retail VM advances
	// gameplay/cutscene script state at the same 25 fps cadence used by
	// wait_frames and JACS timelines.
	_system->delayMillis(40);
	return !shouldQuit();
}

bool ZeroComicoEngine::scheduleScriptThread(const ScriptProgram &program,
                                                 uint32 startIndex, uint32 endIndex) {
	if (startIndex >= endIndex || endIndex > program.instructions().size())
		return false;

	BackgroundScriptThread thread;
	thread.program = &program;
	thread.startIndex = startIndex;
	thread.pc = startIndex;
	thread.endIndex = endIndex;
	_backgroundScriptThreads.push_back(thread);

	debug(1, "Zero Comico: scheduled background script thread [%u, %u)",
	      startIndex, endIndex);
	return true;
}

bool ZeroComicoEngine::runBackgroundScriptThreads() {
	if (_backgroundScriptThreads.empty())
		return true;

	const uint32 now = _system->getMillis();
	if (now - _lastBackgroundScriptTick < 40U)
		return true;
	_lastBackgroundScriptTick = now;

	for (uint32 i = 0; i < _backgroundScriptThreads.size();) {
		BackgroundScriptThread &thread = _backgroundScriptThreads[i];
		if (!thread.program) {
			_backgroundScriptThreads.remove_at(i);
			continue;
		}

		bool finished = false;
		if (!_scriptVM.runThreadStep(*thread.program, thread.startIndex,
		                             thread.pc, thread.endIndex, finished, 256)) {
			warning("Zero Comico: background script thread stopped on an unsupported opcode");
			_backgroundScriptThreads.remove_at(i);
			continue;
		}

		if (finished) {
			_backgroundScriptThreads.remove_at(i);
			continue;
		}
		++i;
	}
	return true;
}

void ZeroComicoEngine::applyMasterColor(Graphics::ManagedSurface &surface) {
	if (_masterColorFadeActive) {
		const uint32 elapsedMillis = _system->getMillis() - _masterColorFadeStartMillis;
		const float elapsedTicks = (float)elapsedMillis * 70.0f / 1000.0f;
		float factor = _masterColorFadeSteps > 0.0f
			? elapsedTicks / _masterColorFadeSteps : 1.0f;
		if (factor >= 1.0f) {
			factor = 1.0f;
			_masterColorFadeActive = false;
		}
		for (int component = 0; component < 4; ++component)
			_masterColor[component] =
				_masterColorFrom[component] * (1.0f - factor) +
				_masterColorTo[component] * factor;
	}

	if (std::fabs(_masterColor[0] - 1.0f) < 1.0e-6f &&
	    std::fabs(_masterColor[1] - 1.0f) < 1.0e-6f &&
	    std::fabs(_masterColor[2] - 1.0f) < 1.0e-6f)
		return;

	for (int y = 0; y < surface.h; ++y) {
		byte *row = static_cast<byte *>(surface.getBasePtr(0, y));
		for (int x = 0; x < surface.w; ++x) {
			row[x * 4 + 0] = (byte)(row[x * 4 + 0] * _masterColor[2] + 0.5f);
			row[x * 4 + 1] = (byte)(row[x * 4 + 1] * _masterColor[1] + 0.5f);
			row[x * 4 + 2] = (byte)(row[x * 4 + 2] * _masterColor[0] + 0.5f);
		}
	}
}

float ZeroComicoEngine::retailSampleVolume(const Common::String &sampleName,
                                                 float sourceDistance) const {
	float farVolume = _sampleDefaultFarVolume;
	float nearVolume = _sampleDefaultNearVolume;
	float minRange = _sampleDefaultMinRange;
	float maxRange = _sampleDefaultMaxRange;

	if (!sampleName.empty()) {
		Common::String key = sampleName;
		key.toLowercase();
		if (key.hasSuffix(".mp3"))
			key = key.substr(0, key.size() - 4);

		for (uint32 i = 0; i < _samplePlaybackParams.size(); ++i) {
			Common::String candidate = _samplePlaybackParams[i].name;
			candidate.toLowercase();
			if (candidate.hasSuffix(".mp3"))
				candidate = candidate.substr(0, candidate.size() - 4);
			if (candidate != key)
				continue;
			farVolume = _samplePlaybackParams[i].farVolume;
			nearVolume = _samplePlaybackParams[i].nearVolume;
			minRange = _samplePlaybackParams[i].minRange;
			maxRange = _samplePlaybackParams[i].maxRange;
			break;
		}
	}

	// Retail Zero Comico linearly interpolates from the near volume at/below
	// minRange to the far volume at/above maxRange.
	if (sourceDistance <= minRange || maxRange <= minRange)
		return nearVolume;
	if (sourceDistance >= maxRange)
		return farVolume;

	const float t = (sourceDistance - minRange) / (maxRange - minRange);
	return nearVolume + (farVolume - nearVolume) * t;
}

byte ZeroComicoEngine::retailChannelVolume(int soundClass, float sourceVolume,
                                           const Common::String &sampleName,
                                           float sourceDistance) const {
	float master = _globalMasterVolume;
	float classVolume = soundClass >= 0 && soundClass < 6
		? _soundClassVolumes[soundClass] : 100.0f;
	float source = sourceVolume;
	float sample = sampleName.empty() ? 100.0f
		: retailSampleVolume(sampleName, sourceDistance);

	if (master < 0.0f) master = 0.0f;
	if (master > 100.0f) master = 100.0f;
	if (classVolume < 0.0f) classVolume = 0.0f;
	if (classVolume > 100.0f) classVolume = 100.0f;
	if (source < 0.0f) source = 0.0f;
	if (source > 100.0f) source = 100.0f;
	if (sample < 0.0f) sample = 0.0f;
	if (sample > 100.0f) sample = 100.0f;

	const float percent =
		master * classVolume * source * sample / 1000000.0f;
	return (byte)(percent * Audio::Mixer::kMaxChannelVolume / 100.0f + 0.5f);
}

void ZeroComicoEngine::startRoomMusic(const Common::String &fileName, float volume) {
	if (fileName.empty()) {
		if (_mixer->isSoundHandleActive(_musicHandle))
			_mixer->stopHandle(_musicHandle);
		_currentMusicName.clear();
		return;
	}

	float clampedVolume = volume;
	if (clampedVolume < 0.0f)
		clampedVolume = 0.0f;
	if (clampedVolume > 100.0f)
		clampedVolume = 100.0f;
	const byte mixerVolume =
		retailChannelVolume(0, clampedVolume, Common::String());

	if (_currentMusicName.equalsIgnoreCase(fileName) &&
	    _mixer->isSoundHandleActive(_musicHandle)) {
		_mixer->setChannelVolume(_musicHandle, mixerVolume);
		return;
	}

	if (_mixer->isSoundHandleActive(_musicHandle))
		_mixer->stopHandle(_musicHandle);
	_currentMusicName.clear();

#ifdef USE_MAD
	Common::File *musicFile = new Common::File();
	const Common::Path musicPath =
		Common::Path("Music").appendComponent(fileName);
	if (!musicFile->open(musicPath)) {
		delete musicFile;
		warning("Zero Comico: cannot open room music %s", musicPath.toString().c_str());
		return;
	}

	Audio::SeekableAudioStream *decoded =
		Audio::makeMP3Stream(musicFile, DisposeAfterUse::YES);
	if (!decoded) {
		delete musicFile;
		warning("Zero Comico: cannot decode room music %s", musicPath.toString().c_str());
		return;
	}

	Audio::AudioStream *loop =
		Audio::makeLoopingAudioStream(decoded, 0);
	_mixer->playStream(Audio::Mixer::kMusicSoundType, &_musicHandle, loop,
	                   -1, mixerVolume);
	_currentMusicName = fileName;
	debug(1, "Zero Comico: room music %s source %.1f%%, retail channel %u/%u",
	      fileName.c_str(), clampedVolume, (uint)mixerVolume,
	      (uint)Audio::Mixer::kMaxChannelVolume);
#else
	(void)volume;
#endif
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

void ZeroComicoEngine::setEnvironmentSound(const Common::String &fileName, bool enabled) {
#ifdef USE_MAD
	if (!enabled || fileName.empty() || fileName.equalsIgnoreCase("none") ||
	    fileName.equalsIgnoreCase("off") || fileName == "0") {
		if (_mixer->isSoundHandleActive(_environmentSoundHandle))
			_mixer->stopHandle(_environmentSoundHandle);
		_environmentSoundActive = false;
		if (!fileName.empty() && !fileName.equalsIgnoreCase("none") &&
		    !fileName.equalsIgnoreCase("off") && fileName != "0")
			_environmentSoundName = fileName;
		return;
	}

	if (_environmentSoundName.equalsIgnoreCase(fileName) &&
	    _mixer->isSoundHandleActive(_environmentSoundHandle))
		return;

	if (_mixer->isSoundHandleActive(_environmentSoundHandle))
		_mixer->stopHandle(_environmentSoundHandle);

	_environmentSoundName = fileName;
	_environmentSoundActive = playNamedMp3Looped(
		_mixer, Audio::Mixer::kSFXSoundType,
		_environmentSoundName, _environmentSoundHandle,
		retailChannelVolume(5, 100.0f, _environmentSoundName));
	if (!_environmentSoundActive)
		warning("Zero Comico: cannot start environment sound %s",
		        _environmentSoundName.c_str());
#else
	_environmentSoundName = enabled ? fileName : Common::String();
	_environmentSoundActive = enabled && !fileName.empty();
#endif
}

void ZeroComicoEngine::stopRoomEnvironmentSounds() {
#ifdef USE_MAD
	for (uint32 i = 0; i < _roomEnvironmentSounds.size(); ++i) {
		RoomEnvironmentRuntime &sound = _roomEnvironmentSounds[i];
		if (sound.active && _mixer->isSoundHandleActive(sound.handle))
			_mixer->stopHandle(sound.handle);
		sound.active = false;
	}
#endif
	_roomEnvironmentSounds.clear();
}

void ZeroComicoEngine::startRoomEnvironmentSounds(const RoomDefinition &room) {
	stopRoomEnvironmentSounds();

	for (uint32 i = 0; i < room.environmentSounds.size(); ++i) {
		const RoomEnvironmentSound &definition = room.environmentSounds[i];
		RoomEnvironmentRuntime runtime;
		runtime.roomName = room.name;
		runtime.name = definition.name;
		runtime.fileName = definition.fileName;
		runtime.entity = definition.entity;
		runtime.emitter = definition.emitter;
		runtime.defaultEnabled = definition.enabled;
		runtime.farVolume = definition.farVolume;
		runtime.nearVolume = definition.nearVolume;
		runtime.minRange = definition.minRange * 100.0f;
		runtime.maxRange = definition.maxRange * 100.0f;
		runtime.active = false;
		_roomEnvironmentSounds.push_back(runtime);
	}

	updateRoomEnvironmentSounds();
}

void ZeroComicoEngine::updateRoomEnvironmentSounds() {
	for (uint32 i = 0; i < _roomEnvironmentSounds.size(); ++i) {
		RoomEnvironmentRuntime &sound = _roomEnvironmentSounds[i];

		bool enabled = sound.defaultEnabled;
		for (uint32 stateIndex = 0;
		     stateIndex < _environmentStateNames.size() &&
		     stateIndex < _environmentStateRooms.size() &&
		     stateIndex < _environmentStateEnabled.size();
		     ++stateIndex) {
			if (_environmentStateRooms[stateIndex].equalsIgnoreCase(sound.roomName) &&
			    _environmentStateNames[stateIndex].equalsIgnoreCase(sound.name)) {
				enabled = _environmentStateEnabled[stateIndex];
				break;
			}
		}

		if (!enabled) {
#ifdef USE_MAD
			if (sound.active && _mixer->isSoundHandleActive(sound.handle))
				_mixer->stopHandle(sound.handle);
#endif
			sound.active = false;
			continue;
		}

		float sourceVolume = sound.nearVolume;
		if (sound.emitter) {
			const NamedMesh *sourceMesh = _activeScene.findMesh(sound.entity);
			if (!sourceMesh) {
				const DynamicSceneEntity *dynamic = findDynamicSceneEntity(sound.entity);
				if (dynamic)
					sourceMesh = &dynamic->mesh;
			}

			if (!sourceMesh) {
				sourceVolume = 0.0f;
			} else {
				const Vec3f &source = sourceMesh->data.transform.translation;
				const float dx = source.x - _playerPosition.x;
				const float dy = source.y - _playerPosition.y;
				const float dz = source.z - _playerPosition.z;
				const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

				if (sound.maxRange > sound.minRange) {
					if (distance <= sound.minRange) {
						sourceVolume = sound.nearVolume;
					} else if (distance >= sound.maxRange) {
						sourceVolume = sound.farVolume;
					} else {
						const float t =
							(distance - sound.minRange) /
							(sound.maxRange - sound.minRange);
						sourceVolume =
							sound.nearVolume +
							(sound.farVolume - sound.nearVolume) * t;
					}
				}
			}
		}

		const byte mixerVolume =
			retailChannelVolume(5, sourceVolume, Common::String());

#ifdef USE_MAD
		if (!sound.active || !_mixer->isSoundHandleActive(sound.handle)) {
			sound.active = playNamedMp3Looped(
				_mixer, Audio::Mixer::kSFXSoundType,
				sound.fileName, sound.handle, mixerVolume);
			if (!sound.active) {
				warning("Zero Comico: cannot start room environment sound %s (%s)",
				        sound.name.c_str(), sound.fileName.c_str());
				continue;
			}
		} else {
			_mixer->setChannelVolume(sound.handle, mixerVolume);
		}
#else
		sound.active = true;
		(void)mixerVolume;
#endif
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

	const NamedCamera *menuCameraRecord = _menuScene.findCamera("int_Camera01");
	if (!menuCameraRecord || menuCameraRecord->data.fov <= 0.0f)
		return false;

	Common::Array<Common::String> menuLightNames;
	Common::Array<bool> menuLightEnabled;
	static const char *const kMenuLightNames[kMenuButtonCount] = {
		"int_nuov_light",
		"int_help_light",
		"int_cred_light",
		"int_abba_light",
		"int_cari_light"
	};
	for (int i = 0; i < kMenuButtonCount; ++i) {
		menuLightNames.push_back(kMenuLightNames[i]);
		menuLightEnabled.push_back(i == selection);
	}

	RenderCamera menuCamera;
	menuCamera.position = menuCameraRecord->data.position;
	menuCamera.target = menuCameraRecord->data.target;
	menuCamera.focalPixels = menuCameraRecord->data.fov * 800.0f / 36.0f;
	menuCamera.shadeEnabled = true;
	menuCamera.lightStateNames = &menuLightNames;
	menuCamera.lightStateEnabled = &menuLightEnabled;

	SoftwareRenderer renderer;
	Graphics::ManagedSurface frame;
	if (!renderer.render(_menuScene, menuCamera,
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
	RenderCamera gameplayCamera = camera;
	gameplayCamera.depthCueEnabled = _depthCueEnabled;
	gameplayCamera.shadeEnabled = _shadeEnabled;
	gameplayCamera.lightStateNames = &_lightStateNames;
	gameplayCamera.lightStateEnabled = &_lightStateEnabled;
	gameplayCamera.depthCueStart = _depthCueStart;
	gameplayCamera.depthCueEnd = _depthCueEnd;

	const uint32 now = _system->getMillis();
	for (uint32 materialIndex = 0; materialIndex < _activeScene.materials.size(); ++materialIndex) {
		NamedMaterial &material = _activeScene.materials[materialIndex];
		material.data.userEffectState = 0;
		material.data.userEffectElapsedMs = 0;
		for (uint32 effectIndex = 0; effectIndex < _userEffectStateNames.size(); ++effectIndex) {
			if (!material.name.equalsIgnoreCase(_userEffectStateNames[effectIndex]))
				continue;
			material.data.userEffectState = _userEffectStates[effectIndex];
			material.data.userEffectElapsedMs = _userEffectElapsedMs[effectIndex];
			if (_userEffectStates[effectIndex] != 0)
				material.data.userEffectElapsedMs += now - _userEffectStateChangedMillis[effectIndex];
			break;
		}
	}
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
		if (!_activeScene.poseSkinnedGeometry(_sceneLoopTargets[loopIndex], "Stay",
		                                     _sceneLoopSources[loopIndex], loopFrame))
			_activeScene.poseRigidAnimation(_sceneLoopTargets[loopIndex],
			                                _sceneLoopSources[loopIndex], loopFrame);
	}

	// Non-looping play commands advance at the same 25 fps cadence, clamp at
	// their final frame, and retain that final pose until another play targets
	// the same entity. This matches doors/actors that change persistent state.
	for (uint32 shotIndex = 0; shotIndex < _sceneOneShotTargets.size(); ++shotIndex) {
		const NamedAnimationClip *clip = _activeScene.findClipBySource(
			_sceneOneShotTargets[shotIndex], _sceneOneShotSources[shotIndex]);
		if (!clip)
			continue;

		const float firstFrame = (float)clip->data.startFrame;
		const float lastFrame = (float)clip->data.endFrame;
		const float elapsedFrames =
			(float)(now - _sceneOneShotStartMillis[shotIndex]) * 25.0f / 1000.0f;
		float shotFrame = firstFrame + elapsedFrames;
		if (shotFrame > lastFrame)
			shotFrame = lastFrame;

		if (!_activeScene.poseSkinnedGeometry(_sceneOneShotTargets[shotIndex], "Stay",
		                                     _sceneOneShotSources[shotIndex], shotFrame))
			_activeScene.poseRigidAnimation(_sceneOneShotTargets[shotIndex],
			                                _sceneOneShotSources[shotIndex], shotFrame);
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
	_activeRenderActors.clear();

	if (!_playerScene.meshes.empty() && _havePlayerStart) {
		const Common::String playerRoot = !_playerSequences.bodyName.empty()
			? _playerSequences.bodyName : _playerCharacterScript.initialBodyName;
		Common::String playerSource = animationSource;
		float playerFrame = animationFrame;

		// Scripted character play/playl commands own the idle actor while their
		// clip is active. Explicit locomotion/pickup rendering remains
		// authoritative so an ambient animation cannot interrupt movement.
		if (animationSource.equalsIgnoreCase("Stay")) {
			for (uint32 loopIndex = 0; loopIndex < _sceneLoopTargets.size(); ++loopIndex) {
				if (!_sceneLoopTargets[loopIndex].equalsIgnoreCase(playerRoot))
					continue;
				const NamedAnimationClip *clip = _playerScene.findClipBySource(
					playerRoot, _sceneLoopSources[loopIndex]);
				if (!clip)
					continue;
				playerSource = _sceneLoopSources[loopIndex];
				const float firstFrame = (float)clip->data.startFrame;
				const float lastFrame = (float)clip->data.endFrame;
				const float frameCount = lastFrame >= firstFrame
					? lastFrame - firstFrame + 1.0f : 1.0f;
				const float elapsedFrames =
					(float)(now - _sceneLoopStartMillis[loopIndex]) * 25.0f / 1000.0f;
				playerFrame = firstFrame + std::fmod(elapsedFrames, frameCount);
				break;
			}

			for (uint32 shotIndex = 0; shotIndex < _sceneOneShotTargets.size(); ++shotIndex) {
				if (!_sceneOneShotTargets[shotIndex].equalsIgnoreCase(playerRoot))
					continue;
				const NamedAnimationClip *clip = _playerScene.findClipBySource(
					playerRoot, _sceneOneShotSources[shotIndex]);
				if (!clip)
					continue;
				const float firstFrame = (float)clip->data.startFrame;
				const float lastFrame = (float)clip->data.endFrame;
				const float elapsedFrames =
					(float)(now - _sceneOneShotStartMillis[shotIndex]) * 25.0f / 1000.0f;
				const float shotFrame = firstFrame + elapsedFrames;
				if (shotFrame <= lastFrame) {
					playerSource = _sceneOneShotSources[shotIndex];
					playerFrame = shotFrame;
				}
				break;
			}
		}

		if (!_playerScene.poseSkinnedGeometry(playerRoot, "Stay", playerSource, playerFrame))
			warning("Zero Comico: could not evaluate player skeletal pose %s at %.2f",
			        playerSource.c_str(), playerFrame);

		Common::Array<Common::String> playerVisible;
		_playerScene.visibleMeshesForSource(playerSource, playerFrame, playerVisible);
		if (!_playerHatVisible) {
			for (uint32 visibleIndex = playerVisible.size(); visibleIndex > 0; --visibleIndex) {
				Common::String lowerName = playerVisible[visibleIndex - 1];
				lowerName.toLowercase();
				if (lowerName.find("cappello") != Common::String::npos)
					playerVisible.remove_at(visibleIndex - 1);
			}
		}

		RenderActor playerActor;
		playerActor.scene = &_playerScene;
		playerActor.textureDirectory = _playerAssetDirectory;
		playerActor.visibleMeshes = playerVisible;
		playerActor.transform.translation = _playerPosition;
		const float faceX = _playerFacingTarget.x - _playerPosition.x;
		const float faceZ = _playerFacingTarget.z - _playerPosition.z;
		playerActor.transform.yawRadians = std::atan2(faceX, faceZ);
		if (!sampleRootTransform(_playerScene, playerRoot, playerSource, playerFrame,
		                         playerActor.transform))
			warning("Zero Comico: player %s root transform missing; using identity root pose",
			        playerSource.c_str());
		_activeRenderActors.push_back(playerActor);
	}

	for (uint32 cpuIndex = 0; cpuIndex < _cpuCharacters.size(); ++cpuIndex) {
		CpuCharacterRuntime &character = _cpuCharacters[cpuIndex];
		if (!character.alive || !character.positioned ||
		    !character.roomName.equalsIgnoreCase(_activeRoomName) ||
		    character.scene.meshes.empty())
			continue;

		Common::String cpuSource("Stay");
		float cpuFrame = 0.0f;
		const NamedAnimationClip *idleClip = character.scene.findClipBySource(
			character.bodyRoot, "Stay");
		if (idleClip) {
			const float firstFrame = (float)idleClip->data.startFrame;
			const float lastFrame = (float)idleClip->data.endFrame;
			const float frameCount = lastFrame >= firstFrame
				? lastFrame - firstFrame + 1.0f : 1.0f;
			const float elapsedFrames =
				(float)(now - character.idleAnimationStartMillis) * 25.0f / 1000.0f;
			cpuFrame = firstFrame + std::fmod(elapsedFrames, frameCount);
		}
		for (uint32 loopIndex = 0; loopIndex < _sceneLoopTargets.size(); ++loopIndex) {
			if (!_sceneLoopTargets[loopIndex].equalsIgnoreCase(character.bodyRoot))
				continue;
			const NamedAnimationClip *clip = character.scene.findClipBySource(
				character.bodyRoot, _sceneLoopSources[loopIndex]);
			if (!clip)
				continue;
			cpuSource = _sceneLoopSources[loopIndex];
			const float firstFrame = (float)clip->data.startFrame;
			const float lastFrame = (float)clip->data.endFrame;
			const float frameCount = lastFrame >= firstFrame
				? lastFrame - firstFrame + 1.0f : 1.0f;
			const float elapsedFrames =
				(float)(now - _sceneLoopStartMillis[loopIndex]) * 25.0f / 1000.0f;
			cpuFrame = firstFrame + std::fmod(elapsedFrames, frameCount);
			break;
		}
		for (uint32 shotIndex = 0; shotIndex < _sceneOneShotTargets.size(); ++shotIndex) {
			if (!_sceneOneShotTargets[shotIndex].equalsIgnoreCase(character.bodyRoot))
				continue;
			const NamedAnimationClip *clip = character.scene.findClipBySource(
				character.bodyRoot, _sceneOneShotSources[shotIndex]);
			if (!clip)
				continue;
			const float firstFrame = (float)clip->data.startFrame;
			const float lastFrame = (float)clip->data.endFrame;
			const float elapsedFrames =
				(float)(now - _sceneOneShotStartMillis[shotIndex]) * 25.0f / 1000.0f;
			const float shotFrame = firstFrame + elapsedFrames;
			if (shotFrame <= lastFrame) {
				cpuSource = _sceneOneShotSources[shotIndex];
				cpuFrame = shotFrame;
			}
			break;
		}

		if (!character.lifeBroken) {
			const CharacterDefinition *definition =
				_playerCharacterScript.findCharacter(character.name);
			const CharacterAnimSet *animSet = nullptr;
			if (definition) {
				for (uint32 animSetIndex = 0; animSetIndex < definition->animSets.size(); ++animSetIndex) {
					if (definition->animSets[animSetIndex].name.equalsIgnoreCase(
							definition->initialAnimSet)) {
						animSet = &definition->animSets[animSetIndex];
						break;
					}
				}
			}

			const NamedAnimationClip *eventClip =
				character.scene.findClipBySource(character.bodyRoot, cpuSource);
			if (definition && animSet && eventClip) {
				const int32 currentEventFrame =
					(int32)std::floor(cpuFrame - (float)eventClip->data.startFrame + 0.0001f);
				const bool sourceChanged =
					!character.haveEventFrame ||
					!character.lastEventSource.equalsIgnoreCase(cpuSource);

				for (uint32 eventIndex = 0; eventIndex < animSet->stepEvents.size(); ++eventIndex) {
					const CharacterStepEvent &event = animSet->stepEvents[eventIndex];
					if (!event.animation.equalsIgnoreCase(cpuSource))
						continue;

					bool crossed = false;
					if (sourceChanged) {
						crossed = event.frame <= currentEventFrame;
					} else if (currentEventFrame >= character.lastEventFrame) {
						crossed = event.frame > character.lastEventFrame &&
						          event.frame <= currentEventFrame;
					} else {
						crossed = event.frame > character.lastEventFrame ||
						          event.frame <= currentEventFrame;
					}
					if (!crossed)
						continue;

#ifdef USE_MAD
					for (uint32 sampleIndex = 0; sampleIndex < definition->samples.size(); ++sampleIndex) {
						if (definition->samples[sampleIndex].id != event.sampleId)
							continue;
						const Common::String &sampleName =
							definition->samples[sampleIndex].fileName;
						const float dx = character.position.x - _playerPosition.x;
						const float dy = character.position.y - _playerPosition.y;
						const float dz = character.position.z - _playerPosition.z;
						const float sourceDistance = std::sqrt(dx * dx + dy * dy + dz * dz);
						playNamedMp3(_mixer, Audio::Mixer::kSFXSoundType,
						             sampleName, nullptr,
						             retailChannelVolume(2, 100.0f, sampleName,
						                                 sourceDistance));
						break;
					}
#endif
				}

				character.lastEventSource = cpuSource;
				character.lastEventFrame = currentEventFrame;
				character.haveEventFrame = true;
			}
		}

		if (!character.scene.poseSkinnedGeometry(character.bodyRoot, "Stay",
		                                        cpuSource, cpuFrame))
			character.scene.poseRigidAnimation(character.bodyRoot, cpuSource, cpuFrame);

		RenderActor cpuActor;
		cpuActor.scene = &character.scene;
		cpuActor.textureDirectory = character.assetDirectory;
		character.scene.visibleMeshesForSource(cpuSource, cpuFrame, cpuActor.visibleMeshes);
		cpuActor.interactionName = character.bodyRoot;
		cpuActor.transform.translation = character.position;
		cpuActor.transform.yawRadians = character.haveFacing
			? std::atan2(character.facing.x, character.facing.z) : 0.0f;
		if (!sampleRootTransform(character.scene, character.bodyRoot, cpuSource, cpuFrame,
		                         cpuActor.transform))
			sampleRootTransform(character.scene, character.bodyRoot, "Stay", 0.0f,
			                    cpuActor.transform);
		_activeRenderActors.push_back(cpuActor);
	}

	if (_activeRenderActors.empty())
		rendered = _gameplayRenderer.render(_activeScene, gameplayCamera, sceneDirectory,
		                                    visibleMeshes, frame, 800, 600);
	else
		rendered = _gameplayRenderer.renderWithActors(_activeScene, gameplayCamera,
		                                              sceneDirectory, visibleMeshes,
		                                              _activeRenderActors, frame, 800, 600);
	if (!rendered)
		return false;

	applyMasterColor(frame);
	drawInventoryOverlay(frame, _inventoryObjects, _selectedInventoryObject,
	                     _combineInventoryFirst);
	_system->copyRectToScreen(frame.getPixels(), frame.pitch, 0, 0, frame.w, frame.h);
	_system->updateScreen();
	return true;
}

bool ZeroComicoEngine::showScriptLine(const Common::String &speaker,
                                            const Common::String &text) {
	if (text.empty())
		return true;

	if (!_scriptDialogueContextActive || !_scriptDialogueFrame) {
		_pendingSaySpeaker = speaker;
		_pendingSayText = text;
		return true;
	}

	if (!renderGameplayFrame(_scriptDialogueCamera, _scriptDialogueSceneDirectory,
	                         _scriptDialoguePlayerDirectory, "Stay", 0.0f,
	                         *_scriptDialogueFrame))
		return false;

	const DialogSpeaker *speakerInfo = _activeDialog.findSpeakerByName(speaker);
	drawCutsceneSubtitle(*_scriptDialogueFrame, speaker, text, speakerInfo);
	_system->copyRectToScreen(_scriptDialogueFrame->getPixels(),
	                          _scriptDialogueFrame->pitch, 0, 0,
	                          _scriptDialogueFrame->w, _scriptDialogueFrame->h);
	_system->updateScreen();

	const uint32 duration =
		retailTextDurationMillis(text, speakerInfo ? speakerInfo->speed : 0.07f);

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

	if (!shouldQuit())
		renderGameplayFrame(_scriptDialogueCamera, _scriptDialogueSceneDirectory,
		                    _scriptDialoguePlayerDirectory, "Stay", 0.0f,
		                    *_scriptDialogueFrame);
	return !shouldQuit();
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

	if (depth == 0)
		_lastDialogCameraName.clear();

	RenderCamera dialogueCamera = camera;
	const ScriptCamera *dialogFirst = _dialogCameraFirstName.empty()
		? nullptr : _activeCameraScript.findCamera(_dialogCameraFirstName);
	const ScriptCamera *dialogSecond = _dialogCameraSecondName.empty()
		? nullptr : _activeCameraScript.findCamera(_dialogCameraSecondName);

	auto applyDialogScriptCamera = [&](const ScriptCamera *scriptCamera) {
		if (!scriptCamera || scriptCamera->horizontalFovDegrees <= 0.0f)
			return false;
		const float radians = scriptCamera->horizontalFovDegrees *
			3.14159265358979323846f / 180.0f;
		const float halfTan = std::tan(radians * 0.5f);
		if (halfTan <= 0.0001f)
			return false;
		dialogueCamera.position = scriptCamera->source;
		dialogueCamera.target = scriptCamera->target;
		dialogueCamera.focalPixels = 400.0f / halfTan;
		dialogueCamera.rollRadians = 0.0f;
		_lastDialogCameraName = scriptCamera->name;
		return true;
	};

	auto finishDialogueCamera = [&]() {
		if (depth != 0)
			return;
		if (_playerNoCameraReset) {
			if (!_lastDialogCameraName.empty())
				_pendingCameraName = _lastDialogCameraName;
		} else if (_cameraMode == 0 && !_defaultRoomCameraName.empty()) {
			_pendingCameraName = _defaultRoomCameraName;
		}
	};

	// Zero Comico.exe selects the pair with an actor-side test. When the
	// speaking actor is the dialogue initiator (the MainPlayer in retail calls),
	// both actor directions are identical and the test deterministically selects
	// the second camera. NPC lines normally select the first camera, but when a
	// SetCharPos_Entity spawn gave us an exact facing vector we reproduce the
	// executable's XZ side test below.
	const ScriptCamera *playerDialogCamera = dialogSecond ? dialogSecond : dialogFirst;
	const ScriptCamera *otherDialogCamera = dialogFirst ? dialogFirst : dialogSecond;
	applyDialogScriptCamera(playerDialogCamera);

	for (uint32 lineIndex = 0; lineIndex < dialog->lines.size() && !shouldQuit(); ++lineIndex) {
		const DialogLine &line = dialog->lines[lineIndex];
		const DialogSpeaker *speaker = _activeDialog.findSpeakerByKey(line.speakerKey);
		const Common::String speakerName = speaker ? speaker->name : line.speakerKey;
		const bool speakerIsPlayer =
			speakerName.equalsIgnoreCase("MainPlayer") ||
			(!_playerCharacterScript.playerName.empty() &&
			 speakerName.equalsIgnoreCase(_playerCharacterScript.playerName));

		const ScriptCamera *lineCamera = speakerIsPlayer
			? playerDialogCamera : otherDialogCamera;
		if (!speakerIsPlayer && dialogFirst && dialogSecond) {
			const CpuCharacterRuntime *cpuSpeaker = nullptr;
			for (uint32 cpuIndex = 0; cpuIndex < _cpuCharacters.size(); ++cpuIndex) {
				if (_cpuCharacters[cpuIndex].name.equalsIgnoreCase(speakerName)) {
					cpuSpeaker = &_cpuCharacters[cpuIndex];
					break;
				}
			}

			Vec3f playerForward = subtractVec3(_playerFacingTarget, _playerPosition);
			playerForward.y = 0.0f;
			if (cpuSpeaker && cpuSpeaker->haveFacing && normalizeVec3(playerForward)) {
				Vec3f directionDelta = subtractVec3(playerForward, cpuSpeaker->facing);
				if (normalizeVec3(directionDelta)) {
					directionDelta.y = 0.0f;
					const float sideY =
						cpuSpeaker->facing.z * directionDelta.x -
						cpuSpeaker->facing.x * directionDelta.z;
					// Retail helper 0x420a2e returns true for sideY <= 0,
					// and that branch uses the second SetDialogCameras argument.
					lineCamera = sideY <= 0.0f ? dialogSecond : dialogFirst;
				}
			}
		}
		applyDialogScriptCamera(lineCamera);

		if (!renderGameplayFrame(dialogueCamera, sceneDirectory, playerDirectory, "Stay", 0.0f, frame))
			return false;
		drawCutsceneSubtitle(frame, speakerName, line.text, speaker);
		_system->copyRectToScreen(frame.getPixels(), frame.pitch, 0, 0, frame.w, frame.h);
		_system->updateScreen();

		const uint32 duration =
			retailTextDurationMillis(line.text, speaker ? speaker->speed : 0.07f);

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

	if (shouldQuit())
		return false;

	if (dialog->doStart != 0xffffffffU && dialog->doEnd != 0xffffffffU &&
	    dialog->doStart < dialog->doEnd) {
		if (!runScriptWithAudioClass(_activeDialog.program(), dialog->doStart,
		                             dialog->doEnd, 256, 3)) {
			warning("Zero Comico: dialogue %s do-block stopped on an unsupported opcode",
			        dialog->name.c_str());
			return false;
		}
	}

	if (dialog->choices.empty()) {
		finishDialogueCamera();
		return true;
	}

	Common::Array<DialogChoice> activeChoices;
	Common::Array<uint32> activeChoiceIndices;
	for (uint32 choiceIndex = 0; choiceIndex < dialog->choices.size(); ++choiceIndex) {
		if (!dialog->choices[choiceIndex].enabled)
			continue;
		activeChoices.push_back(dialog->choices[choiceIndex]);
		activeChoiceIndices.push_back(choiceIndex);
	}
	if (activeChoices.empty()) {
		finishDialogueCamera();
		return true;
	}

	uint32 selected = 0;
	bool chosen = false;
	while (!shouldQuit() && !chosen) {
		if (!renderGameplayFrame(dialogueCamera, sceneDirectory, playerDirectory, "Stay", 0.0f, frame))
			return false;
		drawDialogueChoices(frame, activeChoices, selected);
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
				selected = (selected + activeChoices.size() - 1) % activeChoices.size();
				break;
			}
			if (event.kbd.keycode == Common::KEYCODE_DOWN) {
				selected = (selected + 1) % activeChoices.size();
				break;
			}
			if (event.kbd.keycode >= Common::KEYCODE_1 &&
			    event.kbd.keycode <= Common::KEYCODE_9) {
				const uint32 direct = (uint32)(event.kbd.keycode - Common::KEYCODE_1);
				if (direct < activeChoices.size()) {
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
			if (event.kbd.keycode == Common::KEYCODE_ESCAPE) {
				finishDialogueCamera();
				return true;
			}
		}
		_system->delayMillis(10);
	}

	if (shouldQuit())
		return false;

	const uint32 originalChoice = activeChoiceIndices[selected];
	_lastDialogueChoice = (int32)originalChoice;
	if (dialog->choices[originalChoice].targetDialog.empty()) {
		finishDialogueCamera();
		return true;
	}

	const bool nestedOk = playDialogue(dialog->choices[originalChoice].targetDialog, camera,
	                                  sceneDirectory, playerDirectory, frame, depth + 1);
	if (nestedOk)
		finishDialogueCamera();
	return nestedOk;
}

bool ZeroComicoEngine::applyStagedRestore(Common::Path &playerDirectory) {
	if (!_pendingLoadActive || _pendingLoadData.empty())
		return false;

	const Common::String expectedMainPlace = _pendingLoadMainPlace;
	const Common::String expectedRoomName = _pendingLoadRoomName;

	Common::MemoryReadStream restoreStream(_pendingLoadData.data(), _pendingLoadData.size());
	Common::Serializer restoreSerializer(&restoreStream, nullptr);
	synchronizePersistentState(restoreSerializer, _pendingLoadVersion);
	if (restoreSerializer.err() ||
	    !_currentMainPlace.equalsIgnoreCase(expectedMainPlace) ||
	    !_activeRoomName.equalsIgnoreCase(expectedRoomName)) {
		warning("Zero Comico: staged restore payload does not match %s/%s",
		        expectedMainPlace.c_str(), expectedRoomName.c_str());
		return false;
	}

	// Rebuild the playable body from the saved AnimSet. Decoded P3D/ANJ/SEQ
	// resources are intentionally not part of the save payload.
	const CharacterAnimSet *savedAnimSet =
		_playerCharacterScript.findAnimSet(_playerAnimSetName);
	if (savedAnimSet) {
		Common::String assetStem = savedAnimSet->bodyName;
		const uint32 separator = assetStem.find('_');
		if (separator != Common::String::npos && separator + 1 < assetStem.size())
			assetStem = assetStem.substr(separator + 1);

		Common::Path directory =
			Common::Path("Mpx/bodies").appendComponent(assetStem);
		SceneModel replacementScene;
		bool loaded = replacementScene.loadPair(
			directory.appendComponent(assetStem + ".p3d"),
			directory.appendComponent(assetStem + ".anj"));
		if (!loaded) {
			Common::String lowerFolder = assetStem;
			lowerFolder.toLowercase();
			directory = Common::Path("Mpx/bodies").appendComponent(lowerFolder);
			loaded = replacementScene.loadPair(
				directory.appendComponent(assetStem + ".p3d"),
				directory.appendComponent(assetStem + ".anj"));
		}

		SequenceScript replacementSequences;
		if (!loaded ||
		    !replacementSequences.load(directory.appendComponent(assetStem + ".seq"))) {
			warning("Zero Comico: cannot restore playable AnimSet %s",
			        _playerAnimSetName.c_str());
			return false;
		}

		_playerScene = replacementScene;
		_playerSequences = replacementSequences;
		_playerAssetDirectory = directory;
		playerDirectory = directory;
	}

	// Setp saves retain only stable asset names and controller positions.
	_loadedSetpScenes.clear();
	const Common::Path setpDirectory(_currentMainPlace + "/backgrd");
	for (uint32 i = 0; i < _loadedSetpAssets.size(); ++i) {
		const Common::String &assetStem = _loadedSetpAssets[i];
		SceneModel asset;
		if (!asset.loadPair(setpDirectory.appendComponent(assetStem + ".p3d"),
		                    setpDirectory.appendComponent(assetStem + ".anj"))) {
			warning("Zero Comico: cannot restore Setp asset %s", assetStem.c_str());
			continue;
		}
		_activeScene.mergeFrom(asset);
		_loadedSetpScenes.push_back(asset);
	}
	for (uint32 i = 0;
	     i < _setpControllerNames.size() && i < _setpControllerPositions.size();
	     ++i) {
		_activeScene.translateHierarchy(_setpControllerNames[i],
		                                _setpControllerPositions[i]);
	}

	// play_open_cut saves retain stable room/asset identity and reconstruct the
	// retail P3D/ANJ final pose just like Setp/CloneEntity restore paths.
	for (uint32 i = 0; i < _openCutScenes.size(); ++i)
		if (!rehydrateOpenCutScene(_openCutScenes[i]))
			warning("Zero Comico: could not rehydrate open cutscene %s",
			        _openCutScenes[i].assetStem.c_str());
	installOpenCutScenesForRoom(_activeRoomName);

	// Recreate dynamic CloneEntity objects from their source template. Saved
	// transforms are applied after cloning so moved/rotated stars and helpers
	// return to exactly the state they had at save time.
	Common::Array<DynamicSceneEntity> dynamicSnapshots = _dynamicSceneEntities;
	_dynamicSceneEntities.clear();
	for (uint32 i = 0; i < dynamicSnapshots.size(); ++i) {
		const DynamicSceneEntity &snapshot = dynamicSnapshots[i];
		if (snapshot.sourceName.empty() || snapshot.name.empty() ||
		    !cloneSceneEntity(snapshot.sourceName, snapshot.name)) {
			warning("Zero Comico: cannot restore dynamic clone %s from %s",
			        snapshot.name.c_str(), snapshot.sourceName.c_str());
			continue;
		}

		DynamicSceneEntity *restored = findDynamicSceneEntity(snapshot.name);
		if (!restored)
			continue;
		restored->roomName = snapshot.roomName;
		const ObjectTransform savedTransform = snapshot.mesh.data.transform;
		translateRigidMesh(restored->mesh, savedTransform.translation);
		restored->mesh.data.transform.pivot = savedTransform.pivot;
		restored->mesh.data.transform.scale = savedTransform.scale;
		for (int m = 0; m < 9; ++m)
			restored->mesh.data.transform.matrix[m] = savedTransform.matrix[m];
	}
	installDynamicBackgroundForRoom(_activeRoomName);

	// CPU runtime state stores transform/life state, while body resources are
	// reconstructed from char.isc plus the saved body root.
	for (uint32 cpuIndex = 0; cpuIndex < _cpuCharacters.size(); ++cpuIndex) {
		CpuCharacterRuntime &runtime = _cpuCharacters[cpuIndex];
		Common::String assetStem = runtime.bodyRoot;
		if (assetStem.empty()) {
			const CharacterDefinition *definition =
				_playerCharacterScript.findCharacter(runtime.name);
			if (definition)
				assetStem = definition->initialBodyName;
		}
		const uint32 separator = assetStem.find('_');
		if (separator != Common::String::npos && separator + 1 < assetStem.size())
			assetStem = assetStem.substr(separator + 1);

		Common::String lowerStem = assetStem;
		lowerStem.toLowercase();
		Common::Array<Common::Path> directories;
		directories.push_back(
			Common::Path(_currentMainPlace + "/bodies").appendComponent(lowerStem));
		directories.push_back(Common::Path("Mpx/bodies").appendComponent(lowerStem));
		directories.push_back(Common::Path("Mpx/bodies").appendComponent(assetStem));
		directories.push_back(Common::Path("Mpx/bodies").appendComponent(runtime.name));

		Common::Array<Common::String> fileStems;
		fileStems.push_back(assetStem);
		if (lowerStem != assetStem)
			fileStems.push_back(lowerStem);
		if (!runtime.name.equalsIgnoreCase(assetStem))
			fileStems.push_back(runtime.name);

		bool loaded = false;
		for (uint32 directoryIndex = 0;
		     directoryIndex < directories.size() && !loaded; ++directoryIndex) {
			for (uint32 stemIndex = 0;
			     stemIndex < fileStems.size() && !loaded; ++stemIndex) {
				SceneModel body;
				loaded = body.loadPair(
					directories[directoryIndex].appendComponent(fileStems[stemIndex] + ".p3d"),
					directories[directoryIndex].appendComponent(fileStems[stemIndex] + ".anj"));
				if (loaded) {
					body.resolveSkinnedGeometry();
					runtime.scene = body;
					runtime.assetDirectory = directories[directoryIndex];
				}
			}
		}
		if (!loaded)
			warning("Zero Comico: cannot restore CPU body %s for %s",
			        runtime.bodyRoot.c_str(), runtime.name.c_str());
	}

	installCpuCharactersForRoom(_activeRoomName);

	if (!_activeWalkMapName.empty()) {
		BspMap restoredWalkMap;
		const Common::Path restoredMapPath =
			Common::Path(_currentMainPlace + "/gameplay").appendComponent(_activeWalkMapName);
		if (restoredWalkMap.load(restoredMapPath))
			_activeWalkMap = restoredWalkMap;
		else
			warning("Zero Comico: cannot restore walk map %s",
			        _activeWalkMapName.c_str());
	}

	_playerNavNode = _activeWalkMap.graph.empty()
		? -1 : _activeWalkMap.nearestGraphNode(_playerPosition.x, _playerPosition.z);

	const Common::String restoredEnvironmentSound = _environmentSoundName;
	const bool restoredEnvironmentActive = _environmentSoundActive;
	_environmentSoundName.clear();
	_environmentSoundActive = false;
	setEnvironmentSound(restoredEnvironmentSound, restoredEnvironmentActive);

	_pendingLoadData.clear();
	_pendingLoadMainPlace.clear();
	_pendingLoadRoomName.clear();
	_pendingLoadVersion = 0;
	_pendingLoadActive = false;
	debug(1, "Zero Comico: applied staged restore in %s/%s",
	      _currentMainPlace.c_str(), _activeRoomName.c_str());
	return true;
}

bool ZeroComicoEngine::runMainPlacePreview(const Common::String &mainPlace) {
	if (mainPlace.empty())
		return false;

	Common::String level = mainPlace;
	if (level.size() >= 2 &&
	    (level[0] == 'm' || level[0] == 'M') &&
	    (level[1] == 'p' || level[1] == 'P'))
		level = Common::String("Mp") + level.substr(2);

	const bool restoringStagedSave =
		_pendingLoadActive && _pendingLoadMainPlace.equalsIgnoreCase(level);
	const Common::String stagedRoomName =
		restoringStagedSave ? _pendingLoadRoomName : Common::String();

	_currentMainPlace = level;
	_activeRoomName.clear();
	_activeRoomPrefix.clear();
	_activeRoomMaps.clear();
	_activeRoomCameraMaps.clear();
	_activeWalkMapName.clear();
	_activeAutoCameraTrigger.clear();
	_scriptKeyMask = 0;
	_scriptAudioClass = 3;
	_portalsEnabled = true;
	_cameraMode = 0;
	_cameraModeLocked = false;
	_playerNoCameraReset = false;
	_depthCueEnabled = false;
	_shadeEnabled = false;
	_depthCueStart = 0.0f;
	_depthCueEnd = 0.0f;
	for (int component = 0; component < 4; ++component) {
		_masterColor[component] = 1.0f;
		_masterColorFrom[component] = 1.0f;
		_masterColorTo[component] = 1.0f;
	}
	_masterColorFadeSteps = 0.0f;
	_masterColorFadeStartMillis = 0;
	_masterColorFadeActive = false;
	_spotCameraInitialized = false;
	_dynamicCameraInitialized = false;
	_spotHeight = 85.0f;
	_spotMaxDeltaY = 30.0f;
	_spotDistance = 350.0f;
	_spotMinDistance = 25.0f;
	_spotSmooth = 30.0f;
	_playerHatVisible = true;
	_pendingSaySpeaker.clear();
	_pendingSayText.clear();
	_pendingDialogName.clear();
	_lastDialogueChoice = -1;
	_pendingRoomName.clear();
	_pendingRoomCutscene.clear();
	_pendingRoomMapRoomName.clear();
	_pendingRoomMapName.clear();
	_dialogCameraFirstName.clear();
	_dialogCameraSecondName.clear();
	_lastDialogCameraName.clear();
	_selectedInventoryObject.clear();
	_combineInventoryFirst.clear();
	_combineInventorySecond.clear();
	_inventoryObjects.clear();
	_hiddenSceneMeshes.clear();
	stopRoomEnvironmentSounds();
	if (_mixer->isSoundHandleActive(_environmentSoundHandle))
		_mixer->stopHandle(_environmentSoundHandle);
	_environmentSoundName.clear();
	_environmentSoundActive = false;
	_environmentStateRooms.clear();
	_environmentStateNames.clear();
	_environmentStateEnabled.clear();
	_lightStateNames.clear();
	_lightStateEnabled.clear();
	_userEffectStateNames.clear();
	_userEffectStates.clear();
	_userEffectElapsedMs.clear();
	_userEffectStateChangedMillis.clear();

	// Chapter-end scripts (Mp1..Mp5) intentionally use
	// mch_push_master_volume 0 before their AVI and then ChangeMainplace without
	// a matching pop. The next main-place BeginTime reinitializes the retail
	// logical volume, so also unwind any outstanding mixer snapshot here. This
	// prevents a chapter transition from leaving ScummVM's sound-type mixers at
	// zero while preserving the user's pre-push mixer settings.
	if (_mixer && !_masterVolumeStack.empty()) {
		const MixerVolumeSnapshot snapshot = _masterVolumeStack[0];
		_mixer->setVolumeForSoundType(Audio::Mixer::kPlainSoundType, snapshot.plain);
		_mixer->setVolumeForSoundType(Audio::Mixer::kSFXSoundType, snapshot.sfx);
		_mixer->setVolumeForSoundType(Audio::Mixer::kMusicSoundType, snapshot.music);
		_mixer->setVolumeForSoundType(Audio::Mixer::kSpeechSoundType, snapshot.speech);
		debug(1, "Zero Comico: unwound master-volume stack at main-place boundary");
	}
	_masterVolumeStack.clear();
	_globalMasterVolume = 100.0f;
	for (int soundClass = 0; soundClass < 6; ++soundClass)
		_soundClassVolumes[soundClass] = 100.0f;
	_sampleDefaultFarVolume = 0.0f;
	_sampleDefaultNearVolume = 100.0f;
	_sampleDefaultMinRange = 0.0f;
	_sampleDefaultMaxRange = 500.0f;
	_samplePlaybackParams.clear();
	_sceneLoopTargets.clear();
	_sceneLoopSources.clear();
	_sceneLoopStartMillis.clear();
	_sceneOneShotTargets.clear();
	_sceneOneShotSources.clear();
	_sceneOneShotStartMillis.clear();
	_loopCutScene.clear();
	_loopCutName.clear();
	_loopCutAssetStem.clear();
	_loopCutStartFrame = 0.0f;
	_loopCutEndFrame = 0.0f;
	_loopCutStartMillis = 0;
	_loopCutActive = false;
	_loadedSetpAssets.clear();
	_loadedSetpScenes.clear();
	_openCutScenes.clear();
	_setpControllerNames.clear();
	_setpControllerPositions.clear();
	_dynamicSceneEntities.clear();
	_cpuCharacters.clear();
	_deferredBrokenCpuCharacters.clear();
	_backgroundScriptThreads.clear();
	_lastBackgroundScriptTick = _system->getMillis();
	_activeRenderActors.clear();

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

	auto runRoomCode = [&](const RoomDefinition &roomDefinition) -> bool {
		_shadeEnabled = false;
		if (roomDefinition.codeStart == 0xffffffffU ||
		    roomDefinition.codeEnd == 0xffffffffU ||
		    roomDefinition.codeStart >= roomDefinition.codeEnd)
			return true;
		if (!runScriptWithAudioClass(roomProgram, roomDefinition.codeStart,
		                             roomDefinition.codeEnd, 512, 3)) {
			warning("Zero Comico: room code for %s stopped early",
			        roomDefinition.name.c_str());
			return false;
		}
		return true;
	};

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
		if (!runScriptWithAudioClass(roomProgram, startupStart, startupEnd, 4096, 3)) {
			warning("Zero Comico: failed to execute %s startup state", level.c_str());
			return false;
		}
	}

	const Common::String initialRoomName =
		restoringStagedSave ? stagedRoomName : chapter.startRoom;
	const RoomDefinition *room = chapter.findRoom(initialRoomName);
	if (!room) {
		warning("Zero Comico: %s room %s is not declared",
		        restoringStagedSave ? "saved" : "start", initialRoomName.c_str());
		return false;
	}

	_activeRoomName = room->name;
	_activeRoomPrefix = room->prefix;
	_activeRoomMaps = room->maps;
	_activeRoomCameraMaps = room->cameraMaps;

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
	_playerAnimSetName.clear();
	if (!_playerCharacterScript.load(characterPath)) {
		warning("Zero Comico: cannot parse playable character metadata %s",
		        characterPath.toString().c_str());
	} else {
		_playerAnimSetName = _playerCharacterScript.initialAnimSet;
		const uint32 separator = _playerCharacterScript.initialBodyName.find('_');
		if (separator != Common::String::npos &&
		    separator + 1 < _playerCharacterScript.initialBodyName.size())
			playerAssetStem = _playerCharacterScript.initialBodyName.substr(separator + 1);

		debug(1, "Zero Comico: main player %s uses %s (%s), combine block=%s",
		      _playerCharacterScript.playerName.c_str(),
		      _playerCharacterScript.initialBodyName.c_str(), playerAssetStem.c_str(),
		      _playerCharacterScript.hasCombineBlock() ? "yes" : "no");
	}

	// BeginTime is a once-per-main-place initialization phase in the retail
	// room script. It runs after startup variables exist and after the initial
	// room/player identity is known, but before the main runtime thread.
	uint32 beginTimeStart = roomProgram.instructions().size();
	uint32 beginTimeEnd = roomProgram.instructions().size();
	for (uint32 i = 0; i < roomProgram.instructions().size(); ++i) {
		if (!roomProgram.instructions()[i].opcode.equalsIgnoreCase("BeginTime"))
			continue;
		beginTimeStart = i + 1;
		for (uint32 j = beginTimeStart; j < roomProgram.instructions().size(); ++j) {
			if (roomProgram.instructions()[j].opcode.equalsIgnoreCase("end")) {
				beginTimeEnd = j;
				break;
			}
		}
		break;
	}
	if (beginTimeStart < beginTimeEnd) {
		if (!runScriptWithAudioClass(roomProgram, beginTimeStart, beginTimeEnd, 4096, 3)) {
			warning("Zero Comico: failed to execute %s BeginTime state", level.c_str());
			return false;
		}
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

	_playerAssetDirectory = playerDirectory;

	// CPU bodies are loaded lazily per room. Their initialize blocks run through
	// the same ScriptVM once the destination room scene is available, so spawn
	// markers, wait state and initial play/playl commands remain data-driven.
	ensureCpuCharactersForRoom(_activeRoomName);

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

	_activeCameraTriggers = PuzzleScript();
	_activeCameraShapes = ShapeScript();
	const Common::Path cameraTriggerPath(level + "/gameplay/camera.gsc");
	const Common::Path cameraShapePath(level + "/gameplay/camera.shp");
	const bool haveCameraTriggers = _activeCameraTriggers.load(cameraTriggerPath);
	const bool haveCameraShapes = _activeCameraShapes.load(cameraShapePath);
	if (haveCameraTriggers && haveCameraShapes) {
		debug(1, "Zero Comico: loaded %u automatic camera triggers, %u polygons and %u range shapes",
		      (uint)_activeCameraTriggers.objects.size(),
		      (uint)_activeCameraShapes.polygons().size(),
		      (uint)_activeCameraShapes.shapes().size());
	} else if (level != "Mp0") {
		warning("Zero Comico: automatic camera trigger data is incomplete for %s",
		        level.c_str());
	}

	const Common::Path dialogPath(level + "/gameplay/dialog.isc");
	if (!_activeDialog.load(dialogPath))
		warning("Zero Comico: cannot parse dialogue file %s", dialogPath.toString().c_str());
	else
		debug(1, "Zero Comico: loaded %u dialogues and %u speakers",
		      (uint)_activeDialog.dialogs.size(), (uint)_activeDialog.speakers.size());

	const Common::Path textTablePath(level + "/gameplay/scene.isc");
	if (!_activeTextTables.load(textTablePath))
		warning("Zero Comico: cannot parse scene text tables %s", textTablePath.toString().c_str());
	else
		debug(1, "Zero Comico: loaded %u scene text tables",
		      (uint)_activeTextTables.tables.size());

	// Load both navigation layers declared by room.isc. The ordinary map
	// carries the walkable floor/path graph; cameramap is the camera-control
	// partition used by the original runtime.
	_activeWalkMap = BspMap();
	_activeCameraMap = BspMap();
	if (!room->maps.empty()) {
		const Common::Path mapPath = Common::Path(level + "/gameplay").appendComponent(room->maps[0]);
		if (!_activeWalkMap.load(mapPath))
			warning("Zero Comico: cannot load walk map %s", mapPath.toString().c_str());
		else
			_activeWalkMapName = room->maps[0];
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

	_defaultRoomCameraName = room->camera;
	Common::String cameraName = room->camera;
	RenderCamera renderCamera;
	bool haveRenderCamera = false;

	// Gameplay rooms do not use the editor camera embedded in room*.p3d.
	// room.isc points at an alias whose source/target/FOV live in Camera.scr.
	// Using that script camera fixes the start-room viewpoint instead of
	// falling back to the unrelated exported editor camera.
	_activeCameraScript = CameraScript();
	const Common::Path cameraScriptPath(level + "/gameplay/Camera.scr");
	if (_activeCameraScript.load(cameraScriptPath)) {
		const ScriptCamera *scriptCamera = _activeCameraScript.findCamera(cameraName);
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

	if (!runRoomCode(*room))
		return false;
	startRoomMusic(room->music, room->musicVolume);

	if (restoringStagedSave && !applyStagedRestore(playerDirectory))
		return false;
	startRoomEnvironmentSounds(*room);

	auto applyPendingCamera = [&]() -> bool {
		if (_pendingCameraName.empty())
			return false;

		const Common::String requested = _pendingCameraName;
		_pendingCameraName.clear();

		const ScriptCamera *scriptCamera = _activeCameraScript.findCamera(requested);
		if (scriptCamera) {
			const float radians = scriptCamera->horizontalFovDegrees *
				3.14159265358979323846f / 180.0f;
			const float halfTan = std::tan(radians * 0.5f);
			if (halfTan > 0.0001f) {
				cameraName = requested;
				renderCamera.position = scriptCamera->source;
				renderCamera.target = scriptCamera->target;
				renderCamera.focalPixels = 400.0f / halfTan;
				renderCamera.rollRadians = 0.0f;
				debug(1, "Zero Comico: applied scripted camera %s", requested.c_str());
				return true;
			}
		}

		const NamedCamera *embedded = _activeScene.findCamera(requested);
		if (embedded && embedded->data.fov > 0.0f) {
			cameraName = requested;
			renderCamera.position = embedded->data.position;
			renderCamera.target = embedded->data.target;
			renderCamera.focalPixels = embedded->data.fov * 800.0f / 36.0f;
			renderCamera.rollRadians = 0.0f;
			debug(1, "Zero Comico: applied embedded camera %s", requested.c_str());
			return true;
		}

		warning("Zero Comico: requested camera %s is not available in the active room",
		        requested.c_str());
		return false;
	};

	auto updateAutoCamera = [&]() -> bool {
		if (_cameraMode != 0) {
			_activeAutoCameraTrigger.clear();
			return false;
		}

		const PuzzleObject *activeTrigger = nullptr;
		for (uint32 i = 0; i < _activeCameraTriggers.objects.size(); ++i) {
			const PuzzleObject &candidate = _activeCameraTriggers.objects[i];
			if (!candidate.enabled || !candidate.autoCamera ||
			    !startsWithIgnoreCase(candidate.name, _activeRoomPrefix))
				continue;

			const Common::String &region = candidate.polygon.empty()
				? candidate.rangeShape : candidate.polygon;
			if (region.empty() ||
			    !_activeCameraShapes.containsRegion(region, _playerPosition.x, _playerPosition.z))
				continue;

			activeTrigger = &candidate;
			break;
		}

		if (!activeTrigger) {
			_activeAutoCameraTrigger.clear();
			return false;
		}
		if (_activeAutoCameraTrigger.equalsIgnoreCase(activeTrigger->name))
			return false;

		_activeAutoCameraTrigger = activeTrigger->name;
		debug(1, "Zero Comico: entered automatic camera region %s",
		      activeTrigger->name.c_str());

		if (activeTrigger->enterStart != 0xffffffffU &&
		    activeTrigger->enterEnd != 0xffffffffU &&
		    activeTrigger->enterStart < activeTrigger->enterEnd) {
			if (!runScriptWithAudioClass(_activeCameraTriggers.program(),
			                             activeTrigger->enterStart, activeTrigger->enterEnd,
			                             64, 3)) {
				warning("Zero Comico: automatic camera trigger %s failed",
				        activeTrigger->name.c_str());
				return false;
			}
		} else {
			// All retail camera.gsc triggers currently use an in: SetFocus block,
			// but keep a safe data-driven fallback for malformed/custom data.
			_pendingCameraName = activeTrigger->name;
		}

		return applyPendingCamera();
	};

	auto updateDynamicCamera = [&](const Common::String &cameraAnimationSource,
	                               float cameraAnimationFrame) -> bool {
		if (_cameraMode == 0) {
			_dynamicCameraInitialized = false;
			return false;
		}

		Vec3f forward = subtractVec3(_playerFacingTarget, _playerPosition);
		forward.y = 0.0f;
		if (!normalizeVec3(forward)) {
			forward = subtractVec3(renderCamera.target, renderCamera.position);
			forward.y = 0.0f;
			if (!normalizeVec3(forward)) {
				forward.x = 0.0f;
				forward.y = 0.0f;
				forward.z = -1.0f;
			}
		}

		if (_cameraMode == 1) {
			// The retail Subjective camera follows the live character's *_testa
			// hierarchy node. The shipped playable roots resolve to gio_giotesta,
			// ald_aldtesta and gia_giatesta. Sample that animated attachment in
			// actor-local space and apply exactly the same root/yaw/world placement
			// used by the gameplay renderer. Keep the measured 0.52 m fallback for
			// malformed/custom bodies that do not expose the attachment.
			Vec3f source = _playerPosition;
			source.y += kSubjectiveFallbackEyeHeight;

			const Common::String playerRoot = !_playerSequences.bodyName.empty()
				? _playerSequences.bodyName : _playerCharacterScript.initialBodyName;
			const Common::String headNode = subjectiveHeadNodeName(playerRoot);
			Vec3f headLocal;
			RenderTransform actorTransform;
			actorTransform.translation = _playerPosition;
			actorTransform.yawRadians = std::atan2(forward.x, forward.z);
			if (!headNode.empty() &&
			    _playerScene.sampleHierarchyPoint(playerRoot, headNode,
			                                      cameraAnimationSource,
			                                      cameraAnimationFrame, headLocal) &&
			    sampleRootTransform(_playerScene, playerRoot, cameraAnimationSource,
			                        cameraAnimationFrame, actorTransform)) {
				source = transformActorLocalPoint(headLocal, actorTransform);
			}

			source.x += forward.x * 12.5f;
			source.z += forward.z * 12.5f;

			if (!_activeCameraMap.polygons.empty()) {
				Vec2 constrained;
				if (_activeCameraMap.nearestWalkablePoint(source.x, source.z, constrained)) {
					source.x = constrained.x;
					source.z = constrained.y;
				}
			}

			renderCamera.position = source;
			renderCamera.target = source;
			renderCamera.target.x += forward.x * 40.0f;
			renderCamera.target.z += forward.z * 40.0f;
			renderCamera.rollRadians = 0.0f;
			_dynamicCameraPosition = source;
			_dynamicCameraInitialized = true;
			return true;
		}

		// Retail Spot mode builds the focus at character Y + SpotHeight, rotates
		// a (0,0,SpotDistance) vector by the actor orientation, then resolves that
		// candidate through the camera map. Player-facing direction is the native
		// equivalent of the actor orientation already tracked by this runtime.
		Vec3f focus = _playerPosition;
		focus.y += _spotHeight;

		Vec3f desired = focus;
		desired.x -= forward.x * _spotDistance;
		desired.z -= forward.z * _spotDistance;

		if (!_activeCameraMap.polygons.empty()) {
			if (_activeCameraMap.containsWalkablePoint(focus.x, focus.z)) {
				Vec2 constrained;
				if (_activeCameraMap.clipWalkableSegment(
						focus.x, focus.z, desired.x, desired.z, constrained)) {
					desired.x = constrained.x;
					desired.z = constrained.y;
				}
			} else {
				// The retail update checks the focus against MapCam before clipping.
				// If the focus itself is outside, it collapses the boom to the same
				// actor-relative direction at 10 world units instead of snapping to
				// an unrelated nearest boundary point.
				desired = focus;
				desired.x -= forward.x * 10.0f;
				desired.z -= forward.z * 10.0f;
			}
		}

		const float dx = desired.x - focus.x;
		const float dz = desired.z - focus.z;
		const float actualDistance = std::sqrt(dx * dx + dz * dz);
		const float ratio = actualDistance / _spotDistance;

		// Zero Comico.exe uses the raw distance ratio here: there is no clamp.
		// It raises the camera by (1-ratio)*MaxSpotDeltaY and the focus by half
		// that amount after MapCam has shortened the horizontal boom.
		const float verticalCorrection = (1.0f - ratio) * _spotMaxDeltaY;
		desired.y = focus.y + verticalCorrection;
		focus.y += verticalCorrection * 0.5f;

		// Spot smoothing has its own persistent state in the retail engine.
		// Room/scene setup sets a one-shot reset flag; mode switches do not erase
		// the previous Spot position. On normal frames the exact update is:
		// current += (desired - current) / SpotSmooth.
		if (!_spotCameraInitialized) {
			_spotCameraPosition = desired;
			_spotCameraInitialized = true;
		} else {
			_spotCameraPosition.x += (desired.x - _spotCameraPosition.x) / _spotSmooth;
			_spotCameraPosition.y += (desired.y - _spotCameraPosition.y) / _spotSmooth;
			_spotCameraPosition.z += (desired.z - _spotCameraPosition.z) / _spotSmooth;
		}

		Vec3f lookDirection = subtractVec3(focus, _spotCameraPosition);
		lookDirection.y = 0.0f;
		if (!normalizeVec3(lookDirection))
			lookDirection = forward;

		// The executable pads the camera away from the focus by SpotMinDistance
		// after smoothing, then aims 1.5 m (150 retail world units) beyond focus.
		renderCamera.position = _spotCameraPosition;
		renderCamera.position.x -= lookDirection.x * _spotMinDistance;
		renderCamera.position.z -= lookDirection.z * _spotMinDistance;
		renderCamera.target = focus;
		renderCamera.target.x += lookDirection.x * 150.0f;
		renderCamera.target.z += lookDirection.z * 150.0f;
		renderCamera.rollRadians = 0.0f;
		return true;
	};

	if (!restoringStagedSave) {
		if (!runMainPlaceRuntime(roomProgram))
			warning("Zero Comico: main-place runtime block did not complete cleanly");
	}
	applyPendingCamera();
	updateAutoCamera();
	updateDynamicCamera("Stay", 0.0f);
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
		const DialogSpeaker *pendingSpeaker =
			_activeDialog.findSpeakerByName(_pendingSaySpeaker);
		drawCutsceneSubtitle(frame, _pendingSaySpeaker, _pendingSayText, pendingSpeaker);
		_system->copyRectToScreen(frame.getPixels(), frame.pitch, 0, 0, frame.w, frame.h);
		_system->updateScreen();

		const uint32 sayDuration = retailTextDurationMillis(
			_pendingSayText, pendingSpeaker ? pendingSpeaker->speed : 0.07f);

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

	// Puzzle entities can belong either to the room background or to a separately
	// rendered CPU character. The picker already traverses _activeRenderActors,
	// but older interaction-list construction discarded actor-only entities before
	// they ever reached it. Mp1's coc_cocco (Room1_4) and ope_operaio (Room1_5)
	// are progression-critical examples.
	auto interactionEntityAvailable = [&](const Common::String &entity) -> bool {
		if (_activeScene.findMesh(entity) || _activeScene.hasHierarchy(entity))
			return true;
		for (uint32 cpuIndex = 0; cpuIndex < _cpuCharacters.size(); ++cpuIndex) {
			const CpuCharacterRuntime &character = _cpuCharacters[cpuIndex];
			if (!character.alive || !character.positioned ||
			    !character.roomName.equalsIgnoreCase(_activeRoomName))
				continue;
			if (character.scene.findMesh(entity) ||
			    character.scene.hasHierarchy(entity) ||
			    character.bodyRoot.equalsIgnoreCase(entity))
				return true;
		}
		return false;
	};

	for (uint32 objectIndex = 0; objectIndex < _activePuzzle.objects.size(); ++objectIndex) {
		const PuzzleObject &object = _activePuzzle.objects[objectIndex];
		if (!object.enabled || object.entity.empty())
			continue;
		const Common::String sceneEntity =
			resolveSceneEntity(_activeScene, object.entity, _activeRoomPrefix);
		if (!interactionEntityAvailable(sceneEntity) ||
		    containsIgnoreCase(_hiddenSceneMeshes, sceneEntity))
			continue;
		if (object.examinable)
			examineMeshes.push_back(sceneEntity);
		if (object.operateStart != 0xffffffffU && object.operateEnd != 0xffffffffU &&
		    object.operateStart < object.operateEnd)
			operateMeshes.push_back(sceneEntity);
	}

	auto rebuildInteractionMeshes = [&]() {
		examineMeshes.clear();
		operateMeshes.clear();
		for (uint32 objectIndex = 0; objectIndex < _activePuzzle.objects.size(); ++objectIndex) {
			const PuzzleObject &object = _activePuzzle.objects[objectIndex];
			if (!object.enabled || object.entity.empty())
				continue;
			const Common::String entity = resolveSceneEntity(
				_activeScene, object.entity, _activeRoomPrefix);
			if (!interactionEntityAvailable(entity) ||
			    containsIgnoreCase(_hiddenSceneMeshes, entity))
				continue;
			if (object.examinable)
				examineMeshes.push_back(entity);
			if (object.operateStart != 0xffffffffU &&
			    object.operateEnd != 0xffffffffU &&
			    object.operateStart < object.operateEnd)
				operateMeshes.push_back(entity);
		}
	};

	auto findPortalShape = [&](const RoomDefinition &sourceRoom,
	                           const RoomPortal &portal) -> const ShapeMarker * {
		const Common::String candidates[] = {
			portal.marker, portal.name, portal.destinationPortal
		};
		for (uint32 i = 0; i < ARRAYSIZE(candidates); ++i) {
			if (candidates[i].empty())
				continue;
			const ShapeMarker *shape = _activeShapes.find(candidates[i]);
			if (shape && shape->kind.equalsIgnoreCase("Portal"))
				return shape;
		}

		// Mp5 stores one physical Portal shape for each connection, while the
		// reverse room declaration can use a mirrored logical name. Resolve that
		// reverse declaration through the destination room's outbound portal.
		const RoomDefinition *destination = chapter.findRoom(portal.destinationRoom);
		if (!destination)
			return nullptr;
		for (uint32 i = 0; i < destination->portals.size(); ++i) {
			const RoomPortal &reverse = destination->portals[i];
			if (!reverse.destinationRoom.equalsIgnoreCase(sourceRoom.name))
				continue;
			const Common::String reverseCandidates[] = {
				reverse.marker, reverse.name, reverse.destinationPortal
			};
			for (uint32 j = 0; j < ARRAYSIZE(reverseCandidates); ++j) {
				if (reverseCandidates[j].empty())
					continue;
				const ShapeMarker *shape = _activeShapes.find(reverseCandidates[j]);
				if (shape && shape->kind.equalsIgnoreCase("Portal"))
					return shape;
			}
		}
		return nullptr;
	};

	auto applyPendingRoomTransition = [&]() -> bool {
		if (_pendingRoomName.empty())
			return true;

		if (_pendingRoomCutscene.equalsIgnoreCase("d101"))
			playCutscene("d101_dor");

		const RoomDefinition *nextRoom = chapter.findRoom(_pendingRoomName);
		if (!nextRoom) {
			warning("Zero Comico: destination room %s is not declared",
			        _pendingRoomName.c_str());
			_pendingRoomName.clear();
			_pendingRoomCutscene.clear();
			return true;
		}

		stopRoomEnvironmentSounds();
		room = nextRoom;
		_activeRoomName = room->name;
		_activeRoomPrefix = room->prefix;
		for (uint32 objectIndex = 0; objectIndex < _activePuzzle.objects.size(); ++objectIndex)
			_activePuzzle.objects[objectIndex].inside = false;
		_activeRoomMaps = room->maps;
		_activeRoomCameraMaps = room->cameraMaps;
		Common::String nextStem = room->name;
		nextStem.toLowercase();

		_activeScene.clear();
		if (!_activeScene.loadPair(
				sceneDirectory.appendComponent(nextStem + ".p3d"),
				sceneDirectory.appendComponent(nextStem + ".anj"))) {
			warning("Zero Comico: cannot load destination room %s",
			        room->name.c_str());
			return false;
		}
		for (uint32 setpIndex = 0; setpIndex < _loadedSetpScenes.size(); ++setpIndex)
			_activeScene.mergeFrom(_loadedSetpScenes[setpIndex]);
		installOpenCutScenesForRoom(room->name);

		// Reapply controller offsets after the freshly loaded Setp asset records
		// have been merged into the new room scene.
		for (uint32 controllerIndex = 0; controllerIndex < _setpControllerNames.size(); ++controllerIndex)
			_activeScene.translateHierarchy(_setpControllerNames[controllerIndex],
			                                _setpControllerPositions[controllerIndex]);

		installDynamicBackgroundForRoom(room->name);
		ensureCpuCharactersForRoom(room->name);

		_activeWalkMap = BspMap();
		_activeCameraMap = BspMap();
		Common::String destinationMap;
		if (!room->maps.empty())
			destinationMap = room->maps[0];
		if (_pendingRoomMapRoomName.equalsIgnoreCase(room->name) &&
		    !_pendingRoomMapName.empty()) {
			if (containsIgnoreCase(room->maps, _pendingRoomMapName))
				destinationMap = _pendingRoomMapName;
			else
				warning("Zero Comico: queued map %s is not declared for destination room %s",
				        _pendingRoomMapName.c_str(), room->name.c_str());
		}
		if (!destinationMap.empty()) {
			const Common::Path nextMap = Common::Path(level + "/gameplay")
				.appendComponent(destinationMap);
			if (!_activeWalkMap.load(nextMap))
				warning("Zero Comico: cannot load destination walk map %s",
				        nextMap.toString().c_str());
			else
				_activeWalkMapName = destinationMap;
		}
		_pendingRoomMapRoomName.clear();
		_pendingRoomMapName.clear();

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

		_pendingCameraName.clear();
		_activeAutoCameraTrigger.clear();
		_spotCameraInitialized = false;
		_dynamicCameraInitialized = false;
		_defaultRoomCameraName = room->camera;
		if (!runRoomCode(*room))
			return false;
		startRoomMusic(room->music, room->musicVolume);
		startRoomEnvironmentSounds(*room);
		cameraName = room->camera;

		bool nextCameraReady = false;
		const ScriptCamera *nextScriptCamera = _activeCameraScript.findCamera(cameraName);
		if (nextScriptCamera) {
			const float radians = nextScriptCamera->horizontalFovDegrees *
				3.14159265358979323846f / 180.0f;
			const float halfTan = std::tan(radians * 0.5f);
			if (halfTan > 0.0001f) {
				renderCamera.position = nextScriptCamera->source;
				renderCamera.target = nextScriptCamera->target;
				renderCamera.focalPixels = 400.0f / halfTan;
				renderCamera.rollRadians = 0.0f;
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
				renderCamera.rollRadians = 0.0f;
				nextCameraReady = true;
			}
		}
		if (!nextCameraReady) {
			warning("Zero Comico: destination room %s has no usable camera",
			        room->name.c_str());
			return false;
		}

		rebuildInteractionMeshes();
		updateAutoCamera();
		updateDynamicCamera("Stay", 0.0f);
		debug(1, "Zero Comico: changed place to %s at nav node %d",
		      room->name.c_str(), _playerNavNode);
		renderGameplayFrame(renderCamera, sceneDirectory, playerDirectory,
		                    "Stay", 0.0f, frame);

		_pendingRoomName.clear();
		_pendingRoomCutscene.clear();
		return true;
	};

	auto runPuzzleRegionTransitions = [&]() -> bool {
		for (uint32 objectIndex = 0; objectIndex < _activePuzzle.objects.size(); ++objectIndex) {
			PuzzleObject &object = _activePuzzle.objects[objectIndex];
			if (!object.enabled)
				continue;

			const Common::String regionName = !object.rangeShape.empty()
				? object.rangeShape : object.polygon;
			if (regionName.empty())
				continue;

			// puzzle.isc is a main-place-wide object table, not a set of Room
			// blocks. The retail room association is carried by the same rXY_
			// prefix declared as Room.Prefix in room.isc. A few ordinary objects
			// additionally carry roomscope strings such as " r41_*"; normalize
			// those as prefix wildcards rather than comparing them to Room4_1.
			bool inRoomScope = true;
			if (!object.roomScope.empty()) {
				Common::String scope = object.roomScope;
				scope.trim();
				while (!scope.empty() && scope[scope.size() - 1] == '*')
					scope.deleteLastChar();
				inRoomScope = scope.empty() || startsWithIgnoreCase(_activeRoomPrefix, scope);
			} else if (!_activeRoomPrefix.empty() &&
			           (startsWithIgnoreCase(object.name, "r") ||
			            startsWithIgnoreCase(regionName, "r"))) {
				inRoomScope = startsWithIgnoreCase(object.name, _activeRoomPrefix) ||
				              startsWithIgnoreCase(regionName, _activeRoomPrefix);
			}
			if (!inRoomScope)
				continue;

			const bool insideNow = _activeShapes.containsRegion(
				regionName, _playerPosition.x, _playerPosition.z);
			if (insideNow == object.inside)
				continue;

			object.inside = insideNow;
			const uint32 start = insideNow ? object.enterStart : object.exitStart;
			const uint32 end = insideNow ? object.enterEnd : object.exitEnd;
			if (start == 0xffffffffU || end == 0xffffffffU || start >= end)
				continue;

			debug(1, "Zero Comico: player %s puzzle region %s",
			      insideNow ? "entered" : "left", object.name.c_str());

			_scriptDialogueContextActive = true;
			_scriptDialogueCamera = renderCamera;
			_scriptDialogueSceneDirectory = sceneDirectory;
			_scriptDialoguePlayerDirectory = playerDirectory;
			_scriptDialogueFrame = &frame;
			const bool regionOk =
				runScriptWithAudioClass(_activePuzzle.program(), start, end, 8192, 2);
			_scriptDialogueContextActive = false;
			_scriptDialogueFrame = nullptr;
			if (!regionOk)
				warning("Zero Comico: puzzle region %s stopped on an unsupported opcode",
				        object.name.c_str());

			rebuildInteractionMeshes();
			if (!_pendingDialogName.empty() && !shouldQuit()) {
				playDialogue(_pendingDialogName, renderCamera, sceneDirectory,
				             playerDirectory, frame);
				_pendingDialogName.clear();
			}
			if (!_pendingMainPlace.empty())
				return true;
			if (!_pendingRoomName.empty()) {
				if (!applyPendingRoomTransition())
					return false;
				return true;
			}
		}
		return true;
	};

	bool done = false;
	uint32 lastIdleRender = _system->getMillis();
	const uint32 idleAnimationStart = lastIdleRender;
	uint32 lastControlCodeTick = lastIdleRender - 40U;

	auto runCharacterControlCodes = [&]() -> bool {
		const uint32 now = _system->getMillis();
		if (now - lastControlCodeTick < 40U)
			return true;
		lastControlCodeTick = now;

		for (uint32 characterIndex = 0;
		     characterIndex < _playerCharacterScript.characters.size();
		     ++characterIndex) {
			const CharacterDefinition &definition =
				_playerCharacterScript.characters[characterIndex];
			if (definition.controlStart == 0xffffffffU ||
			    definition.controlEnd == 0xffffffffU ||
			    definition.controlStart >= definition.controlEnd)
				continue;

			if (!definition.mainPlayer) {
				CpuCharacterRuntime *runtime = findCpuCharacter(definition.name);
				if (!runtime || !runtime->alive || runtime->lifeBroken ||
				    !runtime->roomName.equalsIgnoreCase(_activeRoomName))
					continue;
			}

			_scriptDialogueContextActive = true;
			_scriptDialogueCamera = renderCamera;
			_scriptDialogueSceneDirectory = sceneDirectory;
			_scriptDialoguePlayerDirectory = playerDirectory;
			_scriptDialogueFrame = &frame;
			const bool ok = runScriptWithAudioClass(_playerCharacterScript.program(),
			                                        definition.controlStart,
			                                        definition.controlEnd, 512, 2);
			_scriptDialogueContextActive = false;
			_scriptDialogueFrame = nullptr;
			if (!ok) {
				warning("Zero Comico: ControlCode for %s stopped on an unsupported opcode",
				        definition.name.c_str());
				return false;
			}
		}
		return true;
	};

	while (!shouldQuit() && !done) {
		if (_pendingLoadActive && !_pendingLoadMainPlace.empty()) {
			_pendingMainPlace = _pendingLoadMainPlace;
			done = true;
			break;
		}

		_scriptDialogueContextActive = true;
		_scriptDialogueCamera = renderCamera;
		_scriptDialogueSceneDirectory = sceneDirectory;
		_scriptDialoguePlayerDirectory = playerDirectory;
		_scriptDialogueFrame = &frame;
		const bool backgroundOk = runBackgroundScriptThreads();
		_scriptDialogueContextActive = false;
		_scriptDialogueFrame = nullptr;
		if (!backgroundOk)
			return false;

		if (!runCharacterControlCodes())
			return false;
		if (!runPuzzleRegionTransitions())
			return false;
		updateRoomEnvironmentSounds();
		if (!_pendingMainPlace.empty()) {
			done = true;
			break;
		}

		Common::Event event;
		while (_system->getEventManager()->pollEvent(event)) {
			if (event.type == Common::EVENT_QUIT || event.type == Common::EVENT_RETURN_TO_LAUNCHER) {
				quitGame();
				break;
			}
			if (event.type == Common::EVENT_KEYDOWN &&
			    event.kbd.keycode == Common::KEYCODE_F5) {
				saveGameDialog();
				break;
			}
			if (event.type == Common::EVENT_KEYDOWN &&
			    event.kbd.keycode == Common::KEYCODE_F9) {
				if (loadGameDialog() && _pendingLoadActive) {
					_pendingMainPlace = _pendingLoadMainPlace;
					done = true;
				}
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

					const CharacterDefinition *playerDefinition =
						_playerCharacterScript.findCharacter(_playerCharacterScript.playerName);
					if (playerDefinition &&
					    playerDefinition->hidingStart != 0xffffffffU &&
					    playerDefinition->hidingEnd != 0xffffffffU &&
					    playerDefinition->hidingStart < playerDefinition->hidingEnd) {
						_scriptDialogueContextActive = true;
						_scriptDialogueCamera = renderCamera;
						_scriptDialogueSceneDirectory = sceneDirectory;
						_scriptDialoguePlayerDirectory = playerDirectory;
						_scriptDialogueFrame = &frame;
						const bool hidingOk = runScriptWithAudioClass(
							_playerCharacterScript.program(),
							playerDefinition->hidingStart, playerDefinition->hidingEnd,
							2048, 3);
						_scriptDialogueContextActive = false;
						_scriptDialogueFrame = nullptr;
						if (!hidingOk)
							warning("Zero Comico: MainPlayer HidingCode stopped on an unsupported opcode");

						if (!_pendingMainPlace.empty()) {
							done = true;
							break;
						}
						if (!_pendingRoomName.empty() && !applyPendingRoomTransition()) {
							done = true;
							break;
						}
						rebuildInteractionMeshes();
					}

					if (!done && !shouldQuit())
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

				_scriptDialogueContextActive = true;
				_scriptDialogueCamera = renderCamera;
				_scriptDialogueSceneDirectory = sceneDirectory;
				_scriptDialoguePlayerDirectory = playerDirectory;
				_scriptDialogueFrame = &frame;
				const bool combineOk = runScriptWithAudioClass(
					_playerCharacterScript.program(), _playerCharacterScript.combineStart,
					_playerCharacterScript.combineEnd, 4096, 3);
				_scriptDialogueContextActive = false;
				_scriptDialogueFrame = nullptr;
				if (!combineOk)
					warning("Zero Comico: inventory combine block stopped on an unsupported opcode");

				_combineInventoryFirst.clear();
				_combineInventorySecond.clear();
				renderGameplayFrame(renderCamera, sceneDirectory, playerDirectory,
				                    "Stay", 0.0f, frame);

				if (!_pendingSayText.empty() && !shouldQuit()) {
					const Common::String effectiveSpeaker =
						_pendingSaySpeaker.empty()
							? (_playerCharacterScript.playerName.empty()
								? Common::String("MainPlayer") : _playerCharacterScript.playerName)
							: _pendingSaySpeaker;
					const DialogSpeaker *speakerInfo =
						_activeDialog.findSpeakerByName(effectiveSpeaker);
					drawCutsceneSubtitle(frame, effectiveSpeaker, _pendingSayText, speakerInfo);
					_system->copyRectToScreen(frame.getPixels(), frame.pitch,
					                          0, 0, frame.w, frame.h);
					_system->updateScreen();

					const uint32 sayDuration = retailTextDurationMillis(
						_pendingSayText, speakerInfo ? speakerInfo->speed : 0.07f);

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
				if (_gameplayRenderer.pickMeshWithActors(_activeScene, renderCamera,
				                               event.mouse.x, event.mouse.y,
				                               examineMeshes, _activeRenderActors, pickedEntity)) {
					const PuzzleObject *object = findPuzzleObjectForMesh(
						_activePuzzle, _activeScene, _activeRoomPrefix, pickedEntity);
					if (object && !object->examineText.empty()) {
						const Common::String examineSpeaker =
							_playerCharacterScript.playerName.empty()
								? Common::String("MainPlayer") : _playerCharacterScript.playerName;
						const DialogSpeaker *examineSpeakerInfo =
							_activeDialog.findSpeakerByName(examineSpeaker);
						drawCutsceneSubtitle(frame, examineSpeaker, object->examineText,
						                     examineSpeakerInfo);
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
			const PuzzleObject *object = nullptr;
			if (_gameplayRenderer.pickMeshWithActors(_activeScene, renderCamera,
			                               event.mouse.x, event.mouse.y,
			                               operateMeshes, _activeRenderActors, operatedEntity)) {
				object = findPuzzleObjectForMesh(
					_activePuzzle, _activeScene, _activeRoomPrefix, operatedEntity);
			}

			// Some retail puzzle interactions are pure 2D range shapes with no
			// drawable entity. Mp2 uses these around dangerous animals; operating
			// the range after selecting the bombs is what reveals six required
			// stars. If no mesh was hit, project the click onto the gameplay plane
			// and resolve an enabled entity-less operate region.
			if (!object) {
				Vec3f interactionGround;
				if (screenPointToGround(renderCamera, event.mouse.x, event.mouse.y,
				                        800, 600, interactionGround)) {
					for (uint32 objectIndex = 0;
					     objectIndex < _activePuzzle.objects.size(); ++objectIndex) {
						const PuzzleObject &candidate = _activePuzzle.objects[objectIndex];
						if (!candidate.enabled || !candidate.entity.empty() ||
						    candidate.operateStart == 0xffffffffU ||
						    candidate.operateEnd == 0xffffffffU ||
						    candidate.operateStart >= candidate.operateEnd)
							continue;

						const Common::String regionName = !candidate.rangeShape.empty()
							? candidate.rangeShape : candidate.polygon;
						if (regionName.empty())
							continue;

						bool inRoomScope = true;
						if (!candidate.roomScope.empty()) {
							Common::String scope = candidate.roomScope;
							scope.trim();
							while (!scope.empty() && scope[scope.size() - 1] == '*')
								scope.deleteLastChar();
							inRoomScope = scope.empty() ||
							              startsWithIgnoreCase(_activeRoomPrefix, scope);
						} else if ((startsWithIgnoreCase(candidate.name, "r") ||
						            startsWithIgnoreCase(regionName, "r"))) {
							inRoomScope =
								startsWithIgnoreCase(candidate.name, _activeRoomPrefix) ||
								startsWithIgnoreCase(regionName, _activeRoomPrefix);
						}
						if (!inRoomScope)
							continue;

						if (_activeShapes.containsRegion(
								regionName, interactionGround.x, interactionGround.z)) {
							object = &candidate;
							debug(1, "Zero Comico: click selected puzzle region %s",
							      candidate.name.c_str());
							break;
						}
					}
				}
			}

			if (object && object->operateStart < object->operateEnd) {
					_pendingRoomName.clear();
					_pendingRoomCutscene.clear();
					_pendingSaySpeaker.clear();
					_pendingSayText.clear();
					_pendingDialogName.clear();

					_scriptDialogueContextActive = true;
					_scriptDialogueCamera = renderCamera;
					_scriptDialogueSceneDirectory = sceneDirectory;
					_scriptDialoguePlayerDirectory = playerDirectory;
					_scriptDialogueFrame = &frame;
					const bool operateOk = runScriptWithAudioClass(
						_activePuzzle.program(), object->operateStart, object->operateEnd,
						4096, 2);
					_scriptDialogueContextActive = false;
					_scriptDialogueFrame = nullptr;
					if (!operateOk) {
						warning("Zero Comico: object operation %s stopped on an unsupported opcode",
						        object->name.c_str());
					}

					if (!_pendingMainPlace.empty()) {
						done = true;
						break;
					}

					rebuildInteractionMeshes();

					if (!_pendingDialogName.empty() && !done && !shouldQuit()) {
						playDialogue(_pendingDialogName, renderCamera, sceneDirectory,
						             playerDirectory, frame);
						_pendingDialogName.clear();
					}

					if (!applyPendingRoomTransition()) {
						done = true;
						break;
					}

					if (!_pendingSayText.empty() && !done && !shouldQuit()) {
						const Common::String effectiveSpeaker =
							_pendingSaySpeaker.empty()
								? (_playerCharacterScript.playerName.empty()
									? Common::String("MainPlayer") : _playerCharacterScript.playerName)
								: _pendingSaySpeaker;
						const DialogSpeaker *speakerInfo =
							_activeDialog.findSpeakerByName(effectiveSpeaker);
						drawCutsceneSubtitle(frame, effectiveSpeaker, _pendingSayText, speakerInfo);
						_system->copyRectToScreen(frame.getPixels(), frame.pitch,
						                          0, 0, frame.w, frame.h);
						_system->updateScreen();

						const uint32 sayDuration = retailTextDurationMillis(
							_pendingSayText, speakerInfo ? speakerInfo->speed : 0.07f);
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

			Vec3f ground;
			if (!screenPointToGround(renderCamera, event.mouse.x, event.mouse.y, 800, 600, ground))
				continue;
			if (!_activeWalkMap.containsWalkablePoint(ground.x, ground.z)) {
				debug(2, "Zero Comico: ignored click outside walkable floor at %.3f %.3f",
				      ground.x, ground.z);
				continue;
			}

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

			const CharacterAnimSet *walkAnimSet =
				_playerCharacterScript.findAnimSet(_playerAnimSetName);
			const CharacterDefinition *walkDefinition =
				_playerCharacterScript.findCharacter(_playerCharacterScript.playerName);
			Common::String lastPlayerEventSource;
			int32 lastPlayerEventFrame = -1;
			bool havePlayerEventFrame = false;

			auto dispatchPlayerStepEvents = [&](const Common::String &source,
			                                    float sourceFrame) {
#ifdef USE_MAD
				if (!walkAnimSet || !walkDefinition || !room)
					return;

				const NamedAnimationClip *clip =
					_playerScene.findClipBySource(_playerSequences.bodyName, source);
				if (!clip)
					return;

				const int32 currentFrame =
					(int32)std::floor(sourceFrame - (float)clip->data.startFrame + 0.0001f);
				const bool sourceChanged =
					!havePlayerEventFrame || !lastPlayerEventSource.equalsIgnoreCase(source);

				for (uint32 eventIndex = 0; eventIndex < walkAnimSet->stepEvents.size(); ++eventIndex) {
					const CharacterStepEvent &stepEvent = walkAnimSet->stepEvents[eventIndex];
					if (!stepEvent.animation.equalsIgnoreCase(source))
						continue;

					bool crossed = false;
					if (sourceChanged) {
						crossed = stepEvent.frame <= currentFrame;
					} else if (currentFrame >= lastPlayerEventFrame) {
						crossed = stepEvent.frame > lastPlayerEventFrame &&
						          stepEvent.frame <= currentFrame;
					} else {
						crossed = stepEvent.frame > lastPlayerEventFrame ||
						          stepEvent.frame <= currentFrame;
					}
					if (!crossed)
						continue;

					Common::String sampleName;
					for (uint32 soundIndex = 0; soundIndex < room->characterSounds.size(); ++soundIndex) {
						const RoomCharacterSound &sound = room->characterSounds[soundIndex];
						const bool targetsPlayer =
							sound.character.equalsIgnoreCase("MainPlayer") ||
							sound.character.equalsIgnoreCase(walkDefinition->name);
						if (targetsPlayer && sound.sampleId == stepEvent.sampleId) {
							sampleName = sound.fileName;
							break;
						}
					}
					if (sampleName.empty()) {
						for (uint32 sampleIndex = 0; sampleIndex < walkDefinition->samples.size(); ++sampleIndex) {
							if (walkDefinition->samples[sampleIndex].id == stepEvent.sampleId) {
								sampleName = walkDefinition->samples[sampleIndex].fileName;
								break;
							}
						}
					}
					if (sampleName.empty())
						continue;

					playNamedMp3(_mixer, Audio::Mixer::kSFXSoundType, sampleName, nullptr,
					             retailChannelVolume(2, 100.0f, sampleName));
					debug(2, "Zero Comico: player step event %s frame %d -> %d (%s)",
					      source.c_str(), (int)currentFrame, (int)stepEvent.sampleId,
					      sampleName.c_str());
				}

				lastPlayerEventSource = source;
				lastPlayerEventFrame = currentFrame;
				havePlayerEventFrame = true;
#else
				(void)source;
				(void)sourceFrame;
#endif
			};

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
				const Common::String routeRoomName = _activeRoomName;
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
					if (!runCharacterControlCodes()) {
						done = true;
						break;
					}
					const float advance = distance < stepDistance ? distance : stepDistance;
					const Vec3f previousPosition = _playerPosition;
					_playerPosition.x += dx * advance;
					_playerPosition.z += dz * advance;
					distance -= advance;

					if (!runPuzzleRegionTransitions()) {
						done = true;
						break;
					}
					if (!_pendingMainPlace.empty()) {
						done = true;
						break;
					}
					if (!_activeRoomName.equalsIgnoreCase(routeRoomName)) {
						distance = 0.0f;
						break;
					}

					bool crossedPortal = false;
					if (_portalsEnabled && room && !room->portals.empty()) {
						for (uint32 portalIndex = 0; portalIndex < room->portals.size(); ++portalIndex) {
							const RoomPortal &portal = room->portals[portalIndex];
							const ShapeMarker *portalShape = findPortalShape(*room, portal);
							if (!portalShape ||
							    !movementCrossesPortal(previousPosition, _playerPosition, *portalShape))
								continue;

							debug(1, "Zero Comico: crossed retail portal %s from %s to %s",
							      portal.name.c_str(), room->name.c_str(),
							      portal.destinationRoom.c_str());
							_pendingRoomName = portal.destinationRoom;
							_pendingRoomCutscene.clear();
							if (!applyPendingRoomTransition()) {
								done = true;
								break;
							}
							crossedPortal = true;
							break;
						}
					}
					if (done)
						break;
					if (crossedPortal) {
						// The source-room route is no longer valid after crossing.
						distance = 0.0f;
						break;
					}

					updateAutoCamera();
					updateDynamicCamera(animationSource, animationFrame);
					dispatchPlayerStepEvents(animationSource, animationFrame);

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

				if (!_activeRoomName.equalsIgnoreCase(routeRoomName))
					break;
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
						if (!runCharacterControlCodes()) {
							done = true;
							break;
						}
						updateDynamicCamera(stopSource, stopFrame);
						dispatchPlayerStepEvents(stopSource, stopFrame);
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
					updateDynamicCamera("Stay", stayFrame);
					renderGameplayFrame(renderCamera, sceneDirectory, playerDirectory,
					                    "Stay", stayFrame, frame);
				}
			}
		}

		const bool cameraChanged = applyPendingCamera();
		const uint32 idleNow = _system->getMillis();
		if (cameraChanged && _cameraMode == 0 && !done && !shouldQuit()) {
			renderGameplayFrame(renderCamera, sceneDirectory, playerDirectory,
			                    "Stay", 0.0f, frame);
			lastIdleRender = idleNow;
		}
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
			updateDynamicCamera("Stay", stayFrame);
			renderGameplayFrame(renderCamera, sceneDirectory, playerDirectory,
			                    "Stay", stayFrame, frame);
			lastIdleRender = idleNow;
		}
		_system->delayMillis(10);
	}

	return !shouldQuit();
}

void ZeroComicoEngine::synchronizePersistentState(Common::Serializer &s, uint32 version) {
	auto syncStringArray = [&](Common::Array<Common::String> &values) {
		uint32 count = s.isSaving() ? (uint32)values.size() : 0;
		s.syncAsUint32LE(count);
		if (s.isLoading())
			values.clear();
		for (uint32 i = 0; i < count; ++i) {
			Common::String value;
			if (s.isSaving())
				value = values[i];
			s.syncString(value);
			if (s.isLoading())
				values.push_back(value);
		}
	};

	auto syncBoolArray = [&](Common::Array<bool> &values) {
		uint32 count = s.isSaving() ? (uint32)values.size() : 0;
		s.syncAsUint32LE(count);
		if (s.isLoading())
			values.clear();
		for (uint32 i = 0; i < count; ++i) {
			byte value = s.isSaving() && values[i] ? 1 : 0;
			s.syncAsByte(value);
			if (s.isLoading())
				values.push_back(value != 0);
		}
	};

	auto syncIntArray = [&](Common::Array<int32> &values) {
		uint32 count = s.isSaving() ? (uint32)values.size() : 0;
		s.syncAsUint32LE(count);
		if (s.isLoading())
			values.clear();
		for (uint32 i = 0; i < count; ++i) {
			int32 value = s.isSaving() ? values[i] : 0;
			s.syncAsSint32LE(value);
			if (s.isLoading())
				values.push_back(value);
		}
	};

	s.syncString(_currentMainPlace);
	s.syncString(_activeRoomName);
	s.syncString(_activeRoomPrefix);
	s.syncString(_activeWalkMapName);
	s.syncString(_playerAnimSetName);

	s.syncAsFloatLE(_playerPosition.x);
	s.syncAsFloatLE(_playerPosition.y);
	s.syncAsFloatLE(_playerPosition.z);
	s.syncAsFloatLE(_playerFacingTarget.x);
	s.syncAsFloatLE(_playerFacingTarget.y);
	s.syncAsFloatLE(_playerFacingTarget.z);

	byte havePlayerStart = _havePlayerStart ? 1 : 0;
	byte playerHatVisible = _playerHatVisible ? 1 : 0;
	s.syncAsByte(havePlayerStart);
	s.syncAsByte(playerHatVisible);
	if (s.isLoading()) {
		_havePlayerStart = havePlayerStart != 0;
		_playerHatVisible = playerHatVisible != 0;
	}

	int32 navNode = _playerNavNode;
	s.syncAsSint32LE(navNode);
	if (s.isLoading())
		_playerNavNode = navNode;

	syncStringArray(_inventoryObjects);
	s.syncString(_selectedInventoryObject);
	s.syncString(_combineInventoryFirst);
	s.syncString(_combineInventorySecond);
	s.syncAsSint32LE(_lastDialogueChoice);

	byte interfaceDisabled = _interfaceDisabled ? 1 : 0;
	byte enabled3d = _3dEnabled ? 1 : 0;
	byte portalsEnabled = _portalsEnabled ? 1 : 0;
	byte cameraLocked = _cameraModeLocked ? 1 : 0;
	byte noCameraReset = _playerNoCameraReset ? 1 : 0;
	byte depthCueEnabled = _depthCueEnabled ? 1 : 0;
	s.syncAsByte(interfaceDisabled);
	s.syncAsByte(enabled3d);
	s.syncAsByte(portalsEnabled);
	s.syncAsSint32LE(_cameraMode);
	s.syncAsByte(cameraLocked);
	s.syncAsByte(noCameraReset);
	s.syncAsByte(depthCueEnabled);
	s.syncAsFloatLE(_depthCueStart);
	s.syncAsFloatLE(_depthCueEnd);
	if (s.isLoading()) {
		_interfaceDisabled = interfaceDisabled != 0;
		_3dEnabled = enabled3d != 0;
		_portalsEnabled = portalsEnabled != 0;
		_cameraModeLocked = cameraLocked != 0;
		_playerNoCameraReset = noCameraReset != 0;
		_depthCueEnabled = depthCueEnabled != 0;
	}

	for (int component = 0; component < 4; ++component)
		s.syncAsFloatLE(_masterColor[component]);

	syncStringArray(_hiddenSceneMeshes);

	// Persistent Setp state is lightweight: the retail assets themselves are
	// reloaded from disk, while only their names and controller transforms need
	// to survive a save.
	syncStringArray(_loadedSetpAssets);

	// ZCOM v5 adds play_open_cut persistence. Only stable retail identity is
	// serialized; decoded scene geometry is reconstructed from the original
	// P3D/ANJ pair during the staged restore. ZCOM v4 remains readable.
	if (version >= 5) {
		uint32 openCutCount = s.isSaving() ? (uint32)_openCutScenes.size() : 0;
		s.syncAsUint32LE(openCutCount);
		if (s.isLoading())
			_openCutScenes.clear();
		for (uint32 i = 0; i < openCutCount; ++i) {
			OpenCutSceneRuntime runtime;
			if (s.isSaving())
				runtime = _openCutScenes[i];
			s.syncString(runtime.roomName);
			s.syncString(runtime.assetStem);
			if (s.isLoading())
				_openCutScenes.push_back(runtime);
		}
	} else if (s.isLoading()) {
		_openCutScenes.clear();
	}

	uint32 controllerCount = s.isSaving() ? (uint32)_setpControllerNames.size() : 0;
	s.syncAsUint32LE(controllerCount);
	if (s.isLoading()) {
		_setpControllerNames.clear();
		_setpControllerPositions.clear();
	}
	for (uint32 i = 0; i < controllerCount; ++i) {
		Common::String name;
		Vec3f position = {0.0f, 0.0f, 0.0f};
		if (s.isSaving()) {
			name = _setpControllerNames[i];
			if (i < _setpControllerPositions.size())
				position = _setpControllerPositions[i];
		}
		s.syncString(name);
		s.syncAsFloatLE(position.x);
		s.syncAsFloatLE(position.y);
		s.syncAsFloatLE(position.z);
		if (s.isLoading()) {
			_setpControllerNames.push_back(name);
			_setpControllerPositions.push_back(position);
		}
	}

	// Dynamic clones are reconstructed from their retail template. Persist only
	// stable identity plus the final object transform, never duplicated geometry.
	uint32 dynamicCount = s.isSaving() ? (uint32)_dynamicSceneEntities.size() : 0;
	s.syncAsUint32LE(dynamicCount);
	if (s.isLoading())
		_dynamicSceneEntities.clear();
	for (uint32 i = 0; i < dynamicCount; ++i) {
		DynamicSceneEntity entity;
		ObjectTransform transform;
		if (s.isSaving()) {
			entity = _dynamicSceneEntities[i];
			transform = entity.mesh.data.transform;
		} else {
			transform.pivot.x = transform.pivot.y = transform.pivot.z = 0.0f;
			transform.translation.x = transform.translation.y = transform.translation.z = 0.0f;
			transform.scale.x = transform.scale.y = transform.scale.z = 1.0f;
			for (int m = 0; m < 9; ++m)
				transform.matrix[m] = (m == 0 || m == 4 || m == 8) ? 1.0f : 0.0f;
		}

		s.syncString(entity.sourceName);
		s.syncString(entity.name);
		s.syncString(entity.roomName);
		s.syncAsFloatLE(transform.pivot.x);
		s.syncAsFloatLE(transform.pivot.y);
		s.syncAsFloatLE(transform.pivot.z);
		for (int m = 0; m < 9; ++m)
			s.syncAsFloatLE(transform.matrix[m]);
		s.syncAsFloatLE(transform.translation.x);
		s.syncAsFloatLE(transform.translation.y);
		s.syncAsFloatLE(transform.translation.z);
		s.syncAsFloatLE(transform.scale.x);
		s.syncAsFloatLE(transform.scale.y);
		s.syncAsFloatLE(transform.scale.z);

		if (s.isLoading()) {
			entity.mesh.name = entity.name;
			entity.mesh.data.transform = transform;
			_dynamicSceneEntities.push_back(entity);
		}
	}

	// CPU character runtime data is not part of ScriptVM. Save it by retail
	// character name so parser ordering can change without invalidating saves.
	uint32 cpuCount = s.isSaving() ? (uint32)_cpuCharacters.size() : 0;
	s.syncAsUint32LE(cpuCount);
	if (s.isLoading())
		_cpuCharacters.clear();
	for (uint32 i = 0; i < cpuCount; ++i) {
		CpuCharacterRuntime runtime;
		if (s.isSaving())
			runtime = _cpuCharacters[i];

		s.syncString(runtime.name);
		s.syncString(runtime.roomName);
		s.syncString(runtime.bodyRoot);
		s.syncString(runtime.initialEntity);
		s.syncString(runtime.initialVector);
		s.syncAsFloatLE(runtime.position.x);
		s.syncAsFloatLE(runtime.position.y);
		s.syncAsFloatLE(runtime.position.z);
		s.syncAsFloatLE(runtime.facing.x);
		s.syncAsFloatLE(runtime.facing.y);
		s.syncAsFloatLE(runtime.facing.z);

		byte alive = runtime.alive ? 1 : 0;
		byte lifeBroken = runtime.lifeBroken ? 1 : 0;
		byte positioned = runtime.positioned ? 1 : 0;
		byte haveFacing = runtime.haveFacing ? 1 : 0;
		s.syncAsByte(alive);
		s.syncAsByte(lifeBroken);
		s.syncAsByte(positioned);
		s.syncAsByte(haveFacing);
		s.syncAsSint32LE(runtime.waitState);

		if (s.isLoading()) {
			runtime.alive = alive != 0;
			runtime.lifeBroken = lifeBroken != 0;
			runtime.positioned = positioned != 0;
			runtime.haveFacing = haveFacing != 0;
			runtime.idleAnimationStartMillis = _system->getMillis();
			runtime.lastEventSource.clear();
			runtime.lastEventFrame = -1;
			runtime.haveEventFrame = false;

			// Scene/assetDirectory are deliberately reconstructed after load from
			// char.isc rather than serialized as decoded binary resources.
			_cpuCharacters.push_back(runtime);
		}
	}

	syncStringArray(_deferredBrokenCpuCharacters);
	syncStringArray(_environmentStateRooms);
	syncStringArray(_environmentStateNames);
	syncBoolArray(_environmentStateEnabled);
	syncStringArray(_lightStateNames);
	syncBoolArray(_lightStateEnabled);
	syncStringArray(_userEffectStateNames);
	syncIntArray(_userEffectStates);

	uint32 userEffectCount = s.isSaving() ? (uint32)_userEffectElapsedMs.size() : 0;
	s.syncAsUint32LE(userEffectCount);
	if (s.isLoading()) {
		_userEffectElapsedMs.clear();
		_userEffectStateChangedMillis.clear();
	}
	for (uint32 i = 0; i < userEffectCount; ++i) {
		uint32 elapsed = s.isSaving() ? _userEffectElapsedMs[i] : 0;
		s.syncAsUint32LE(elapsed);
		if (s.isLoading()) {
			_userEffectElapsedMs.push_back(elapsed);
			_userEffectStateChangedMillis.push_back(_system->getMillis());
		}
	}

	s.syncString(_environmentSoundName);
	byte environmentSoundActive = _environmentSoundActive ? 1 : 0;
	s.syncAsByte(environmentSoundActive);
	if (s.isLoading())
		_environmentSoundActive = environmentSoundActive != 0;
	s.syncString(_currentMusicName);

	// All shipped resumable begin_thread loops live in puzzle.isc. Persist
	// their execution ranges/PC and reconnect them to the freshly parsed
	// PuzzleScript program when loading.
	uint32 threadCount = 0;
	if (s.isSaving()) {
		for (uint32 i = 0; i < _backgroundScriptThreads.size(); ++i)
			if (_backgroundScriptThreads[i].program == &_activePuzzle.program())
				++threadCount;
	}
	s.syncAsUint32LE(threadCount);
	if (s.isLoading())
		_backgroundScriptThreads.clear();

	if (s.isSaving()) {
		for (uint32 i = 0; i < _backgroundScriptThreads.size(); ++i) {
			const BackgroundScriptThread &thread = _backgroundScriptThreads[i];
			if (thread.program != &_activePuzzle.program())
				continue;
			uint32 startIndex = thread.startIndex;
			uint32 pc = thread.pc;
			uint32 endIndex = thread.endIndex;
			s.syncAsUint32LE(startIndex);
			s.syncAsUint32LE(pc);
			s.syncAsUint32LE(endIndex);
		}
	} else {
		for (uint32 i = 0; i < threadCount; ++i) {
			BackgroundScriptThread thread;
			thread.program = &_activePuzzle.program();
			s.syncAsUint32LE(thread.startIndex);
			s.syncAsUint32LE(thread.pc);
			s.syncAsUint32LE(thread.endIndex);
			if (thread.startIndex < thread.endIndex &&
			    thread.pc >= thread.startIndex &&
			    thread.pc <= thread.endIndex &&
			    thread.endIndex <= _activePuzzle.program().instructions().size())
				_backgroundScriptThreads.push_back(thread);
		}
		_lastBackgroundScriptTick = _system->getMillis();
	}

	_scriptVM.synchronize(s);
	_activePuzzle.synchronizeState(s);
	_activeCameraTriggers.synchronizeState(s);
	_activeDialog.synchronizeState(s);
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
		// A load requested from ScummVM's global menu can arrive without going
		// through Zero Comico's own CARICA button. Consume the staged request
		// here just like the native menu path.
		if (_pendingLoadActive && !_pendingLoadMainPlace.empty()) {
			Common::String nextMainPlace = _pendingLoadMainPlace;
			while (!nextMainPlace.empty() && !shouldQuit()) {
				_pendingMainPlace.clear();
				if (!runMainPlacePreview(nextMainPlace))
					break;
				nextMainPlace = _pendingMainPlace;
			}
			if (!shouldQuit())
				renderMenuFrame(selection);
			continue;
		}

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
			case 4: { // CARICA
				if (loadGameDialog() && _pendingLoadActive) {
					Common::String nextMainPlace = _pendingLoadMainPlace;
					while (!nextMainPlace.empty() && !shouldQuit()) {
						_pendingMainPlace.clear();
						if (!runMainPlacePreview(nextMainPlace))
							break;
						nextMainPlace = _pendingMainPlace;
					}
				}
				if (!shouldQuit())
					renderMenuFrame(selection);
				break;
			}
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
