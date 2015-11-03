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
#include <stdexcept>
#include <cassert>
#include <algorithm>

int main(int argc, char *argv[])
{
	u64 seed = 1;

	while (strncmp(argv[1], "--", 2) == 0) {
		char *endp;
		if (strncmp(argv[1], "--seed=", strlen("--seed=")) == 0) {
			seed = strtoul(argv[1] + strlen("--seed="), &endp, 10);
			if (*endp || !seed)
				errx(1, "Invalid --seed");
		} else
			errx(1, "Unknown argument %s", argv[1]);
		argc--;
		argv++;
	}

	if (argc > 3 || argc == 1)
			errx(1, "Usage: %s [--seed=<seed>] <iblt-buckets>", argv[1]);
	size_t buckets = strtoul(argv[1], NULL, 10);
	std::istream &in = input_file(argv[2]);

	unsigned int blocknum, overhead;
	txmap block;

	while (read_blockline(in, &blocknum, &overhead, &block)) {
		raw_iblt riblt(buckets, seed, block);

		std::vector<u8> encoded;
		add_varint(buckets, add_linearize, &encoded);
		// Seed will be 128 bits (FIXME: endian!)
		u8 seedstr[16] = { 0 };
		memcpy(seedstr, &seed, sizeof(seed));
		encoded.insert(encoded.end(), seedstr, seedstr + sizeof(seedstr));
		std::vector<u8> riblt_encoded = riblt.write();
		encoded.insert(encoded.end(), riblt_encoded.begin(), riblt_encoded.end());
		overhead += encoded.size();

		write_blockline(std::cout, blocknum, overhead, block);
		char hexstr[hex_str_size(encoded.size())];
		hex_encode(encoded.data(), encoded.size(), hexstr, sizeof(hexstr));
		std::cout << "iblt:" << hexstr << std::endl;

		txmap mempool;
		std::string peername;
		while (read_mempool(in, &peername, &mempool)) {
			write_mempool(std::cout, peername, mempool);
		}
	}
}
		
			
			
