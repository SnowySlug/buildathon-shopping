# Shopping Sessions native entry

Exact C++17 pipeline for the 2026 Builders Buildathon Shopping Sessions track.
DuckDB is used only to convert the final daily aggregate CSVs to Parquet.

The native engine implements the specified CSV dialect, ASCII timestamp and
price validation, and transitive linked IDs with byte-wise minimum names.
It maps source files read-only, parses independent 4MiB chunks in parallel,
and partitions event IDs into64 shards. A collision-checked hash table selects
the earliest ingestion copy before validation. Surviving events are repartitioned
by canonical person, sorted by event time, and streamed into sessions and daily
aggregates. A sliding seven-day click window computes latest-click credit and
distinct campaign assists without expanding a large interval join. Source
hashes only partition/look up exact IDs; every equality checks the original ID.

No input data, expected answers, dataset-specific identifiers, or private logs
are included. All output values are computed from the mounted runtime dataset.

Build: `docker build --platform linux/amd64 -t shopping-entry .`

The default command reads DATA_DIR=/data, writes OUTPUT_DIR=/output, and uses
SCRATCH_DIR=/scratch. It supports NCPU=8,MEM_GB=24, HOME=/scratch, a read-only
root filesystem, no network, and uid1000. Compiler/native code lives in the
build stage; the runtime stage contains the executable, Python, DuckDB1.5.6,
and Polars1.38.1 (only needed by local validation tools).

## Validation

- All36 official edge fixtures pass, including CR-as-data and quoted values.
- Three exact public1M-event/250k-click dev comparisons MATCH:117,038 user rows
  and27,400 campaign rows; no missing, extra, differing, or duplicate-key rows.
- An independent Python oracle, itself validated on36/36 fixtures, matches fresh
 50,000-event/12,500-click generator seeds2,7,29. Seed29 was rerun after the final
  explicit intermediate-CSV dialect fix and matched again.
- Final-code1M local4CPU/4GB Linux-volume runs:1.855,1.314,1.391 internal seconds
  (median1.391s). Host contention varied. These are not official scoring times.
- Full-size memory/runtime scaling remains to be measured; no final-size result
  is claimed by these small-data checks.

## Sources and disclosure

Original implementation was written during the event with OpenAI Codex AI
assistance. The official starter/rules/fixtures/generator were used as protocol
reference and for validation. No pre-weekend project implementation was reused.
Project-authored code is MIT licensed; dependencies retain upstream licenses.

- https://www.joinbuilders.org/buildathon/tracks/shopping-sessions
- https://www.joinbuilders.org/buildathon/tracks/shopping-sessions/files/rules.md?inline=1
- https://www.joinbuilders.org/buildathon/tracks/shopping-sessions/files/starter-duckdb.zip
- https://www.joinbuilders.org/buildathon/tracks/shopping-sessions/files/generator.zip
- https://www.joinbuilders.org/buildathon/tracks/shopping-sessions/files/dev-answer.zip
