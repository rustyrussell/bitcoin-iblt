#include "rawiblt.h"
#include "tx.h"
#include <stdexcept>
#include <algorithm>

/*  "We will show that hash_count values of 3 or 4 work well in practice"

    From:

    Eppstein, David, et al. "What's the difference?: efficient set reconciliation without prior context." ACM SIGCOMM Computer Communication Review. Vol. 41. No. 4. ACM, 2011. http://conferences.sigcomm.org/sigcomm/2011/papers/sigcomm/p218.pdf
*/
/* Kalle Rosenbaum showed 3 was good enough. */
#define NUM_HASHES 3

void raw_iblt::frob_bucket(size_t n, const txslice &s, int dir)
{
    u8 *dest = buckets[n].as_bytes();
    const u8 *src = s.as_bytes();

    counts[n] += dir;
    for (size_t i = 0; i < s.size(); i++)
        dest[i] ^= src[i];
}

// FIXME: Use std::array
std::vector<size_t> raw_iblt::select_buckets(const txslice &s)
{
	std::vector<size_t> buckets(NUM_HASHES);
	
	for (size_t i = 0; i < buckets.size(); i++) {
        // FIXME: Can skip divide if we force buckets to power of 2.
        buckets[i] = MurmurHash3(i, s.as_bytes(), s.size()) % size();
    }

	return buckets;
}

void raw_iblt::frob_buckets(const txslice &s, int dir)
{
    std::vector<size_t> buckets = select_buckets(s);
    for (size_t i = 0; i < buckets.size(); i++) {
        frob_bucket(buckets[i], s, dir);
    }
}

void raw_iblt::insert(const txslice &s)
{
    frob_buckets(s, 1);
}

void raw_iblt::remove(const txslice &s)
{
    frob_buckets(s, -1);
}

size_t raw_iblt::size() const
{
    return buckets.size();
}
    
raw_iblt::raw_iblt(size_t size)
    : buckets(size), counts(size)
{
}

raw_iblt::raw_iblt(size_t size, u64 seed,
						  const std::unordered_set<const tx *> &txs)
    : buckets(size), counts(size)
{
    for (const auto &t : txs) {
        for (const auto &s : slice_tx(*t->btx, txid48(seed, t->btx->txid()))) {
            insert(s);
        }
    }
}

std::vector<u8> raw_iblt::write() const
{
    size_t buckets_len = size() * buckets[0].size(), counts_len = size() * sizeof(counts[0]);
    std::vector<u8> vec(counts_len + buckets_len);

    // The joys of plain ol' data.
    memcpy(vec.data(), counts.data(), counts_len);
    for (size_t i = 0; i < size(); i++)
        memcpy(vec.data() + counts_len + i * buckets[i].size(),
               buckets[i].as_bytes(), buckets[i].size());
    return vec;
}

        
bool raw_iblt::read(const u8 *p, size_t len)
{
    size_t buckets_len = size() * buckets[0].size(), counts_len = size() * sizeof(counts[0]);
    if (len != counts_len + buckets_len)
        return false;

    memcpy(counts.data(), p, counts_len);
    for (size_t i = 0; i < size(); i++)
        memcpy(buckets[i].as_bytes(),
               p + counts_len + i * buckets[i].size(),
               buckets[i].size());
    return true;
}

