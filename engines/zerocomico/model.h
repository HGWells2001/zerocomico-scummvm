/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: P3D / ANJ record envelope
 */

#ifndef ZEROCOMICO_MODEL_H
#define ZEROCOMICO_MODEL_H

#include "common/array.h"
#include "common/path.h"
#include "common/str.h"
#include "common/scummsys.h"

namespace ZeroComico {

struct ModelRecord {
	uint32 offset;
	uint16 type;
	Common::String name;
	uint32 bodyOffset;
	uint32 bodySize;
	bool group;
	uint32 groupSize;
};

class ModelArchive {
public:
	bool load(const Common::Path &path);
	bool parse(const Common::Array<byte> &decoded);

	const Common::Array<byte> &data() const { return _data; }
	const Common::Array<ModelRecord> &records() const { return _records; }

private:
	bool walk(uint32 lo, uint32 hi);
	static bool hasHeader(const byte *p);
	static bool hasTrailer(const Common::Array<byte> &data);
	static uint16 readU16(const byte *p);
	static uint32 readU32(const byte *p);

	Common::Array<byte> _data;
	Common::Array<ModelRecord> _records;
};

} // namespace ZeroComico

#endif
