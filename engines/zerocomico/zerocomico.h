/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine
 */

#ifndef ZEROCOMICO_H
#define ZEROCOMICO_H

#include "engines/engine.h"

struct ADGameDescription;

namespace ZeroComico {

class ZeroComicoEngine : public Engine {
public:
	ZeroComicoEngine(OSystem *syst, const ADGameDescription *desc);
	~ZeroComicoEngine() override = default;

	Common::Error run() override;
	bool hasFeature(EngineFeature f) const override;

private:
	void playIntroIfPresent();
	void showBootstrapScreen();
	void waitForExit();

	const ADGameDescription *_gameDescription;
};

} // namespace ZeroComico

#endif
