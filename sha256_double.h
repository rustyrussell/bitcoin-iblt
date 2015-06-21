#ifndef BITCOIN_IBLT_SHA256_DOUBLE_H
#define BITCOIN_IBLT_SHA256_DOUBLE_H
extern "C" {
#include <ccan/crypto/sha256/sha256.h>
};

struct sha256_double {
	sha256_double() { }
	// Just SHA256(SHA256(p, len))
	sha256_double(const void *p, size_t len);
	// We've added everything, just give us the answer.
	sha256_double(sha256_ctx &sha256);

	// Plain old data.
	struct sha256 sha;
};
#endif /* BITCOIN_IBLT_SHA256_DOUBLE_H */
