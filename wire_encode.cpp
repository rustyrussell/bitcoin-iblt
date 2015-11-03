#include "wire_encode.h"
#include "bitcoin_tx.h"
#include <cassert>

void add_linearize(const void *data, size_t len, void *pvec)
{
    std::vector<u8> *vec = (std::vector<u8> *)pvec;

    vec->insert(vec->end(), (u8 *)data, (u8 *)data + len);
}

void add_bitset(std::vector<u8> *arr, const txbitsSet &bset)
{
    size_t i, min = bset.size() - 1, max = 0;

    for (i = 1; i < bset.size(); i++) {
        if (bset[i].size() == 0) {
            continue;
        }
        if (i < min)
            min = i;
        if (i > max)
            max = i;
    }

    // Empty set?  Encode as 0, with 0 length.
    if (max < min) {
        add_varint(0, add_linearize, arr);
        add_varint(0, add_linearize, arr);
        return;
    }

    // First we write out min and number of bitstrings.
    add_varint(min, add_linearize, arr);
    add_varint(max - min + 1, add_linearize, arr);

    // Now we write out the number for each of those
    size_t num_bits = 0;
    for (size_t i = min; i <= max; i++) {
        add_varint(bset[i].size(), add_linearize, arr);
        num_bits += i * bset[i].size();
    }

    // Now linearize the bitset for each of them.
    std::vector<u8> bits((num_bits + 7) / 8);
    size_t bitoff = 0;
    for (size_t i = min; i <= max; i++) {
        for (const auto &vec : bset[i]) {
            for (const auto &b : vec) {
                if (b) {
                    bits[bitoff / 8] |= (1 << (bitoff % 8));
                }
                bitoff++;
            }
        }
    }
    assert(bitoff == num_bits);
    add_linearize(bits.data(), bits.size(), arr);
}
                
static bool pull_bit(const u8 **p, size_t *len, unsigned int *bitoff)
{
    bool b;

    if (!*p) {
        return false;
    }
    b = **p & (1 << *bitoff);
    if (++(*bitoff) == 8) {
        *bitoff = 0;

        // Consume another byte.
        if (!*len) {
            *p = NULL;
        } else {
            (*p)++;
            (*len)--;
        }
    }
    return b;
}

bool decode_bitset(const u8 **p, size_t *len, txbitsSet &bset)
{
    varint_t min, num;

    // First we get min and max sizes of bitstrings.
    min = pull_varint(p, len);
    num = pull_varint(p, len);

    // Too large? */
    if (num > bset.size())
        return false;

    bset = txbitsSet();
    std::vector<varint_t> nums(min + num);

    // Now we read in the number for each of those
    for (size_t i = min; i < min + num; i++) {
        nums[i] = pull_varint(p, len);
        bset[i] = std::unordered_set<std::vector<bool>>(nums[i]);
    }

    // Already failed?  Stop here. */
    if (!*p)
        return false;

    // Now pull bits off the bitset for each of them.
    unsigned int bitoff = 0;
    for (size_t i = min; i < min + num; i++) {
        for (size_t j = 0; j < nums[i]; j++) {
            std::vector<bool> v(i);
            for (size_t k = 0; k < i; k++) {
                if (pull_bit(p, len, &bitoff)) {
                    v[k] = true;
                }
            }
            bset[i].insert(v);
            // In case they gave us stupid numbers, check now.
            if (!*p) {
                return false;
            }
        }
    }

    // Rest of byte must be zero.
    while (bitoff != 0) {
        if (pull_bit(p, len, &bitoff)) {
            return false;
        }
    }

    return *p != NULL;
}
