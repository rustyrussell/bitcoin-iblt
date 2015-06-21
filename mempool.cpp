#include "mempool.h"
#include <stdexcept>

tx *mempool::find(const bitcoin_txid &id)
{
    auto pos = tx_by_txid.find(id);
    if (pos != tx_by_txid.end()) {
        return pos->second;
    } else {
        return NULL;
    }
}
