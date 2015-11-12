/* Figure out how many IBLT buckets we need to be able to extract M elements
 * 95% of the time.
 */
#include <stdio.h>
#include <ccan/hash/hash.h>
#include <ccan/err/err.h>
#include <ccan/read_write_all/read_write_all.h>
#include <ccan/short_types/short_types.h>
#include <ccan/str/str.h>
#include <ccan/opt/opt.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

/* See http://arxiv.org/pdf/1101.2245.pdf para 2.2:
   ... hashes yield distinct locations ..
*/
static bool use_subtables = false;

static unsigned int hash_pos(unsigned val, unsigned buckets,
			     unsigned round, unsigned iter)
{
	unsigned subsize = buckets / 3;

	/* Ignore subsize for pathological buckets < 3 case */
	if (!use_subtables || subsize == 0)
		return hash_u32(&val, 1, iter+round) % buckets;

	/* Apply any integer rounding to last subtable. */
	switch (iter) {
	case 0:
		return hash_u32(&val, 1, iter+round) % subsize;
	case 1:
		return subsize + hash_u32(&val, 1, iter+round) % subsize;
	case 2: {
		unsigned int remainder = buckets - subsize * 2;
		return subsize * 2 + hash_u32(&val, 1, iter+round) % remainder;
	}
	}
	/* We only have 3 rounds */
	abort();
}
		
static void add_element(unsigned *arr, unsigned *counts, unsigned buckets,
			unsigned round, unsigned val)
{
	size_t i;

	/* We add each one in three places. */
	for (i = 0; i < 3; i++) {
		unsigned int pos = hash_pos(val, buckets, round, i);
		counts[pos]++;
		arr[pos] ^= val;
	}
}

static void remove_element(unsigned *arr, unsigned *counts, unsigned buckets,
			   unsigned round, unsigned val)
{
	size_t i;

	/* We add each one in three places. */
	for (i = 0; i < 3; i++) {
		unsigned int pos = hash_pos(val, buckets, round, i);
		assert(counts[pos]);
		counts[pos]--;
		arr[pos] ^= val;
	}
}


static int find_singleton(const unsigned *counts, unsigned buckets, int prev)
{
	size_t i;

	for (i = prev; i < buckets; i++) {
		if (counts[i] == 1)
			return i;
	}
	for (i = 0; i < prev; i++) {
		if (counts[i] == 1)
			return i;
	}
	return -1;
}

static bool add_and_extract(unsigned *arr, unsigned *counts, unsigned buckets,
			    unsigned elements, unsigned round)
{
	int i, bucket = 0;

	memset(arr, 0, sizeof(*arr) * buckets);
	memset(counts, 0, sizeof(*counts) * buckets);

	for (i = 0; i < elements; i++)
		add_element(arr, counts, buckets, round, i);

	for (i = 0; i < elements; i++) {
		bucket = find_singleton(counts, buckets, bucket);
		if (bucket < 0)
			return false;
		assert(bucket < buckets);
		remove_element(arr, counts, buckets, round, arr[bucket]);
	}
	return true;
}

#define NUM_RUNS 1000000

int main(int argc, char *argv[])
{
	unsigned elements, *arr, *counts;
	bool verbose = false, found = false, naive = false;
	unsigned min, max;

	err_set_progname(argv[0]);
	opt_register_noarg("-v|--verbose", opt_set_bool, &verbose,
			   "Print details on each step");
	opt_register_noarg("-n|--naive", opt_set_bool, &naive,
			   "Don't use binary search, but simple increment");
	opt_register_noarg("--sub-tables", opt_set_bool, &use_subtables,
			   "Use subtables to ensure distinct hashes");
	opt_parse(&argc, argv, opt_log_stderr_exit);
	
	if (argc != 2 || (elements = atoi(argv[1])) == 0)
		opt_usage_exit_fail("<num-elements> must be positive");
	
	/* Solution will be in this range. */
	min = elements;
	if (elements < 20)
		max = 6 * elements;
	else if (elements < 50)
		max = 3 * elements;
	else
		max = 2 * elements;
	arr = calloc(max, sizeof(arr[0]));
	counts = calloc(max, sizeof(counts[0]));

	while (min != max) {
		unsigned buckets;
		unsigned i, successes = 0;

		if (naive)
			buckets = min;
		else
			buckets = (min + max) / 2;

		for (i = 0; i < NUM_RUNS; i++)
			successes += add_and_extract(arr, counts, buckets,
						     elements, i);
		if (verbose)
			printf("%u: %5g%%\n",
			       buckets, successes * 100.0 / NUM_RUNS);
		if (naive) {
			if (successes >= NUM_RUNS * 95 / 100) {
				found = true;
				break;
			}
			min++;
		} else {
			if (successes >= NUM_RUNS * 95 / 100) {
				max = buckets;
				found = true;
			} else {
				min = buckets + 1;
			}
		}
	}
	if (!found)
		errx(1, "Could not find solution");
	printf("%u\n", min);
	exit(0);
}
