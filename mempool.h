#ifndef MEMPOOL_H
#define MEMPOOL_H
// What we need from a mempool.
#include "bitcoin_tx.h"
#include "txid48.h"
#include "tx.h"
#include <vector>
#include <map>
#include <unordered_map>

// FIXME: Leaky hack
class mempool {
public:
    // A map of txids -> txs.
    std::unordered_map<bitcoin_txid, const tx *> tx_by_txid;

    mempool() { }
    ~mempool() { }
    void add(const tx *t) {
        tx_by_txid.insert(std::make_pair(t->txid, t));
    }
    bool del(const bitcoin_txid &txid) {
        return tx_by_txid.erase(txid) != 0;
    }
    size_t size() const { return tx_by_txid.size(); }
    size_t length() const;
    
    // Membership check.
    const tx *find(const bitcoin_txid &id);
};
#endif // MEMPOOL_H
