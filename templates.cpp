// Holy shit, C++ is a mess.
#include "rawiblt.cpp"
#include "iblt.cpp"
#include "wire_encode.cpp"
#include "txslice.cpp"

template class raw_iblt<IBLT_SIZE>;
template class iblt<IBLT_SIZE>;
template class txslice<IBLT_SIZE>;
std::vector<u8> wire_encode(const bitcoin_tx &coinbase,
                            const u64 min_fee_per_byte,
                            const u64 seed,
							const txbitsSet &added,
							const txbitsSet &removed,
                            const raw_iblt<IBLT_SIZE> &iblt);

raw_iblt<IBLT_SIZE> wire_decode(const std::vector<u8> &incoming,
								bitcoin_tx &coinbase,
								u64 &min_fee_per_byte,
								u64 &seed,
								txbitsSet &added,
								txbitsSet &removed);

