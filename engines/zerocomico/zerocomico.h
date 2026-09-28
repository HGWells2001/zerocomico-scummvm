/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine
 */

#ifndef ZEROCOMICO_H
#define ZEROCOMICO_H

#include "engines/engine.h"

#include "zerocomico/scene_model.h"

struct ADGameDescription;

namespace ZeroComico {

class ZeroComicoEngine : public Engine {
public:
	ZeroComicoEngine(OSystem *syst, const ADGameDescription *desc);
	~ZeroComicoEngine() override = default;

	Common::Error run() override;
	bool hasFeature(EngineFeature f) const override;

private:
	bool runStartupScript(const Common::String &mainPlace);
	void playFilmIfPresent(const Common::Path &path);
	bool loadMenuScene();
	void showBootstrapScreen();
	void waitForExit();

	const ADGameDescription *_gameDescription;
	SceneModel _menuScene;
};

} // namespace ZeroComico

#endif
