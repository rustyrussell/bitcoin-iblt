#include "mempool.h"
#include <stdexcept>

const tx *mempool::find(const bitcoin_txid &id)
{
    auto pos = tx_by_txid.find(id);
    if (pos != tx_by_txid.end()) {
        return pos->second;
    } else {
        return NULL;
    }
}

// Inefficient, but just for stats printing
size_t mempool::length() const
{
    size_t len = 0;
    for (const auto &i : tx_by_txid) {
        len += i.second->btx->length();
    }
    return len;
}
