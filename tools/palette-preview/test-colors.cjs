const assert=require('node:assert/strict');
const {compute,palettes,covers,backlights,contrast}=require('./colors.js');
const fromHex=h=>[1,3,5].map(i=>parseInt(h.slice(i,i+2),16)/255);
const checkPair=(t,a,b,s)=>assert(contrast(fromHex(t[a]),fromHex(t[b]))>=4.5,JSON.stringify({s,pair:[a,b],foreground:t[a],background:t[b]}));
let checked=0;
for(const palette of Object.keys(palettes))for(const theme of ['light','dark'])for(const primary of ['palette','gray','blue','cover','orange','yellow','green','purple','pink'])for(const cover of Object.keys(covers))for(const tint of [false,true])for(const backlight of Object.keys(backlights))for(const forceColorPlaceholders of [false,true]){
 const s={palette,theme,primary,cover,tint,backlight,forceColorPlaceholders}, t=compute(s);
 for(const [name,ratio] of Object.entries(t.ratios))assert(ratio>=4.5,JSON.stringify({s,name,ratio}));
 assert.equal(t.primary,compute({...s,tint:!tint}).primary,'Tint must not affect primary');
 assert.equal(t.bg,compute({...s,primary:primary==='blue'?'gray':'blue'}).bg,'Primary must not affect base');
 if(!tint)assert.equal(t.bg,compute({...s,cover:cover==='teal'?'rose':'teal'}).bg,'Untinted base must not follow cover');
 if(primary!=='cover')assert.equal(t.primary,compute({...s,cover:cover==='teal'?'rose':'teal'}).primary,'Fixed primary must not follow cover');
 if(primary!=='palette'||palette!=='lcd')assert.equal(t.primary,compute({...s,backlight:backlight==='olive'?'ice':'olive'}).primary,'LCD backlight must not affect manual primary or other palettes');
 for(const pair of [['text','bg'],['muted','bg'],['onPrimary','primary'],['onPrimary','primaryPressed'],['panelText','panel'],['panelText','panelPress'],['panelText','panelActive'],['panelMuted','panel'],['panelMuted','panelPress'],['panelMuted','panelActive'],['panelOnPrimary','panelPrimary'],['panelOnPrimary','panelPrimaryPressed'],['panelAction','panelActive'],['panelAction','panelPress'],['disabledInk','disabledFill'],['panelDisabledInk','panelDisabledFill'],['listText',t.listSurface==='transparent'?'bg':'listSurface'],['listText','listPress'],['listText','listActive'],['listMuted',t.listSurface==='transparent'?'bg':'listSurface'],['listMuted','listPress'],['listMuted','listActive'],['listAction','listActive'],['listAction','listPress'],['listDisabledInk','listDisabledFill']])checkPair(t,...pair,s);
 for(let i=0;i<6;i++)for(const prefix of ['tileInk','tileMuted','tileIcon'])checkPair(t,prefix+i,'tile'+i,s);
 for(let i=0;i<4;i++)checkPair(t,'placeholderInk'+i,'placeholder'+i,s);
 assert.equal(t.listSurface==='transparent',!(palette==='cyber2077'&&theme==='light'),'Only the light 2077 inversion uses one shared list panel');
 if(!forceColorPlaceholders&&(palette==='mono'||palette==='terminal'))for(let i=0;i<4;i++){const rgb=fromHex(t['placeholder'+i]);assert(Math.max(...rgb)-Math.min(...rgb)<.02,'Monochrome placeholders must stay neutral');}
 if(palette==='vivid'&&theme==='dark')assert.equal(t.bg,'#000000','Bright contrast must preserve its black canvas');
 if(forceColorPlaceholders){const reference=compute({...s,palette:'mono',theme:'light',primary:'gray',tint:false,backlight:'white'});for(let i=0;i<4;i++)assert.equal(t['placeholder'+i],reference['placeholder'+i],'Forced-color identity must survive palette, theme and tint changes');}
 const changedArt=compute({...s,forceColorPlaceholders:!forceColorPlaceholders});
 assert.equal(t.bg,changedArt.bg,'Forced artwork colors must not change the page');
 assert.equal(t.primary,changedArt.primary,'Forced artwork colors must not change primary');
 checked++;
}
console.log(`${checked} configurations: contrast and independent controls passed`);
