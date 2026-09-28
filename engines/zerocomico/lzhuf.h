/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: LZHUF decoder
 *
 * Decoder behavior follows the classic Haruhiko Okumura LZHUF format used
 * inside Zero Comico's JFX1 and JGF5 containers. This implementation is new
 * code based on the documented bitstream format.
 */

#ifndef ZEROCOMICO_LZHUF_H
#define ZEROCOMICO_LZHUF_H

#include "common/array.h"
#include "common/scummsys.h"

namespace ZeroComico {

class LzhufDecoder {
public:
	static bool decode(const byte *input, uint32 inputSize, uint32 outputSize, Common::Array<byte> &output);
};

} // namespace ZeroComico

#endif
