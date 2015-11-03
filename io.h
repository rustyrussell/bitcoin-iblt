#ifndef IO_H
#define IO_H
#include <unordered_map>
#include <iostream>
#include "tx.h"

typedef std::unordered_map<bitcoin_txid, const tx *> txmap;

std::istream &input_file(const char *argv);
bool read_blockline(std::istream &in,
		    unsigned int *blocknum, unsigned int *overhead,
		    txmap *block);
bool read_mempool(std::istream &in,
		  std::string *peername, txmap *mempool);

void write_blockline(std::ostream &out,
		     unsigned int blocknum, unsigned int overhead,
		     const txmap &block);
void write_mempool(std::ostream &out,
		   const std::string &peername, const txmap &mempool);

#endif /* IO_H */
