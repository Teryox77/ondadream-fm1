// the preview (pv4.html, its own code in Chromium) rendered offline with the scenarios of c_seq.c
const {chromium}=require('playwright');const fs=require('fs');
(async()=>{
const b=await chromium.launch();
for(const sc of (process.argv[2]||'0,1,2,3,4,5,6,7,8,9,10,11,12').split(',').map(Number)){
  const p=await b.newPage();const errs=[];p.on('pageerror',e=>errs.push(e.message));
  await p.addInitScript(()=>{
    const AC=window.AudioContext;window.AudioContext=class extends AC{constructor(){super({sampleRate:44100});}};window.webkitAudioContext=window.AudioContext;
    window.__cur=null;Math.random=()=>{const c=window.__cur;if(!c)return 0;let x=c.rs;x^=x<<13;x>>>=0;x^=x>>>17;x^=x<<5;x>>>=0;c.rs=x;return (x>>>8)/16777216;};
  });
  await p.goto('file:///home/claude/seq/pv4_fix.html');await p.waitForTimeout(200);
  const data=await p.evaluate((sc)=>{
    let hits=0;
    const t0=DrumVoice.prototype.trigger,r0=DrumVoice.prototype.render;
    DrumVoice.prototype.trigger=function(...a){t0.apply(this,a);this.rs=(Math.imul(++hits,0x9E3779B9)|1)>>>0;};
    DrumVoice.prototype.render=function(...a){window.__cur=this;try{return r0.apply(this,a);}finally{window.__cur=null;}};
    P.vol=.8;
    let sc0=sc;if(sc>=13){TRACK_STATE[0].rhythmIdx=sc-13;TRACK_STATE[0].layers=cloneLayers(sc-13);sc=11;}
    if(sc===11)SEQ.trackMute[1]=SEQ.trackMute[2]=SEQ.trackMute[3]=true;
    if(sc===12)SEQ.trackMute[0]=true;
    if(sc>=11){P.cho=P.eco=P.rev=0;}
    if(sc>0&&sc<11){
      const i=sc-1;applyPreset(i*4%43);P.vol=.8;
      TRACK_STATE[0].rhythmIdx=i;TRACK_STATE[0].layers=cloneLayers(i);
      SEQ.bpm=90+i*12;
      for(let r=1;r<4;r++){const ts=TRACK_STATE[r];ts.presetIdx=(i*5+r*7)%43;ts.pan=((i+r)%5-2)*.4;ts.fx=((i*3+r)%4)*.3;ts.vol=.5+((i+2*r)%4)*.15;
        for(const row of ts.pitchGrid)row.fill(false);}
      const T=TRACK_STATE,m=(r,p,mask)=>{for(let c=0;c<16;c++)if(mask>>c&1)T[r].pitchGrid[p][c]=true;};
      T[0].pan=(i%3-1)*.5;T[0].fx=(i%4)*.25;T[0].vol=.6+(i%3)*.15;
      m(1,12,0x0101);m(1,16,0x0101);m(1,19,0x0101);
      m(2,i%24,0x1248);m(2,(i*7)%24,0x8421);
      m(3,23-i,0x5555);
      SEQ.trackMute[i%4]=(i%3===2);
    }
    startAudio();const fn=node.onaudioprocess;node.onaudioprocess=null;node.disconnect();ctx.suspend();
    const L=new Float32Array(1024),R=new Float32Array(1024),ev={outputBuffer:{getChannelData:c=>c?R:L}};
    const out=[];
    const run=(nb,keep)=>{for(let k=0;k<nb;k++){fn(ev);if(keep)for(let j=0;j<1024;j++){out.push(L[j],R[j]);}}};
    run(86,false);seqPlay();run(sc&&sc<11?216:345,true);seqStop();run(43,true);
    return Array.from(new Float32Array(out));
  },sc);
  fs.writeFileSync(`/tmp/sfid/r_${sc}.f32`,Buffer.from(new Float32Array(data).buffer));
  console.log(sc,data.length,errs);await p.close();
}
await b.close();})();
