/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: JFX1 / JGF5 resource readers
 */

#ifndef ZEROCOMICO_RESOURCE_H
#define ZEROCOMICO_RESOURCE_H

#include "common/array.h"
#include "common/path.h"
#include "common/scummsys.h"
#include "graphics/managed_surface.h"

namespace Common {
class SeekableReadStream;
}

namespace ZeroComico {

struct JgfInfo {
	uint32 width;
	uint32 height;
	uint32 decodedSize;
	uint32 encodedSize;
};

class ResourceReader {
public:
	static bool decodeJfx(Common::SeekableReadStream &stream, Common::Array<byte> &decoded);
	static bool decodeJfxFile(const Common::Path &path, Common::Array<byte> &decoded);
	static Common::SeekableReadStream *openDecodedJfxFile(const Common::Path &path);
	static bool decodeJgf(Common::SeekableReadStream &stream, Graphics::ManagedSurface &surface, JgfInfo *info = nullptr);
	static bool decodeJgfFile(const Common::Path &path, Graphics::ManagedSurface &surface, JgfInfo *info = nullptr);
};

} // namespace ZeroComico

#endif
