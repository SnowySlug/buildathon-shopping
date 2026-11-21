# Shopping Sessions exact native pipeline

Original C++17 implementation for the 2026 Builders Buildathon Shopping
Sessions track. DuckDB converts only final daily aggregates to Parquet.
Every output value comes from the mounted input; this repository includes no data.

The pipeline preserves strict CSV parsing, bytewise identifiers, deduplication
before business validation, transitive identity links, and exact timestamp and
price rules. Event-ID hash fingerprints only filter candidates: equality always
requires the complete original ID. Canonical people receive exact integer IDs;
stable radix passes order person/time keys. A sliding click window computes
credit, bytewise tie breaking, and distinct campaign assists.

After loading all identity links, an immutable flat alias table resolves roots
once and references stable owned strings. Hash fingerprints only filter probes;
every matching alias requires full hash and exact byte equality. Lookup supports
arbitrary byte strings, including Unicode and embedded NUL bytes.

Build: `docker build --platform linux/amd64 -t shopping-entry .`

The default command reads DATA_DIR=/data, writes OUTPUT_DIR=/output and uses
SCRATCH_DIR=/scratch. It supports NCPU=8, MEM_GB=24, HOME=/scratch, read-only root,
offline execution, uid 1000 and 64 MiB /tmp. Dependencies: Python, DuckDB 1.5.6;
Polars 1.38.1 is installed for local validation support.

## Validation and measured limits

- All 36 official fixtures pass.
- Public 1M events/250k clicks: exact official answer match, 117,038 user rows
  and 27,400 campaign rows; zero differing or duplicate-key rows.
- Independent Python oracle: exact matches for 50k-event seeds 2, 7 and 29.
- Independent edge cases: Unicode/NUL IDs, wide timestamps, bytewise ties,
  long leading zeros, leap day and invalid earliest duplicate copies all match.
- Representative 20M events/5M clicks: exact all-row match against prior v5,
  2,237,784 user rows and 28,014 campaign rows, zero duplicate-key errors.
- Quiet ABBA local 8-CPU/6-GB comparison, v5/alias/alias/v5 CPU seconds:
  42.468 / 34.679 / 35.473 / 40.143. Alias mean 35.076 versus 41.306,
  about 15.1% lower. Internal wall means: 6.849 versus 7.556 seconds,
  about 9.4% lower. Candidate peaks were 2.42-2.56 GiB. These are local
  Docker Desktop measurements, not official scores; host variance remains.
- Final 60M/24GB execution is unmeasured. No official improvement is claimed.

## Sources and AI disclosure

Original implementation written during the event with OpenAI Codex AI
assistance. Official rules, starter, fixtures and generator were protocol and
validation references. No pre-weekend implementation was reused. Original
code is MIT licensed; dependencies retain their upstream licenses.

- https://www.joinbuilders.org/buildathon/tracks/shopping-sessions
- https://www.joinbuilders.org/buildathon/tracks/shopping-sessions/files/rules.md?inline=1
- https://www.joinbuilders.org/buildathon/tracks/shopping-sessions/files/starter-duckdb.zip
- https://www.joinbuilders.org/buildathon/tracks/shopping-sessions/files/generator.zip
- https://www.joinbuilders.org/buildathon/tracks/shopping-sessions/files/dev-answer.zip
