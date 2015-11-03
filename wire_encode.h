#ifndef WIRE_ENCODE_H
#define WIRE_ENCODE_H
extern "C" {
#include <ccan/short_types/short_types.h>
}
#include <vector>
#include <array>
#include <unordered_set>

class bitcoin_tx;

// Each vector entry contains a set of bitstrings of that size.
// ie. txbitsSet[0] contains 0 bit strings (none), txbitsSet[48] contains 48 bit strings.
typedef std::array<std::unordered_set<std::vector<bool>>, 49> txbitsSet;

// Helper for using add_* routines from bitcoin_tx.h
void add_linearize(const void *data, size_t len, void *pvec);

void add_bitset(std::vector<u8> *arr, const txbitsSet &bset);
bool decode_bitset(const u8 **p, size_t *len, txbitsSet &bset);
#endif // WIRE_ENCODE_H
