import numpy as np
W,H=2048,1024; win=np.hanning(W); fr=np.fft.rfftfreq(W,1/44100)
edges=[50*2**(k/3) for k in range(26)]; bands=[(fr>=edges[k])&(fr<edges[k+1]) for k in range(25)]
ld=lambda p:np.fromfile(p,'<f4').reshape(-1,2)
def spec(m):
    n=(len(m)-W)//H; F=np.stack([m[i*H:i*H+W]*win for i in range(n)]); S=np.abs(np.fft.rfft(F,axis=1))**2
    return 10*np.log10(S.sum(1)+1e-12),10*np.log10(np.stack([S[:,b].sum(1) for b in bands],1)+1e-12)
import sys
for sc in (map(int,sys.argv[1].split(',')) if len(sys.argv)>1 else range(23)):
    r=ld(f'/tmp/sfid/r_{sc}.f32'); c=ld(f'/tmp/sfid/c_{sc}.f32')
    res=[]
    for ch in (0,1):
        a=r[:,ch]; b=c[:,ch]; err=a-b
        snr=10*np.log10((a**2).sum()/((err**2).sum()+1e-20))
        res.append(snr)
    # stereo balance
    bal_r=10*np.log10((r[:,0]**2).sum()/(r[:,1]**2).sum()); bal_c=10*np.log10((c[:,0]**2).sum()/(c[:,1]**2).sum())
    m_r=r.mean(1); m_c=c.mean(1)
    er,br=spec(m_r); ec,bc=spec(m_c); act=er>er.max()-45
    lvl=ec[act].mean()-er[act].mean(); env=np.abs(er-ec)[act].mean()
    rb=br[act]-er[act,None]; cb=bc[act]-ec[act,None]; tim=np.abs(rb-cb)[rb>-40].mean()
    print(f"sc {sc:2d} SNR L {res[0]:5.1f} R {res[1]:5.1f} dB | level {lvl:+5.2f} env {env:4.2f} timbre {tim:4.2f} dB | L/R balance prev {bal_r:+5.2f} fw {bal_c:+5.2f} | peak prev {np.abs(r).max():.3f} fw {np.abs(c).max():.3f}")
