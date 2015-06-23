Bitcoin IBLT Test Code
======================

This proposes a wire format for sending bitcoin IBLT data, it then
evaluates the minimum possible bytes to successfully send a block
between nodes, using real node data from the bitcoin-corpus:

	https://github.com/rustyrussell/bitcoin-corpus

Technical Details
-----------------
The IBLT contains buckets as shown in [txslice.h](txslice.h):

	u64 txidbits : 48;
	u16 fragid;
	u8 contents[IBLT_SIZE];

The txid48 is created for a transaction by hashing the txid with a
64-bit per-iblt seed.  This avoids iblt bombing by creating many
similar txids.

The fragid is a counter, starting at hash(txidbits).  This makes
it easier to detect likely-bogus bucket entries.

The transaction is prefixed by the number of buckets (as a varint) and
sliced into the contents fields.

The [wire format](wire_encode.cpp) contains:

1. 64-bit seed.
2. Minimum fee per byte (fixed point at 2^13)
3. IBLT bucket size.
4. The coinbase transaction.
5. A bitset of identifiers of transactions below the minimum fee in (2).
6. A bitset of identifiers of transactions not included in the block.
7. The IBLT itself.

When generating a block, I assume we record the minimum fee per byte
(ignoring any free transactions).

To create an IBLT from a block:

1. Pick a 64-bit seed, and encode all the txids in the mempool (try
   again if it has a collision).
2. For each transaction in the block which is under the minimum fee
   per byte, record the minimum bit prefix of the txid48 to uniquely
   identify it in the mempool.  This becomes the "added" set.
3. For each transaction in the mempool which is greater than or equal
   to the minimum fee per byte but isn't in the block, record the
   minimum bit prefix of the txid48 to uniquely identify it in the
   mempool.  This becomes the "removed" set.
4. Insert all the transactions into the iblt.

Recovery of a block from an IBLT proceeds as follows:

1. Use the 64-bit seed to encode all txids in the mempool.
2. Select all those which exceed the minimum fee per byte.
3. Add all those which match the "added" bitset in (2) above.
4. Remove all those which match the "removed" bitset in (3) above.
5. Construct the iblt from the selected set.
6. Subtract this iblt from the received iblt.
7. Recover iblt as normal.


How to Use
----------
You need a bunch of mempool data and transactions (with fee
information).  I have included (most of) the transactions for the
bitcoin-corpus in txcache.tar.xz.

1. Fetch https://github.com/rustyrussell/bitcoin-corpus and unxz the corpuses
   you want to play with.
2. Unpack the transactions into txcache:
   `$ tar cvfJ txcache.xz`
3. Run the program like this:
   `$ ./iblt-test ../bitcoin-corpus/au ../bitcoin-corpus/{sf,sf-rn,sg}`

You will get output like this:

    blocknum,blocksize,knownbytes,unknownbytes,mempoolbytes,addedbitesetsize,removedbitsetsize,sf,sf-rn,sg
    352720,674752,674650,0,793136,1550,5,55020,1738,1738
    352721,372504,372384,0,393241,748,2,951,951,1691

The fields are as follows:

1. *blocknum*: the block number.
2. *blocksize*: the total size of the block (sum of transactions).
3. *knownbytes*: the bytes of transactions this node had in its mempool already.
4. *unknownbytes*: the bytes of transactions this node didn't have in mempool.
   (the two will add up to less than blocksize, since coinbase isn't counted)
5. *mempoolbytes*: the bytes in the mempool once we'd added any unknowns.
6. *addedbitsetsize*: size of the "added" bitset on the wire.
7. *removedbitsetsize*: size of the "removed" bitset on the wire.
8. The rest are the minimum bytes which allowed each node to
   reconstruct the block.

TODO
----

1. Play with IBLT_SIZE
2. Turn off frag_off and see if it's really helping.
3. Benchmark creation/extraction of IBLT.
4. Size IBLT based on feedback from peers.
5. Don't include redundant added/removed: if all txs matching prefix are
   the to be added/removed, just use that prefix.
6. Include other literal transactions, not just coinbase.
7. All the FIXMEs...

Enjoy!

Rusty Russell <rusty@rustcorp.com.au>
