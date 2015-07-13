CCANDIR := ccan
CFLAGS := -Wall -I$(CCANDIR) -g -O3 -flto
IBLT_SIZE := 64
CXXFLAGS := $(CFLAGS) -I../bitcoin-corpus -std=c++11 -DIBLT_SIZE=$(IBLT_SIZE) #-D_GLIBCXX_DEBUG
OBJS := iblt-test.o iblt.o mempool.o sha256_double.o bitcoin_tx.o txslice.o murmur.o wire_encode.o ibltpool.o txslice.o rawiblt.o
HEADERS := iblt.h mempool.h sha256_double.h txid48.h bitcoin_tx.h txslice.h murmur.h wire_encode.h

CCAN_OBJS := ccan-crypto-sha256.o ccan-err.o ccan-tal.o ccan-tal-str.o ccan-take.o ccan-list.o ccan-str.o ccan-opt-helpers.o ccan-opt.o ccan-opt-parse.o ccan-opt-usage.o ccan-read_write_all.o ccan-str-hex.o ccan-tal-grab_file.o ccan-noerr.o ccan-rbuf.o

default: utils/add-to-txcache iblt-test

# Where the uncompressed bitcoin-corpus files are.
CORPUS_DIR := ../bitcoin-corpus
CORPORA := au sf sf-rn sg
OUTPUTS := $(foreach src, $(CORPORA), $(filter-out stats-$(src)_$(src)-$(IBLT_SIZE).output, $(foreach dst, $(CORPORA), stats-$(src)_$(dst)-$(IBLT_SIZE).output)))

stats: slicesrecovered-$(IBLT_SIZE).stats txsdiscarded-$(IBLT_SIZE).stats slicesdiscarded-$(IBLT_SIZE).stats total-bytes-$(IBLT_SIZE)

stats-%-$(IBLT_SIZE).output: iblt-test
	./iblt-test $(CORPUS_DIR)/`echo $* | cut -d_ -f1` $(CORPUS_DIR)/`echo $* | cut -d_ -f2` > $@

# Output is ....,ibltslices,slicesrecovered,slicesdiscarded,txsdiscarded
# From this, we can see how many how many slices recovered/discarded and txs discarded

slicesrecovered-$(IBLT_SIZE).stats: $(OUTPUTS)
	(echo "IBLT slices, slices recovered"; sed -n 's/.*,\([0-9]*\),\([0-9]*\),0,0$$/\1,\2/p' $^ | grep -v ',0$$') > $@

txsdiscarded-$(IBLT_SIZE).stats: $(OUTPUTS)
	(echo "IBLT slices, txs discarded"; sed -n 's/.*,\([0-9]*\),0,[0-9]*,\([0-9]*\)$$/\1,\2/p' $^ | grep -v ',0$$') > $@

slicesdiscarded-$(IBLT_SIZE).stats: $(OUTPUTS)
	(echo "IBLT slices, slices discarded"; sed -n 's/.*,\([0-9]*\),0,\([0-9]*\),[0-9]*$$/\1,\2/p' $^ | grep -v ',0$$') > $@

total-bytes-$(IBLT_SIZE): $(OUTPUTS)
	cut -d, -f8 $^ | awk '{ SUM += $$1 } END { print SUM }' > $@

iblt-test: $(OBJS) $(CCAN_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

utils/add-to-txcache: utils/add-to-txcache.o $(CCAN_OBJS)

clean:
	$(RM) $(OBJS) $(CCAN_OBJS)

distcleaen: clean
	$(RM) slicesrecovered-*.stats txsdiscarded-*.stats slicesdiscarded-*.stats total-bytes-*

ccan-tal.o: $(CCANDIR)/ccan/tal/tal.c
	$(CC) $(CFLAGS) -c -o $@ $<
ccan-tal-str.o: $(CCANDIR)/ccan/tal/str/str.c
	$(CC) $(CFLAGS) -c -o $@ $<
ccan-tal-grab_file.o: $(CCANDIR)/ccan/tal/grab_file/grab_file.c
	$(CC) $(CFLAGS) -c -o $@ $<
ccan-take.o: $(CCANDIR)/ccan/take/take.c
	$(CC) $(CFLAGS) -c -o $@ $<
ccan-list.o: $(CCANDIR)/ccan/list/list.c
	$(CC) $(CFLAGS) -c -o $@ $<
ccan-read_write_all.o: $(CCANDIR)/ccan/read_write_all/read_write_all.c
	$(CC) $(CFLAGS) -c -o $@ $<
ccan-str.o: $(CCANDIR)/ccan/str/str.c
	$(CC) $(CFLAGS) -c -o $@ $<
ccan-opt.o: $(CCANDIR)/ccan/opt/opt.c
	$(CC) $(CFLAGS) -c -o $@ $<
ccan-opt-helpers.o: $(CCANDIR)/ccan/opt/helpers.c
	$(CC) $(CFLAGS) -c -o $@ $<
ccan-opt-parse.o: $(CCANDIR)/ccan/opt/parse.c
	$(CC) $(CFLAGS) -c -o $@ $<
ccan-opt-usage.o: $(CCANDIR)/ccan/opt/usage.c
	$(CC) $(CFLAGS) -c -o $@ $<
ccan-err.o: $(CCANDIR)/ccan/err/err.c
	$(CC) $(CFLAGS) -c -o $@ $<
ccan-noerr.o: $(CCANDIR)/ccan/noerr/noerr.c
	$(CC) $(CFLAGS) -c -o $@ $<
ccan-str-hex.o: $(CCANDIR)/ccan/str/hex/hex.c
	$(CC) $(CFLAGS) -c -o $@ $<
ccan-crypto-sha256.o: $(CCANDIR)/ccan/crypto/sha256/sha256.c
	$(CC) $(CFLAGS) -c -o $@ $<
ccan-rbuf.o: $(CCANDIR)/ccan/rbuf/rbuf.c
	$(CC) $(CFLAGS) -c -o $@ $<
