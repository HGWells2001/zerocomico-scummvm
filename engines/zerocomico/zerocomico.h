/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine
 */

#ifndef ZEROCOMICO_H
#define ZEROCOMICO_H

#include "engines/engine.h"

#include "zerocomico/scene_model.h"
#include "zerocomico/script_vm.h"

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
	void playFilmIfPresent(const Common::Path &path);
	bool loadMenuScene();
	bool renderMenuFrame(int selection);
	bool runMainPlacePreview(const Common::String &mainPlace);
	void runMenu();
	void showImageModal(const Common::Path &path);
	void showBootstrapScreen();
	void waitForExit();

	const ADGameDescription *_gameDescription;
	SceneModel _menuScene;
	SceneModel _activeScene;
	ScriptVM _scriptVM;
	bool _interfaceDisabled;
	bool _3dEnabled;
};

} // namespace ZeroComico

#endif
