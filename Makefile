CCANDIR := ccan
CFLAGS := -Wall -I$(CCANDIR) -g -O3 -flto $(EXTRAFLAGS)
IBLT_SIZE := 64
CXXFLAGS := $(CFLAGS) -I../bitcoin-corpus -std=c++11 -DIBLT_SIZE=$(IBLT_SIZE) #-D_GLIBCXX_DEBUG
OBJS := iblt-test-$(IBLT_SIZE).o iblt-$(IBLT_SIZE).o mempool-$(IBLT_SIZE).o sha256_double.o bitcoin_tx.o txslice-$(IBLT_SIZE).o murmur.o wire_encode.o ibltpool.o rawiblt-$(IBLT_SIZE).o txcache.o io.o
HEADERS := bitcoin_tx.h iblt.h ibltpool.h io.h mempool.h murmur.h rawiblt.h sha256_double.h txcache.h tx.h txid48.h txslice.h txtree.h wire_encode.h

CCAN_OBJS := ccan-crypto-sha256.o ccan-err.o ccan-tal.o ccan-tal-str.o ccan-take.o ccan-list.o ccan-str.o ccan-opt-helpers.o ccan-opt.o ccan-opt-parse.o ccan-opt-usage.o ccan-read_write_all.o ccan-str-hex.o ccan-tal-grab_file.o ccan-noerr.o ccan-rbuf.o ccan-hash.o

default: utils/add-to-txcache utils/monte-carlo iblt-space buckets-for-txs iblt-selection-heuristic iblt-encode iblt-decode

# Simply make all objs depend on all headers. 
$(OBJS): $(HEADERS)

%-$(IBLT_SIZE).o: %.cpp
	$(COMPILE.cpp) $(OUTPUT_OPTION) $<

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
