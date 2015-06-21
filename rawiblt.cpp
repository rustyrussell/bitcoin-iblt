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

template <unsigned int BYTES>
void raw_iblt<BYTES>::frob_bucket(size_t n, const txslice<BYTES> &s, int dir)
{
    u8 *dest = buckets[n].as_bytes();
    const u8 *src = s.as_bytes();

    counts[n] += dir;
    for (size_t i = 0; i < s.size(); i++)
        dest[i] ^= src[i];
}

// FIXME: Use std::array
template <unsigned int BYTES>
std::vector<size_t> raw_iblt<BYTES>::select_buckets(const txslice<BYTES> &s)
{
	std::vector<size_t> buckets(NUM_HASHES);
	
	for (size_t i = 0; i < buckets.size(); i++) {
        // FIXME: Can skip divide if we force buckets to power of 2.
        buckets[i] = MurmurHash3(i, s.as_bytes(), s.size()) % buckets.size();
    }

	return buckets;
}

template <unsigned int BYTES>
void raw_iblt<BYTES>::frob_buckets(const txslice<BYTES> &s, int dir)
{
    std::vector<size_t> buckets = select_buckets(s);
    for (size_t i = 0; i < buckets.size(); i++) {
        frob_bucket(buckets[i], s, dir);
    }
}

template <unsigned int BYTES>
void raw_iblt<BYTES>::insert(const txslice<BYTES> &s)
{
    frob_buckets(s, 1);
}

template <unsigned int BYTES>
void raw_iblt<BYTES>::remove(const txslice<BYTES> &s)
{
    frob_buckets(s, -1);
}

template <unsigned int BYTES>
size_t raw_iblt<BYTES>::size() const
{
    return buckets.size();
}
    
template <unsigned int BYTES>
raw_iblt<BYTES>::raw_iblt(size_t size)
    : buckets(size), counts(size)
{
}

template <unsigned int BYTES>
raw_iblt<BYTES>::raw_iblt(size_t size, u64 seed,
						  const std::unordered_set<const tx *> &txs)
    : buckets(size), counts(size)
{
    for (const auto &t : txs) {
        for (const auto &s : slice_tx<BYTES>(*t->btx, txid48(seed, t->btx->txid()))) {
            insert(s);
        }
    }
}

template <unsigned int BYTES>
std::vector<u8> raw_iblt<BYTES>::write() const
{
    size_t buckets_len = size() * buckets[0].size(), counts_len = size() * sizeof(counts[0]);
    std::vector<u8> vec(counts_len + buckets_len);

    // The joys of plain ol' data.
    memcpy(vec.data(), counts.data(), counts_len);
    memcpy(vec.data() + counts_len, buckets.data(), buckets_len);
    return vec;
}

        
template <unsigned int BYTES>
bool raw_iblt<BYTES>::read(const u8 *p, size_t len)
{
    size_t buckets_len = size() * buckets[0].size(), counts_len = size() * sizeof(counts[0]);
    if (len != counts_len + buckets_len)
        return false;

    memcpy(counts.data(), p, counts_len);
    memcpy(buckets.data(), p + counts_len, buckets_len);
    return true;
}

