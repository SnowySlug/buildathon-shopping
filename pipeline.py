"""Exact Shopping Sessions pipeline: strict native input + DuckDB + streaming attribution."""
import datetime as dt
import json
import os
from pathlib import Path
import subprocess
import sys
import time

import duckdb


def main():
    started = time.perf_counter()
    data = Path(os.environ.get("DATA_DIR", "/data"))
    out = Path(os.environ.get("OUTPUT_DIR", "/output"))
    work = Path(os.environ.get("SCRATCH_DIR", "/scratch"))
    work.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((data / "manifest.json").read_text())
    epoch = lambda s: int(dt.datetime.fromisoformat(s.replace("Z", "+00:00")).timestamp() * 1000)
    subprocess.run(["/app/fastio", "sanitize", str(data), str(work),
                    str(epoch(manifest["window_start"])), str(epoch(manifest["window_end"]))], check=True)
    print(f"sanitized {time.perf_counter()-started:.3f}s", file=sys.stderr)
    con = duckdb.connect()
    con.execute(f"SET threads={int(os.environ.get('NCPU', '4'))}")
    con.execute(f"SET memory_limit='{max(1, int(float(os.environ.get('MEM_GB', '4'))*.75))}GB'")
    con.execute("SET preserve_insertion_order=false")
    con.execute("SET temp_directory=?", [str(work / "spill")])
    q = lambda p: "'" + str(p).replace("'", "''") + "'"
    events = q(work / "events.csv")
    clicks = q(work / "clicks.csv")
    con.execute(f"""
      CREATE TABLE ev AS
      SELECT r.u AS u,r.t AS t,r.k AS k,r.cat AS cat,r.p AS p
      FROM (SELECT arg_min(struct_pack(u:=u,t:=t,i:=i,k:=k,cat:=cat,p:=p), i) r
            FROM read_csv({events}, header=true,
              columns={{'eid':'VARCHAR','u':'VARCHAR','t':'BIGINT','i':'BIGINT','k':'TINYINT','cat':'VARCHAR','p':'INTEGER'}},
              nullstr='',allow_quoted_nulls=true,parallel=true)
            GROUP BY eid)
      WHERE r.u IS NOT NULL AND r.t IS NOT NULL AND r.k IN (1,2,3)
        AND r.cat IS NOT NULL AND r.p IS NOT NULL AND r.i-r.t<=86400000;
      CREATE TABLE session_category AS
      SELECT u,sid,cat,min(t) AS first_t,count(*) AS events,sum(p)::BIGINT AS revenue,
             count_if(k=3)::BIGINT AS purchases
      FROM (SELECT *,sum(is_start::BIGINT) OVER (PARTITION BY u ORDER BY t
                         RANGE BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW) AS sid
            FROM (SELECT *,coalesce(t-lag(t) OVER(PARTITION BY u ORDER BY t)>1800000,true) AS is_start FROM ev))
      GROUP BY u,sid,cat;
      DROP TABLE ev;
      CREATE TABLE sessions AS
      SELECT u,sid,min(first_t) AS t,epoch_ms(min(first_t))::DATE AS day,
             sum(events)::BIGINT AS events,sum(revenue)::BIGINT AS revenue,
             (sum(purchases)>0)::UTINYINT AS purchasing
      FROM session_category GROUP BY u,sid;
    """)
    for table in ("user_daily", "campaign_daily"):
        (out / table).mkdir(parents=True, exist_ok=True)
    con.execute(f"""
      COPY (
        WITH daily AS (
          SELECT u,day,count(*)::BIGINT AS sessions,sum(events)::BIGINT AS events,
                 sum(revenue)::BIGINT AS revenue_cents FROM sessions GROUP BY u,day
        ), top AS (
          SELECT c.u,s.day,c.cat,sum(c.revenue) AS revenue
          FROM session_category c JOIN sessions s USING(u,sid)
          WHERE c.purchases>0 GROUP BY c.u,s.day,c.cat
          QUALIFY row_number() OVER(PARTITION BY c.u,s.day ORDER BY sum(c.revenue) DESC,c.cat)=1
        )
        SELECT d.u AS user_id,d.day,d.sessions,d.events,d.revenue_cents,t.cat AS top_category
        FROM daily d LEFT JOIN top t USING(u,day)
      ) TO {q(out/'user_daily'/'part-0.parquet')} (FORMAT PARQUET, COMPRESSION SNAPPY);
      DROP TABLE session_category;
      CREATE TABLE clicks AS
      SELECT DISTINCT id,u,c,t FROM read_csv({clicks},header=true,
           columns={{'id':'VARCHAR','u':'VARCHAR','c':'VARCHAR','t':'BIGINT'}},nullstr='',allow_quoted_nulls=true);
      COPY (
        SELECT * FROM (
          SELECT u,t,0 AS kind,id,c,0::BIGINT AS revenue,0::UTINYINT AS purchasing FROM clicks
          UNION ALL
          SELECT u,t,1 AS kind,'' AS id,'' AS c,revenue,purchasing FROM sessions
        ) ORDER BY u,t,kind,id
      ) TO {q(work/'timeline.csv')} (HEADER true);
    """)
    print(f"sessions/timeline {time.perf_counter()-started:.3f}s", file=sys.stderr)
    subprocess.run(["/app/fastio", "reduce", str(work)], check=True)
    con.execute(f"""COPY (
      SELECT * FROM read_csv({q(work/'campaign_daily.csv')},header=true,
        columns={{'campaign_id':'VARCHAR','channel':'VARCHAR','day':'DATE','sessions':'BIGINT',
                 'purchasing_sessions':'BIGINT','revenue_cents':'BIGINT','assisted_sessions':'BIGINT'}})
      ) TO {q(out/'campaign_daily'/'part-0.parquet')} (FORMAT PARQUET, COMPRESSION SNAPPY)""")
    print(f"finished {time.perf_counter()-started:.3f}s", file=sys.stderr)


if __name__ == "__main__":
    main()
