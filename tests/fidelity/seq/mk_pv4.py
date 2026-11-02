# the preview with 4 tracks (1-3 synth + D drums), as the user asked; English labels as the firmware
import re,sys
s=open('/home/claude/work/pv_local.html').read()
def rep(a,b,cnt=1):
    global s
    n=s.count(a)
    assert n==cnt,(a,n)
    s=s.replace(a,b)
rep('const SEQ_STEPS=16,SEQ_ROWS=5;','const SEQ_STEPS=16,SEQ_ROWS=4;')
rep('''TRACK_COLORS.push("#B26BFF");
TRACK_STATE.push({kind:"synth",presetIdx:23,pool:Array.from({length:4},()=>new Voice()),pitchGrid:makePitchGrid(),vol:.8,fx:.3,pan:.15});
TRACK_STATE[4].pitchGrid[PIANO_ROOT+7][6]=true;TRACK_STATE[4].pitchGrid[PIANO_ROOT+7][14]=true;
SEQ.trackMute.push(false);SEQ.rowVoices.push([]);SEQ.rowGateOff.push(Infinity);
const DISP=[1,2,3,4,0];''','const DISP=[1,2,3,0];')
rep('selTrack=DISP[clamp(slotOf(selTrack)+s,0,4)]','selTrack=DISP[clamp(slotOf(selTrack)+s,0,3)]')
rep('-135+slotOf(selTrack)/4*270','-135+slotOf(selTrack)/3*270')
rep('''    else if(bi>=0&&bi<5){selTrack=DISP[bi];}
    else if(bi>=5&&isDrumSel()){selDrum=(bi-5)%8;auditionDrum(DRUM_TYPES[selDrum]);}''','''    else if(bi>=0&&bi<4){selTrack=DISP[bi];}
    else if(bi>=4&&isDrumSel()){selDrum=(bi-4)%8;auditionDrum(DRUM_TYPES[selDrum]);}''')
rep('''    if(wi>=0&&wi<5){const t=DISP[wi];SEQ.trackMute[t]=!SEQ.trackMute[t];if(SEQ.trackMute[t]&&t>0)seqReleaseRow(t);}
    else if(bi>=0&&bi<5){selTrack=DISP[bi];}''','''    if(wi>=0&&wi<4){const t=DISP[wi];SEQ.trackMute[t]=!SEQ.trackMute[t];if(SEQ.trackMute[t]&&t>0)seqReleaseRow(t);}
    else if(bi>=0&&bi<4){selTrack=DISP[bi];}''')
rep('''        else if(bi<5)on=DISP[bi]===selTrack;
        else on=isDrumSel()&&bi-5===selDrum;''','''        else if(bi<4)on=DISP[bi]===selTrack;
        else on=isDrumSel()&&bi-4===selDrum;''')
rep('''        if(wi>=0&&wi<5)on=!SEQ.trackMute[DISP[wi]];
        else if(bi>=0&&bi<5)on=DISP[bi]===selTrack;''','''        if(wi>=0&&wi<4)on=!SEQ.trackMute[DISP[wi]];
        else if(bi>=0&&bi<4)on=DISP[bi]===selTrack;''')
rep('const SEQ_ROW_Y=[41,72,103,134,165],SEQ_ROW_H=31;','const SEQ_ROW_Y=[41,80,119,158],SEQ_ROW_H=31;')
rep('  for(let d=0;d<5;d++){\n    const t=DISP[d],y=SEQ_ROW_Y[d]','  for(let d=0;d<4;d++){\n    const t=DISP[d],y=SEQ_ROW_Y[d]')
rep('  for(let d=0;d<5;d++){\n    const t=DISP[d],x=d*48-6,','  for(let d=0;d<4;d++){\n    const t=DISP[d],x=d*60,')
# English, as the firmware's other pages
rep('{lbl:"Tempo",get:()=>(SEQ.bpm-40)/200,set:v=>{SEQ.bpm=Math.round(40+v*200);},txt:()=>String(SEQ.bpm)}','{lbl:"Tempo",get:()=>(SEQ.bpm-40)/200,set:v=>{SEQ.bpm=Math.round(40+v*200);},txt:()=>String(SEQ.bpm)}')
rep('{get lbl(){return isDrumSel()?"Strum":"Suono";},','{get lbl(){return isDrumSel()?"Inst":"Sound";},',2)
rep('{get lbl(){return isDrumSel()?"Ritmo":"Nota";},','{get lbl(){return isDrumSel()?"Rhythm":"Note";},',2)
rep('{lbl:"Riverb",','{lbl:"Reverb",',2)
rep('toast(recOn?"REC: SUONA SUI TASTI":"REC OFF")','toast(recOn?"REC: PLAY THE KEYS":"REC OFF")')
rep('PAGE_TITLE={SYNTH:"HOME",FILTRO:"FILTRO",ENV:"INVILUPPO",MOD:"LFO",ARP:"ARPEGGIO",FX:"EFFETTI",SEQ:"SEQUENCER",MIX:"MIXER",GLO:"GLOBALE"}',
    'PAGE_TITLE={SYNTH:"HOME",FILTRO:"FILTER",ENV:"ENVELOPE",MOD:"LFO",ARP:"ARPEGGIO",FX:"EFFECTS",SEQ:"SEQUENCER",MIX:"MIXER",GLO:"GLOBAL"}')
s=re.sub(r'5 tracce: 1–4 synth, 5 \(D\) batteria 808\. I primi 5 tasti neri','4 tracce: 1–3 synth, D batteria 808. I primi 4 tasti neri',s)
s=s.replace('i primi 5 tasti bianchi = mute','i primi 4 tasti bianchi = mute')
open('/home/claude/seq/pv4.html','w').write(s)
print('ok')
