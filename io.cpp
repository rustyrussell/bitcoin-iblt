#include "io.h"
#include <fstream>
#include "txcache.h"
extern "C" {
#include <ccan/str/hex/hex.h>
#include <ccan/err/err.h>
};

// Input and output are of forms:
// <FILE> := <BLOCKDESC>*
// For each block, in incrementing order:
// <BLOCKDESC> := <BLOCK-LINE><MEMPOOL-LINE>+
// <BLOCK-LINE> := block,<BLOCKHEIGHT>,<OVERHEAD-BYTES>[,<TXID>]*
// <BLOCKHEIGHT> := integer
// <OVERHEAD-BYTES> := integer
// <TXID> := hex // TXID
// For each peer, after each <BLOCK-LINE>:
// <MEMPOOL-LINE> := mempool,<PEERNAME>[,<TXID>]*

std::istream &input_file(const char *argv)
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
	case ',':
		in >> txid;
		if (!in)
			throw std::runtime_error("Expected txid");
		return true;
	default:
		throw std::runtime_error("Expected ,");
	}
}

static txmap read_txids(std::istream &in,
						std::unordered_set<bitcoin_txid> *unknown)
{
	txmap map;
	bitcoin_txid txid;

	while (get_txid(in, txid)) {
		tx *t = get_tx(txid, false);
		if (!t) {
			if (!unknown || unknown->insert(txid).second) {
				char hexstr[hex_str_size(sizeof(txid))];
				hex_encode(&txid.shad, sizeof(txid.shad), hexstr, sizeof(hexstr));
				warnx("could not find tx %s", hexstr);
			}
			continue;
		}
		map.insert(std::make_pair(txid, t));
	}
	return map;
}

bool read_blockline(std::istream &in,
					unsigned int *blocknum, unsigned int *overhead,
					txmap *block,
					std::unordered_set<bitcoin_txid> *unknown)
{
	if (in.peek() != 'b')
		return false;

	std::string blockstr;
	std::getline(in, blockstr, ',');
	if (blockstr != "block")
		throw std::runtime_error("Bad block line");

	in >> *blocknum;
	if (!in)
		throw std::runtime_error("Bad blocknum");

	if (in.get() != ',')
		throw std::runtime_error("Bad blocknum or ,");
	in >> *overhead;
	*block = read_txids(in, unknown);
	return true;
}
	
bool read_mempool(std::istream &in,
				  std::string *peername, txmap *mempool,
				  std::unordered_set<bitcoin_txid> *unknown)
{
	if (in.peek() != 'm')
		return false;

	std::string mempoolstr;
	std::getline(in, mempoolstr, ',');
	if (mempoolstr != "mempool")
		throw std::runtime_error("Bad mempool line");

	peername->clear();
	while (in.peek() != ',' && in.peek() != '\n') {
		if (!in)
			throw std::runtime_error("Bad peername");
		*peername += in.get();
	}

	*mempool = read_txids(in, unknown);
	return true;
}

void write_blockline(std::ostream &out,
					 unsigned int blocknum, unsigned int overhead,
					 const txmap &block)
{
	out << "block," << blocknum << "," << overhead;
	for (const auto &pair: block)
		out << "," << pair.first;
	out << std::endl;
}
	
void write_mempool(std::ostream &out,
				   const std::string &peername, const txmap &mempool)
{
	out << "mempool," << peername;
	for (const auto &tx: mempool) {
		out << "," << tx.first;
	}
	out << std::endl;
}
