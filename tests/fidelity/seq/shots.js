const {chromium}=require('playwright');const fs=require('fs');
(async()=>{const b=await chromium.launch();const p=await b.newPage({viewport:{width:900,height:700}});const errs=[];p.on('pageerror',e=>errs.push(e.message));
await p.goto('file:///home/claude/seq/pv4.html');await p.waitForTimeout(400);
const r=await p.evaluate(()=>{
  window.requestAnimationFrame=()=>0;ctx={resume(){}};
  const out={};const grab=n=>{out[n]=Array.from(g.getImageData(0,0,240,240).data);};
  const T=10350;
  page="SEQ";loop(T);grab('A');
  SEQ.playing=true;SEQ.visualStep=5;recOn=true;selTrack=0;loop(T);grab('B');
  selTrack=2;SEQ.trackMute[3]=true;TRACK_STATE[2].pitchGrid[16][0]=TRACK_STATE[2].pitchGrid[16][4]=true;TRACK_STATE[2].selNote=16;SEQ.bpm=97;recOn=false;SEQ.visualStep=13;
  TRACK_STATE[2].presetIdx=20;TRACK_STATE[1].vol=.4;selDrum=3;loop(T);grab('D');
  page="MIX";SEQ.visualStep=4;SEQ.trackMute[1]=true;loop(T);grab('C');
  TRACK_STATE[2].pan=-.5;TRACK_STATE[2].fx=.6;loop(T);grab('E');
  return out;});
for(const k in r){const px=r[k];let s='P6 240 240 255\n';const buf=Buffer.alloc(240*240*3);for(let i=0;i<240*240;i++){buf[3*i]=px[4*i];buf[3*i+1]=px[4*i+1];buf[3*i+2]=px[4*i+2];}
fs.writeFileSync(`p_${k}.ppm`,Buffer.concat([Buffer.from(s),buf]));}
console.log(Object.keys(r),errs);await b.close();})();
