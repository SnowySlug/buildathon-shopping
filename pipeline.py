"""Native exact engine; DuckDB handles only small final CSV-to-Parquet output."""
import datetime as dt
import json
import os
from pathlib import Path
import subprocess
import sys
import time

import duckdb


def main():
    started=time.perf_counter()
    data=Path(os.environ.get("DATA_DIR","/data"))
    out=Path(os.environ.get("OUTPUT_DIR","/output"))
    work=Path(os.environ.get("SCRATCH_DIR","/scratch"))
    manifest=json.loads((data/"manifest.json").read_text())
    epoch=lambda s:int(dt.datetime.fromisoformat(s.replace("Z","+00:00")).timestamp()*1000)
    subprocess.run(["/app/fastio",str(data),str(work),str(epoch(manifest["window_start"])),str(epoch(manifest["window_end"]))],check=True)
    con=duckdb.connect()
    con.execute(f"SET threads={int(os.environ.get('NCPU','4'))}")
    con.execute("SET preserve_insertion_order=false")
    con.execute("SET temp_directory=?",[str(work/'spill')])
    con.execute(f"SET memory_limit='{max(1,int(float(os.environ.get('MEM_GB','4'))*.75))}GB'")
    q=lambda p:"'"+str(p).replace("'","''")+"'"
    for table,filename,columns in [
      ('user_daily','users-*.csv',{'user_id':'VARCHAR','day':'DATE','sessions':'BIGINT','events':'BIGINT','revenue_cents':'BIGINT','top_category':'VARCHAR'}),
      ('campaign_daily','campaign_daily.csv',{'campaign_id':'VARCHAR','channel':'VARCHAR','day':'DATE','sessions':'BIGINT','purchasing_sessions':'BIGINT','revenue_cents':'BIGINT','assisted_sessions':'BIGINT'})]:
        (out/table).mkdir(parents=True,exist_ok=True)
        con.execute(f"""COPY (SELECT * FROM read_csv({q(work/filename)},header=true,columns={columns!r},
            auto_detect=false,delim=',',quote='"',escape='"',nullstr='',allow_quoted_nulls=true))
            TO {q(out/table/'part-0.parquet')} (FORMAT PARQUET, COMPRESSION SNAPPY)""")
    print(f"finished {time.perf_counter()-started:.3f}s",file=sys.stderr)


if __name__=='__main__':
    main()
