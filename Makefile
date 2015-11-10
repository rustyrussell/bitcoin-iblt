CCANDIR := ccan
CFLAGS := -Wall -I$(CCANDIR) -g #-O3 -flto
IBLT_SIZE := 64
CXXFLAGS := $(CFLAGS) -I../bitcoin-corpus -std=c++11 -DIBLT_SIZE=$(IBLT_SIZE) #-D_GLIBCXX_DEBUG
OBJS := iblt-test-$(IBLT_SIZE).o iblt-$(IBLT_SIZE).o mempool-$(IBLT_SIZE).o sha256_double.o bitcoin_tx.o txslice-$(IBLT_SIZE).o murmur.o wire_encode.o ibltpool.o rawiblt-$(IBLT_SIZE).o txcache.o
HEADERS := iblt.h mempool.h sha256_double.h txid48.h bitcoin_tx.h txslice.h murmur.h wire_encode.h txcache.h

CCAN_OBJS := ccan-crypto-sha256.o ccan-err.o ccan-tal.o ccan-tal-str.o ccan-take.o ccan-list.o ccan-str.o ccan-opt-helpers.o ccan-opt.o ccan-opt-parse.o ccan-opt-usage.o ccan-read_write_all.o ccan-str-hex.o ccan-tal-grab_file.o ccan-noerr.o ccan-rbuf.o ccan-hash.o

default: utils/add-to-txcache utils/monte-carlo iblt-test-$(IBLT_SIZE) iblt-space buckets-for-txs iblt-selection-heuristic iblt-encode iblt-decode

%-$(IBLT_SIZE).o: %.cpp
	$(COMPILE.cpp) $(OUTPUT_OPTION) $<

# Where the uncompressed bitcoin-corpus files are.
CORPUS_DIR := ../bitcoin-corpus
CORPORA := au sf sf-rn sg
OUTPUTS := $(foreach src, $(CORPORA), stats-$(src)-$(IBLT_SIZE).output)

stats: slicesrecovered-$(IBLT_SIZE).stats txsdiscarded-$(IBLT_SIZE).stats slicesdiscarded-$(IBLT_SIZE).stats total-bytes-$(IBLT_SIZE)

stats-%-$(IBLT_SIZE).output: iblt-test-$(IBLT_SIZE)
	./iblt-test-$(IBLT_SIZE) $(IBLT_SEED) $(CORPUS_DIR)/$* $(filter-out $(CORPUS_DIR)/$*, $(foreach other,$(CORPORA),$(CORPUS_DIR)/$(other))) > $@

# Output is blocknum,blocksize,knownbytes,unknownbytes,mempoolbytes,addedbitsetsize,removedbitsetsize,A,A-ibltslices,A-slicesrecovered,A-slicesdiscarded,A-txsdiscarded,B,B-ibltslices,B-slicesrecovered,B-slicesdiscarded,B-txsdiscarded,C,C-ibltslices,C-slicesrecovered,C-slicesdiscarded,C-txsdiscarded

# We want ibltslices,slicesrecovered,slicesdiscarded,txsdiscarded for each one.
iblt-data-$(IBLT_SIZE).stats: $(OUTPUTS)
	tail -q -n +2 $^ | cut -d, -f8- | sed -e 's/[0-9]*,\([0-9]*,[0-9]*,[0-9]*,[0-9]*\),/\1\n/g' -e 's/[0-9]*,\([0-9]*,[0-9]*,[0-9]*,[0-9]*\)$$/\1/' > $@

# Non-zero slices recovered where none discarded
slicesrecovered-$(IBLT_SIZE).stats: iblt-data-$(IBLT_SIZE).stats
	(echo "IBLT slices, slices recovered"; grep ",0,0$$" $< | cut -d, -f1,2 | grep -v ',0$$') > $@

# Non-zero txs discarded where none recovered.
txsdiscarded-$(IBLT_SIZE).stats: iblt-data-$(IBLT_SIZE).stats
	(echo "IBLT slices, txs discarded"; grep "^[0-9]*,0," $< | cut -d, -f1,4 | grep -v ',0$$') > $@

# Non-zero slices discarded where none recovered.
slicesdiscarded-$(IBLT_SIZE).stats: iblt-data-$(IBLT_SIZE).stats
	(echo "IBLT slices, slices discarded"; grep "^[0-9]*,0," $< | cut -d, -f1,3 | grep -v ',0$$') > $@

# Sum total bytes for each one.
total-bytes-$(IBLT_SIZE): $(OUTPUTS)
	tail -q -n +2 $^ | cut -d, -f8,13,18 | tr ',' '\012' | awk '{ SUM += $$1 } END { print SUM }' > $@

iblt-test-$(IBLT_SIZE): $(OBJS) $(CCAN_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

iblt-space: iblt-space.o iblt-$(IBLT_SIZE).o sha256_double.o bitcoin_tx.o txslice-$(IBLT_SIZE).o murmur.o wire_encode.o rawiblt-$(IBLT_SIZE).o $(CCAN_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

iblt-encode: iblt-encode.o wire_encode.o sha256_double.o rawiblt-$(IBLT_SIZE).o bitcoin_tx.o io.o murmur.o txslice-$(IBLT_SIZE).o txcache.o $(CCAN_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

iblt-decode: iblt-decode.o wire_encode.o sha256_double.o rawiblt-$(IBLT_SIZE).o bitcoin_tx.o io.o murmur.o txslice-$(IBLT_SIZE).o iblt.o ibltpool.o txcache.o $(CCAN_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

iblt-selection-heuristic: iblt-selection-heuristic.o sha256_double.o bitcoin_tx.o txcache.o murmur.o ibltpool.o wire_encode.o io.o $(CCAN_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

iblt-selection-heuristic.o: iblt-selection-heuristic.cpp
	$(COMPILE.cpp) $(OUTPUT_OPTION) $<


buckets-for-txs: buckets-for-txs.o $(CCAN_OBJS)
utils/add-to-txcache: utils/add-to-txcache.o $(CCAN_OBJS)
utils/monte-carlo: utils/monte-carlo.o $(CCAN_OBJS)

clean:
	$(RM) $(OBJS) $(CCAN_OBJS)
	$(RM) *.o
	$(RM) utils/add-to-txcache iblt-test-$(IBLT_SIZE) iblt-space buckets-for-txs iblt-selection-heutistic

distclean: clean
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
ccan-hash.o: $(CCANDIR)/ccan/hash/hash.c
	$(CC) $(CFLAGS) -c -o $@ $<
