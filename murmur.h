#ifndef MURMUR_H
#define MURMUR_H
extern "C" {
#include <ccan/short_types/short_types.h>
};
#include <cstring>

u32 MurmurHash3(u32, const u8 *data, size_t len);
#endif // MURMUR_H
