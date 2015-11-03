#include "ibltpool.h"
#include "txtree.h"

void ibltpool::add(const txid48 &id48, const tx *t)
{
	struct tx_with_id48 *txwid48 = new tx_with_id48(id48, t);

    tx_by_txid48.insert(std::make_pair(id48, t));
    tree->insert(txwid48);
}

ibltpool::ibltpool(u64 s, const std::unordered_map<bitcoin_txid, tx *> &tx_by_txid)
	: seed(s), tree(new tx_tree())
{
	for (const auto &p : tx_by_txid) {
		add(txid48(seed, p.first), p.second);
	}
}

ibltpool::~ibltpool()
{
	delete tree;
}

// Recursive accumulate of all txs
static std::vector<const tx *> gather(struct tx_tree *t)
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
    struct tx_tree *t = tree;

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

