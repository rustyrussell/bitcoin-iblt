#ifndef WIRE_ENCODE_H
#define WIRE_ENCODE_H
extern "C" {
#include <ccan/short_types/short_types.h>
}
#include <vector>
#include <unordered_set>
#include "iblt.h"

class bitcoin_tx;

// Each vector entry contains a set of bitstrings of that size.
// ie. txbitsSet[0] contains 0 bit strings (none), txbitsSet[48] contains 48 bit strings.
typedef std::vector<std::unordered_set<std::vector<bool>>> txbitsSet;

template<unsigned int BYTES>
std::vector<u8> wire_encode(const bitcoin_tx &coinbase,
                            const u64 min_fee_per_byte,
                            const u64 seed,
			    const txbitsSet &added,
			    const txbitsSet &removed,
                            const raw_iblt<BYTES> &iblt);

template<unsigned int BYTES>
raw_iblt<BYTES> wire_decode(const std::vector<u8> &incoming,
			    bitcoin_tx &coinbase,
			    u64 &min_fee_per_byte,
			    u64 &seed,
			    txbitsSet &added,
			    txbitsSet &removed);
#endif // WIRE_ENCODE_H
