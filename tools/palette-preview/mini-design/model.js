/* Code-native design sketches. No player state, network or gesture handlers. */
(function(root){
const concepts=[
 {id:'current',name:'Текущий акцент',short:'Для сравнения',note:'Вся карточка акцентная. Хорошо видна, но сильнее притягивает взгляд.'},
 {id:'studio',name:'Студия',short:'Самый цельный',note:'Спокойная поверхность. Цвет остаётся у кнопки и прогресса; обложка заметнее.'},
 {id:'sleeve',name:'Крупная обложка',short:'Самый выразительный',note:'Обложка 96 px вместо 64. Больше характера, немного меньше места для текста.'},
 {id:'console',name:'Пульт',short:'Более приборный',note:'Отдельная зона управления и квадратная кнопка. Чёткий, собранный вид.'},
 {id:'capsule',name:'Капсула',short:'Самый мягкий',note:'Округлая панель и круглая обложка. У некоторых обложек обрезаются углы.'}
];
const geometry={normal:{height:112,slot:128,hit:112,playHit:88,play:64},compact:{height:56,slot:72,hit:64,playHit:64,play:44}};
let serial=0;
const esc=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const hex=v=>'#'+v.map(n=>Math.round(n).toString(16).padStart(2,'0')).join('');
const rgb=s=>[1,3,5].map(i=>parseInt(s.slice(i,i+2),16));
const mix=(a,b,t)=>hex(rgb(a).map((v,i)=>v*(1-t)+rgb(b)[i]*t));
const grey=s=>{const v=rgb(s);const y=.2126*v[0]+.7152*v[1]+.0722*v[2];return hex([y,y,y]);};
const lum=s=>rgb(s).map(v=>v/255).reduce((n,v,i)=>n+[.2126,.7152,.0722][i]*(v<=.04045?v/12.92:((v+.055)/1.055)**2.4),0);
const contrast=(a,b)=>(Math.max(lum(a),lum(b))+.05)/(Math.min(lum(a),lum(b))+.05);
const inkFor=bg=>contrast(bg,'#102630')>=contrast(bg,'#f7fafc')?'#102630':'#f7fafc';
function rect(x,y,w,h,r,fill,stroke='none',sw=1){return `<rect x="${x}" y="${y}" width="${w}" height="${h}" rx="${r}" fill="${fill}" stroke="${stroke}" stroke-width="${sw}"/>`;}
function text(value,x,y,size,fill,weight=400,attrs=''){return `<text x="${x}" y="${y}" font-size="${size}" fill="${fill}" font-family="${weight>=600?'Segoe UI Semibold':'Segoe UI'},Arial,sans-serif" font-weight="${weight}" ${attrs}>${esc(value)}</text>`;}
function fit(value,width,size){const max=Math.max(2,Math.floor(width/(size*.53)));return value.length>max?value.slice(0,max-1).trimEnd()+'…':value;}
function cover(x,y,size,r,missing=false,tone='#885d47'){
 const id='mini-art-'+(++serial);
 if(missing){
  let shape=rect(x,y,size,size,r,tone);const ink=inkFor(tone);
  if(missing==='book')return shape+`<rect x="${x+size*.26}" y="${y+size*.20}" width="${size*.48}" height="${size*.62}" rx="${size*.045}" fill="none" stroke="${ink}" stroke-width="${size*.038}"/><path d="M ${x+size*.35} ${y+size*.23} V ${y+size*.79} M ${x+size*.45} ${y+size*.39} H ${x+size*.64} M ${x+size*.45} ${y+size*.51} H ${x+size*.64}" stroke="${ink}" stroke-width="${size*.032}"/>`;
  if(missing==='radio')return shape+`<rect x="${x+size*.18}" y="${y+size*.35}" width="${size*.64}" height="${size*.40}" rx="${size*.06}" fill="none" stroke="${ink}" stroke-width="${size*.04}"/><path d="M ${x+size*.23} ${y+size*.35} L ${x+size*.72} ${y+size*.20}" stroke="${ink}" stroke-width="${size*.04}"/><circle cx="${x+size*.39}" cy="${y+size*.55}" r="${size*.09}" fill="none" stroke="${ink}" stroke-width="${size*.035}"/>`;
  return shape+text('♪',x+size/2,y+size*.66,size*.48,ink,400,'text-anchor="middle"');
 }
 const scale=size/316;
 return `<defs><clipPath id="${id}">${rect(x,y,size,size,r,'white')}</clipPath></defs><g clip-path="url(#${id})"><image x="${x-82*scale}" y="${y-124*scale}" width="${480*scale}" height="${800*scale}" href="assets/cover-source.png" xlink:href="assets/cover-source.png"/></g>`;
}
function glyph(kind,cx,cy,size,color){
 const x=cx-size/2,y=cy-size/2;
 if(kind==='stop')return rect(cx-size*.30,cy-size*.30,size*.60,size*.60,2,color);
 if(kind==='play')return `<path d="M ${x+size*.26} ${y+size*.12} L ${x+size*.84} ${cy} L ${x+size*.26} ${y+size*.88} Z" fill="${color}"/>`;
 return rect(x+size*.22,y+size*.16,size*.18,size*.68,1.4,color)+rect(x+size*.60,y+size*.16,size*.18,size*.68,1.4,color);
}
function tokens(options={}){
 const dark=options.theme!=='light',accent=options.accent||'#a8d2e5',page=dark?'#11151a':'#e5e9ec';
 const primary=dark?accent:mix(accent,'#142d42',.67);
 const pageInk=dark?'#edf3f7':'#182731',muted=dark?'#b0bec9':'#4b606e';
 const primaryInk=inkFor(primary);
 const coverTone='#b88c62';
 return {dark,page:options.tint?mix(page,coverTone,.085):page,surface:options.tint?mix(dark?'#202a32':'#f5f8fa',coverTone,.10):dark?'#202a32':'#f5f8fa',ink:pageInk,muted,accent:primary,onAccent:primaryInk,line:dark?'#4e5e69':'#9cadb9',placeholder:options.accent==='#bcbcbc'?'#777777':'#ad744e'};
}
function now(options){
 if(options.state==='radio')return {title:'Radio Paradise',artist:'Прямой эфир',kind:'stop'};
 if(options.state==='long')return {title:'В душе драм, в сердце светлая Русь — концертная версия',artist:'Нейромонах Феофан и приглашённые музыканты',kind:'pause'};
 if(options.state==='book')return {title:'Глава 12. Через лес',artist:'Аудиокнига · сохранённое место',kind:'pause'};
 return {title:'Козаки',artist:'Нейромонах Феофан',kind:options.state==='paused'?'play':'pause'};
}
function footer(id,mode,options={}){
 const g=geometry[mode],compact=mode==='compact',t=tokens(options),n=now(options),empty=options.state==='empty';
 if(empty)return {width:480,height:g.slot,svg:rect(0,0,480,g.slot,0,t.page)+text('Трек не выбран — панель скрыта',240,g.slot/2+7,19,t.muted,400,'text-anchor="middle"'),geometry:{...g,reserved:0}};
 const y=8,h=g.height,cy=y+h/2,cx=compact?432:408;
 const current=id==='current',capsule=id==='capsule',sleeve=id==='sleeve',consoleStyle=id==='console';
 const fill=current||capsule?t.accent:t.surface,ink=current||capsule?t.onAccent:t.ink;
 const sub=current||capsule?mix(ink,fill,.18):t.muted;
 const stroke=current?'none':capsule?mix(fill,ink,.24):mix(t.surface,t.ink,t.dark?.16:.18);
 const radius=capsule?h/2:20;
 let svg=rect(0,0,480,g.slot,0,t.page)+rect(16,y,448,h,radius,fill,stroke);
 let tx=compact?34:112,ty=cy-5,artistY=cy+24,maxText=compact?354:240;
 if(!compact){
  const size=sleeve?96:capsule?72:64,ax=sleeve?24:28,ay=cy-size/2;
  svg+=cover(ax,ay,size,capsule?size/2:sleeve?14:12,options.state==='book'?'book':options.state==='radio'?'radio':options.state==='missing',t.placeholder);
  if(sleeve){tx=136;maxText=214;}
  if(capsule){tx=116;maxText=230;}
 }else if(sleeve){
  svg+=rect(25,y+15,3,26,1.5,t.accent);tx=39;maxText=347;
 }
 const buttonSize=compact?44:consoleStyle?48:64;
 let buttonFill=current||capsule?ink:t.accent,buttonInk=current||capsule?fill:t.onAccent;
 if(consoleStyle){
  const divider=compact?390:356;
  svg+=`<path d="M ${divider} ${y+13} V ${y+h-13}" stroke="${stroke}" stroke-width="1"/>`;
 }
 const textClip='mini-text-'+(++serial);
 svg+=`<defs><clipPath id="${textClip}">${rect(tx,y,maxText,h,0,'white')}</clipPath></defs><g clip-path="url(#${textClip})">`;
 if(compact){
  const title=fit(n.title+' · '+n.artist,maxText,22);
  svg+=text(title,tx,cy+7,22,ink,500);
 }else{
  svg+=text(fit(n.title,maxText,24),tx,ty,24,ink,600);
  svg+=text(fit(n.artist,maxText,20),tx,artistY,20,sub);
 }
 svg+='</g>';
 svg+=rect(cx-buttonSize/2,cy-buttonSize/2,buttonSize,buttonSize,consoleStyle?12:buttonSize/2,buttonFill);
 svg+=glyph(n.kind,cx,cy,compact?23:29,buttonInk);
 if(options.state!=='radio'){
  const railX=capsule?(compact?42:60):34,railW=480-railX*2,railY=y+h-7,p=options.progress??.42;
  const rail=current||capsule?mix(fill,ink,.28):mix(fill,t.ink,.22),progress=current||capsule?ink:t.accent;
  svg+=rect(railX,railY,railW,3,1.5,rail)+rect(railX,railY,Math.max(0,Math.min(1,p))*railW,3,1.5,progress);
 }
 if(options.targets){
  const hitY=(g.slot-g.hit)/2,right=compact?400:364;
  svg+=`<rect x="16" y="${hitY}" width="${right-16}" height="${g.hit}" fill="none" stroke="${ink}" stroke-dasharray="5 5"/><rect x="${right}" y="${hitY}" width="${g.playHit}" height="${g.hit}" fill="none" stroke="${ink}" stroke-dasharray="5 5"/>`;
 }
 return {width:480,height:g.slot,svg,geometry:{...g,reserved:g.slot}};
}
function svgDocument(w,h,body,label='Эскиз мини-плеера'){
 return `<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="${w}" height="${h}" viewBox="0 0 ${w} ${h}" role="img" aria-label="${esc(label)}">${body}</svg>`;
}
function comparison(options={}){
 const W=1088,H=1340,theme=options.theme==='light'?'светлая':'тёмная';
 let s=rect(0,0,W,H,0,'#e9ecef')+text('Мини-плеер · пять направлений',40,53,30,'#1c2a34',600)+text(`Рисунки интерфейса, ${theme} тема. Геометрия R1 сохранена.`,40,83,15,'#536773');
 s+=text('ОБЫЧНЫЙ · 112 PX / РЕЗЕРВ 128',40,125,12,'#536773',600)+text('КОМПАКТНЫЙ · 56 PX / РЕЗЕРВ 72',568,125,12,'#536773',600);
 concepts.forEach((c,i)=>{
  const y=155+i*222;
  s+=text(String(i+1).padStart(2,'0')+'  '+c.name,40,y+6,21,'#1c2a34',600)+text(c.short,568,y+4,13,c.id==='studio'||c.id==='sleeve'?'#2e6a68':'#536773',500);
  const normal=footer(c.id,'normal',options),compact=footer(c.id,'compact',options);
  s+=`<g transform="translate(40 ${y+28})">${normal.svg}</g><g transform="translate(568 ${y+84})">${compact.svg}</g>`;
  s+=text(c.note,40,y+185,14,'#536773');
 });
 s+=text('Тап, свайпы и функции — прежние. Без blur, анимации обложки и новых кнопок.',40,H-32,14,'#536773');
 return svgDocument(W,H,s,'Пять вариантов мини-плеера, '+theme+' тема');
}
function device(id,mode,options={}){
 const t=options.tokenOverride||tokens(options),g=geometry[mode],empty=options.state==='empty',reserve=empty?0:g.slot;
 const context='context-'+(++serial),clip='screen-'+(++serial),bottom=800-reserve;
 let s=`<defs><clipPath id="${clip}">${rect(0,0,480,800,24,'white')}</clipPath><clipPath id="${context}">${rect(0,132,480,bottom-132,0,'white')}</clipPath></defs><g clip-path="url(#${clip})">`;
 s+=rect(0,0,480,800,0,t.page)+text('40',18,31,20,t.muted)+text('18:46',240,31,20,t.muted,400,'text-anchor="middle"')+text('78%',456,31,20,t.muted,400,'text-anchor="end"');
 s+=text('‹',24,99,39,t.ink)+text('Треки',75,98,32,t.ink,600)+text('⋮',439,98,30,t.ink);
 const records=[['Козаки','Нейромонах Феофан','2:22'],['Небо','Нейромонах Феофан','3:42'],['Белые списки','Нико Паскаль','3:03'],['ОРГАНИЗАЦИЯ','Oxxxymiron','3:21'],['Outro','Хаски','2:01'],['Intro','Хаски','2:18'],['Пляски с медведем','Нейромонах Феофан','4:08']];
 s+=`<g clip-path="url(#${context})">`;
 records.forEach(([title,artist,duration],i)=>{
  const y=140+i*88;
  s+=rect(28,y+10,56,56,12,i%2?mix(t.surface,t.accent,.34):t.placeholder);
  s+=text('♪',56,y+48,28,inkFor(i%2?mix(t.surface,t.accent,.34):t.placeholder),400,'text-anchor="middle"');
  const titleFont=options.rowTitleFont||25,subFont=options.rowSubFont||21;
  s+=text(fit(title,283,titleFont),98,y+34,titleFont,t.ink,500)+text(fit(artist,283,subFont),98,y+62,subFont,t.muted)+text(duration,452,y+47,19,t.muted,400,'text-anchor="end"');
 });
 s+='</g>';
 if(!empty)s+=`<g transform="translate(0 ${800-g.slot})">${options.miniSvg||footer(id,mode,options).svg}</g>`;
 if(options.overlaySvg)s+=options.overlaySvg;
 s+='</g>';
 return svgDocument(480,800,s,concepts.find(c=>c.id===id).name+', '+mode+', схематичный контекст R1');
}
function contextBoard(){
 let s=rect(0,0,1088,1880,0,'#e9ecef')+text('Как это выглядит на экране 480×800',40,52,30,'#1c2a34',600)+text('Схематичный список. Это не новые снимки устройства.',40,82,15,'#536773');
 const panels=[['studio','normal','dark'],['sleeve','normal','dark'],['studio','compact','light'],['sleeve','compact','light']];
 panels.forEach(([id,mode,theme],i)=>{
  const x=40+(i%2)*528,y=134+Math.floor(i/2)*860;
  s+=text(concepts.find(c=>c.id===id).name+' · '+(mode==='normal'?'обычный':'компактный'),x,y-16,19,'#1c2a34',600);
  const source=device(id,mode,{theme});s+=`<g transform="translate(${x} ${y})">${source.replace(/^<svg[^>]*>/,'').replace(/<\/svg>$/,'')}</g>`;
 });
 return svgDocument(1088,1880,s,'Два рекомендованных дизайна в контексте экрана R1');
}
function statesBoard(){
 let s=rect(0,0,1088,870,0,'#e9ecef')+text('Студия · состояния',40,52,30,'#1c2a34',600)+text('Никаких дополнительных действий и постоянных служебных подписей.',40,82,15,'#536773');
 const states=[['paused','Пауза'],['missing','Без обложки'],['long','Длинное название'],['radio','Радио: Stop'],['book','Книга'],['empty','Нет трека']];
 states.forEach(([state,label],i)=>{
  const x=40+(i%2)*528,y=132+Math.floor(i/2)*240;
  s+=text(label,x,y,19,'#1c2a34',600);s+=`<g transform="translate(${x} ${y+20})">${footer('studio','normal',{theme:i%2?'light':'dark',state}).svg}</g>`;
 });
 return svgDocument(1088,870,s,'Состояния предложенного мини-плеера');
}
const api={concepts,geometry,footer,device,comparison,contextBoard,statesBoard,svgDocument,tokens,contrast,cover,rect,text,fit,glyph,now,mix};
if(typeof module!=='undefined')module.exports=api;else root.MiniDesign=api;
})(typeof window==='undefined'?globalThis:window);
