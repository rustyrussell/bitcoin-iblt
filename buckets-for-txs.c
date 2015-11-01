#include <stdio.h>

#define OVERHEAD 8
#define MAX_TXSIZE 100000

static size_t calc_wastage(const unsigned int *counts, size_t bucket_size)
{
	size_t txsize, wastage = 0;

	for (txsize = 0; txsize < MAX_TXSIZE; txsize++) {
		size_t num_buckets = (txsize + bucket_size-1) / bucket_size;
		size_t per_tx_wastage;

		/* Overhead */
		per_tx_wastage = num_buckets * OVERHEAD;
		/* Space left in last bucket. */
		per_tx_wastage += num_buckets * bucket_size - txsize;

		/* Sum total wastage. */
		wastage += per_tx_wastage * counts[txsize];
	}
	return wastage;
}
		     
int main(int argc, char *argv[])
{
	unsigned int counts[MAX_TXSIZE] = { 0 };
	size_t i, txsize, total = 0, discarded = 0, best_i = 0, best_wastage = -1UL;

	if (argv[1])
		freopen(argv[1], "r", stdin);

	while (scanf("%zu\n", &txsize) == 1) {
		if (txsize >= MAX_TXSIZE)
			discarded++;
		else {
			counts[txsize]++;
			total++;
		}
	}

	fprintf(stderr, "%zu discarded, %zu accepted\n", discarded, total);
	for (i = 8; i < 1000; i++) {
		size_t wastage = calc_wastage(counts, i);
		printf("%zu,%zu\n", i, wastage);
		if (wastage < best_wastage) {
			best_i = i;
			best_wastage = wastage;
		}
	}
	fprintf(stderr, "Best value: %zu (%zu bytes wasted)\n",
		best_i, best_wastage);

	return 0;
}
