// This code takes blocks and mempools and uses a heuristic encoding to
// reduce the transactions under consideration.

// This heuristic is:
// 1: Minimum fee-per-byte.
// 2: TXids not included despite being above the minfee.
// 3: TXids which are included despite being below the minfee.

// Input and output are of forms:
// <FILE> := <BLOCKDESC>*
// For each block, in incrementing order:
// <BLOCKDESC> := <BLOCK-LINE><MEMPOOL-LINE>+
// <BLOCK-LINE> := <BLOCKHEIGHT>:<OVERHEAD-BYTES>:<TXID>*
// <BLOCKHEIGHT> := integer
// <OVERHEAD-BYTES> := integer // block header + coinbase + metadata size
// <TXID> := hex // TXID
// For each peer, after each <BLOCK-LINE>:
// <MEMPOOL-LINE> := mempool:<PEERNAME>:<TXID>*

#include "txcache.h"
#include "txtree.h"
#include "ibltpool.h"
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <cassert>
#include <algorithm>

typedef std::unordered_map<bitcoin_txid, const tx *> txmap;
static bool verbose = false;

static std::istream &input_file(const char *argv)
{
	if (!argv)
		return std::cin;
	return *(new std::ifstream(argv, std::ios::in));
}

static bool get_txid(std::istream &in, bitcoin_txid &txid)
{
	switch (in.get()) {
	case '\n':
		return false;
	case ':':
		in >> txid;
		if (!in)
			throw std::runtime_error("Expected txid");
		return true;
	default:
		throw std::runtime_error("Expected :");
	}
}

static txmap read_txids(std::istream &in)
{
	txmap map;
	bitcoin_txid txid;

	while (get_txid(in, txid)) {
		tx *t = get_tx(txid, false);
		// Ingore unknowns; we warn already.
		if (!t)
			continue;
		map.insert(std::make_pair(txid, t));
	}
	return map;
}

typedef std::pair<const bitcoin_txid, u64> txidfee;
				  
// Sorts low to high
static bool fee_compare(const tx *a, const tx *b)
{
	return a->satoshi_per_byte() < b->satoshi_per_byte();
}

// Find next fee which is greater.
static bool next_fee(const std::vector<const tx *> &a, size_t *ai,
					 const std::vector<const tx *> &b, size_t *bi,
					 u64 *fee)
{
	while (*ai < a.size() && a[*ai]->satoshi_per_byte() == *fee)
		(*ai)++;
	while (*bi < b.size() && b[*bi]->satoshi_per_byte() == *fee)
		(*bi)++;

	// They're in increasing order.
	assert(*ai == a.size() || a[*ai]->satoshi_per_byte() > *fee);
	assert(*bi == b.size() || b[*bi]->satoshi_per_byte() > *fee);

	if (*ai == a.size() && *bi == b.size())
		return false;

	if (*ai == a.size())
		*fee = b[*bi]->satoshi_per_byte();
	else if (*bi == b.size())
		*fee = a[*ai]->satoshi_per_byte();
	else if (a[*ai]->satoshi_per_byte() < b[*bi]->satoshi_per_byte())
		*fee = a[*ai]->satoshi_per_byte();
	else
		*fee = b[*bi]->satoshi_per_byte();
	return true;
}

static u64 estimate_fees(const txmap &block, const txmap &mempool)
{
	// We aim to minimize the number of exceptions; things in our mempool
	// above the fee estimate, plus things in block below fee estimate.

	// FIXME: This can be optimized.  Exercise for reader.
	std::vector<const tx *> bvec, mvec;

	for (txmap::const_iterator it = block.begin(); it != block.end(); ++it)
    	bvec.push_back(it->second);
	std::sort(bvec.begin(), bvec.end(), fee_compare);

	// Consider things in our mempool *not* in block.
	for (txmap::const_iterator it = mempool.begin(); it != mempool.end(); ++it) {
		if (block.find(it->first) == block.end())
			mvec.push_back(it->second);
	}
	std::sort(mvec.begin(), mvec.end(), fee_compare);

	// We can set fee to zero, and simply list everything only in mempool to be
	// excluded.
	u64 best_fee = 0;
	size_t num_txs_excepted = mvec.size();

	size_t bi = 0, mi = 0;
	u64 fee = 0;
	if (verbose) {
		std::cerr << "Fee:" << fee << " block extra:" << bi << " mempool excl:" << mvec.size() - mi << std::endl;
	}

	while (next_fee(bvec, &bi, mvec, &mi, &fee)) {
		// We have to encode things in block below fee threshold, and
		// things in mempool above fee threshhold.
		size_t cost = bi + (mvec.size() - mi);
		if (verbose) {
			std::cerr << "Fee:" << fee << " block extra:" << bi << " mempool excl:" << mvec.size() - mi << std::endl;
		}
		if (cost < num_txs_excepted) {
			num_txs_excepted = cost;
			best_fee = fee;
		}
	}

	return best_fee;
}

int main(int argc, char *argv[])
{
    /* FIXME: cmdline option */
	u64 seed = 1;
	std::istream &in = input_file(argv[1]);

	std::default_random_engine generator;
	std::uniform_real_distribution<double> distribution(0.0,1.0);	

	while (in) {
		unsigned int blocknum, overhead;
		bitcoin_txid txid;
		txmap block;

		in >> blocknum;
		if (!in) break;

		if (in.get() != ':')
			throw std::runtime_error("Bad blocknum or :");
		in >> overhead;
		block = read_txids(in);

		txbitsSet added_list, removed_list;
		u64 fee_hint;
		bool first_peer = true;

		while (!in.eof() && in.peek() == 'm') {
			std::string mempoolstr;
			std::getline(in, mempoolstr, ':');
			if (mempoolstr != "mempool")
				throw std::runtime_error("Bad mempool line");
			std::string peername;
			while (in.peek() != ':' && in.peek() != '\n') {
				if (!in)
					throw std::runtime_error("Bad peername");
				peername += in.get();
			}

			txmap mempool = read_txids(in);
			ibltpool ibltpool(seed, mempool);

			if (first_peer) {
				// Get optimal fee for encoding.
				fee_hint = estimate_fees(block, mempool);

				// Encode txs included-though-too-low and
				// excluded-though-high-enough.
				size_t num_added = 0, num_removed = 0;
				for (const auto &pair: mempool) {
					if (pair.second->satoshi_per_byte() < fee_hint) {
						if (block.find(pair.first) != block.end()) {
							num_added++;
							txid48 id48(seed, pair.first);
							std::vector<bool> bvec = ibltpool.tree->get_unique_bitid(id48);
							added_list[bvec.size()].insert(bvec);
						}
					} else {
						if (block.find(pair.first) == block.end()) {
							num_removed++;
							txid48 id48(seed, pair.first);
							std::vector<bool> bvec = ibltpool.tree->get_unique_bitid(id48);
							removed_list[bvec.size()].insert(bvec);
						}
					}
				}
				
				if (verbose) {
					std::cerr << "Block " << blocknum << std::endl;
					std::cerr << "Block size: " << block.size() << std::endl;
					std::cerr << "Mempool size: " << mempool.size() << std::endl;
					std::cerr << "Fee hint: " << fee_hint << std::endl;
					std::cerr << "Txs added: " << num_added << std::endl;
					std::cerr << "Txs removed: " << num_removed << std::endl;
				}

				// Encode the bitsets to get the length.
				std::vector<u8> bytes;
				add_varint(seed, add_linearize, &bytes);
				add_varint(fee_hint, add_linearize, &bytes);
				add_bitset(&bytes, added_list);
				add_bitset(&bytes, removed_list);

				overhead += bytes.size();
				first_peer = false;

				std::cout << blocknum << ":" << overhead;
				for (const auto &pair: block)
					std::cout << ":" << pair.first;
				std::cout << std::endl;
				
				// We don't output mempool for first peer, since that's
				// not relevant; they're doing the transmission.
				continue;
			}

			txmap newmempool;

			// Start by including every tx which meets fee hint.
			for (const auto &pair : mempool) {
				if (pair.second->satoshi_per_byte() >= fee_hint) {
					newmempool.insert(pair);
				}
			}

			// Now insert any added.
			for (size_t i = 0; i < added_list.size(); i++) {
				for (const auto &v: added_list[i]) {
					for (const auto &tx: ibltpool.get_txs(v)) {
						// This can get false positives; fortunately insert()
						// does nothing if it already exists.
						newmempool.insert(std::make_pair(tx->txid, tx));
					}
				}
			}

			// Now remove any removed.
			for (size_t i = 0; i < removed_list.size(); i++) {
				for (const auto &v: removed_list[i]) {
					for (const auto &tx: ibltpool.get_txs(v)) {
						newmempool.erase(tx->txid);
					}
				}
			}

			// Now selection criteria will filter in all txs in the block, but
			// there can be false positives if a txid48 matches one of the
			// included bit prefixes.
			std::cout << "mempool:" << peername;
			for (const auto &tx: newmempool) {
				std::cout << ":" << tx.first;
			}
			std::cout << std::endl;
		}
	}
}
		
			
			
