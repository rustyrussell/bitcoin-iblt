// This code takes blocks and mempools and uses an IBLT to encode them,
// producing iblt:<hex> lines in the output.

extern "C" {
#include <ccan/err/err.h>
#include <ccan/str/hex/hex.h>
}
#include "txtree.h"
#include "ibltpool.h"
#include "io.h"
#include "rawiblt.h"
#include "txslice.h"
#include <stdexcept>
#include <cassert>
#include <algorithm>

static size_t dynamic_buckets(const txmap &block, const txmap &mempool)
{
	// Start with enough slices to decode a median 300-byte tx.
	size_t slices = txslice::num_slices_for(300);

	// Now add in each tx we didn't know about.
	for (const auto &pair: block) {
		if (mempool.find(pair.first) != mempool.end())
			continue;
		slices += txslice::num_slices_for(pair.second->btx->length());
	}

	// FIXME: Figure out the number of buckets we need to give 95% chance that
	// at least one has only a single element in it.

	// Meanwhile, assume 2x overhead.
	return slices * 2;
}

static size_t total_size(const txmap &block)
{
	size_t sum = 0;

	for (const auto &pair: block) {
		sum += pair.second->btx->length();
	}
	return sum;
}

int main(int argc, char *argv[])
{
	u64 seed = 1;
	size_t fixed_buckets = 0;

	while (strncmp(argv[1], "--", 2) == 0) {
		char *endp;
		if (strncmp(argv[1], "--seed=", strlen("--seed=")) == 0) {
			seed = strtoul(argv[1] + strlen("--seed="), &endp, 10);
			if (*endp || !seed)
				errx(1, "Invalid --seed");
		} else if (strncmp(argv[1], "--buckets=", strlen("--buckets=")) == 0) {
			fixed_buckets = strtoul(argv[1] + strlen("--buckets="), &endp, 10);
			if (*endp || !fixed_buckets)
				errx(1, "Invalid --buckets");
		} else
			errx(1, "Unknown argument %s", argv[1]);
		argc--;
		argv++;
	}

	if (argc != 2)
			errx(1, "Usage: %s [--seed=<seed>][--buckets=buckets]", argv[0]);
	std::istream &in = input_file(argv[1]);

	unsigned int blocknum, overhead;
	txmap block;
	std::unordered_set<bitcoin_txid> unknowns;

	while (read_blockline(in, &blocknum, &overhead, &block, &unknowns)) {
		size_t buckets;

		std::string peername;
		txmap mempool;
		if (!read_mempool(in, &peername, &mempool, &unknowns))
			errx(1, "Failed reading first mempool line");

		if (fixed_buckets) {
			buckets = fixed_buckets;
		} else {
			buckets = dynamic_buckets(block, mempool);
		}

		raw_iblt riblt(buckets, seed, block);

		std::vector<u8> encoded;
		add_varint(buckets, add_linearize, &encoded);
		// Seed will be 128 bits (FIXME: endian!)
		u8 seedstr[16] = { 0 };
		memcpy(seedstr, &seed, sizeof(seed));
		encoded.insert(encoded.end(), seedstr, seedstr + sizeof(seedstr));
		std::vector<u8> riblt_encoded = riblt.write();
		encoded.insert(encoded.end(), riblt_encoded.begin(), riblt_encoded.end());

		// Don't encode if it'll be larger than block itself!
		size_t blocksz = total_size(block);
		if (encoded.size() >= blocksz) {
			overhead += blocksz;
			write_blockline(std::cout, blocknum, overhead, block);
		} else {
			overhead += encoded.size();

			write_blockline(std::cout, blocknum, overhead, block);
			char hexstr[hex_str_size(encoded.size())];
			hex_encode(encoded.data(), encoded.size(), hexstr, sizeof(hexstr));
			std::cout << "iblt:" << hexstr << std::endl;
		}

		while (read_mempool(in, &peername, &mempool, &unknowns)) {
			write_mempool(std::cout, peername, mempool);
		}
	}
}
		
			
			
