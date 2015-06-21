// Test driver for IBLT code.
extern "C" {
#include "bitcoin-corpus.h"
#include <ccan/err/err.h>
#include <ccan/str/hex/hex.h>
#include <ccan/tal/grab_file/grab_file.h>
#include <ccan/tal/tal.h>
}
#include "mempool.h"
#include "iblt.h"
#include "ibltpool.h"
#include <iostream>
#define IBLT_SIZE 64

//#include "templates.cpp"

struct peer {
	mempool mp;
	corpus_entry *corpus;
	size_t corpus_num;
	size_t cursor;
	size_t decode_fail;

	peer() : corpus(NULL), cursor(0), decode_fail(0) { }
};

static bitcoin_txid txid_from_corpus(const struct corpus_entry &e)
{
	bitcoin_txid txid;
	memcpy(txid.shad.sha.u.u8, e.txid.id, sizeof(txid.shad.sha.u.u8));
	return txid;
}

static tx *get_tx(const bitcoin_txid &txid)
{
	char filename[sizeof("txcache/01234567890123456789012345678901234567890123456789012345678901234567")] = "txcache/";
	char *txstring;
	const u8 *txbytes;
	size_t len;
	u64 fee;

	txstring = filename + strlen("txcache/");
	if (!hex_encode(txid.shad.sha.u.u8, sizeof(txid.shad.sha.u.u8),
					txstring,
					sizeof(filename) - strlen("txcache/")))
		throw std::logic_error("txid doesn't fit in filename");

	txbytes = (u8 *)grab_file(NULL, filename);
	if (!txbytes) {
		errx(1, "Could not find tx %s", txstring);
	}

	len = tal_count(txbytes)-1;
	if (len < 8)
		errx(1, "Truncated %s", filename);
	memcpy(&fee, txbytes, 8);
	txbytes += 8;
	len -= 8;
	return new tx(fee, new bitcoin_tx(&txbytes, &len));
}

static std::vector<u8> generate_block(peer &p, size_t blocknum, size_t maxbytes,
									  u64 seed)
{
	if (corpus_entry_type(p.corpus + p.cursor) != COINBASE)
		errx(1, "Expected coinbase for block %zu", blocknum);

	tx *cb = get_tx(txid_from_corpus(p.corpus[p.cursor]));
	p.cursor++;

	std::unordered_set<const tx *> block;

	// Top up mempool with any txs we didn't know, get all txs in the block.
	while (p.cursor < p.corpus_num) {
		switch (corpus_entry_type(p.corpus + p.cursor)) {
		// These two cover the entire block contents.
		case UNKNOWN:
		case KNOWN: {
			bitcoin_txid txid = txid_from_corpus(p.corpus[p.cursor]);
			tx *t;

			t = p.mp.find(txid); 
			if (!t) {
				t = get_tx(txid);
				p.mp.add(t);
			}
			block.insert(t);
			break;
		}
		default:
			goto out;
		}
		p.cursor++;
	}

  out:
	// FIXME: We assume 10,000 satoshi for 1000 bytes.
	u64 min_fee_per_byte = 10;

	// Create set of added txs.
	txbitsSet added(48);

	// We include *everything* in our mempool; this ensures that our
	// "added" bitset distinguishes uniquely in our mempool.
	ibltpool pool(seed, p.mp);

	// FIXME: Sorting by fee per byte would speed this a little.
	for (const auto &txp : block) {
		// If this was an exception to our minimum, encode it.
		if (txp->satoshi_per_byte() < min_fee_per_byte) {
			txid48 id48(seed, txp->txid);
			std::vector<bool> bvec = pool.get_unique_bitid(id48);
			added[bvec.size()].insert(bvec);
		}
	}

	// FIXME: We currently assume none removed; simulate Eligius?
	txbitsSet removed;

	// Now, build iblt.
	raw_iblt<IBLT_SIZE> riblt(maxbytes / raw_iblt<IBLT_SIZE>::WIRE_BYTES,
							  seed, block);

	return wire_encode(*cb->btx, min_fee_per_byte, seed, added, removed, riblt);
}

static bool decode_block(peer &p, const std::vector<u8> in)
{
	bitcoin_tx cb(varint_t(0), varint_t(0));
	u64 min_fee_per_byte;
	u64 seed;
	txbitsSet added, removed;

	raw_iblt<IBLT_SIZE> their_riblt = wire_decode<IBLT_SIZE>(in, cb, min_fee_per_byte, seed, added, removed);

	// Create ids from my mempool, using their seed.
	ibltpool pool(seed, p.mp);

	// Start building up candidates.
	std::unordered_set<const tx *> candidates;

	// First, take all which exceed the given satoshi_per_byte.
	for (auto it = pool.tx_by_value.lower_bound(min_fee_per_byte);
		 it != pool.tx_by_value.end();
		 ++it) {
		candidates.insert(it->second);
	}

	// Now, remove any which they explicity said to remove
    for (const auto &s: removed) {
        for (const auto &vec : s) {
			// We can have more than one match: remove them all.
			for (const auto &t: pool.get_txs(vec)) {
				candidates.erase(t);
			}
		}
	}

	// Add any they said to add.
    for (const auto &s: added) {
        for (const auto &vec : s) {
			// We can have more than one match: add those not already in
			// due to fee-per-byte criterion.
			for (const auto &t: pool.get_txs(vec)) {
				if (t->satoshi_per_byte() < min_fee_per_byte) {
					candidates.insert(t);
				}
			}
		}
	}

	// Put this into a raw iblt.
	raw_iblt<IBLT_SIZE> our_riblt(their_riblt.size(), seed, candidates);

	// Create iblt with differences.
	iblt<IBLT_SIZE> diff(their_riblt, our_riblt);

	iblt<IBLT_SIZE>::bucket_type t;
	txslice<IBLT_SIZE> s;

	// For each txid48, we keep all the fragments.
	std::set<txslice<IBLT_SIZE>> slices;

	// While there are still singleton buckets...
	while ((t = diff.next(s)) != iblt<IBLT_SIZE>::NEITHER) {
		if (t == iblt<IBLT_SIZE>::OURS) {
			auto it = pool.tx_by_txid48.find(s.get_txid48());
			// If we can't find it, we're probably corrupt already, but
			// ignore it and try to keep going.
			if (it != pool.tx_by_txid48.end()) {
				// Remove entire tx.
				diff.remove(*it->second->btx, s.get_txid48());
			}
		} else if (t == iblt<IBLT_SIZE>::THEIRS) {
			slices.insert(s);
			diff.remove(s);
		}
	}

	// If we didn't empty it, we've failed decode.
	if (!diff.empty()) {
		return false;
	}

	// Try to assemble the slices into txs.
	size_t count = 0;
	std::vector<txslice<IBLT_SIZE>> transaction;

	for (const auto &s: slices) {
		if (count == 0) {
			size_t num = s.slices_expected();
			if (!num || num > 0xFFFF)
				return false;
			transaction = std::vector<txslice<IBLT_SIZE>>(num);
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
			if (count == transaction.size()) {
				// We recovered the entire transaction!
				// FIXME: Reconstruct block!
				count = 0;
			}
		}
	}

	// Some left over?
	return count == 0;
}

static void forward_to_block(peer &p, size_t blocknum)
{
	while (p.cursor < p.corpus_num) {
		switch (corpus_entry_type(p.corpus + p.cursor)) {
		case COINBASE:
			if (corpus_blocknum(p.corpus + p.cursor) == blocknum)
				return;
		default:
			p.cursor++;
		}
	}
	errx(1, "Could not find block number %zu", blocknum);
}

int main(int argc, char *argv[])
{
	size_t blocknum = 352305, end = 353305;
	u64 seed = 1;

	if (argc < 4)
		errx(1, "Usage: %s <max-bytes> <generator-corpus> <peer-corpus>...", argv[0]);

	// We keep track of everyone's mempools.
	size_t num_pools = argc - 2;
	std::vector<peer> peers(num_pools);

	size_t argnum = 3;
	// Get corpuses.  Hope you have plenty of memory!
	for (auto &p : peers) {
		p.corpus = (corpus_entry *)grab_file(NULL, argv[argnum]);
		p.corpus_num = tal_count(p.corpus) / sizeof(*p.corpus);
		forward_to_block(p, blocknum);
	}

	do {
		// Now, first peer generates block (may have to add to pool)
		std::vector<u8> incoming = generate_block(peers[0], blocknum,
												  atoi(argv[1]), seed++);

		// Others receive the block.
		for (size_t i = 1; i < num_pools; i++)
			peers[i].decode_fail += !decode_block(peers[i], incoming);
		blocknum++;

		for (auto &p : peers)
			forward_to_block(p, blocknum);
	} while (blocknum != end);

	for (size_t i = 1; i < num_pools; i++) {
		std::cout << "Peer " << i << ": " << peers[i].decode_fail << std::endl;
	}
}
