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

// Calculated to hit 95% chance of reconstruction: see utils/monte-carlo.c
struct buckets_for_slices {
	unsigned int slices, buckets;
};

static struct buckets_for_slices bfors_table[] = {
	{ 1,5 },
	{ 2,9 },
	{ 3,12 }, /* 4x */
	{ 4,15 },
	{ 5,17 },
	{ 6,20 },
	{ 7,22 },
	{ 8,24 },
	{ 9,27 }, /* 3x */
	{ 10,29 },
	{ 11,31 },
	{ 12,32 },
	{ 13,34 },
	{ 14,36 },
	{ 15,38 },
	{ 16,40 },
	{ 17,41 },
	{ 18,43 },
	{ 19,45 },
	{ 20,46 },
	{ 21,48 },
	{ 22,49 },
	{ 23,51 },
	{ 24,53 },
	{ 25,54 },
	{ 26,56 },
	{ 27,57 },
	{ 28,58 },
	{ 29,60 },
	{ 30,61 }, /* 2x */
	{ 40,75 },
	{ 50,88 },
	{ 60,100 },
	{ 70,112 },
	{ 80,124 },
	{ 90,137 },
	{ 100,149 }, /* 1.5x */
	{ 200,273 },
	{ 300,398 },
	{ 400,524 },
	{ 500,649 },
	{ 600,774 },
	{ 700,898 },
	{ 800,1023 },
	{ 900,1147 },
	{ 1000,1271 },
	{ 2000,2510 },
	{ 3000,3745 },
	{ 4000,4978 },
	{ 5000,6209 },
	{ 6000,7440 },
	{ 7000,8669 },
	{ 8000,9898 },
	{ 9000,11127 },
	{ 10000,12355 }
};

/*
 * Full corpus results for various settings:
 * INITIAL_TXS		EXTRA_FACTOR	Size		Number Correct (of 2112)
 * 1				1.0				13843452	1885
 * 2				1.0	            15564618	1949
 * 3				1.0             16886340    1972
 * 1				1.1             14652786	1912
 * 2				1.1				16516038    1983
 * 3				1.1				17979912    1999
 */

// Base to assume how different their mempool is
#define INITIAL_TXS 1
// Magnification for final result.
#define EXTRA_FACTOR 1.0

static size_t dynamic_buckets(const txmap &block, const txmap &mempool)
{
	// Start with enough slices to decode two 300-byte txs.
	size_t slices = txslice::num_slices_for(300) * INITIAL_TXS;

	// Now add in each tx we didn't know about.
	for (const auto &pair: block) {
		if (mempool.find(pair.first) != mempool.end())
			continue;
		slices += txslice::num_slices_for(pair.second->btx->length());
	}

	// Find previous entry in table, use that factor to give 95% chance
	double factor = 0;
	for (size_t i = 0; i < sizeof(bfors_table)/sizeof(bfors_table[0]); i++) {
		if (bfors_table[i].slices > slices)
			break;
		factor = (double)bfors_table[i].buckets / bfors_table[i].slices;
	}

	return slices * factor * EXTRA_FACTOR;
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
	bool do_iblt = true;

	while (argv[1] && strncmp(argv[1], "--", 2) == 0) {
		char *endp;
		if (strncmp(argv[1], "--seed=", strlen("--seed=")) == 0) {
			seed = strtoul(argv[1] + strlen("--seed="), &endp, 10);
			if (*endp || !seed)
				errx(1, "Invalid --seed");
		} else if (strncmp(argv[1], "--buckets=", strlen("--buckets=")) == 0) {
			fixed_buckets = strtoul(argv[1] + strlen("--buckets="), &endp, 10);
			if (*endp || !fixed_buckets)
				errx(1, "Invalid --buckets");
		} else if (strcmp(argv[1], "--no-iblt") == 0) {
			do_iblt = false;
		} else
			errx(1, "Unknown argument %s", argv[1]);
		argc--;
		argv++;
	}

	if (argc > 2)
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
		if (encoded.size() >= blocksz || !do_iblt) {
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
		
			
			
