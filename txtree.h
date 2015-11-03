/* A primitive bit tree, to find unique bit prefixes. */
#ifndef TXTREE_H
#define TXTREE_H
#include "tx.h"
#include "txid48.h"

// Better than calulating each time.
struct tx_with_id48 {
    // This can never be 0xFFF....; we count on that!
    txid48 id48;
    const tx *t;

    tx_with_id48(const txid48 &i, const tx *tp)
    : id48(i), t(tp) {
    }
};

// FIXME: Use critbit, it's smaller.  This also doesn't handle delete.
struct tx_tree {
    static const u64 IS_NODE = 0xFFFFFFFFFFFFFFFFULL;
    u64 discriminant;
    union {
        // Discriminant
        u64 *disc;
        tx_tree *node;
        tx_with_id48 *leaf;
    } u[2];

    tx_tree() : discriminant(IS_NODE) {
	u[0].node = NULL;
	u[1].node = NULL;
    }

    bool is_node(bool side) {
        return (*u[side].disc == IS_NODE);
    }

    ~tx_tree() {
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
            u[side].node = new tx_tree();
            u[side].node->insert(oldleaf, depth+1);
        }
        u[side].node->insert(leaf, depth+1);
    }

    /* FIXME: If two exceptions share the same bitid prefix with nothing else,
     * we can combine them. */
    std::vector<bool> get_unique_bitid(const txid48 &id48)
    {
	std::vector<bool> v;
	tx_tree *t = this;

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

};
#endif /* TXTREE_H */
