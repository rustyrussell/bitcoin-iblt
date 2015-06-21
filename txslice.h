#ifndef TXSLICE_H
#define TXSLICE_H
extern "C" {
#include <ccan/short_types/short_types.h>
};
#include <vector>
#include <cstring>
#include "txid48.h"

struct bitcoin_tx;

// An individual bucket: Must be plain old data!
template<unsigned int BYTES>
struct txslice {
	u64 txidbits : 48;
	u16 fragid;
	u8 contents[BYTES];

	friend bool operator <(const txslice &lhs, const txslice &rhs) {
		if (lhs.txidbits != rhs.txidbits) {
			return lhs.txidbits < rhs.txidbits;
		}
		return lhs.fragid < rhs.fragid;
	}
	
	u8 *as_bytes() { return (u8 *)this; }
	const u8 *as_bytes() const { return (const u8 *)this; }
	static size_t size() { return sizeof(txslice); }

	txid48 get_txid48() const { return txid48(txidbits); }

	bool empty() const;
	
	// Only valid if it's the first slice.
	varint_t slices_expected() const;
};

template<unsigned int BYTES>
std::vector<txslice<BYTES>> slice_tx(const bitcoin_tx &btx, const txid48 &id);

#endif // TXSLICE_H
