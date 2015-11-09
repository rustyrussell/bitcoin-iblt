#include "txcache.h"
extern "C" {
#include <ccan/err/err.h>
#include <ccan/str/hex/hex.h>
#include <ccan/tal/tal.h>
#include <ccan/tal/grab_file/grab_file.h>
#include <assert.h>
};

tx *get_tx(const bitcoin_txid &txid, bool must_exist)
{
	char filename[sizeof("txcache/01234567890123456789012345678901234567890123456789012345678901234567")] = "txcache/";
	char *txstring;
	const u8 *txbytes;
	size_t len;
	u64 fee;
	tx *t;

	txstring = filename + strlen("txcache/");
	if (!hex_encode(txid.shad.sha.u.u8, sizeof(txid.shad.sha.u.u8),
					txstring,
					sizeof(filename) - strlen("txcache/")))
		throw std::logic_error("txid doesn't fit in filename");

	txbytes = (u8 *)grab_file(NULL, filename);
	if (!txbytes) {
		if (must_exist)
			errx(1, "Could not find tx %s", txstring);
		return NULL;
	}

	len = tal_count(txbytes)-1;
	if (len < 8)
		errx(1, "Truncated %s", filename);
	memcpy(&fee, txbytes, 8);
	txbytes += 8;
	len -= 8;
	t = new tx(fee, new bitcoin_tx(&txbytes, &len));
	assert(t->txid == txid);
	return t;
}
