// Print probability of recovering the given txs with the given IBLT size.
extern "C" {
#include "bitcoin-corpus.h"
#include <ccan/err/err.h>
#include <ccan/str/hex/hex.h>
#include <ccan/tal/grab_file/grab_file.h>
#include <ccan/read_write_all/read_write_all.h>
#include <ccan/tal/tal.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
}
#include "iblt.h"
#include "rawiblt.h"
#include <iostream>
#include <unordered_map>
#include "tx.h"

static bool try_iblt(size_t buckets, unsigned int seed,
					 const std::unordered_set<const tx *> &plus_txs,
					 const std::unordered_set<const tx *> &minus_txs,
					 size_t *iblt_size)
{
	raw_iblt plus(buckets, seed, plus_txs),	minus(buckets, seed, minus_txs);
	iblt diff(plus, minus);

	if (iblt_size) {
		*iblt_size = plus.write().size();
	}
	
	// Create map for reconstruction.
	std::unordered_map<txid48, const tx *> plus_txids, minus_txids;

	for (const auto &t : plus_txs)
		plus_txids.insert(std::make_pair(txid48(seed, t->txid), t));
	for (const auto &t : minus_txs)
		minus_txids.insert(std::make_pair(txid48(seed, t->txid), t));

	iblt::bucket_type t;
	txslice s;

	// For each txid48, we keep all the fragments.
	std::set<txslice> slices;

	// While there are still singleton buckets...
	while ((t = diff.next(s)) != iblt::NEITHER) {
		if (t == iblt::OURS) {
			auto it = minus_txids.find(s.get_txid48());
			// If we can't find it, we're corrupt.
			if (it == minus_txids.end())
				return false;
			else {
				// Remove entire tx.
				diff.remove_our_tx(*it->second->btx, s.get_txid48());
				// Make sure we make progress: remove it from consideration.
				minus_txids.erase(s.get_txid48());
			}
		} else if (t == iblt::THEIRS) {
			// Gave us the same slice twice?  Fail.
			if (!slices.insert(s).second) {
				return false;
			}
			diff.remove_their_slice(s);
		}
	}

	// If we didn't empty it, we've failed decode.
	if (!diff.empty())
		return false;

	// Try to assemble the slices into txs.
	size_t count = 0;
	std::vector<txslice> transaction;

	for (const auto &s: slices) {
		if (count == 0) {
			// Do we expect this to be the first fragment?
			if (s.get_txid48().frag_base() != s.fragid) {
				return false;
			}
			size_t num = s.slices_expected();
			if (!num || num > 0xFFFF) {
				return false;
			}
			transaction = std::vector<txslice>(num);
			transaction[0] = s;
			count = 1;
		} else {
			// Missing part of transaction?
			if (s.txidbits != transaction[count-1].txidbits) {
				return false;
			}
			// Fragment id wrong?
			if (s.fragid != transaction[count-1].fragid + 1) {
				return false;
			}
			transaction[count++] = s;
		}
		if (count == transaction.size()) {
			// We recovered the entire transaction!
			// FIXME: Reconstruct tx, check id48 correct.
			count = 0;
		}
	}

	// Some left over?
	return count == 0;
}
	
int main(int argc, char *argv[])
{
	size_t buckets;
	std::unordered_set<const tx *> plus_txs, minus_txs;
	size_t plus_bytes = 0, minus_bytes = 0;
	
	if (argc < 3)
		errx(1, "Usage: iblt-space <buckets> [+/-<tx-filename>]...");

	buckets = atoi(argv[1]);
	if (!buckets)
		errx(1, "Buckets must be > 0");

	for (int i = 2; i < argc; i++) {
		const u8 *txbytes;
		u64 fee;
		tx *t;

		txbytes = (u8 *)grab_file(NULL, argv[i]+1);
		if (!txbytes)
			errx(1, "Could not open tx %s", argv[i]+1);

		size_t len = tal_count(txbytes)-1;
		if (len < 8)
			errx(1, "Truncated %s", argv[i]+1);
		memcpy(&fee, txbytes, 8);
		txbytes += 8;
		len -= 8;

		size_t bytes = len;
		t = new tx(fee, new bitcoin_tx(&txbytes, &len));
		if (argv[i][0] == '+') {
			plus_txs.insert(t);
			plus_bytes += bytes;
		} else if (argv[i][0] == '-') {
			minus_txs.insert(t);
			minus_bytes += bytes;
		} else
			errx(1, "%s doesn't start with + or -", argv[i]);
	}

	unsigned int successes = 0;
	size_t iblt_size = 0;
	for (unsigned int i = 0; i < 100; i++)
		successes += try_iblt(buckets, i*1082093+1, plus_txs, minus_txs,
							  i == 0 ? &iblt_size : NULL);

	// Buckets, bytes, txplus, bytesplus, txminus, bytesminus, successes.
	printf("%zu,%zu,%zu,%zu,%zu,%zu,%u\n", buckets, iblt_size,
		   plus_txs.size(), plus_bytes,
		   minus_txs.size(), minus_bytes,
		   successes);
}
	
	
	  
