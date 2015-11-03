// Test driver for IBLT code.
extern "C" {
#include "bitcoin-corpus.h"
#include <ccan/err/err.h>
#include <ccan/read_write_all/read_write_all.h>
#include <ccan/tal/tal.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
}
#include "mempool.h"
#include "iblt.h"
#include "ibltpool.h"
#include "txcache.h"
#include "txtree.h"
#include <iostream>

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
		const tx *t;

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
	std::cout << "," << blocksize
			  << "," << known
			  << "," << unknown
			  << "," << p.mp.length();

	// FIXME: We assume 10,000 satoshi for 1000 bytes.
	min_fee_per_byte = 10 << 13;

	// Create set of added txs.
	added = txbitsSet(48);

	// We include *everything* in our mempool; this ensures that our
	// "added" bitset distinguishes uniquely in our mempool.
	ibltpool pool(seed, p.mp.tx_by_txid);

	// FIXME: Sorting by fee per byte would speed this a little.
	for (const auto &txp : block) {
		// If this was an exception to our minimum, encode it.
		if (txp->satoshi_per_byte() < min_fee_per_byte) {
			txid48 id48(seed, txp->txid);
			std::vector<bool> bvec = pool.tree->get_unique_bitid(id48);
			added[bvec.size()].insert(bvec);
		}
	}

	size_t num_removed = 0;
	// What didn't we include, that would be expected?
	removed = txbitsSet(48);
	for (const auto &t : pool.tx_by_txid48) {
		if (t.second->satoshi_per_byte() >= min_fee_per_byte) {
			if (block.find(t.second) == block.end()) {
				std::vector<bool> bvec = pool.tree->get_unique_bitid(t.first);
				removed[bvec.size()].insert(bvec);
				num_removed++;
			}
		}
	}

	std::vector<u8> for_length;
	add_bitset(&for_length, added);
	std::cout << "," << for_length.size();
	for_length = std::vector<u8>();
	add_bitset(&for_length, removed);
	std::cout << "," << for_length.size();

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

static raw_iblt wire_decode(const std::vector<u8> &incoming,
                     bitcoin_tx &coinbase,
                     u64 &min_fee_per_byte,
                     u64 &seed,
                     txbitsSet &added,
                     txbitsSet &removed)
{
    size_t len = incoming.size();
    const u8 *p = incoming.data();
    varint_t size;

    seed = pull_varint(&p, &len);
    min_fee_per_byte = pull_varint(&p, &len);
    size = pull_varint(&p, &len);
    coinbase = bitcoin_tx(&p, &len);
    
    if (!decode_bitset(&p, &len, added) || !decode_bitset(&p, &len, removed))
        throw std::runtime_error("bad bitset");

    // Sanity check size first: forget it if it's bigger than 100M.
    if (size > 100 * 1024 * 1024 / IBLT_SIZE)
        throw std::runtime_error("bad size");

    raw_iblt iblt(size);
    // Fails if not exactly the right amount left.
    if (!iblt.read(p, len))
        throw std::runtime_error("bad iblt");
      
    return iblt;
}

static bool decode_block(const peer &p, const std::vector<u8> in, size_t blocknum, size_t &slices_recovered, size_t &slices_discarded, size_t &txs_discarded)
{
	bitcoin_tx cb(varint_t(0), varint_t(0));
	u64 min_fee_per_byte;
	u64 seed;
	txbitsSet added, removed;

	raw_iblt their_riblt = wire_decode(in, cb, min_fee_per_byte, seed, added, removed);

	// Create ids from my mempool, using their seed.
	ibltpool pool(seed, p.mp.tx_by_txid);

	// Start building up candidates.
	std::unordered_set<const tx *> candidates;

	// First, take all which exceed the given satoshi_per_byte.
	for (const auto it: pool.tx_by_txid48) {
		candidates.insert(it.second);
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

	txs_discarded = 0;
	slices_recovered = 0;
	slices_discarded = 0;
	
	// While there are still singleton buckets...
	while ((t = diff.next(s)) != iblt::NEITHER) {
		if (t == iblt::OURS) {
			auto it = pool.tx_by_txid48.find(s.get_txid48());
			// If we can't find it, we're corrupt.
			if (it == pool.tx_by_txid48.end()) {
				return fail(p, blocknum, txs_discarded, slices_recovered);
			} else {
				// Remove entire tx.
				slices_discarded += diff.remove_our_tx(*it->second->btx, s.get_txid48());
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
			// Do we expect this to be the first fragment?
			if (s.get_txid48().frag_base() != s.fragid) {
				return fail(p, blocknum, txs_discarded, slices_recovered);
			}
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
		}
		if (count == transaction.size()) {
			// We recovered the entire transaction!
			// FIXME: Reconstruct tx, check id48 correct.
			// FIXME: Reconstruct block!
			count = 0;
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

static bool maybe_orphan(size_t blocknum)
{
	return blocknum == 352802;
}

// We identify orphaned blocks by coinbase.
static bool orphaned(const bitcoin_txid &txid)
{
	// This is the 352802 orphan
	return (txid == bitcoin_txid("79b1c309ab8ab92bca4d07508e0f596f872f66c6db4d3667133a37172055e97b", strlen("79b1c309ab8ab92bca4d07508e0f596f872f66c6db4d3667133a37172055e97b")));
}

// Sync up mempool based on next block.
static void next_block(peer &p, size_t blocknum)
{
	// Keep going until next coinbase;
	while (p.next_entry()) {
		switch (corpus_entry_type(&p.e)) {
		case COINBASE:
			// If it's orphaned, ignore it.
			if (orphaned(txid_from_corpus(p.e)))
				break;
			// If this fails, we hit an orphan!
			assert(corpus_blocknum(&p.e) == blocknum);
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
	errx(1, "No block %zu for peer %s", blocknum, p.file);
}

// We sync the mempool at the block before.
static void forward_to_block(peer &p, size_t blocknum)
{
	bool prev_block = false;

	// Corner case: previous blocknum is orphan.  Re-use next_block logic().
	if (maybe_orphan(blocknum - 1)) {
		forward_to_block(p, blocknum - 2);
		next_block(p, blocknum-1);
		next_block(p, blocknum);
		return;
	}

	do {
		switch (corpus_entry_type(&p.e)) {
		case COINBASE:
			// If it's orphaned, ignore it.
			if (orphaned(txid_from_corpus(p.e))) {
				break;
			}
			if (corpus_blocknum(&p.e) == blocknum) {
				assert(prev_block);
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
//					std::cout << "Added " << t->txid << std::endl;
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

static std::vector<u8> wire_encode(const bitcoin_tx &coinbase,
								   const u64 min_fee_per_byte,
								   const u64 seed,
								   const txbitsSet &added,
								   const txbitsSet &removed,
								   const raw_iblt &iblt)
{
    std::vector<u8> arr;

    add_varint(seed, add_linearize, &arr);
    add_varint(min_fee_per_byte, add_linearize, &arr);
    add_varint(iblt.size(), add_linearize, &arr);
    coinbase.add_tx(add_linearize, &arr);

    add_bitset(&arr, added);
    add_bitset(&arr, removed);

    std::vector<u8> ib = iblt.write();
    add_linearize(ib.data(), ib.size(), &arr);

    return arr;
}

static size_t min_decode(std::unordered_set<const tx *> block,
					  const txbitsSet &added,
					  const txbitsSet &removed,
					  u64 min_fee_per_byte,
					  const bitcoin_tx &cb,
					  const peer &p, u64 seed, size_t blocknum,
					  size_t &iblt_slices, size_t &slices_recovered,
					  size_t &slices_discarded, size_t &txs_discarded)
{
	// Try up to 16MB worth of IBLT.
	const size_t max_possible = 16 * 1024 * 1024 / IBLT_SIZE;
	size_t min_buckets = 1, max_buckets = max_possible;
	size_t num, data_size = 16 * 1024 * 1024;

	slices_recovered = txs_discarded = slices_discarded = iblt_slices = 0;
	while (min_buckets != max_buckets) {
		num = (min_buckets + max_buckets) / 2;
		raw_iblt riblt(num, seed, block);
		size_t srecovered, sdiscarded, tdiscarded;

		std::vector<u8> data = wire_encode(cb, min_fee_per_byte, seed,
										   added, removed, riblt);

		if (decode_block(p, data, blocknum, srecovered, sdiscarded, tdiscarded)) {
			max_buckets = num;
			data_size = data.size();
			slices_recovered = srecovered;
			slices_discarded = sdiscarded;
			txs_discarded = tdiscarded;
			iblt_slices = riblt.size();
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
	u64 seed = 0;

	if (argc < 3)
		errx(1, "Usage: %s [--range=a,b] [--seed=<seed>] <generator-corpus> <peer-corpus>...", argv[0]);

	while (strncmp(argv[1], "--", 2) == 0) {
		char *endp;
		if (strncmp(argv[1], "--range=", strlen("--range=")) == 0) {
			blocknum = strtoul(argv[1] + strlen("--range="), &endp, 10);
			if (*endp != ',')
				errx(1, "Invalid --range");
			end = strtoul(endp+1, &endp, 10) + 1;
			if (end <= blocknum)
				errx(1, "Invalid --range");
		} else if (strncmp(argv[1], "--seed=", strlen("--seed=")) == 0){
			seed = strtoul(argv[1] + strlen("--seed="), &endp, 10);
			if (*endp || !seed)
				errx(1, "Invalid --seed");
		} else
			errx(1, "Unknown argument %s", argv[1]);
		argc--;
		argv++;
	}

	if (seed == 0)
		seed = blocknum - 352719;
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

	std::cout << "blocknum,blocksize,knownbytes,unknownbytes,mempoolbytes,addedbitsetsize,removedbitsetsize";
	for (size_t i = 2; i < argc; i++) {
		const char *slash = strrchr(argv[i], '/');
		if (!slash)
			slash = argv[1];
		else
			slash++;
		std::cout << "," << slash << ","
				  << slash << "-ibltslices" << ","
				  << slash << "-slicesrecovered" << ","
				  << slash << "-slicesdiscarded" << ","
				  << slash << "-txsdiscarded";
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
			size_t iblt_slices, slices_recovered, slices_discarded, txs_discarded;
			size_t min_size = min_decode(block, added, removed,
									  min_fee_per_byte, *coinbase->btx,
									  peers[i],
									  seed, blocknum,
									  iblt_slices, slices_recovered, slices_discarded, txs_discarded);
			std::cout << "," << min_size << "," << iblt_slices << "," << slices_recovered << "," << slices_discarded << "," << txs_discarded;
		}
		std::cout << std::endl;
		blocknum++;
		for (auto &p : peers)
			next_block(p, blocknum);
	} while (blocknum != end);
}
