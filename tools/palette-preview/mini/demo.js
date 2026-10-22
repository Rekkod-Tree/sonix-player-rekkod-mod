const {modes,geometry}=MiniProposal;
const state={theme:'light',screen:'list',content:'normal',targets:false,playing:true,selected:null};
let choices=['normal','compact','minimal'];
const tracks=['Арбузная корка','Бедняжка','Ведьма и осёл','Голые коки','Смельчак и ветер','Лесник','Камнем по голове','Прыгну со скалы'];
const iconFiles={play:'ui3-play',pause:'ui3-pause',music:'ui3-music',search:'ui3-music'};
function icon(name){return `<span class="mp-icon" style="--icon:url('../assets/native/${iconFiles[name]||name}.svg')" aria-hidden="true"></span>`;}
function mark(){return '<svg class="mp-open-mark" viewBox="0 0 24 24" aria-hidden="true"><path d="M5 9l7 7 7-7" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/></svg>';}
function now(){
 if(state.content==='radio')return {title:'Radio Paradise',artist:'Прямой эфир',live:true};
 if(state.content==='long')return {title:'Небо над бесконечной дорогой — концертная версия',artist:'Нейромонах Феофан и приглашённые музыканты'};
 if(state.selected!==null)return {title:tracks[state.selected],artist:'Король и Шут',listArt:true};
 return {title:'Небо',artist:'Нейромонах Феофан'};
}
function vars(){return state.theme==='light'?{'--page':'#e5e9ec','--surface':'#f7f9fa','--press':'#d4dde3','--ink':'#1a252e','--muted':'#465663','--focus':'#245a7b','--dock':'#9e4b00','--dockInk':'#ffffff','--dockMuted':'#f4dfce','--dockTrack':'#bf8150'}:{'--page':'#11151a','--surface':'#1e252d','--press':'#303b46','--ink':'#f1f4f7','--muted':'#aebbc6','--focus':'#8fbad4','--dock':'#e1bc8d','--dockInk':'#302216','--dockMuted':'#59452e','--dockTrack':'#b89972'};}
function status(){return '<div class="mp-status"><span>♩ 40</span><span>18:46</span><span>78% ▰</span></div>';}
function header(){return `<div class="mp-header"><span class="mp-back" aria-hidden="true">‹</span><h3>${state.screen==='list'?'Треки':'Внешний вид'}</h3></div>`;}
function list(){return `<div class="mp-group">Все треки · 1 248</div>${tracks.map((title,i)=>`<button type="button" class="mp-row" data-action="track" data-track="${i}"><span class="mp-list-art" aria-hidden="true"></span><span class="mp-row-copy"><b>${title}</b><small>Король и Шут</small></span><span class="mp-time">${['3:24','4:12','3:11','3:04','3:10','3:11','2:37','3:12'][i]}</span></button>`).join('')}`;}
function settings(index,mode){return `<div class="mp-setting-card"><div class="mp-setting-line"><span>Тема</span><small>${state.theme==='light'?'Светлая':'Тёмная'}</small></div></div><fieldset class="mp-setting-card"><legend>Мини-плеер</legend>${Object.entries(modes).map(([id,m])=>`<label class="mp-radio"><input type="radio" name="mode-${index}" id="mode-${index}-${id}" value="${id}" data-frame="${index}" ${mode===id?'checked':''}><span><b>${m.name}</b><small>${id==='normal'?'Обложка и две строки':id==='compact'?'Название и исполнитель в строку':'Только название и управление'}</small></span></label>`).join('')}<p class="mp-setting-caption">Меняется только нижняя панель.<br>Жесты остаются прежними.</p></fieldset><div class="mp-setting-card"><div class="mp-setting-line"><span>Цвет акцента</span><small>Из обложки</small></div></div>`;}
function dock(mode){
 if(state.content==='empty')return '';
 const t=now(),stop=t.live&&state.playing;
 return `<div class="mp-dock" data-action="open"><div class="mp-dock-card"></div><button type="button" class="mp-body-hit" data-action="open" aria-label="Открыть полный плеер: ${t.title}"><span class="mp-now-art" style="${t.listArt?'background-position:-28px -325px':''}" aria-hidden="true"></span>${mark()}<span class="mp-dock-copy"><span class="mp-name" title="${t.title}">${t.title}</span><span class="mp-artist" title="${t.artist}">${t.artist}</span></span></button><button type="button" class="mp-play-hit" data-action="play" aria-label="${stop?'Остановить радио':state.playing?'Пауза':'Воспроизвести'}"><span class="mp-play-disc">${stop?'<span class="mp-stop" aria-hidden="true"></span>':icon(state.playing?'pause':'play')}</span></button><div class="mp-progress" aria-hidden="true"><i style="width:${t.live?'100%':'39%'}"></i></div></div>`;
}
function render(){
 const focusId=document.activeElement?.id;
 const scrolls=[...document.querySelectorAll('.mp-content')].map(n=>n.scrollTop);
 document.querySelector('#comparison').innerHTML=choices.map((mode,index)=>{
  const m=modes[mode],g=geometry(mode,state.content!=='empty'),css={...vars(),'--reserve':g.reserved+'px','--visible':m.height+'px','--hit':m.hit+'px','--playWidth':m.playWidth+'px'};
  return `<article class="mp-variant" data-frame="${index}" data-mode="${mode}"><h2>${m.name}${mode==='compact'?'<small>рекомендую</small>':''}</h2><p class="mp-meta">${m.description}</p><div class="mp-stage"><div class="mp-device mp-mode-${mode}" data-targets="${state.targets}" style="${Object.entries(css).map(([k,v])=>k+':'+v).join(';')}">${status()}${header()}<div class="mp-content">${state.screen==='list'?list():settings(index,mode)}</div>${dock(mode)}</div></div><p class="mp-after">Видимая панель: <b>${g.visibleHeight} px</b> · резерв: <b>${g.reserved} px</b></p><p class="mp-explain">${mode==='normal'?'Как в текущей компоновке.':`Освобождает ${g.gained} px экрана. Пауза и открытие — по тапу.`}</p></article>`;
 }).join('');
 document.querySelectorAll('.mp-content').forEach((n,i)=>{n.scrollTop=scrolls[i]||0;});
 if(focusId){const target=document.getElementById(focusId);if(target)target.focus({preventScroll:true});}
}
for(const name of ['theme','screen','content','targets'])document.getElementById(name).addEventListener('change',e=>{
 state[name]=name==='targets'?e.target.checked:e.target.value;
 if(name==='content'){state.selected=null;state.playing=true;}
 render();
});
document.querySelector('#comparison').addEventListener('change',e=>{
 if(e.target.matches('input[type=radio]')){choices[Number(e.target.dataset.frame)]=e.target.value;render();}
});
document.querySelector('#comparison').addEventListener('click',e=>{
 const target=e.target.closest('[data-action]');if(!target)return;
 const action=target.dataset.action;
 if(action==='open'){document.querySelector('#full-player').showModal();return;}
 if(action==='play'){state.playing=!state.playing;document.querySelector('#event').textContent=now().live?(state.playing?'В макете радио запущено.':'В макете радио остановлено.'):(state.playing?'В макете воспроизведение включено.':'В макете пауза.');}
 if(action==='track'){state.selected=Number(target.dataset.track);state.content='normal';state.playing=true;document.querySelector('#content').value='normal';document.querySelector('#event').textContent='Выбран пример трека: '+tracks[state.selected]+'.';}
 render();
});
document.querySelector('#close-player').addEventListener('click',()=>document.querySelector('#full-player').close());
document.querySelector('#reset').addEventListener('click',()=>{choices=['normal','compact','minimal'];render();});
render();
