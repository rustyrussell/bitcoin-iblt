#include "bitcoin_tx.h"
extern "C" {
#include <ccan/endian/endian.h>
#include <ccan/err/err.h>
#include <ccan/str/hex/hex.h>
#include <ccan/str/str.h>
#include <ccan/tal/grab_file/grab_file.h>
#include <ccan/tal/tal.h>
};
#include <cassert>
#include <cstring>
#include <stdexcept>
#include <algorithm>

void add_varint(varint_t v,
				void (*add)(const void *, size_t, void *), void *addp)
{
	u8 buf[9], *p = buf;

	if (v < 0xfd) {
		*(p++) = v;
	} else if (v <= 0xffff) {
		(*p++) = 0xfd;
		(*p++) = v;
		(*p++) = v >> 8;
	} else if (v <= 0xffffffff) {
		(*p++) = 0xfe;
		(*p++) = v;
		(*p++) = v >> 8;
		(*p++) = v >> 16;
		(*p++) = v >> 24;
	} else {
		(*p++) = 0xff;
		(*p++) = v;
		(*p++) = v >> 8;
		(*p++) = v >> 16;
		(*p++) = v >> 24;
		(*p++) = v >> 32;
		(*p++) = v >> 40;
		(*p++) = v >> 48;
		(*p++) = v >> 56;
	}
	add(buf, p - buf, addp);
}

static void add_le32(u32 v,
		     void (*add)(const void *, size_t, void *), void *addp)
{
	le32 l = cpu_to_le32(v);
	add(&l, sizeof(l), addp);
}

static void add_le64(u64 v,
		     void (*add)(const void *, size_t, void *), void *addp)
{
	le64 l = cpu_to_le64(v);
	add(&l, sizeof(l), addp);
}

static void add_tx_input(const struct bitcoin_tx_input *input,
			 void (*add)(const void *, size_t, void *), void *addp)
{
	add(&input->txid, sizeof(input->txid), addp);
	add_le32(input->index, add, addp);
	add_varint(input->script_length, add, addp);
	add(input->script, input->script_length, addp);
	add_le32(input->sequence_number, add, addp);
}

static void add_tx_output(const struct bitcoin_tx_output *output,
			  void (*add)(const void *, size_t, void *), void *addp)
{
	add_le64(output->amount, add, addp);
	add_varint(output->script_length, add, addp);
	add(output->script, output->script_length, addp);
}

void bitcoin_tx::add_tx(void (*add)(const void *, size_t, void *), void *addp) const
{
	varint_t i;

	add_le32(version, add, addp);
	add_varint(input_count, add, addp);
	for (i = 0; i < input_count; i++)
		add_tx_input(&input[i], add, addp);
	add_varint(output_count, add, addp);
	for (i = 0; i < output_count; i++)
		add_tx_output(&output[i], add, addp);
	add_le32(lock_time, add, addp);
}

/* Sets *cursor to NULL and returns NULL when a pull fails. */
static const u8 *pull(const u8 **cursor, size_t *max, void *copy, size_t n)
{
	const u8 *p = *cursor;

	if (*max < n) {
		*cursor = NULL;
		*max = 0;
		/* Just make sure we don't leak uninitialized mem! */
		if (copy)
			memset(copy, 0, n);
		return NULL;
	}
	*cursor += n;
	*max -= n;
	if (copy)
		memcpy(copy, p, n);
	return p;
}

u64 pull_varint(const u8 **cursor, size_t *max)
{
	u64 ret;
	const u8 *p;

	p = pull(cursor, max, NULL, 1);
	if (!p)
		return 0;

	if (*p < 0xfd) {
		ret = *p;
	} else if (*p == 0xfd) {
		p = pull(cursor, max, NULL, 2);
		if (!p)
			return 0;
		ret = ((u64)p[1] << 8) + p[0];
	} else if (*p == 0xfe) {
		p = pull(cursor, max, NULL, 4);
		if (!p)
			return 0;
		ret = ((u64)p[3] << 24) + ((u64)p[2] << 16)
			+ ((u64)p[1] << 8) + p[0];
	} else {
		p = pull(cursor, max, NULL, 8);
		if (!p)
			return 0;
		ret = ((u64)p[7] << 56) + ((u64)p[6] << 48)
			+ ((u64)p[5] << 40) + ((u64)p[4] << 32)
			+ ((u64)p[3] << 24) + ((u64)p[2] << 16)
			+ ((u64)p[1] << 8) + p[0];
	}
	return ret;
}

static u32 pull_le32(const u8 **cursor, size_t *max)
{
	le32 ret;

	if (!pull(cursor, max, &ret, sizeof(ret)))
		return 0;
	return le32_to_cpu(ret);
}

static u64 pull_le64(const u8 **cursor, size_t *max)
{
	le64 ret;

	if (!pull(cursor, max, &ret, sizeof(ret)))
		return 0;
	return le64_to_cpu(ret);
}

static bool pull_sha256_double(const u8 **cursor, size_t *max,
			       struct sha256_double *h)
{
	return pull(cursor, max, h, sizeof(*h));
}

static void pull_input(const u8 **cursor, size_t *max,
					   struct bitcoin_tx_input *input)
{
	pull_sha256_double(cursor, max, &input->txid.shad);
	input->index = pull_le32(cursor, max);
	input->script_length = pull_varint(cursor, max);
	input->script = new u8[input->script_length];
	pull(cursor, max, input->script, input->script_length);
	input->sequence_number = pull_le32(cursor, max);
}

static void pull_output(const u8 **cursor, size_t *max,
						struct bitcoin_tx_output *output)
{
	output->amount = pull_le64(cursor, max);
	output->script_length = pull_varint(cursor, max);
	output->script = new u8[output->script_length];
	pull(cursor, max, output->script, output->script_length);
}

static bool pull_bitcoin_tx(bitcoin_tx *tx,
							const u8 **cursor, size_t *max)
{
	size_t i;

	tx->version = pull_le32(cursor, max);
	tx->input_count = pull_varint(cursor, max);
	tx->input = new bitcoin_tx_input[tx->input_count];
	for (i = 0; i < tx->input_count; i++)
		pull_input(cursor, max, tx->input + i);
	tx->output_count = pull_varint(cursor, max);
	tx->output = new bitcoin_tx_output[tx->output_count];
	for (i = 0; i < tx->output_count; i++)
		pull_output(cursor, max, tx->output + i);
	tx->lock_time = pull_le32(cursor, max);

	/* If we ran short, fail. */
	return (*cursor);
}

static void add_sha(const void *data, size_t len, void *shactx_)
{
	struct sha256_ctx *ctx = (sha256_ctx *)shactx_;
	sha256_update(ctx, data, len);
}

void bitcoin_tx::sha256_update(struct sha256_ctx *ctx) const
{
	add_tx(add_sha, ctx);
}

static void add_linearize(const void *data, size_t len, void *pvec)
{
	std::vector<u8> *vec = (std::vector<u8> *)pvec;

	vec->insert(vec->end(), (u8 *)data, (u8 *)data + len);
}

std::vector<u8> bitcoin_tx::linearize() const
{
	std::vector<u8> arr;
	add_tx(add_linearize, &arr);
	return arr;
}

bitcoin_txid bitcoin_tx::txid() const
{
	struct sha256_ctx ctx = SHA256_INIT;

	sha256_update(&ctx);

	return bitcoin_txid(ctx);
}

bitcoin_tx::bitcoin_tx(varint_t in_count, varint_t out_count)
	: version(1),
	  input_count(in_count), input(new struct bitcoin_tx_input[in_count]),
	  output_count(out_count), output(new struct bitcoin_tx_output[out_count]),
	  lock_time(0xFFFFFFFF)
{
}

bitcoin_tx::bitcoin_tx(const u8 **p, size_t *len) : input(NULL), output(NULL)
{
	if (!pull_bitcoin_tx(this, p, len))
		throw std::runtime_error("bad tx");
}

bitcoin_tx::bitcoin_tx(const char *filename) : input(NULL), output(NULL)
{
	char *hex;

	/* Grabs file, add nul at end. */
	hex = (char *)grab_file(NULL, filename);
	if (!hex)
		err(1, "Opening %s", filename);

	if (strends(hex, "\n"))
		hex[strlen(hex)-1] = '\0';

	size_t len = hex_data_size(strlen(hex));
	std::vector<u8> linear_tx(len);
	if (!hex_decode(hex, strlen(hex), linear_tx.data(), linear_tx.size()))
		errx(1, "Bad hex string in %s", filename);

	const u8 *p = linear_tx.data();
	if (!pull_bitcoin_tx(this, &p, &len) || len != 0)
		errx(1, "Bad transaction in %s", filename);
	tal_free(hex);
}

/* <sigh>.  Bitcoind represents hashes as little-endian for RPC.  This didn't
 * stick for blockids (everyone else uses big-endian, eg. block explorers),
 * but it did stick for txids. */
static void reverse_bytes(u8 *arr, size_t len)
{
	unsigned int i;

	for (i = 0; i < len / 2; i++) {
		unsigned char tmp = arr[i];
		arr[i] = arr[len - 1 - i];
		arr[len - 1 - i] = tmp;
	}
}

bitcoin_txid::bitcoin_txid(const char *hexstr, size_t hexstr_len)
{
	if (!hex_decode(hexstr, hexstr_len, shad.sha.u.u8, sizeof(shad.sha.u.u8)))
		throw std::runtime_error("bad hex");
	reverse_bytes(shad.sha.u.u8, sizeof(shad.sha.u.u8));
}

static void add_length(const void *data, size_t len, void *vplen)
{
	size_t *plen = (size_t *)vplen;

	*plen += len;
}

size_t bitcoin_tx::length() const
{
	size_t len = 0;
	add_tx(add_length, &len);
	return len;
}
