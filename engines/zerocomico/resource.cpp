/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: JFX1 / JGF5 resource readers
 */

#include "zerocomico/resource.h"
#include "zerocomico/lzhuf.h"

#include "common/file.h"
#include "common/stream.h"
#include "graphics/pixelformat.h"

#include <cstring>

namespace ZeroComico {

static bool readPayload(Common::SeekableReadStream &stream, uint32 encodedSize, Common::Array<byte> &payload) {
	if (encodedSize > uint32(stream.size() - stream.pos()))
		return false;
	payload.resize(encodedSize);
	return encodedSize == 0 || stream.read(payload.data(), encodedSize) == encodedSize;
}

bool ResourceReader::decodeJfx(Common::SeekableReadStream &stream, Common::Array<byte> &decoded) {
	stream.seek(0);
	char magic[4];
	if (stream.read(magic, 4) != 4 || memcmp(magic, "JFX1", 4) != 0)
		return false;

	const uint32 decodedSize = stream.readUint32LE();
	const uint32 encodedSize = stream.readUint32LE();
	if (stream.err() || uint32(stream.size()) != encodedSize + 12)
		return false;

	Common::Array<byte> payload;
	if (!readPayload(stream, encodedSize, payload))
		return false;
	return LzhufDecoder::decode(payload.data(), payload.size(), decodedSize, decoded);
}

bool ResourceReader::decodeJfxFile(const Common::Path &path, Common::Array<byte> &decoded) {
	Common::File file;
	if (!file.open(path))
		return false;
	return decodeJfx(file, decoded);
}

bool ResourceReader::decodeJgf(Common::SeekableReadStream &stream, Graphics::ManagedSurface &surface, JgfInfo *info) {
	stream.seek(0);
	char magic[4];
	if (stream.read(magic, 4) != 4 || memcmp(magic, "JGF5", 4) != 0)
		return false;

	const uint32 field4 = stream.readUint32LE();
	const uint32 field8 = stream.readUint32LE();
	const uint32 width = stream.readUint32LE();
	const uint32 height = stream.readUint32LE();
	const uint32 decodedSize = stream.readUint32LE();
	const uint32 encodedSize = stream.readUint32LE();

	if (stream.err() || field4 != 0 || field8 != 3 || width == 0 || height == 0)
		return false;
	if (decodedSize != width * height * 4 || uint32(stream.size()) != encodedSize + 28)
		return false;

	Common::Array<byte> payload;
	Common::Array<byte> pixels;
	if (!readPayload(stream, encodedSize, payload))
		return false;
	if (!LzhufDecoder::decode(payload.data(), payload.size(), decodedSize, pixels))
		return false;

	// The engine stores four-byte pixels in BGRA byte order.
	surface.create(width, height, Graphics::PixelFormat::createFormatBGRA32());
	for (uint32 y = 0; y < height; ++y)
		memcpy(surface.getBasePtr(0, y), pixels.data() + y * width * 4, width * 4);

	if (info) {
		info->width = width;
		info->height = height;
		info->decodedSize = decodedSize;
		info->encodedSize = encodedSize;
	}
	return true;
}

bool ResourceReader::decodeJgfFile(const Common::Path &path, Graphics::ManagedSurface &surface, JgfInfo *info) {
	Common::File file;
	if (!file.open(path))
		return false;
	return decodeJgf(file, surface, info);
}

} // namespace ZeroComico
