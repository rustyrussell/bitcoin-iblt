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
private:
    // A map of txids -> txs.
    std::unordered_map<bitcoin_txid, tx *> tx_by_txid;

public:
    mempool() { }
    ~mempool() { }
    void add(tx *t) {
        tx_by_txid.insert(std::make_pair(t->txid, t));
    }
    bool del(const bitcoin_txid &txid) {
        return tx_by_txid.erase(txid) != 0;
    }
    size_t size() const { return tx_by_txid.size(); }
    size_t length() const;
    
    // Membership check.
    tx *find(const bitcoin_txid &id);

    friend class ibltpool;
};
#endif // MEMPOOL_H
