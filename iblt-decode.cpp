// This code takes blocks, iblts and mempools and tries to decode them.
// It produces output as:
// blocknum,overhead,ibltbytes,peername,[0|1]

extern "C" {
#include <ccan/err/err.h>
#include <ccan/str/hex/hex.h>
}
#include "txtree.h"
#include "ibltpool.h"
#include "io.h"
#include "rawiblt.h"
#include "iblt.h"
#include <stdexcept>
#include <cassert>
#include <algorithm>

static raw_iblt *read_iblt(std::istream &in, size_t *ibltsize, u64 *seed)
{
	std::string ibltstr;

	// If they choose not to IBLT encode.
	if (in.peek() != 'i')
		return NULL;

	std::getline(in, ibltstr, ':');
	if (ibltstr != "iblt")
		throw std::runtime_error("Bad iblt line");

	std::string iblthex;
	std::getline(in, iblthex);
	if (!in)
		throw std::runtime_error("Bad iblt hex line");

	*ibltsize = hex_data_size(iblthex.size());
	u8 data[*ibltsize];
	if (!hex_decode(iblthex.c_str(), iblthex.size(), data, sizeof(data)))
		throw std::runtime_error("Bad iblt hex");

	const u8 *p = data;
	size_t len = sizeof(data);
	u64 size = pull_varint(&p, &len);
	if (!p)
		throw std::runtime_error("Bad iblt size");

	if (len < 16)
		throw std::runtime_error("Bad iblt seed");
	memcpy(seed, p, sizeof(*seed));
	p += 16;
	len -= 16;

	raw_iblt *riblt = new raw_iblt(size);
	if (!riblt->read(p, len))
		throw std::runtime_error("Bad iblt");
	
	return riblt;
}

static bool recover_block(const raw_iblt &theirs,
						  const raw_iblt &ours,
						  u64 seed,
						  const txmap &mempool,
						  txmap block)
{
	// Difference iblt.
	iblt diff(theirs, ours);

	// Create ids from my mempool, using their seed.
	ibltpool pool(seed, mempool);

	iblt::bucket_type t;
	txslice s;

	// For each txid48, we keep all the slices.
	std::set<txslice> slices;

	// While there are still singleton buckets...
	while ((t = diff.next(s)) != iblt::NEITHER) {
		if (t == iblt::OURS) {
			auto it = pool.tx_by_txid48.find(s.get_txid48());
			// If we can't find it, we're corrupt.
			if (it == pool.tx_by_txid48.end()) {
				return false;
			} else {
				// Remove entire tx.
				diff.remove_our_tx(*it->second->btx, s.get_txid48());
				// Make sure we make progress: remove it from consideration.
				if (!pool.tx_by_txid48.erase(s.get_txid48()))
					return false;
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
	std::vector<bitcoin_tx> recovered;

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
			bitcoin_tx tx((varint_t)0, (varint_t)0);

			if (!rebuild_tx(transaction, tx))
				return false;

			recovered.push_back(tx);
			count = 0;
		}
	}

	// Some left over?
	if (count != 0) {
		return false;
	}

	// The block contents should be equal to recovered + tx_by_txid48.
	for (const auto &tx: recovered) {
		if (!block.erase(tx.txid()))
			return false;
	}
	for (const auto &pair: pool.tx_by_txid48) {
		if (!block.erase(pair.second->txid))
			return false;
	}
	return block.empty();
}

int main(int argc, char *argv[])
{
	std::istream &in = input_file(argv[1]);

	unsigned int blocknum, overhead;
	txmap block;

	while (read_blockline(in, &blocknum, &overhead, &block, NULL)) {
		u64 seed;
		size_t ibltsize;
		raw_iblt *theirs = read_iblt(in, &ibltsize, &seed);

		txmap mempool;
		std::string peername;
		while (read_mempool(in, &peername, &mempool, NULL)) {
			if (!theirs) {
				std::cout << blocknum << "," << overhead << ",0,"
						  << peername << ","
						  << true
						  << std::endl;
			} else {
				// Create our equivalent iblt.
				raw_iblt ours(theirs->size(), seed, mempool);

				std::cout << blocknum << "," << overhead << "," << ibltsize
						  << "," << peername << ","
						  << recover_block(*theirs, ours, seed, mempool, block)
						  << std::endl;
			}
		}
	}
}
