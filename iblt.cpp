#include "iblt.h"
#include <stdexcept>
#include <algorithm>
extern "C" {
#include <ccan/ilog/ilog.h>
}

std::set<size_t> &iblt_todo::get_todo(u16 fragoff)
{
    if (fragoff <= SOON) {
        return todo[fragoff];
    } else {
        return todo[SOON + ilog32(fragoff - SOON)];
    }
}

void iblt_todo::add(u16 fragoff, size_t bucket)
{
    if (!get_todo(fragoff).insert(bucket).second) {
        throw std::runtime_error("Bucket already in todo");
    }
}

void iblt_todo::del(u16 fragoff, size_t bucket)
{
    if (!get_todo(fragoff).erase(bucket)) {
        throw std::runtime_error("Bucket not in todo");
    }
}

size_t iblt_todo::next_todo() const
{
    // First ones are sets of the same fragid, so don't need to sort.
    // Later are getting fairly desperate, so don't bother sorting.
    for (size_t i = 0; i < sizeof(todo) / sizeof(todo[0]); i++) {
        if (!todo[i].empty())
            return i;
    }
    return (size_t)-1;
}

size_t iblt_todo::next(size_t next_todo)
{
    return *todo[next_todo].begin();
}

iblt::iblt(const raw_iblt &theirs, const raw_iblt &ours)
    : riblt(theirs)
{
    if (ours.size() != theirs.size()) {
        throw std::runtime_error("IBLTs not same size");
    }

    // FIXME: txslice as union!
    u8 *dst = (u8 *)riblt.buckets.data();
    const u8 *src = (u8 *)ours.buckets.data();

    // XOR the two.
    // FIXME: do this faster, of course, using alignment assumptions.
    for (size_t i = 0; i < riblt.buckets.size() * riblt.buckets[0].size(); i++) {
        dst[i] ^= src[i];
    }

    // Now fixup the counters.
    for (size_t i = 0; i < riblt.counts.size(); i++) {
        riblt.counts[i] -= ours.counts[i];
        add_todo_if_singleton(i);
    }
}

void iblt::add_todo_if_singleton(size_t n)
{
    enum bucket_type t;

    if (riblt.counts[n] == -1) {
        t = OURS;
    } else if (riblt.counts[n] == 1) {
        t = THEIRS;
    } else {
        return;
    }

    // Offset by fragment index base, and add to todo list.
    txid48 id = riblt.buckets[n].get_txid48();
    todo[t].add(riblt.buckets[n].fragid - id.frag_base(), n);
}

void iblt::remove_todo_if_singleton(size_t n)
{
    enum bucket_type t;

    if (riblt.counts[n] == -1) {
        t = OURS;
    } else if (riblt.counts[n] == 1) {
        t = THEIRS;
    } else {
        return;
    }

    // Offset by fragment index base, and add to todo list.
    txid48 id = riblt.buckets[n].get_txid48();
    todo[t].del(riblt.buckets[n].fragid - id.frag_base(), n);
}

enum iblt::bucket_type iblt::next(txslice &s)
{
    size_t our_best_prio, their_best_prio, n;
    bucket_type t;
    int dir;

    our_best_prio = todo[OURS].next_todo();
    their_best_prio = todo[THEIRS].next_todo();

    // Prefer to find our slices in case of draw (may allow us to
    // remove entire tx)
    if (our_best_prio <= their_best_prio) {
        if (our_best_prio == (size_t)-1) {
            return NEITHER;
        }
        t = OURS;
        n = todo[OURS].next(our_best_prio);
        dir = 1;
    } else {
        t = THEIRS;
        n = todo[THEIRS].next(their_best_prio);
        dir = -1;
    }

    assert(riblt.counts[n] == -dir);

    // Take a copy before we erase it.
    s = riblt.buckets[n];

    std::vector<size_t> bucket_nums = riblt.select_buckets(s);

    // We should be cancelling that bucket!
    assert(std::find(bucket_nums.begin(), bucket_nums.end(), n) != bucket_nums.end());
    
    for (size_t i = 0; i < bucket_nums.size(); i++) {
        // We're about to change count; may take it off todo.
        remove_todo_if_singleton(bucket_nums[i]);
        riblt.frob_bucket(bucket_nums[i], s, dir);
        add_todo_if_singleton(bucket_nums[i]);
    }

    // So they know whether it's a positive or negative.
    return t;
}

bool iblt::empty() const
{
    for (size_t i = 0; i < riblt.size(); i++) {
        if (riblt.counts[i]) {
            return false;
        }
        if (!riblt.buckets[i].empty()) {
            return false;
        }
    }
    return true;
}

void iblt::remove(const struct bitcoin_tx &btx, const txid48 &id)
{
    std::vector<txslice> v = slice_tx(btx, id);
    for (const auto &s : v) {
        remove(s);
    }
}

void iblt::remove(const txslice &s)
{
    riblt.remove(s);
}
