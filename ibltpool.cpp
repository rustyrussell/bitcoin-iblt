#include "ibltpool.h"

// Better than calulating each time.
struct tx_with_id48 {
	// This can never be 0xFFF....; we count on that!
	txid48 id48;
	const tx *t;

	tx_with_id48(const txid48 &i, const tx *tp)
		: id48(i), t(tp)
	{
	}
};

// FIXME: Use critbit, it's smaller.  This also doesn't handle delete.
struct tx_node {
    static const u64 IS_NODE = 0xFFFFFFFFFFFFFFFFULL;
    u64 discriminant;
    union {
        // Discriminant
        u64 *disc;
        tx_node *node;
        tx_with_id48 *leaf;
    } u[2];

    tx_node() : discriminant(IS_NODE) {
        u[0].node = NULL;
        u[1].node = NULL;
    }

    bool is_node(bool side) {
        return (*u[side].disc == IS_NODE);
    }

    ~tx_node() {
        if (u[0].disc) {
			if (is_node(0))
				delete u[0].node;
			else
				delete u[0].leaf;
		}
        if (u[1].disc) {
			if (is_node(1))
				delete u[1].node;
			else
				delete u[1].leaf;
		}
     }

    void insert(tx_with_id48 *leaf, size_t depth = 0) {
        bool side = (leaf->id48.get_id() >> depth) & 1;
        if (u[side].disc == NULL) {
            u[side].leaf = leaf;
            return;
        }
        if (!is_node(side)) {
            tx_with_id48 *oldleaf = u[side].leaf;
            u[side].node = new tx_node();
            u[side].node->insert(oldleaf, depth+1);
        }
        u[side].node->insert(leaf, depth+1);
    }
};

void ibltpool::add(const txid48 &id48, const tx *t)
{
	struct tx_with_id48 *txwid48 = new tx_with_id48(id48, t);

	tx_by_value.insert(std::make_pair(t->satoshi_per_byte(), t));
	tree->insert(txwid48);
}

ibltpool::ibltpool(u64 s, const mempool &mp)
	: seed(s)
{
	for (const auto &p : mp.tx_by_txid) {
		add(txid48(seed, p.first), p.second);
	}
}

ibltpool::~ibltpool()
{
	delete tree;
}

/* FIXME: If two exceptions share the same bitid prefix with nothing else,
 * we can combine them. */
std::vector<bool> ibltpool::get_unique_bitid(const txid48 &id48)
{
    std::vector<bool> v;
    tx_node *t = tree;

    for (size_t i = 0; i < txid48::BITS; ++i) {
        if (!t)
            throw std::invalid_argument("tx not found");
        bool side = (id48.get_id() >> i) & 1;
        v.push_back(side);
        if (t->is_node(side))
            t = t->u[side].node;
        else {
            assert(t->u[side].leaf->id48 == id48);
            return v;
        }
    }
    throw std::invalid_argument("tx not found");
}

// Recursive accumulate of all txs
static std::vector<const tx *> gather(struct tx_node *t)
{
	std::vector<const tx *> vec;

    for (int side = 0; side < 2; side++) {
        if (t->u[side].disc) {
            if (t->is_node(side)) {
				std::vector<const tx *> v = gather(t->u[side].node);
                vec.insert(vec.end(), v.begin(), v.end());
            } else {
                vec.push_back(t->u[side].leaf->t);
            }
        }
    }
	return vec;
}

// For decoding: get the txs (if any) matching this bitid.
std::vector<const tx *> ibltpool::get_txs(const std::vector<bool> &vec)
{
    struct tx_node *t = tree;

    for (size_t i = 0; i < vec.size(); ++i) {
        /* Hit the end?  Empty. */
        if (!t->u[vec[i]].disc)
            return std::vector<const tx *>();
            
        if (!t->is_node(vec[i])) {
            // Hit a leaf.  Return it it matches.
            if (t->u[vec[i]].leaf->id48.matches(vec))
                return std::vector<const tx *>(1, t->u[vec[i]].leaf->t);
            else
                // Otherwise empty vector.
                return std::vector<const tx *>();
        }

        // Keep traversing.
        t = t->u[vec[i]].node;
    }

    /* Now add all the others from here down. */
    return gather(t);
}

