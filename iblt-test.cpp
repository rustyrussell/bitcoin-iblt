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

static bool verbose;

struct peer {
	mempool mp;
	int infd;
	const char *file;
	struct corpus_entry e;

	peer() : infd(-1) { }

	// FIXME: Read .xz files directly...
	bool next_entry() {
		return read_all(infd, &e, sizeof(e));
	}
	bool open(const char *filename) {
		file = filename;
		infd = ::open(filename, O_RDONLY);
		return infd >= 0 && next_entry();
	}

	std::unordered_set<bitcoin_txid> unknown;
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

static tx *get_tx(const bitcoin_txid &txid, bool must_exist = true)
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
		if (must_exist)
			errx(1, "Could not find tx %s", txstring);
		warnx("could not find tx %s", txstring);
		return NULL;
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

static std::unordered_set<const tx *>
generate_block(peer &p, size_t blocknum, u64 seed,
			   tx *&cb,
			   txbitsSet &added, txbitsSet &removed,
			   u64 &min_fee_per_byte)
{
	if (corpus_entry_type(&p.e) != COINBASE)
		errx(1, "Expected coinbase for block %zu", blocknum);

	cb = get_tx(txid_from_corpus(p.e));

	std::unordered_set<const tx *> block;
	size_t blocksize = cb->btx->length(), unknown = 0, known = 0;

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
			blocksize += t->btx->length();
			unknown += t->btx->length();
			break;
		}
		case KNOWN: {
			t = p.mp.find(txid_from_corpus(p.e));
			block.insert(t);
			blocksize += t->btx->length();
			known += t->btx->length();
			break;
		}
		default:
			goto out;
		}
	}

  out:
	std::cout << ":" << blocksize
			  << ":" << known
			  << ":" << unknown
			  << ":" << p.mp.length();

	// FIXME: We assume 10,000 satoshi for 1000 bytes.
	min_fee_per_byte = 10 << 13;

	// Create set of added txs.
	added = txbitsSet(48);

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

	size_t num_removed = 0;
	// What didn't we include, that would be expected?
	removed = txbitsSet(48);
	for (const auto &t : pool.tx_by_txid48) {
		if (t.second->satoshi_per_byte() >= min_fee_per_byte) {
			if (block.find(t.second) == block.end()) {
				std::vector<bool> bvec = pool.get_unique_bitid(t.first);
				removed[bvec.size()].insert(bvec);
				num_removed++;
			}
		}
	}

	std::vector<u8> for_length;
	add_bitset(&for_length, added);
	std::cout << ":" << for_length.size();
	for_length = std::vector<u8>();
	add_bitset(&for_length, removed);
	std::cout << ":" << for_length.size();

	// Now remove everything in block from our mempool.
	for (auto &t: block) {
		p.mp.del(t->txid);
	}

	return block;
}

// Debugger attach point;
static bool fail(const peer &p,
				 size_t blocknum, size_t txs_discarded, size_t slices_recovered)
{
	if (verbose)
		std::cout << std::string(p.file)
				  << ":" << blocknum
				  << ":FAILED"
				  << ": transactions removed " << txs_discarded
				  << ", slices recovered " << slices_recovered
				  << std::endl;
	return false;
}

static bool decode_block(const peer &p, const std::vector<u8> in, size_t blocknum)
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
	size_t num_removed = 0;
    for (const auto &s: removed) {
        for (const auto &vec : s) {
			// We can have more than one match: remove them all.
			for (const auto &t: pool.get_txs(vec)) {
				if (candidates.erase(t)) {
					num_removed++;
				}
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

	size_t txs_discarded = 0;
	size_t slices_recovered = 0;

	// While there are still singleton buckets...
	while ((t = diff.next(s)) != iblt::NEITHER) {
		if (t == iblt::OURS) {
			auto it = pool.tx_by_txid48.find(s.get_txid48());
			// If we can't find it, we're corrupt.
			if (it == pool.tx_by_txid48.end()) {
				return fail(p, blocknum, txs_discarded, slices_recovered);
			} else {
				// Remove entire tx.
				diff.remove_our_tx(*it->second->btx, s.get_txid48());
				// Make sure we make progress: remove it from consideration.
				pool.tx_by_txid48.erase(s.get_txid48());
				txs_discarded++;
			}
		} else if (t == iblt::THEIRS) {
			// Gave us the same slice twice?  Fail.
			if (!slices.insert(s).second) {
				return fail(p, blocknum, txs_discarded, slices_recovered);
			}
			diff.remove_their_slice(s);
			slices_recovered++;
		}
	}

	// If we didn't empty it, we've failed decode.
	if (!diff.empty()) {
		return fail(p, blocknum, txs_discarded, slices_recovered);
	}

	// Try to assemble the slices into txs.
	size_t count = 0;
	std::vector<txslice> transaction;

	for (const auto &s: slices) {
		if (count == 0) {
			size_t num = s.slices_expected();
			if (!num || num > 0xFFFF) {
				return fail(p, blocknum, txs_discarded, slices_recovered);
			}
			transaction = std::vector<txslice>(num);
			transaction[0] = s;
			count = 1;
		} else {
			// Missing part of transaction?
			if (s.txidbits != transaction[count-1].txidbits) {
				return fail(p, blocknum, txs_discarded, slices_recovered);
			}
			// Fragment id wrong?
			if (s.fragid != transaction[count-1].fragid + 1) {
				return fail(p, blocknum, txs_discarded, slices_recovered);
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
	if (count != 0) {
		return fail(p, blocknum, txs_discarded, slices_recovered);
	}
	if (verbose)
		std::cout << std::string(p.file)
				  << ":" << blocknum
				  << ":SUCCESS"
				  << ": transactions removed " << txs_discarded
				  << ", slices recovered " << slices_recovered
				  << std::endl;
	return true;
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
				// Add this to the mempool; ignore a few unknowns.
				tx *t = get_tx(txid_from_corpus(p.e), false);
				if (t) {
					p.mp.add(t);
					assert(p.mp.find(txid_from_corpus(p.e)));
				} else {
					p.unknown.insert(txid_from_corpus(p.e));
				}
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
		case INCOMING_TX: {
			// Add this to the mempool; ignore a few unknowns.
			tx *t = get_tx(txid_from_corpus(p.e), false);
			if (t) {
				p.mp.add(t);
			} else {
				p.unknown.insert(txid_from_corpus(p.e));
			}
			break;
		}
		case KNOWN:
			// It was in block, so remove from mempool.
			if (!p.mp.del(txid_from_corpus(p.e))) {
				if (!p.unknown.erase(txid_from_corpus(p.e))) {
					errx(1, "Peer %s has bad txid in block?", p.file);
				}
			}
			break;
		case MEMPOOL_ONLY:
			assert(p.mp.find(txid_from_corpus(p.e))
				   || p.unknown.find(txid_from_corpus(p.e)) != p.unknown.end());
			break;
		case UNKNOWN:
			assert(!p.mp.find(txid_from_corpus(p.e)));
			break;
		}
	}
	errx(1, "No next block for peer %s", p.file);
}

static int min_decode(std::unordered_set<const tx *> block,
					  const txbitsSet &added,
					  const txbitsSet &removed,
					  u64 min_fee_per_byte,
					  const bitcoin_tx &cb,
					  const peer &p, u64 seed, size_t blocknum)
{
	// Try up to 4MB
	const size_t max_possible = 4 * 1024 * 1024 / raw_iblt::WIRE_BYTES;
	size_t min_buckets = 1, max_buckets = max_possible;
	size_t num, data_size = -1;

	while (min_buckets != max_buckets) {
		num = (min_buckets + max_buckets) / 2;
		raw_iblt riblt(num, seed, block);

		std::vector<u8> data = wire_encode(cb, min_fee_per_byte, seed,
										   added, removed, riblt);

		if (decode_block(p, data, blocknum)) {
			max_buckets = num;
			data_size = data.size();
		} else {
			min_buckets = num + 1;
			// Complete failure?
			if (min_buckets == max_possible - 1) {
				break;
			}
		}
	}

	return data_size;
}

int main(int argc, char *argv[])
{
	// 352792 to 352810 is a time of backlog, so include that.
	size_t blocknum = 352720, end = 352820;
	u64 seed = 1;

	if (argc < 4)
		errx(1, "Usage: %s <generator-corpus> <peer-corpus>...", argv[0]);

	// We keep track of everyone's mempools.
	size_t num_pools = argc - 1;
	std::vector<peer> peers(num_pools);

	size_t argnum = 1;
	// Open corpuses.
	for (auto &p : peers) {
		if (!p.open(argv[argnum++]))
			err(1, "Opening %s", argv[argnum-1]);
		forward_to_block(p, blocknum);
	}

	std::cout << "blocknum:blocksize:knownbytes:unknownbytes:mempoolbytes:addedbitesetsize:removedbitsetsize";
	for (size_t i = 2; i < argc; i++) {
		const char *slash = strrchr(argv[i], '/');
		if (!slash)
			slash = argv[1];
		else
			slash++;
		std::cout << ":" << slash;
	}
	std::cout << std::endl;

	do {
		seed++;
		
		// Peer 0 generates a block.
		txbitsSet added, removed;
		tx *coinbase;
		std::unordered_set<const tx *> block;
		u64 min_fee_per_byte;

		std::cout << blocknum;
		block = generate_block(peers[0], blocknum, seed, coinbase, added, removed, min_fee_per_byte);

		// See how small we can encode it for each peer.
		for (size_t i = 1; i < num_pools; i++) {
			int min_size = min_decode(block, added, removed,
									  min_fee_per_byte, *coinbase->btx,
									  peers[i],
									  seed, blocknum);
			std::cout << ":" << min_size;
		}
		std::cout << std::endl;
		blocknum++;
		for (auto &p : peers)
			next_block(p);
	} while (blocknum != end);
}
