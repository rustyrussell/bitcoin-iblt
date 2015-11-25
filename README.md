Bitcoin IBLT Test Code
======================

This proposes a wire format for sending bitcoin IBLT data, it then
simulates sending that between nodes, using input from:

	https://github.com/rustyrussell/weak-blocks

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
You can feed in the data from weak-blocks/results, which includes non-weak
examples.  The first "mempool" line is assumed to be the peer sending
the block to the other peers.

You also need a txcache/ directory to map txids to the actual
transactions and their fee amount.  You can generate this yourself
using `utils/create-txcache.sh` or simply unpack `txcache.tar.xz` (and
`txcache-352304.tar.xz` for block 352304).

Each program is a filter, as follows:

1. `iblt-selection-heuristic`: creates the seed, fee hint, and trees for included/excluded, and uses these to trim the mempools appropriately for the next step.
2. `iblt-encode`: encode the block from the first peer, by default basing the IBLT size on the amount the first peer would require to extract the block.
3. `iblt-decode`: try to recover the block for each peer.

The `iblt-decode` output is as follows:

```
blocknum,bytes,ibltbytes,peername,[0|1]
```

Where `bytes` is the the number of bytes for the block header,
coinbase, seed, fee hint and added/removed sets as well as the
iblt. `ibltbytes` is the raw IBLT size, `peername` is the recipient,
and the final field is `1` if reconstruction succeeded, `0` if not.

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
