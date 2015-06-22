#ifndef IBLTPOOL_H
#define IBLTPOOL_H
#include "mempool.h"
#include "wire_encode.h"

class ibltpool {
private:
    // Seed we're using for tx48ids.
    u64 seed;

    // Add to them all.
    void add(const txid48 &id48, const tx *t);
    
public:
    // For building it when generating actual block.
    // FIXME: Handle clashes!
    ibltpool(u64 seed, const mempool &mp);

    ~ibltpool();

    /* For encoding: get the bitid for this tx */
    std::vector<bool> get_unique_bitid(const txid48 &id48);

    /* For decoding: get the txs (if any) matching this bitid. */
    std::vector<const tx *> get_txs(const std::vector<bool> &vec);

    // We need txs sorted by satoshi per byte.
    // FIXME: Actually, we only need a set of below and a set of above.
    std::multimap<u64, const tx *> tx_by_value;

    // We also need it in a binary tree of txid48, for encoding additions.
    class tx_node *tree;

    // And a map of txid48s -> txs.
    std::unordered_map<txid48, const tx *> tx_by_txid48;
};
#endif // IBLTPOOL_H
