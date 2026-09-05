# Ranking

The explainable score tiers are intentionally far apart:

1. exact file name: 1200;
2. file-name prefix: 1050;
3. exact folder name: 1000;
4. other name prefix: 700;
5. name substring: 450;
6. path: 180;
7. content: approximately 120.

Recent modification adds at most 20 and opening from PurrFind adds at most 20,
plus 8 for use in the last week. These bounded signals cannot move a weak
content match above a filename tier. `scoreExplanation` is returned through IPC
and appears in Properties only when `PURRFIND_SCORE_DEBUG` is set. Usage history
observes only successful opens initiated by PurrFind, stays in
the local database, is configurable, and can be cleared. Fuzzy matching remains
disabled: measured SQLite latency meets the target, while unconditional fuzzy
candidates would reduce precision and increase query cost.
