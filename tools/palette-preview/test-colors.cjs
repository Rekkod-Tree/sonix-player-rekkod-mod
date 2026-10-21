const assert=require('node:assert/strict');
const {compute,palettes,covers}=require('./colors.js');
let checked=0;
for(const palette of Object.keys(palettes))for(const theme of ['light','dark'])for(const primary of ['gray','blue','cover','orange','green','purple'])for(const cover of Object.keys(covers))for(const tint of [false,true]){
 const s={palette,theme,primary,cover,tint}, t=compute(s);
 for(const [name,ratio] of Object.entries(t.ratios))assert(ratio>=4.5,JSON.stringify({s,name,ratio}));
 assert.equal(t.primary,compute({...s,tint:!tint}).primary,'Tint must not affect primary');
 assert.equal(t.bg,compute({...s,primary:primary==='blue'?'gray':'blue'}).bg,'Primary must not affect base');
 if(!tint)assert.equal(t.bg,compute({...s,cover:cover==='teal'?'rose':'teal'}).bg,'Untinted base must not follow cover');
 if(primary!=='cover')assert.equal(t.primary,compute({...s,cover:cover==='teal'?'rose':'teal'}).primary,'Fixed primary must not follow cover');
 checked++;
}
console.log(`${checked} configurations: contrast and independent controls passed`);
