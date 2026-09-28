/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine
 */

#include "base/plugins.h"
#include "engines/advancedDetector.h"
#include "zerocomico/zerocomico.h"

class ZeroComicoMetaEngine : public AdvancedMetaEngine<ADGameDescription> {
public:
	const char *getName() const override { return "zerocomico"; }

	Common::Error createInstance(OSystem *syst, Engine **engine, const ADGameDescription *desc) const override {
		*engine = new ZeroComico::ZeroComicoEngine(syst, desc);
		return Common::kNoError;
	}
};

#if PLUGIN_ENABLED_DYNAMIC(ZEROCOMICO)
REGISTER_PLUGIN_DYNAMIC(ZEROCOMICO, PLUGIN_TYPE_ENGINE, ZeroComicoMetaEngine);
#else
REGISTER_PLUGIN_STATIC(ZEROCOMICO, PLUGIN_TYPE_ENGINE, ZeroComicoMetaEngine);
#endif
