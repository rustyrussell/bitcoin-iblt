#! /usr/bin/make
WEAK_RESULTS := ../../weak-blocks/results

%-size: %.csv
	@cut -d, -f2 < $< | (tr -s '\n' +; echo 0) | bc -l
%-success: %.csv
	@echo $$(cut -d, -f5 < $< | (tr -s '\n' +; echo 0) | bc -l) / $$(wc -l < $<) \* 100 | bc -l

# We use iblt-encode in pass-through mode, so iblt-decode gives us stats.
raw.csv: $(WEAK_RESULTS)/no-weak-full.csv.xz ../iblt-encode ../iblt-decode
	xzcat $(WEAK_RESULTS)/no-weak-full.csv.xz | ../iblt-encode --no-iblt 2>/dev/null | ../iblt-decode > $@

# Dynamic iblt.
iblt-dynamic.csv: $(WEAK_RESULTS)/no-weak-full.csv.xz ../iblt-encode ../iblt-decode ../iblt-selection-heuristic
	xzcat $(WEAK_RESULTS)/no-weak-full.csv.xz | ../iblt-selection-heuristic 2>/dev/null | ../iblt-encode | ../iblt-decode > $@

# Static iblt.
iblt-%.csv: $(WEAK_RESULTS)/no-weak-full.csv.xz ../iblt-encode ../iblt-decode ../iblt-selection-heuristic
	xzcat $(WEAK_RESULTS)/no-weak-full.csv.xz | ../iblt-selection-heuristic 2>/dev/null | ../iblt-encode --buckets=$* | ../iblt-decode > $@

# Weak encoding only (30 second weak blocks with 16x first-boost).
weak.csv: $(WEAK_RESULTS)/30-second-16-firstbonus-full.csv.xz ../iblt-encode ../iblt-decode
	xzcat $(WEAK_RESULTS)/30-second-16-firstbonus-full.csv.xz | ../iblt-encode --no-iblt 2>/dev/null | ../iblt-decode > $@

# Weak encoding as above, but include weak blocks in stats.
weak-all.csv: $(WEAK_RESULTS)/30-second-16-firstbonus-with-weak-full.csv.xz ../iblt-encode ../iblt-decode
	xzcat $(WEAK_RESULTS)/30-second-16-firstbonus-with-weak-full.csv.xz | ../iblt-encode --no-iblt 2>/dev/null | ../iblt-decode > $@

# Weak encoding *and* IBLT.
weak-and-iblt.csv: $(WEAK_RESULTS)/30-second-16-firstbonus-full.csv.xz ../iblt-encode ../iblt-decode ../iblt-selection-heuristic
	xzcat $(WEAK_RESULTS)/30-second-16-firstbonus-full.csv.xz | ../iblt-selection-heuristic 2>/dev/null | ../iblt-encode | ../iblt-decode > $@

# Weak encoding *and* IBLT as above, but include weak blocks in stats.
weak-and-iblt-all.csv: $(WEAK_RESULTS)/30-second-16-firstbonus-with-weak-full.csv.xz ../iblt-encode ../iblt-decode ../iblt-selection-heuristic
	xzcat $(WEAK_RESULTS)/30-second-16-firstbonus-with-weak-full.csv.xz | ../iblt-selection-heuristic 2>/dev/null | ../iblt-encode | ../iblt-decode > $@

# As above, but fixed IBLT size
weak-and-iblt-all-%.csv: $(WEAK_RESULTS)/30-second-16-firstbonus-with-weak-full.csv.xz ../iblt-encode ../iblt-decode ../iblt-selection-heuristic
	xzcat $(WEAK_RESULTS)/30-second-16-firstbonus-with-weak-full.csv.xz | ../iblt-selection-heuristic 2>/dev/null | ../iblt-encode --buckets=$* | ../iblt-decode > $@

# Weak blocks and IBLT only for weak blocks.
weak-and-iblt-weak-only-%.csv: weak-and-iblt-all-%.csv
	for peer in sf sf-rn au sg; do grep /$$peer, $< | awk -F , '{ if ($$1 == PREV_BLOCK) print PREV; PREV_BLOCK=$$1; PREV=$$0; }'; done > $@


