#ifndef TXCACHE_H
#define TXCACHE_H
#include "tx.h"

tx *get_tx(const bitcoin_txid &txid, bool must_exist = true);
#endif /* TXCACHE_H */
