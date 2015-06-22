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

void iblt_todo::del(u16 fragoff, size_t bucket, bool manual)
{
    if (!get_todo(fragoff).erase(bucket) && !manually_removed) {
        throw std::runtime_error("Bucket not in todo");
    }
    if (manual) {
        manually_removed = true;
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

size_t iblt_todo::next(size_t next_todo) const
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

    // Offset by fragment index base, and remove from todo list.
    txid48 id = riblt.buckets[n].get_txid48();
    todo[t].del(riblt.buckets[n].fragid - id.frag_base(), n);
}

void iblt::frob_buckets(const txslice &s, int dir)
{
    std::vector<size_t> buckets = riblt.select_buckets(s);
    for (size_t i = 0; i < buckets.size(); i++) {
        // We're about to change count; may take it off todo.
        remove_todo_if_singleton(buckets[i]);
        riblt.frob_bucket(buckets[i], s, dir);
        add_todo_if_singleton(buckets[i]);
    }
}

enum iblt::bucket_type iblt::next(txslice &s) const
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

    // Take a copy.
    s = riblt.buckets[n];

    // So they know whether it's a positive or negative.
    return t;
}

void iblt::remove_todo(bucket_type t, const txslice &s)
{
    size_t n = todo[t].next(todo[t].next_todo());

    txid48 id = s.get_txid48();
    todo[t].del(s.fragid - id.frag_base(), n, true);
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

void iblt::remove_our_tx(const struct bitcoin_tx &btx, const txid48 &id)
{
    std::vector<txslice> v = slice_tx(btx, id);
    for (const auto &s : v) {
        frob_buckets(s, 1);
    }
}

void iblt::remove_their_slice(const txslice &s)
{
    frob_buckets(s, -1);
}
