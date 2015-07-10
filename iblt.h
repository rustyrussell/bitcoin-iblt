#ifndef IBLT_H
#define IBLT_H
#include "txid48.h"
#include "txslice.h"
#include "rawiblt.h"
#include <vector>
#include <set>

// We keep a postman-sorted TODO list of candidate buckets, based on
// how low their fragid is.
class iblt_todo {
private:
	// These are individual buckets, after this it's power of 2.
	static const size_t SOON_LOG2 = 3;
	static const size_t SOON = 1U << SOON_LOG2;

	// First contain fragids 0 to SOON-1, then powers of 2.
	std::set<size_t> todo[SOON + 16 + 1];

	// Get the todo list for this frag offset
	std::set<size_t> &get_todo(u16 fragoff);

	// Are we in sync with iblt?
	bool manually_removed;

public:
	iblt_todo() { }
	void add(u16 fragoff, size_t bucket);
	void del(u16 fragoff, size_t bucket, bool manual = false);

	// Returns (size_t)-1 if empty, otherwise a priority.
	size_t next_todo() const;

	// Call with results of (successful) next_todo.
	size_t next(size_t next_todo) const;
};

class iblt {
public:
	// Construct by subtracting two raw IBLTs.
	iblt(const raw_iblt &theirs, const raw_iblt &ours);

	// Two kind of buckets are interesting: count == 1 (in theirs, not ours)
	// and count == -1 (in ours, not theirs).
	enum bucket_type {
		OURS,
		THEIRS,
		NEITHER
	};
	
	// Extract data from a slice.  Returns NEITHER if none avail.
	bucket_type next(txslice &b) const;

	// All done?  Not very cheap, so only call after next() fails.
	bool empty() const;

	// Remove a single slice.
	void remove_their_slice(const txslice &s);

	// Remove an entire tx (returns slices removed)
	size_t remove_our_tx(const struct bitcoin_tx &btx, const txid48 &id);

	// If we don't remove anything, this cancels todo.
	void remove_todo(bucket_type, const txslice &);

private:
	void add_todo_if_singleton(size_t bucket);
	void remove_todo_if_singleton(size_t bucket);

	void frob_buckets(const txslice &s, int dir);

	// One for count == 1, one for count == -1.
	iblt_todo todo[THEIRS + 1];

	// Raw IBLT.
	raw_iblt riblt;
};

#endif // IBLT_H
