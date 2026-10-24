/* Selected large-art direction, aligned to Graphite component tokens. */
(function(root){
const M=typeof module!=='undefined'?require('./model.js'):root.MiniDesign;
const P=typeof module!=='undefined'?require('../colors.js'):root.PalettePreview;
const icons=typeof module!=='undefined'?require('./native-icons.js'):root.MiniNativeIcons;
const {rect,text,fit,mix,svgDocument}=M;
const geometry={cover:88,coverRadius:12,panelRadius:20,inside:12,gap:12,title:24,artist:22,compactFont:22,normalTextX:128,normalTextW:224,compactTextX:40,compactTextW:348,progress:3,progressInset:18,progressBottom:4};
let serial=0;
function roles(o={}){
 const family=o.family||'graphite',dark=o.theme!=='light',primary=o.primary||'palette';
 const cover=o.cover||'amber',tint=Boolean(o.tint);
 if(family==='graphite'){
  const accents={palette:'#a8d2e5',cover:cover==='rose'?'#d8a5bb':cover==='teal'?'#a2cfc5':'#ddb990',blue:'#a8d2e5',gray:'#bcbcbc'};
  const t=M.tokens({theme:o.theme,accent:accents[primary]||accents.palette,tint});
  return {...t,border:'none',coverTone:primary==='gray'?'#777777':'#ad744e',coverInk:primary==='gray'?'#ffffff':'#102630'};
 }
 const p=P.compute({palette:family,theme:dark?'dark':'light',primary,tint,cover,backlight:'olive',forceColorPlaceholders:false});
 return {dark,page:p.bg,pageInk:p.text,pageMuted:p.muted,surface:p.panel,ink:p.panelText,muted:p.panelMuted,accent:p.panelPrimary,onAccent:p.panelOnPrimary,line:p.panelLine,border:p.panelBorder==='transparent'?'none':p.panelBorder,placeholder:p.placeholder0,coverTone:p.placeholder0,coverInk:p.placeholderInk0};
}
function icon(name,x,y,size,ink){
 const i=icons[name];
 return `<svg x="${x}" y="${y}" width="${size}" height="${size}" viewBox="${i.viewBox}" ${i.attrs.replaceAll('currentColor',ink)}>${i.body.replaceAll('currentColor',ink)}</svg>`;
}
function art(x,y,size,o,t){
 if(!['missing','book','radio'].includes(o.state))return M.cover(x,y,size,geometry.coverRadius);
 const name=o.state==='book'?'ui3-book':o.state==='radio'?'ui3-radio':'ui3-music';
 return rect(x,y,size,size,geometry.coverRadius,t.coverTone)+icon(name,x+(size-36)/2,y+(size-36)/2,36,t.coverInk);
}
function footer(mode,o={}){
 const compact=mode==='compact',g=M.geometry[mode],t=roles(o),n=M.now(o);
 if(o.state==='empty')return {...M.footer('sleeve',mode,o),svg:rect(0,0,480,g.slot,0,t.page)+text('Трек не выбран — панель скрыта',240,g.slot/2+7,19,t.pageInk||t.ink,400,'text-anchor="middle"'),roles:t};
 const cy=8+g.height/2,cx=compact?432:408;
 let s=rect(0,0,480,g.slot,0,t.page)+rect(16,8,448,g.height,geometry.panelRadius,t.surface,t.border);
 if(!compact)s+=art(28,20,geometry.cover,o,t);
 const clip='refine-text-'+(++serial),x=compact?geometry.compactTextX:geometry.normalTextX,w=compact?geometry.compactTextW:geometry.normalTextW;
 s+=`<defs><clipPath id="${clip}">${rect(x,8,w,g.height,0,'white')}</clipPath></defs><g clip-path="url(#${clip})">`;
 if(compact)s+=text(fit(n.title+' · '+n.artist,w,22),x,cy+7,22,t.ink);
 else s+=text(fit(n.title,w,24),x,cy-9,24,t.ink,600)+text(fit(n.artist,w,22),x,cy+24,22,t.muted);
 s+='</g>';
 s+=rect(cx-g.play/2,cy-g.play/2,g.play,g.play,g.play/2,t.accent);
 if(n.kind==='stop')s+=M.glyph('stop',cx,cy,compact?23:29,t.onAccent);
 else s+=icon(n.kind==='play'?'ui3-play':'ui3-pause',cx-(compact?23:29)/2,cy-(compact?23:29)/2,compact?23:29,t.onAccent);
 const track={x:34,y:8+g.height-7,width:compact?354:412,height:3};
 if(o.state!=='radio'){
  const amount=Math.max(0,Math.min(1,o.progress??.42));
  s+=rect(track.x,track.y,track.width,3,1.5,mix(t.surface,t.ink,.20))+rect(track.x,track.y,track.width*amount,3,1.5,t.accent);
 }
 if(o.targets){const left=compact?400:364,top=(g.slot-g.hit)/2;s+=`<rect x="16" y="${top}" width="${left-16}" height="${g.hit}" fill="none" stroke="${t.ink}" stroke-dasharray="5 5"/><rect x="${left}" y="${top}" width="${g.playHit}" height="${g.hit}" fill="none" stroke="${t.ink}" stroke-dasharray="5 5"/>`;}
 return {width:480,height:g.slot,svg:s,geometry:{...g,reserved:g.slot,cover:compact?0:geometry.cover,textX:x,textW:w,track,button:{cx,cy,size:g.play,hit:g.playHit}},roles:t};
}
function shade(t){
 // Existing icon assets and the established four-column arrangement.
 let s=rect(0,0,480,380,0,t.surface)+rect(0,356,480,48,24,t.surface);
 s+=text('40',18,31,20,t.ink)+text('18:46',240,31,20,t.ink,400,'text-anchor="middle"')+text('78%',456,31,20,t.ink,400,'text-anchor="end"');
 const entries=[['wifi','Wi-Fi',true],['airplay-status','AirPlay',false],['lineout','Лин. выход',false],['low-gain','Усиление',false],['bluetooth','Bluetooth',true],['mseb','MSEB',true],['equalizer','PEQ',true]];
 entries.forEach(([name,label,on],i)=>{
  const cx=69+(i%4)*112,cy=94+Math.floor(i/4)*122;
  s+=rect(cx-32,cy-32,64,64,32,on?t.accent:mix(t.surface,t.ink,.13));
  s+=icon(name,cx-17,cy-17,34,on?t.onAccent:t.ink)+text(label,cx,cy+59,20,t.ink,400,'text-anchor="middle"');
 });
 s+=rect(373,184,64,64,32,mix(t.surface,t.ink,.13));
 s+=icon('moon',388,199,34,t.ink)+text('Сон',405,275,20,t.ink,400,'text-anchor="middle"');
 s+=icon('sun',20,342,30,t.ink)+rect(78,355,310,6,3,mix(t.surface,t.ink,.24))+rect(78,355,190,6,3,t.accent)+rect(257,347,22,22,11,t.accent)+icon('settings-2',417,342,30,t.ink);
 return s;
}
function volume(t){
 // Native pill: 64 x 300, right margin 14, vertically centred.
 return rect(402,250,64,300,32,t.surface,t.border)+text('40',434,291,22,t.ink,600,'text-anchor="middle"')+rect(427,317,14,213,7,mix(t.surface,t.ink,.22))+rect(427,437,14,93,7,t.accent);
}
function device(mode,o={}){
 const t=roles(o),f=footer(mode,o);
 return M.device('sleeve',mode,{...o,miniSvg:f.svg,tokenOverride:{...t,ink:t.pageInk||t.ink,muted:t.pageMuted||t.muted},rowTitleFont:26,rowSubFont:20,overlaySvg:o.screen==='shade'?shade(t):o.screen==='volume'?volume(t):''});
}
function overview(){
 let s=rect(0,0,1088,850,0,'#e9ecef')+text('Крупная обложка · доводка Graphite 2',40,52,29,'#1c2a34',600)+text('Одна геометрия, общие цвета компонентов и привычные действия.',40,82,15,'#536773');
 for(const [i,theme] of ['dark','light'].entries()){
  const x=40+i*528;s+=text(theme==='dark'?'Тёмная тема':'Светлая тема',x,130,20,'#1c2a34',600);
  s+=text('Обычный · 112 px / резерв 128',x,161,12,'#536773');s+=`<g transform="translate(${x} 176)">${footer('normal',{theme}).svg}</g>`;
  s+=text('Компактный · 56 px / резерв 72',x,354,12,'#536773');s+=`<g transform="translate(${x} 371)">${footer('compact',{theme}).svg}</g>`;
 }
 s+=text('Что приведено к общей системе',40,511,23,'#1c2a34',600);
 const notes=[['Обложка 88 × 88','12 px до края карточки.','Радиус 12 px, как в списках.'],['Текст 24 / 22','Semibold для названия.','Regular для исполнителя.'],['Кнопки 64 / 44','Зоны 88 × 112 / 64 × 64.','Штатные значки.'],['Прогресс 3 px','В компактном — до кнопки.','Без лишней цветной черты.']];
 notes.forEach((n,i)=>{const x=40+(i%2)*528,y=560+Math.floor(i/2)*101;s+=text(n[0],x,y,18,'#1c2a34',600)+text(n[1],x,y+28,14,'#536773')+text(n[2],x,y+51,14,'#536773');});
 s+=text('Эскизы. Карточка, резерв, жесты и действия сохраняются; прошивка не изменена.',40,817,14,'#536773');
 return svgDocument(1088,850,s,'Доработанный вариант крупной обложки в двух темах');
}
function contextBoard(){
 let s=rect(0,0,1088,1880,0,'#e9ecef')+text('В системе компонентов Graphite 2',40,52,30,'#1c2a34',600)+text('Схематичный контекст: список, шторка и прежняя по геометрии громкость.',40,82,15,'#536773');
 const panels=[['normal','dark','list','Обычный · список'],['normal','dark','shade','Та же поверхность · шторка'],['compact','light','list','Компактный · список'],['compact','light','volume','Та же поверхность · громкость']];
 panels.forEach(([mode,theme,screen,label],i)=>{const x=40+(i%2)*528,y=134+Math.floor(i/2)*860;const svg=device(mode,{theme,screen});s+=text(label,x,y-16,19,'#1c2a34',600)+`<g transform="translate(${x} ${y})">${svg.replace(/^<svg[^>]*>/,'').replace(/<\/svg>$/,'')}</g>`;});
 return svgDocument(1088,1880,s,'Доведённый мини-плеер со списком, шторкой и громкостью');
}
function paletteBoard(){
 const families=[['graphite','Графит 2'],...Object.entries(P.palettes).map(([id,p])=>[id,p.name])];
 let s=rect(0,0,1088,1900,0,'#e9ecef')+text('Одна форма · существующие палитры',40,52,29,'#1c2a34',600)+text('Слева тёмная тема, справа светлая. Цвета — иллюстрация ролей, не новая палитра.',40,82,14,'#536773');
 families.forEach(([family,label],i)=>{const y=130+i*194;s+=text(label,40,y,19,'#1c2a34',600);for(const [j,theme] of ['dark','light'].entries())s+=`<g transform="translate(${40+j*528} ${y+18})">${footer('normal',{family,theme}).svg}</g>`;});
 return svgDocument(1088,1900,s,'Мини-плеер с существующими семействами палитр');
}
function statesBoard(){
 let s=rect(0,0,1088,880,0,'#e9ecef')+text('Крупная обложка · состояния',40,52,30,'#1c2a34',600);
 [['playing','Музыка'],['paused','Пауза'],['long','Длинное название'],['radio','Прямой эфир: Stop'],['missing','Без обложки'],['book','Аудиокнига']].forEach(([state,label],i)=>{const x=40+(i%2)*528,y=127+Math.floor(i/2)*240;s+=text(label,x,y,19,'#1c2a34',600)+`<g transform="translate(${x} ${y+21})">${footer('normal',{theme:i%2?'light':'dark',state}).svg}</g>`;});
 return svgDocument(1088,880,s,'Состояния доработанного варианта');
}
const api={geometry,roles,footer,device,overview,contextBoard,paletteBoard,statesBoard};
if(typeof module!=='undefined')module.exports=api;else root.RefinedMini=api;
})(typeof window==='undefined'?globalThis:window);
