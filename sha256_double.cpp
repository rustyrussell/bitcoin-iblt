#include "sha256_double.h"

sha256_double::sha256_double(const void *p, size_t len)
{
	sha256(&sha, p, len);
	sha256(&sha, &sha, sizeof(sha));
}

sha256_double::sha256_double(sha256_ctx &shactx)
{
	sha256_done(&shactx, &sha);
	sha256(&sha, &sha, sizeof(sha));
}
