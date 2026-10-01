#!/usr/bin/env python3
import json,argparse,matplotlib.pyplot as plt
p=argparse.ArgumentParser();p.add_argument('results');a=p.parse_args();d=json.load(open(a.results));r=d['strong'];x=[z['threads'] for z in r];seq=r[0]['seq'];sp=[seq/z['optimized'] for z in r];ef=[s/n for s,n in zip(sp,x)]
plt.figure();plt.plot(x,sp,'o-',label='Redução local');plt.plot(x,x,'--',label='Ideal');plt.xlabel('Número de threads');plt.ylabel('Speedup');plt.title('Strong Scaling - Speedup');plt.legend();plt.grid(True);plt.savefig('strong_speedup.png',dpi=150,bbox_inches='tight')
plt.figure();plt.plot(x,ef,'o-',label='Eficiência');plt.axhline(1,ls='--',label='Ideal (100%)');plt.xlabel('Número de threads');plt.ylabel('Eficiência');plt.title('Strong Scaling - Eficiência');plt.legend();plt.grid(True);plt.savefig('efficiency.png',dpi=150,bbox_inches='tight')
plt.figure();plt.plot(x,[z['mutex'] for z in r],'o-',label='Mutex global');plt.plot(x,[z['optimized'] for z in r],'o-',label='Redução local');plt.xlabel('Número de threads');plt.ylabel('Tempo (s)');plt.title('Comparação entre abordagens paralelas');plt.legend();plt.grid(True);plt.savefig('parallel_comparison.png',dpi=150,bbox_inches='tight')
g=d['granularity'];plt.figure();plt.plot([z['block_bytes']/1024 for z in g],[z['optimized'] for z in g],'o-');plt.xlabel('Tamanho do bloco (KB)');plt.ylabel('Tempo (s)');plt.title('Análise de Granularidade');plt.grid(True);plt.savefig('granularity.png',dpi=150,bbox_inches='tight')
if d['weak']:
 w=d['weak'];plt.figure();plt.plot([z['threads'] for z in w],[z['optimized'] for z in w],'o-');plt.xlabel('Número de threads');plt.ylabel('Tempo (s)');plt.title('Weak Scaling');plt.grid(True);plt.savefig('weak_scaling.png',dpi=150,bbox_inches='tight')
