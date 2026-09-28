/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: JFX1-wrapped Autodesk FLC
 */

#ifndef ZEROCOMICO_WRAPPED_FLIC_H
#define ZEROCOMICO_WRAPPED_FLIC_H

#include "common/path.h"
#include "video/flic_decoder.h"

namespace ZeroComico {

class WrappedFlicDecoder : public Video::FlicDecoder {
public:
	bool loadJfxFile(const Common::Path &path);
};

} // namespace ZeroComico

#endif
