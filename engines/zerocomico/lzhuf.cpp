/* ScummVM - Graphic Adventure Engine
 * Zero Comico engine: LZHUF decoder
 */

#include "zerocomico/lzhuf.h"

namespace ZeroComico {

namespace {

enum {
	kN = 4096,
	kF = 60,
	kThreshold = 2,
	kNChar = 256 - kThreshold + kF, // 314
	kT = kNChar * 2 - 1,           // 627
	kR = kT - 1,                   // 626
	kMaxFreq = 0x8000
};

static const byte kPositionCode[64] = {
	0x00, 0x20, 0x30, 0x40, 0x50, 0x58, 0x60, 0x68,
	0x70, 0x78, 0x80, 0x88, 0x90, 0x94, 0x98, 0x9c,
	0xa0, 0xa4, 0xa8, 0xac, 0xb0, 0xb4, 0xb8, 0xbc,
	0xc0, 0xc2, 0xc4, 0xc6, 0xc8, 0xca, 0xcc, 0xce,
	0xd0, 0xd2, 0xd4, 0xd6, 0xd8, 0xda, 0xdc, 0xde,
	0xe0, 0xe2, 0xe4, 0xe6, 0xe8, 0xea, 0xec, 0xee,
	0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
	0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
};

class BitReader {
public:
	BitReader(const byte *data, uint32 size) : _data(data), _size(size), _pos(0), _buffer(0), _bits(0) {}

	uint bit() {
		fill();
		const uint v = (_buffer >> 15) & 1;
		_buffer = (_buffer << 1) & 0xffff;
		--_bits;
		return v;
	}

	uint byteValue() {
		fill();
		const uint v = (_buffer >> 8) & 0xff;
		_buffer = (_buffer << 8) & 0xffff;
		_bits -= 8;
		return v;
	}

private:
	void fill() {
		while (_bits <= 8) {
			const byte v = _pos < _size ? _data[_pos++] : 0;
			_buffer |= uint32(v) << (8 - _bits);
			_bits += 8;
		}
	}

	const byte *_data;
	uint32 _size;
	uint32 _pos;
	uint32 _buffer;
	int _bits;
};

class AdaptiveHuffman {
public:
	AdaptiveHuffman() {
		for (int i = 0; i <= kT; ++i)
			_freq[i] = 0;
		for (int i = 0; i < kT + kNChar; ++i)
			_parent[i] = 0;
		for (int i = 0; i < kT; ++i)
			_son[i] = 0;

		for (int i = 0; i < kNChar; ++i) {
			_freq[i] = 1;
			_son[i] = i + kT;
			_parent[i + kT] = i;
		}

		int i = 0;
		for (int j = kNChar; j <= kR; ++j) {
			_freq[j] = _freq[i] + _freq[i + 1];
			_son[j] = i;
			_parent[i] = _parent[i + 1] = j;
			i += 2;
		}

		_freq[kT] = 0xffff;
		_parent[kR] = 0;
	}

	int decodeChar(BitReader &bits) {
		int c = _son[kR];
		while (c < kT) {
			c += bits.bit();
			c = _son[c];
		}
		c -= kT;
		update(c);
		return c;
	}

private:
	void reconstruct() {
		int j = 0;
		for (int i = 0; i < kT; ++i) {
			if (_son[i] >= kT) {
				_freq[j] = (_freq[i] + 1) / 2;
				_son[j] = _son[i];
				++j;
			}
		}

		int i = 0;
		j = kNChar;
		while (j < kT) {
			int k = i + 1;
			const uint16 f = _freq[i] + _freq[k];
			_freq[j] = f;
			k = j - 1;
			while (f < _freq[k])
				--k;
			++k;
			for (int m = j; m > k; --m) {
				_freq[m] = _freq[m - 1];
				_son[m] = _son[m - 1];
			}
			_freq[k] = f;
			_son[k] = i;
			i += 2;
			++j;
		}

		for (i = 0; i < kT; ++i) {
			const int k = _son[i];
			if (k >= kT) {
				_parent[k] = i;
			} else {
				_parent[k] = i;
				_parent[k + 1] = i;
			}
		}
	}

	void update(int c) {
		if (_freq[kR] == kMaxFreq)
			reconstruct();

		c = _parent[c + kT];
		while (true) {
			const uint16 k = ++_freq[c];
			int l = c + 1;
			if (k > _freq[l]) {
				while (k > _freq[l + 1])
					++l;

				_freq[c] = _freq[l];
				_freq[l] = k;

				int i = _son[c];
				_parent[i] = l;
				if (i < kT)
					_parent[i + 1] = l;

				const int j = _son[l];
				_son[l] = i;
				_parent[j] = c;
				if (j < kT)
					_parent[j + 1] = c;
				_son[c] = j;
				c = l;
			}

			c = _parent[c];
			if (c == 0)
				break;
		}
	}

	uint16 _freq[kT + 1];
	int _parent[kT + kNChar];
	int _son[kT];
};

static void buildPositionTables(byte *decodeCode, byte *decodeLen) {
	for (int i = 0; i < 64; ++i) {
		const int lo = kPositionCode[i];
		const int hi = i == 63 ? 256 : kPositionCode[i + 1];
		const int width = hi - lo;
		int log2Width = 0;
		for (int v = width; v > 1; v >>= 1)
			++log2Width;
		const int len = 8 - log2Width;
		for (int b = lo; b < hi; ++b) {
			decodeCode[b] = i;
			decodeLen[b] = len;
		}
	}
}

static int decodePosition(BitReader &bits) {
	static byte decodeCode[256];
	static byte decodeLen[256];
	static bool tablesReady = false;
	if (!tablesReady) {
		buildPositionTables(decodeCode, decodeLen);
		tablesReady = true;
	}

	uint i = bits.byteValue();
	uint c = uint(decodeCode[i]) << 6;
	int j = decodeLen[i] - 2;
	while (j-- > 0)
		i = ((i << 1) + bits.bit()) & 0xffff;
	return c | (i & 0x3f);
}

} // namespace

bool LzhufDecoder::decode(const byte *input, uint32 inputSize, uint32 outputSize, Common::Array<byte> &output) {
	output.clear();
	output.resize(outputSize);
	if (outputSize == 0)
		return true;
	if (!input && inputSize)
		return false;

	BitReader bits(input, inputSize);
	AdaptiveHuffman huffman;
	byte ring[kN];
	for (int i = 0; i < kN; ++i)
		ring[i] = 0x20;

	int r = kN - kF;
	uint32 outPos = 0;
	while (outPos < outputSize) {
		const int c = huffman.decodeChar(bits);
		if (c < 256) {
			output[outPos++] = byte(c);
			ring[r] = byte(c);
			r = (r + 1) & (kN - 1);
		} else {
			const int source = (r - decodePosition(bits) - 1) & (kN - 1);
			const int count = c - 255 + kThreshold;
			for (int k = 0; k < count && outPos < outputSize; ++k) {
				const byte v = ring[(source + k) & (kN - 1)];
				output[outPos++] = v;
				ring[r] = v;
				r = (r + 1) & (kN - 1);
			}
		}
	}

	return outPos == outputSize;
}

} // namespace ZeroComico
