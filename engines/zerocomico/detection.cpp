/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "base/plugins.h"
#include "engines/advancedDetector.h"

static const PlainGameDescriptor zeroComicoGames[] = {
	{"zerocomico", "Zero Comico"},
	{nullptr, nullptr}
};

namespace ZeroComico {

static const ADGameDescription gameDescriptions[] = {
	{
		"zerocomico",
		"Italian retail",
		AD_ENTRY2s("Zero Comico.exe", "741b42094a37b66ba96ece978e248d96", 1658880,
		           "Config.gsc", "6ebce334d2118fdf554345ce7fe6067e", 74),
		Common::IT_ITA,
		Common::kPlatformWindows,
		ADGF_UNSTABLE | ADGF_DROPPLATFORM | ADGF_DROPLANGUAGE,
		GUIO1(GUIO_NOMIDI)
	},
	{
		"zerocomico",
		"Italian retail (alternate executable)",
		AD_ENTRY2s("Zero Comico.exe", "298d0c2111428606dddd61bf7c93cf99", 1658880,
		           "Config.gsc", "6ebce334d2118fdf554345ce7fe6067e", 74),
		Common::IT_ITA,
		Common::kPlatformWindows,
		ADGF_UNSTABLE | ADGF_DROPPLATFORM | ADGF_DROPLANGUAGE,
		GUIO1(GUIO_NOMIDI)
	},

	AD_TABLE_END_MARKER
};

} // namespace ZeroComico

class ZeroComicoMetaEngineDetection : public AdvancedMetaEngineDetection<ADGameDescription> {
public:
	ZeroComicoMetaEngineDetection() : AdvancedMetaEngineDetection(ZeroComico::gameDescriptions, zeroComicoGames) {}

	const char *getName() const override { return "zerocomico"; }
	const char *getEngineName() const override { return "Zero Comico"; }
	const char *getOriginalCopyright() const override {
		return "Zero Comico (C) 2001 GMM Entertainment / Medusa Games";
	}
};

REGISTER_PLUGIN_STATIC(ZEROCOMICO_DETECTION, PLUGIN_TYPE_ENGINE_DETECTION, ZeroComicoMetaEngineDetection);
