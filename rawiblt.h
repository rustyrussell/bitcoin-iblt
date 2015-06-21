#ifndef RAWIBLT_H
#define RAWIBLT_H
// C++ implementation of an Invertible Bloom Lookup Table.
#include "txid48.h"
#include "txslice.h"
#include <vector>
#include <set>
#include <unordered_set>

class tx;

// Raw IBLT for handing over the wire.
class raw_iblt {
public:
    // Empty IBLT
    raw_iblt(size_t size);
    // Construct an IBLT from a series of transactions.
    raw_iblt(size_t size, u64 seed, const std::unordered_set<const tx *> &txs);

    // Get size arg as passed to constructor.
    size_t size() const;

    // Linearize this iblt.
    std::vector<u8> write() const;

    // De-linearize this iblt.
    bool read(const u8 *p, size_t len);

    // Overhead on the wire for each bucket (6 txid48, 2 fragid, 2 counter)
    static const std::size_t OVERHEAD = 6 + 2 + 2;
    static const std::size_t WIRE_BYTES = IBLT_SIZE + OVERHEAD;

private:
    friend class iblt;

    // Put slice into a single bucket (or remove, if dir = -1)
    void frob_bucket(size_t bucket, const txslice &s, int dir);

    // Put slice into all its buckets (or remove, if dir = -1)
    void frob_buckets(const txslice &s, int dir);

    // For iblt to open-code frob_bucket() calls
    std::vector<size_t> select_buckets(const txslice &s);

    // Convenience wrappers for above.
    void insert(const txslice &s);
    void remove(const txslice &s);

    std::vector<txslice> buckets;
    std::vector<s16> counts;
};
#endif // RAWIBLT_H
