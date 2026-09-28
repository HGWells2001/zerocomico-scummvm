/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: JFX1-wrapped Autodesk FLC
 */

#include "zerocomico/wrapped_flic.h"
#include "zerocomico/resource.h"

namespace ZeroComico {

bool WrappedFlicDecoder::loadJfxFile(const Common::Path &path) {
	Common::SeekableReadStream *stream = ResourceReader::openDecodedJfxFile(path);
	if (!stream)
		return false;

	// FlicDecoder takes ownership of the stream, including on failure.
	return loadStream(stream);
}

} // namespace ZeroComico
