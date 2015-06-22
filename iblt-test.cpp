// Test driver for IBLT code.
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
#include "mempool.h"
#include "iblt.h"
#include "ibltpool.h"
#include <iostream>

#define IBLT_SIZE 64

struct peer {
	mempool mp;
	size_t decode_fail;
	int infd;
	const char *file;
	struct corpus_entry e;

	peer() : decode_fail(0), infd(-1) { }

	// FIXME: Read .xz files directly...
	bool next_entry() {
		return read_all(infd, &e, sizeof(e));
	}
	bool open(const char *filename) {
		file = filename;
		infd = ::open(filename, O_RDONLY);
		return infd >= 0 && next_entry();
	}
};

static bitcoin_txid txid_from_corpus(const struct corpus_entry &e)
{
	bitcoin_txid txid;
	/* Corpus txids are backwards :( */
	for (size_t n = 0; n < sizeof(e.txid.id); n++) {
		txid.shad.sha.u.u8[sizeof(e.txid.id)-1-n] = e.txid.id[n];
	}
	return txid;
}

static tx *get_tx(const bitcoin_txid &txid)
{
	char filename[sizeof("txcache/01234567890123456789012345678901234567890123456789012345678901234567")] = "txcache/";
	char *txstring;
	const u8 *txbytes;
	size_t len;
	u64 fee;
	tx *t;

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
	t = new tx(fee, new bitcoin_tx(&txbytes, &len));
	assert(t->txid == txid);
	return t;
}

static std::vector<u8> generate_block(peer &p, size_t blocknum, size_t maxbytes,
									  u64 seed)
{
	if (corpus_entry_type(&p.e) != COINBASE)
		errx(1, "Expected coinbase for block %zu", blocknum);

	tx *cb = get_tx(txid_from_corpus(p.e));

	std::unordered_set<const tx *> block;

	// Top up mempool with any txs we didn't know, get all txs in the block.
	while (p.next_entry()) {
		tx *t;

		switch (corpus_entry_type(&p.e)) {
		// These two cover the entire block contents.
		case UNKNOWN: {
			bitcoin_txid txid = txid_from_corpus(p.e);

			assert(!p.mp.find(txid));

			t = get_tx(txid);
			p.mp.add(t);
			block.insert(t);
			break;
		}
		case KNOWN: {
			t = p.mp.find(txid_from_corpus(p.e));
			block.insert(t);
			break;
		}
		default:
			goto out;
		}
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
	raw_iblt riblt(maxbytes / raw_iblt::WIRE_BYTES, seed, block);

	return wire_encode(*cb->btx, min_fee_per_byte, seed, added, removed, riblt);
}

static bool decode_block(peer &p, const std::vector<u8> in)
{
	bitcoin_tx cb(varint_t(0), varint_t(0));
	u64 min_fee_per_byte;
	u64 seed;
	txbitsSet added, removed;

	raw_iblt their_riblt = wire_decode(in, cb, min_fee_per_byte, seed, added, removed);

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
	raw_iblt our_riblt(their_riblt.size(), seed, candidates);

	// Create iblt with differences.
	iblt diff(their_riblt, our_riblt);

	iblt::bucket_type t;
	txslice s;

	// For each txid48, we keep all the fragments.
	std::set<txslice> slices;

	// While there are still singleton buckets...
	while ((t = diff.next(s)) != iblt::NEITHER) {
		if (t == iblt::OURS) {
			auto it = pool.tx_by_txid48.find(s.get_txid48());
			// If we can't find it, we're corrupt.
			// FIXME: Maybe keep going?.
			if (it == pool.tx_by_txid48.end()) {
				return false;
			} else {
				// Remove entire tx.
				diff.remove_our_tx(*it->second->btx, s.get_txid48());
				// Make sure we make progress: remove it from consideration.
				pool.tx_by_txid48.erase(s.get_txid48());
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
	if (!diff.empty()) {
		return false;
	}

	// Try to assemble the slices into txs.
	size_t count = 0;
	std::vector<txslice> transaction;

	for (const auto &s: slices) {
		if (count == 0) {
			size_t num = s.slices_expected();
			if (!num || num > 0xFFFF)
				return false;
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
			if (count == transaction.size()) {
				// We recovered the entire transaction!
				// FIXME: Reconstruct block!
				count = 0;
			}
		}
	}

	if (count == 0)
		std::cout << std::string(p.file) << " decoding block SUCCESS" << std::endl;
	else
		std::cout << std::string(p.file) << " decoding block left a fragment" << std::endl;
	// Some left over?
	return count == 0;
}

// We sync the mempool at the block before.
static void forward_to_block(peer &p, size_t blocknum)
{
	bool prev_block = false;
	do {
		switch (corpus_entry_type(&p.e)) {
		case COINBASE:
			if (corpus_blocknum(&p.e) == blocknum) {
				return;
			}
			assert(!prev_block);
			prev_block = (corpus_blocknum(&p.e) == blocknum - 1);
			break;
		case INCOMING_TX:
		case MEMPOOL_ONLY:
			if (prev_block) {
				// Add this to the mempool
				p.mp.add(get_tx(txid_from_corpus(p.e)));
				assert(p.mp.find(txid_from_corpus(p.e)));
			}
			break;
		case KNOWN:
		case UNKNOWN:
			break;
		}
	} while (p.next_entry());
	errx(1, "No block number %zu for peer %s", blocknum, p.file);
}

static void next_block(peer &p)
{
	// Keep going until next coinbase;
	while (p.next_entry()) {
		switch (corpus_entry_type(&p.e)) {
		case COINBASE:
			return;
		case INCOMING_TX:
			// Add this to the mempool
			p.mp.add(get_tx(txid_from_corpus(p.e)));
			break;
		case KNOWN:
			// It was in block, so remove from mempool.
			if (!p.mp.del(txid_from_corpus(p.e))) {
				errx(1, "Peer %s has bad txid in block?", p.file);
			}
			break;
		case MEMPOOL_ONLY:
			assert(p.mp.find(txid_from_corpus(p.e)));
			break;
		case UNKNOWN:
			assert(!p.mp.find(txid_from_corpus(p.e)));
			break;
		}
	}
	errx(1, "No next block for peer %s", p.file);
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

	size_t argnum = 2;
	// Get corpuses.  Hope you have plenty of memory!
	for (auto &p : peers) {
		if (!p.open(argv[argnum++]))
			err(1, "Opening %s", argv[argnum-1]);
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
			next_block(p);
	} while (blocknum != end);

	for (size_t i = 1; i < num_pools; i++) {
		std::cout << "Peer " << i << ": " << peers[i].decode_fail << std::endl;
	}
}
