#!/usr/bin/env python3
import argparse,json,subprocess,time,statistics,re

def measure(cmd,repeats=3):
    vals=[]
    for _ in range(repeats):
        t=time.perf_counter(); p=subprocess.run(cmd,capture_output=True,text=True)
        if p.returncode: raise RuntimeError(p.stderr)
        vals.append(time.perf_counter()-t)
    return statistics.mean(vals)

ap=argparse.ArgumentParser(description='Experimentos do Projeto 1')
ap.add_argument('log'); ap.add_argument('--threads',default='1,2,4,8,16'); ap.add_argument('--weak',default='',help='lista threads:arquivo, ex. 1:log1,2:log2,4:log4,8:log8'); ap.add_argument('--out',default='results.json'); a=ap.parse_args()
ths=[int(x) for x in a.threads.split(',')]
r={'strong':[],'weak':[],'granularity':[]}
for n in ths:
    r['strong'].append({'threads':n,'seq':measure(['./log_analyzer_seq',a.log]),'mutex':measure(['./log_analyzer_par',a.log,str(n)]),'optimized':measure(['./log_analyzer_par_optimized',a.log,str(n)])})
for x in a.weak.split(',') if a.weak else []:
    n,fn=x.split(':',1); n=int(n); r['weak'].append({'threads':n,'log':fn,'optimized':measure(['./log_analyzer_par_optimized',fn,str(n)])})
for bs in [1024,2048,4096,8192,16384,32768,65536,131072,262144,524288,1048576]:
    r['granularity'].append({'block_bytes':bs,'mutex':measure(['./log_analyzer_par',a.log,'4',str(bs)]),'optimized':measure(['./log_analyzer_par_optimized',a.log,'4',str(bs)])})
json.dump(r,open(a.out,'w'),indent=2);print(json.dumps(r,indent=2))
