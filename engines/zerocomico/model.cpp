/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: P3D / ANJ record envelope
 */

#include "zerocomico/model.h"
#include "zerocomico/resource.h"

#include <cstring>

namespace ZeroComico {

static const byte kMark[2] = {0xbb, 0xaa};
static const byte kEnd[3] = {0xed, 0xff, 0xff};
static const uint16 kGroupType = 0xf044;

uint16 ModelArchive::readU16(const byte *p) {
	return uint16(p[0]) | (uint16(p[1]) << 8);
}

uint32 ModelArchive::readU32(const byte *p) {
	return uint32(p[0]) | (uint32(p[1]) << 8) | (uint32(p[2]) << 16) | (uint32(p[3]) << 24);
}

bool ModelArchive::hasHeader(const byte *p) {
	return (p[0] == 0x02 || p[0] == 0x01) && p[1] == 0x00 && p[2] == 0x3d && p[3] == 0x0e;
}

bool ModelArchive::hasTrailer(const Common::Array<byte> &data) {
	const uint32 n = data.size();
	return n >= 4 && data[n - 4] == 0x00 && data[n - 3] == 0xed && data[n - 2] == 0xff && data[n - 1] == 0xff;
}

bool ModelArchive::load(const Common::Path &path) {
	Common::Array<byte> decoded;
	if (!ResourceReader::decodeJfxFile(path, decoded))
		return false;
	return parse(decoded);
}

bool ModelArchive::parse(const Common::Array<byte> &decoded) {
	_records.clear();
	_data = decoded;
	if (_data.size() < 8 || !hasHeader(_data.data()) || !hasTrailer(_data))
		return false;
	return walk(4, _data.size() - 4);
}

bool ModelArchive::walk(uint32 lo, uint32 hi) {
	uint32 pos = lo;
	while (pos < hi) {
		if (pos + 4 > hi)
			return false;

		const bool normalMark = _data[pos] == kMark[0] && _data[pos + 1] == kMark[1];
		const bool headerShapedRecord = hasHeader(_data.data() + pos);
		if (!normalMark && !headerShapedRecord)
			return false;

		const uint16 type = readU16(_data.data() + pos + 2);
		if (normalMark && type == kGroupType) {
			if (pos + 8 > hi)
				return false;
			const uint32 length = readU32(_data.data() + pos + 4);
			if (length < 4)
				return false;
			const uint32 end = pos + 4 + length;
			if (end > hi)
				return false;

			ModelRecord rec;
			rec.offset = pos;
			rec.type = type;
			rec.name.clear();
			rec.bodyOffset = pos + 8;
			rec.bodySize = length - 4;
			rec.group = true;
			rec.groupSize = length - 4;
			_records.push_back(rec);

			if (!walk(pos + 8, end))
				return false;
			pos = end;
			continue;
		}

		// Named records have a 32-byte name field after mark+type. The tail of
		// that field is commonly padded with ASCII '0', not NUL.
		if (pos + 36 > hi)
			return false;
		const byte *nameBytes = _data.data() + pos + 4;
		uint32 nameLen = 0;
		while (nameLen < 32 && nameBytes[nameLen] != 0)
			++nameLen;
		const Common::String name(reinterpret_cast<const char *>(nameBytes), nameLen);
		const uint32 bodyStart = pos + 36;

		// ED FF FF can occur inside float data. Try each candidate and keep only
		// one that allows the remainder of this enclosing range to parse exactly.
		for (uint32 endMark = bodyStart; endMark + 3 <= hi; ++endMark) {
			if (memcmp(_data.data() + endMark, kEnd, 3) != 0)
				continue;

			const uint32 keep = _records.size();
			ModelRecord rec;
			rec.offset = pos;
			rec.type = type;
			rec.name = name;
			rec.bodyOffset = bodyStart;
			rec.bodySize = endMark - bodyStart;
			rec.group = false;
			rec.groupSize = 0;
			_records.push_back(rec);

			if (walk(endMark + 3, hi))
				return true;
			_records.resize(keep);
		}
		return false;
	}
	return pos == hi;
}

} // namespace ZeroComico
