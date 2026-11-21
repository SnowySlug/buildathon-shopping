# Shopping Sessions entry

Exact C++17 and DuckDB implementation for the 2026 Builders Buildathon
Shopping Sessions track. The root Dockerfile builds linux/amd64; the default
command reads DATA_DIR, writes Parquet under OUTPUT_DIR, and uses SCRATCH_DIR.
Runtime has no network and works with a read-only root as uid1000.

The native frontend implements the challenge's CSV dialect, strict timestamps,
purchase prices, and transitive ID linking. It preserves invalid earliest
event duplicates until DuckDB selects the smallest ingestion time. DuckDB
deduplicates, sessionizes with equal-timestamp handling, and aggregates daily
user/category results. A sorted native timeline computes last-touch campaign
credit and seven-day assists without a potentially explosive interval join.

Validation for v1: all36 official edge fixtures pass. The public seed1 dev set
(1,000,000 events/250,000 clicks) exactly matches both official answer tables:
117,038 user rows and27,400 campaign rows, with no differing/missing/extra rows
or duplicate keys. One local Docker run at4CPU/4GB took16.957 internal seconds
with Windows bind-mounted data/scratch; this is not an official scoring time.

Required environment: DATA_DIR=/data,OUTPUT_DIR=/output,SCRATCH_DIR=/scratch,
HOME=/scratch,NCPU=8,MEM_GB=24. The image writes only output and scratch.
Build with `docker build --platform linux/amd64 -t shopping-entry .`.

This repository contains code only. No dataset, expected answers, private logs,
or generated output is part of the entry.

## Sources and disclosure

Original implementation was written during this event with OpenAI Codex AI
assistance. It uses Python, GCC/C++17, DuckDB1.5.6, and Polars1.38.1 (the latter
supports local validation tooling; the pipeline does not import it).
The organizer-provided skeleton/rules/fixtures/generator were used for protocol
reference and validation. No pre-weekend project implementation was reused.
Project-authored code is MIT licensed; dependencies retain upstream licenses.

Official sources:

- https://www.joinbuilders.org/buildathon/tracks/shopping-sessions
- https://www.joinbuilders.org/buildathon/tracks/shopping-sessions/files/rules.md?inline=1
- https://www.joinbuilders.org/buildathon/tracks/shopping-sessions/files/starter-duckdb.zip
- https://www.joinbuilders.org/buildathon/tracks/shopping-sessions/files/generator.zip
- https://www.joinbuilders.org/buildathon/tracks/shopping-sessions/files/dev-answer.zip
