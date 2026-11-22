"""Atomic reports and compact summaries; no dependency on experiment runners."""
import gzip
import json


def save(path, report):
    path.parent.mkdir(parents=True,exist_ok=True)
    compressed = path.suffix=='.gz'
    data = json.dumps(report,allow_nan=False,**(dict(separators=(',',':')) if compressed else dict(indent=2))).encode()
    temporary = path.with_suffix(path.suffix+'.tmp')
    temporary.write_bytes(gzip.compress(data,compresslevel=1,mtime=0) if compressed else data)
    temporary.replace(path)

