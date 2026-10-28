"""Summarize route_worker_bench batch-NN.log files alongside cases.json.

Usage: summarize_route_batch.py EVIDENCE_DIR
Writes analysis.json and results.csv; endpoint flags require manual diagnosis.
A returned route is not proof of road connectivity or endpoint correctness.
"""
import csv,json,pathlib,statistics,sys
from collections import Counter
p=pathlib.Path(sys.argv[1])
meta={x['id']:x for x in json.loads((p/'cases.json').read_text())['cases']}
rows=[]
def fields(line): return dict(x.split('=',1) for x in line.strip().split() if '=' in x)
for log in sorted(p.glob('batch-[0-9][0-9].log')):
    case={}
    for line in log.read_text().splitlines():
        if line.startswith('batch=begin '): case=fields(line);case['log']=log.name
        elif 'worker=route_failed ' in line: case['failure_reason']=fields(line).get('why','unknown')
        elif 'worker=route_rejected ' in line: case['rejected']=fields(line)
        elif 'worker=omaps_error why=' in line: case['turn_error']=line.split('why=',1)[1].rsplit(' ms=',1)[0]
        elif line.startswith('ch_diag start_roads='): case['snap']=fields(line)
        elif 'worker=ch_route ' in line: case['timing']=fields(line)
        elif line.startswith('batch=case '):
            case.update(fields(line));case.update({k:meta[case['id']][k] for k in ('kind','start','end','air_km')})
            for k in ['wall_ms','routes','air_m','distance_m','start_gap_m','end_gap_m','geometry_m','max_step_m','finite','maneuvers','omaps_errors']:
                case[k]=float(case[k])
            case['endpoint_suspect']=case['status']=='ok' and max(case['start_gap_m'],case['end_gap_m'])>100
            case['degenerate']=case['status']=='ok' and case['air_m']>10 and case['geometry_m']<1
            case['distance_suspect']=case['status']=='ok' and abs(case['geometry_m']-case['distance_m'])>max(100,case['distance_m']*.05)
            rows.append(case)
if not rows: print('No completed cases');raise SystemExit
ok=[r for r in rows if r['status']=='ok']
times=sorted(r['wall_ms'] for r in ok)
summary={'completed':len(rows),'expected':len(meta),'status':dict(Counter(r['status'] for r in rows)),
 'failure_reasons':dict(Counter(r.get('failure_reason','unknown') for r in rows if r['status']!='ok')),
 'endpoint_suspect':sum(r['endpoint_suspect'] for r in rows),'distance_suspect':sum(r['distance_suspect'] for r in rows),
 'degenerate':sum(r['degenerate'] for r in rows),
 'omaps_errors':sum(r['omaps_errors'] for r in rows),'nonfinite':sum(not r['finite'] for r in ok),
 'latency_ms':{'median':statistics.median(times),'p95':times[min(len(times)-1,int(.95*len(times)))],'max':max(times)} if times else {}}
for kind in ['regional','cross','short']:
    part=[r for r in rows if r['kind']==kind]
    summary[kind]={'completed':len(part),'status':dict(Counter(r['status'] for r in part))}
(p/'analysis.json').write_text(json.dumps({'summary':summary,'cases':rows},indent=2,ensure_ascii=False))
with (p/'results.csv').open('w') as f:
    keys=['id','kind','status','wall_ms','distance_m','start_gap_m','end_gap_m','geometry_m','max_step_m','maneuvers','omaps_errors','endpoint_suspect','distance_suspect','degenerate','failure_reason','turn_error']
    w=csv.DictWriter(f,fieldnames=keys,extrasaction='ignore');w.writeheader();w.writerows(rows)
print(json.dumps(summary,indent=2))
for r in rows:
    if r['status']!='ok' or r['endpoint_suspect'] or r['distance_suspect'] or r['omaps_errors']:
        print('FLAG',r['id'],r['start']['region'],r['end']['region'],r['status'],r.get('failure_reason'),r['wall_ms'],r['start_gap_m'],r['end_gap_m'],r.get('snap',{}))
