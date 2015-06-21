#ifndef TX_H
#define TX_H
#include "bitcoin_tx.h"

// We keep each tx by contents and fee.
struct tx {
    // FIXME: Just record fee/byte?
    u64 fee;
    struct bitcoin_txid txid;
    const bitcoin_tx *btx;

    tx(u64 bfee, const bitcoin_tx *txin)
        : fee(bfee), txid(txin->txid()), btx(txin) { }

    // Fee is actually capped at 2,100,000,000,000,000 satoshi.
    // 2^51 == 2,251,799,813,685,248, so we have 13 bits remaining.
    u64 satoshi_per_byte() const {
        return fee << 13 / btx->length();
    }
};
#endif // TX_H
