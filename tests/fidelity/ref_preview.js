// Reference: the preview's own DSP code (lines cut from the OndaDream HTML), rendered offline.
const fs=require('fs');
const L=fs.readFileSync(process.argv[2],'utf8').split('\n');
const cut=(a,b)=>L.slice(a-1,b).join('\n');
let seed=12345; Math.random=()=>{seed|=0;seed=seed+0x6D2B79F5|0;let t=Math.imul(seed^seed>>>15,1|seed);t=t+Math.imul(t^t>>>7,61|t)^t;return((t^t>>>14)>>>0)/4294967296;};
const code=cut(427,534)+'\n'+cut(552,755).replace('const i0=p|0,fr=p-i0,i1=(i0+1)%len;','const i0=(p|0)%len,fr=p-(p|0),i1=(i0+1)%len; /*harness: float edge*/')+'\n'+cut(1347,1358)+`
let aiOn=false,aiRng=mulberry32(1);
function setPreset(i){for(const k of Object.keys(DEFAULTS))if(k!=="arpPattern"&&k!=="vol")P[k]=PRESETS[i].p[k];setArpPattern(PRESETS[i].p.arpPattern);}
function render(sec,events){ // events: [t, 'on'|'off', note]
  const sr=44100,n=1024,N=Math.ceil(sec*sr/n)*n,outL=new Float32Array(N),outR=new Float32Array(N);
  mix=new Float32Array(n);mixR=new Float32Array(n);fxSend=new Float32Array(n);trackScratch=new Float32Array(n);FX=makeFX(sr);
  for(const k in S){ /* smoothers start at the preset's values */ }
  let ev=0;
  for(let b=0;b<N;b+=n){
    while(ev<events.length&&events[ev][0]*sr<=b){const e=events[ev++];if(e[1]==='on'){heldAdd(e[2],.85);if(!arpOn())directNoteOn(e[2],.85);}else{const was=arpOn();heldRemove(e[2]);if(!was)directNoteOff(e[2]);}}
    const L=outL.subarray(b,b+n),R=outR.subarray(b,b+n);
`+(()=>{ // the process loop, from startAudio, without the drums and the sequencer
  let s=cut(1366,1480);
  s=s.replace(/^[\s\S]*?const L=ev\.outputBuffer[^\n]*\n/,'');
  s=s.replace('const n=L.length,sr=ctx.sampleRate;','');
  s=s.replace(/if\(SEQ\.playing&&SEQ\.next[^\n]*\n/,'');
  s=s.replace(/for\(let row=1;row<SEQ_ROWS;row\+\+\)\{\n\s*if\(SEQ\.rowGateOff[^\n]*\n\s*\}\n/,'');
  s=s.replace(/\/\/ batteria[\s\S]*?i\+=seg;sampleClock\+=seg;/,'i+=seg;sampleClock+=seg;');
  s=s.replace(/if\(SEQ\.playing&&sampleClock>=SEQ\.next\)[^\n]*\n\s*seqCheckGates\(sr\);\n/,'');
  s=s.replace(/&&\(!SEQ\.playing\|\|SEQ\.next>sampleClock\)\s*&&SEQ\.rowGateOff\.every\(g=>g>sampleClock\)\)break;/,')break;');
  s=s.replace(/node\.connect[\s\S]*$/,'');
  s=s.replace(/\};\s*$/,'');
  return s;
})()+`
  }
  return [outL,outR];
}
module.exports={render,setPreset,PRESETS,P,S};
`;
fs.writeFileSync('/tmp/ref_eval.js',code);
const m={};new Function('module','performance',code)(m,{now:()=>0});
const {render,setPreset,PRESETS}=m.exports;
const which=process.argv[3]?process.argv[3].split(',').map(Number):PRESETS.map((_,i)=>i);
const EV=[[0.05,'on',57],[0.05,'on',60],[0.05,'on',64],[1.25,'off',57],[1.25,'off',60],[1.25,'off',64],[2.6,'on',45],[3.2,'off',45]];
for(const i of which){
  // fresh engine per preset: reload the code
  const mm={};seed=12345;new Function('module','performance',code)(mm,{now:()=>0});
  mm.exports.setPreset(i);
  // smoothers: let them settle on the preset (as after a preset change in the preview)
  for(const k in mm.exports.S) mm.exports.S[k](k==='tmod'?0:mm.exports.P[k],44100*5,44100);
  const [l,r]=mm.exports.render(4.5,EV);
  const buf=Buffer.alloc(l.length*8);for(let k=0;k<l.length;k++){buf.writeFloatLE(l[k],8*k);buf.writeFloatLE(r[k],8*k+4);}
  fs.writeFileSync(`/tmp/fid/ref_${i}.f32`,buf);
}
console.log('ref done',which.length);
