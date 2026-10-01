#!/usr/bin/env python3
import argparse,json,subprocess,time,statistics,re
p=argparse.ArgumentParser();p.add_argument('log');p.add_argument('--out',default='results.json');a=p.parse_args()
def one(exe,n):
 t=[];internal=[]
 for _ in range(3):
  s=time.perf_counter();r=subprocess.run([exe,a.log,str(n)],capture_output=True,text=True);t.append(time.perf_counter()-s)
  if r.returncode:raise RuntimeError(r.stderr)
  m=re.search(r'TEMPO DE EXECUÇÃO: ([0-9.]+)',r.stdout);internal.append(float(m.group(1)) if m else None)
 return {'wall_mean':statistics.mean(t),'program_mean':statistics.mean(internal)}
r={'log':a.log,'runs':[]}
for n in [1,2,4,8,16]:r['runs'].append({'threads':n,'mutex':one('./log_analyzer_par',n),'optimized':one('./log_analyzer_par_optimized',n)})
json.dump(r,open(a.out,'w'),indent=2);print(json.dumps(r,indent=2))
