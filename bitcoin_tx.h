#ifndef BITCOIN_TX_H
#define BITCOIN_TX_H
extern "C" {
#include <ccan/short_types/short_types.h>
};
#include "sha256_double.h"
#include "murmur.h"
#include <vector>

/* We unpack varints for our in-memory representation */
typedef u64 varint_t;

struct bitcoin_txid {
    struct sha256_double shad;

    bitcoin_txid() { }
    bitcoin_txid(sha256_ctx &ctx) : shad(ctx) { }
    bitcoin_txid(const char *hexstr, size_t hexstr_len);
};

inline bool operator ==(const bitcoin_txid &lhs, const bitcoin_txid &rhs)
{
	return memcmp(lhs.shad.sha.u.u8, rhs.shad.sha.u.u8,
				  sizeof(lhs.shad.sha.u.u8)) == 0;
}

struct bitcoin_tx_output {
    u64 amount;
    varint_t script_length;
    u8 *script;

    bitcoin_tx_output() : amount(0), script_length(0), script(NULL)
        { }
    ~bitcoin_tx_output()
        { delete[] script; }
};

struct bitcoin_tx_input {
    bitcoin_txid txid;
    u32 index; /* output number referred to by above */
    varint_t script_length;
    u8 *script;
    u32 sequence_number;

    bitcoin_tx_input() : script_length(0), script(NULL), sequence_number(0xFFFFFFFF)
        { }
    ~bitcoin_tx_input()
        { delete[] script; }
};

/* Plain old data */
struct bitcoin_tx {
    u32 version;
    varint_t input_count;
    struct bitcoin_tx_input *input;
    varint_t output_count;
    struct bitcoin_tx_output *output;
    u32 lock_time;

    bitcoin_tx(varint_t input_count, varint_t output_count);
    bitcoin_tx(const u8 **p, size_t *len);
    bitcoin_tx(const char *filename);
    bitcoin_txid txid() const;
    void sha256_update(struct sha256_ctx *ctx) const;
    std::vector<u8> linearize() const;
    size_t length() const;

    // Generic accumulate function.
    void add_tx(void (*add)(const void *, size_t, void *), void *addp) const;
};

// To place them in unordered_set
namespace std {
	template <>
    struct hash<bitcoin_txid> {
		std::size_t operator() (const bitcoin_txid &txid) const {
			// This only gives 32 bits, but ok for testing
			return MurmurHash3(0, txid.shad.sha.u.u8,
							   sizeof(txid.shad.sha.u.u8));
		}
	};

#if 0
	template <>
    struct hash<bitcoin_tx> {
		std::size_t operator() (const bitcoin_tx &tx) const {
			// Horribly inefficient
			bitcoin_txid txid = tx.txid();
			return MurmurHash3(0, txid.shad.sha.u.u8,
							   sizeof(txid.shad.sha.u.u8));
		}
	};
#endif
}

void add_varint(varint_t v, void (*add)(const void *, size_t, void *), void *addp);
u64 pull_varint(const u8 **cursor, size_t *max);
#endif /* BITCOIN_TX_H */
