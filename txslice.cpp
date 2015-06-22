#include "txslice.h"
#include "txid48.h"


struct slice_state {
    size_t index;
    size_t off;
    std::vector<txslice> vec;
};

static void add_slice(const void *data, size_t len, void *pvec)
{
    slice_state *s = (slice_state *)pvec;

    while (len) {
        size_t m = std::min(len, sizeof(s->vec[s->index].contents) - s->off);
        memcpy(s->vec[s->index].contents + s->off, data, m);
        data = (const void *)((const u8 *)data + m);
        len -= m;
        s->off += m;
        if (s->off == sizeof(s->vec[s->index].contents)) {
            s->index++;
            s->off = 0;
        }
    }
}

bool txslice::empty() const
{
    if (txidbits != 0 || fragid != 0) {
        return false;
    }
    for (size_t i = 0; i < sizeof(contents); i++) {
        if (contents[i]) {
            return false;
        }
    }
    return true;
}

// Size when varint_t is encoded at the front.
static size_t varint_len(varint_t v)
{
    if (v < 0xfd) {
        return 1;
    } else if (v <= 0xffff) {
        return 3;
    } else if (v <= 0xffffffff) {
        return 5;
    } else {
        return 9;
    }
}

static inline size_t num_slices_for(size_t s)
{
        return (s + IBLT_SIZE-1) / IBLT_SIZE;
}

std::vector<txslice> slice_tx(const bitcoin_tx &btx, const txid48 &id)
{
    // Optimistically assume we'll fit len in single byte.
    varint_t n_slices = num_slices_for(1 + btx.length());

    // If it would take 3 bytes to encode we have to recalculate.
    if (varint_len(n_slices) > 1) {
            // We only have 16 bit slice ids
            assert(n_slices <= 0xffff);
            n_slices = num_slices_for(varint_len(n_slices)
                                             + btx.length());
    }
            
    std::vector<txslice> vec(n_slices);
    slice_state s = { 0, 0, vec };

    // We 0 pad the end.
    memset(vec[n_slices-1].contents, 0, sizeof(vec[n_slices-1].contents));
    for (size_t i = 0; i < vec.size(); ++i) {
        vec[i].txidbits = id.get_id();
        assert(vec[i].txidbits == id.get_id());
        vec[i].fragid = i;
    }

    // Now linearize into it.
    add_varint(n_slices, add_slice, &s);
    btx.add_tx(add_slice, &s);
    assert(s.index == vec.size() - 1 || (s.index == vec.size() && s.off == 0));

    return vec;
}

varint_t txslice::slices_expected() const
{
    const u8 *p = contents;
    size_t len = sizeof(contents);
    return pull_varint(&p, &len);
}
