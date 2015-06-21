// 48-bit TXID, using a random seed to avoid hash bombing
#ifndef TXID48_H
#define TXID48_H
extern "C" {
#include <ccan/short_types/short_types.h>
#include <ccan/endian/endian.h>
#include <ccan/crypto/sha256/sha256.h>
};

#include <cassert>
#include <cstring>
#include "bitcoin_tx.h"

class txid48 {
public:
    // We only use 48 bits: small but combines nicely with 16-bit id
    static const unsigned int BITS = 48;

private:
    le64 id;

public:
    // Txid48 == SHA256(txid || seed)
    txid48(u64 seed, const bitcoin_txid &txid) : id(0)
    {
        sha256_ctx ctx;
        struct sha256 h;
        le64 lseed = cpu_to_le64(seed);

        assert(seed);
        sha256_init(&ctx);
        sha256_update(&ctx, &txid.shad.sha.u, sizeof(txid.shad.sha.u));
        sha256_update(&ctx, &lseed, sizeof(lseed));
        sha256_done(&ctx, &h);
        memcpy(&id, h.u.u8, BITS / 8);
    }

    txid48(u64 seed, const bitcoin_tx &tx)
    {
        *this = txid48(seed, tx.txid());
    }

    explicit txid48(u64 txid)
    {
        id = cpu_to_le64(txid);
    }

    friend bool operator ==(const txid48 &lhs, const txid48 &rhs) { return lhs.id == rhs.id; }

    // Get the actual id.
    u64 get_id() const { return le64_to_cpu(id); }

    // Does it match this bitvector?
    bool matches(const std::vector<bool> &bitvec) const {
        for (size_t i = 0; i < bitvec.size(); i++) {
            if (((get_id() >> i) & 1) != bitvec[i]) {
                return false;
            }
        }
        return true;
    }

    // Offset counter by hash of id, helps us deprioritize false-positives.
    u16 frag_base() const
    {
        struct sha256 h;
        sha256(&h, &id, BITS / 8);
        return ((u16)h.u.u8[1]) << 8 | h.u.u8[0];
    }
};

// To place them in unordered_set
namespace std {
	template <>
    struct hash<txid48> {
		std::size_t operator() (const txid48 &id) const {
            u64 n = id.get_id();
			// This only gives 32 bits, but ok for testing
			return MurmurHash3(0, (const u8 *)&n, sizeof(n));
		}
	};
};
#endif // TXID48_H
