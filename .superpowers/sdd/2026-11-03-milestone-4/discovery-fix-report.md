# Discovery fix report

Bug: sk_parse.c wanted() dropped the whole electrical tree for multi-segment prefixes.
Fix: leaves are kept only when equal to / under a prefix (segment boundary); non-leaf nodes are descended when wanted or a proper ancestor of a prefix (worth_descending). Filter applies at every depth, so unwanted subtrees are not interned.

Tests (tests/test_sk_parse.c): test_discovery_multi_segment_prefix, _prefix_segment_boundary, _single_segment_still_works.
RED: multi_segment test failed ("Expected Not-Equal") before the fix. GREEN: ctest 21/21 pass, check-layering.sh clean.
IDF: idf.py -D BOARD=lcd7b build succeeds.
Live (Signal K 2.35.1, victron-power, sim800, 20 s): IN 668 W, BATTERY 161 W, LOAD 507 W, SOC 82 %, SHORE; no FAIL. Capture: scratchpad/fix-sum.png (only the 20 s point was captured, before the 45 s solar-null).
docs/testing.md updated: bug marked fixed.
