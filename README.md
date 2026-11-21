# Shopping Sessions radix and fingerprint entry

Exact C++17 pipeline for the 2026 Builders Buildathon Shopping Sessions track.
DuckDB only converts final daily aggregate CSV files to Parquet. Every output
value is computed from the mounted dataset; no data or expected answers are
included in the repository.

Input handling preserves the challenge's strict CSV dialect, ASCII timestamps
and prices, dedup-before-validation order, transitive identity links, and
byte-wise identity names. Runtime input files are mapped read-only. Event IDs
are partitioned into64 shards. The dedup table stores a32-bit hash fingerprint
as a cache prefilter: a match still requires the complete64-bit hash AND exact
original event ID before choosing the earliest ingestion copy. Fingerprints
never establish equality by themselves.

Canonical people are interned with exact byte comparisons into per-shard integer
IDs. Normal-window event records are16bytes. Stable byte radix passes order
the integer person/time keys; wider windows retain full64-bit timestamp offsets.
A sliding click window computes latest-click credit, byte-wise click-ID ties,
and distinct campaign assists. Engineering does not depend on specific dataset
identifiers, seeds, file counts, or file ordering.

Build: `docker build --platform linux/amd64 -t shopping-entry .`

The default command reads DATA_DIR=/data, writes OUTPUT_DIR=/output, and uses
SCRATCH_DIR=/scratch. It supports NCPU=8,MEM_GB=24,HOME=/scratch, a read-only
root filesystem, no network, uid1000, and64MiB /tmp. Runtime dependencies are
DuckDB1.5.6 and Python; Polars1.38.1 supports the local validation tools.

## Validation and limits

- All36 official edge fixtures pass.
- The public1M-event/250k-click dataset exactly matches both official answers:
 117,038 user rows and27,400 campaign rows; no missing, extra, differing or
 duplicate-key rows.
- Independent Python-oracle comparisons match fresh50k-event seeds2,7,29.
- Special independent cases match: wide64-bit timestamps, NUL/Unicode IDs,
 byte-wise click ties, long leading zeros, leap day, and invalid earliest copies.
- A20M-event/5M-click seed11 run exactly matches the prior native-v3 output:
 2,237,784 user rows and28,014 campaign rows, with no row or duplicate-key errors.
- Two local8CPU/6GB runs took7.974 and10.449 internal seconds, with47.919 and
 64.457 CPU seconds and2.40–2.44GiB peak container memory. An adjacent unchanged
 native-v3 control took13.725 internal seconds and89.578 CPU seconds. Local hardware/cache effects
 vary; these are not official scores and do not imply a fivefold overall gain.
- Full60M/24GB final-size execution remains unmeasured.

## Sources and disclosure

Original implementation was written during the event with OpenAI Codex AI
assistance. Official rules, starter, fixtures, and generator were used as
protocol references and for validation. No pre-weekend project implementation
was reused. Project-authored code is MIT licensed; dependencies retain their
upstream licenses.

- https://www.joinbuilders.org/buildathon/tracks/shopping-sessions
- https://www.joinbuilders.org/buildathon/tracks/shopping-sessions/files/rules.md?inline=1
- https://www.joinbuilders.org/buildathon/tracks/shopping-sessions/files/starter-duckdb.zip
- https://www.joinbuilders.org/buildathon/tracks/shopping-sessions/files/generator.zip
- https://www.joinbuilders.org/buildathon/tracks/shopping-sessions/files/dev-answer.zip
