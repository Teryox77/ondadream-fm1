import numpy as np, json, sys
P=json.load(open('/home/claude/ondadream-fm1/tools/presets.json'))
W,H=2048,1024; win=np.hanning(W); fr=np.fft.rfftfreq(W,1/44100)
edges=[50*2**(k/3) for k in range(26)]; bands=[(fr>=edges[k])&(fr<edges[k+1]) for k in range(25)]
ld=lambda p:np.fromfile(p,'<f4').reshape(-1,2)
def spec(m):
    n=(len(m)-W)//H; F=np.stack([m[i*H:i*H+W]*win for i in range(n)]); S=np.abs(np.fft.rfft(F,axis=1))**2
    return 10*np.log10(S.sum(1)+1e-12),10*np.log10(np.stack([S[:,b].sum(1) for b in bands],1)+1e-12)
out=[]
for i in range(43):
    r=ld(f'/tmp/fid/fref_{i}.f32').mean(1); c=ld(f'/tmp/fid/zc_{i}.f32').mean(1)
    lag=0
    if P[i]['p']['arpPattern']>.5: lag=0
    c=np.roll(c,lag)
    seg=slice(4096,int(1.2*44100))
    err=r[seg]-c[seg]; snr=10*np.log10((r[seg]**2).sum()/((err**2).sum()+1e-20))
    er,br=spec(r); ec,bc=spec(c); n=min(len(er),len(ec))
    act=np.maximum(er[:n],ec[:n])>er[:n].max()-45
    lvl=ec[:n][act].mean()-er[:n][act].mean(); env=np.abs(er[:n]-ec[:n])[act].mean()
    rb=br[:n][act]-er[:n][act,None]; cb=bc[:n][act]-ec[:n][act,None]; tim=np.abs(rb-cb)[rb>-40].mean()
    out.append((i,P[i]['name'],lvl,env,tim,snr))
    print(f"{i:2d} {P[i]['name']:12} level {lvl:+5.2f}  env {env:5.2f}  timbre {tim:5.2f}  wave-SNR {snr:5.1f} dB")
a=np.array([o[2:] for o in out]);print('mean |level| %.2f env %.2f timbre %.2f snr %.1f'%(np.abs(a[:,0]).mean(),a[:,1].mean(),a[:,2].mean(),np.median(a[:,3])))
