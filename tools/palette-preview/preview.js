const {palettes,covers,compute}=PalettePreview;
const state={primary:'palette',tint:false,cover:'teal',page:'home',backlight:'olive',monoArt:false,forceColorPlaceholders:false,uiState:'normal',sample:'normal',playing:true,liked:false,quick:{wifi:true,bluetooth:true,airplay:false,mseb:true,eq:true,fade:false,gain:false,lineout:false},visible:Object.keys(palettes)};
const paths={music:'M9 18V5l12-2v13 M9 9l12-2 M9 18a3 3 0 1 1-3-3h3 M21 16a3 3 0 1 1-3-3h3',
 stream:'M5 18a10 10 0 0 1 0-12 M9 15a6 6 0 0 1 0-6 M19 6a10 10 0 0 1 0 12 M15 9a6 6 0 0 1 0 6 M12 12h.01',
 book:'M3 4h7l2 2 2-2h7v15h-7l-2 2-2-2H3z M12 6v15',folder:'M3 5h7l2 3h9v12H3z',
 settings:'M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8 M10 2h4l1 3 3 2 3 0 2 4-2 2-1 3 0 3-4 2-2-2-3-1-3 0-2-4 2-2 1-3-1-3 4-2z',
 more:'M5 12h.1 M12 12h.1 M19 12h.1',search:'M10 3a7 7 0 1 0 0 14 7 7 0 0 0 0-14 M15 15l6 6',
 back:'M15 4l-8 8 8 8',chevron:'M9 5l7 7-7 7',down:'M5 9l7 7 7-7',play:'M8 5l11 7-11 7z',pause:'M8 5v14 M16 5v14',
 next:'M5 5l11 7-11 7z M20 5v14',previous:'M19 5L8 12l11 7z M4 5v14',
 queue:'M3 5h18 M3 11h18 M3 17h10 M18 15l4 3-4 3z',heart:'M12 21L3 12C-2 4 8 0 12 7c4-7 14-3 9 5z',
 volume:'M3 9h4l5-4v14l-5-4H3z M16 8a6 6 0 0 1 0 8 M19 5a10 10 0 0 1 0 14',
 battery:'M2 6h18v12H2z M23 10v4 M5 9h12v6H5z',wifi:'M2 8a17 17 0 0 1 20 0 M6 12a11 11 0 0 1 12 0 M10 16a4 4 0 0 1 4 0 M12 20h.01',
 bluetooth:'M7 7l10 10-5 4V3l5 4L7 17',sun:'M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8 M12 1v3 M12 20v3 M1 12h3 M20 12h3 M4 4l2 2 M18 18l2 2 M4 20l2-2 M18 6l2-2',shuffle:'M3 5h3l12 14h3 M17 15l4 4-4 4 M3 19h3l4-5 M14 9l4-4h3 M17 1l4 4-4 4'};
const nativeIcons={music:'ui3-music',stream:'ui3-radio',network:'ui3-globe',book:'ui3-book',settings:'ui3-sliders',homeMore:'ui3-grid',more:'ellipsis-vertical',play:'ui3-play',pause:'ui3-pause',next:'ui3-skipf',previous:'ui3-skipb',shuffle:'shuffle',heart:'heart-outline',heartFilled:'heart',quickSettings:'settings-2',wifi:'wifi',bluetooth:'bluetooth',airplay:'airplay-status',mseb:'mseb',eq:'equalizer',fade:'fade-track',gain:'low-gain',lineout:'lineout',sun:'sun'};
function icon(name){if(nativeIcons[name])return `<span class="ico native-icon" style="--icon:url('assets/native/${nativeIcons[name]}.svg')" aria-hidden="true"></span>`;return `<svg class="ico" viewBox="0 0 24 24" aria-hidden="true"><path d="${paths[name]||paths.music}"/></svg>`;}
const art=()=>`assets/${covers[state.cover].image}`;
const currentTitle=()=>state.sample==='long'?'Когда закончится эта бесконечная ночь — концертная версия':'Quiet Structures';
const currentArtist=()=>state.sample==='long'?'Очень длинное имя исполнителя и приглашённые музыканты':'Alpha';
function placeholder(index=0,large=false){
 return `<div class="${large?'disc':'thumb'} placeholder" style="--coverBg:var(--placeholder${index});--coverInk:var(--placeholderInk${index})" role="img" aria-label="Обложка отсутствует">${icon('music')}</div>`;
}
function artwork(large=false){return state.sample==='missing'?placeholder(0,large):`<img class="${large?'disc':'thumb'}" src="${art()}" alt="${large?'Демонстрационная обложка':''}">`;}
function action(name,label,{primary=false,checked=null}={}){
 return `<button type="button" class="icon-button ${primary?'play primary-action':''} ${checked?'is-selected':''}" data-action="${primary?'play':name==='heartFilled'?'heart':name}" aria-label="${label}" ${checked===null?'':`aria-pressed="${checked}"`} ${state.uiState==='disabled'?'disabled':''}>${icon(name)}</button>`;
}
function status(){return `<div class="status ${state.page==='shade'?'surface-scope':''}"><span>${icon('volume')}40</span><span>18:46</span><span>78% ${icon('battery')}</span></div>`;}
function mini(){return `<div class="mini surface-scope">${artwork()}<div class="copy"><b title="${currentTitle()}">${currentTitle()}</b><small title="${currentArtist()}">${currentArtist()}</small></div>${action(state.playing?'pause':'play',state.playing?'Пауза':'Воспроизвести',{primary:true})}</div>`;}
function head(title,search=false){return `<div class="head">${action('back','Назад')}<h3>${title}</h3>${search?action('search','Поиск'):''}</div>`;}
function home(){return `<div class="body home-grid">${[['music','Музыка','1 248 треков'],['stream','Стриминг','2 сервиса'],['network','Сеть','Wi-Fi · Передача файлов'],['book','Аудиокниги','3 книги'],['settings','Настройки',''],['homeMore','Ещё','']].map(([i,t,s],n)=>`<div class="tile" style="background:var(--tile${n});color:var(--tileInk${n});--tileMuted:var(--tileMuted${n});--tileIcon:var(--tileIcon${n})"><span>${icon(i)}</span><div class="tile-copy"><b>${t}</b><small>${s||'&nbsp;'}</small></div></div>`).join('')}</div>${mini()}`;}
function list(){
 const tracks=[[currentTitle(),currentArtist(),'4:32'],['Open Water','North','3:48'],['Тёплый свет','Север','5:06'],['Evening Lines','Alpha','3:21'],['Невесомость','Север','4:18']];
 return `${head('Треки',true)}<div class="body"><div class="group">Все треки · 1 248</div><div class="track-list list-scope">${tracks.map(([title,artist,time],i)=>`<div class="row ${i===0?'now':''}" ${i===0&&state.uiState==='disabled'?'aria-disabled="true"':''}>${i===0?artwork():placeholder(i-1)}<div class="copy"><b title="${title}">${title}</b><small title="${artist}">${artist}</small></div><span class="duration">${time}</span></div>`).join('')}</div></div>${mini()}`;
}
function player(){return `<div class="head">${action('down','Свернуть плеер')}<div class="next"><div><small>Далее</small><b>${state.sample==='long'?'Следующая композиция — полная концертная версия':'Open Water'}</b></div>${icon('chevron')}</div></div>${artwork(true)}<h3 class="track-title" title="${currentTitle()}">${currentTitle()}</h3><p class="artist" title="${currentArtist()}">${currentArtist()}</p><div class="seek"><i></i></div><div class="times"><span>1:46</span><span>4:32</span></div><div class="transport">${action('shuffle','Перемешать')}${action('previous','Предыдущий трек')}${action(state.playing?'pause':'play',state.playing?'Пауза':'Воспроизвести',{primary:true})}${action('next','Следующий трек')}${action('queue','Очередь')}</div><div class="bottom-actions"><span>FLAC · 44,1 кГц</span>${action(state.liked?'heartFilled':'heart',state.liked?'Убрать из избранного':'В избранное',{checked:state.liked})}${action('more','Меню трека')}</div>`;}
function settings(theme){return `${head('Внешний вид')}<div class="body settings-body"><div class="card surface-scope"><div class="card-line">Палитра ${icon('chevron')}</div><p class="value-accent">${palettes[theme.palette].name}</p></div><div class="card surface-scope"><div class="card-line">Тема</div><div class="segment"><span class="${theme.theme==='light'?'selected primary-action':''}">Светлая</span><span class="${theme.theme==='dark'?'selected primary-action':''}">Тёмная</span></div></div><div class="card surface-scope"><div class="card-line"><span>Primary-цвет</span><span class="primary-sample" aria-hidden="true"></span></div><p>${document.querySelector('#primary').selectedOptions[0].textContent}</p></div><div class="card surface-scope"><div class="card-line"><span>Цветные заглушки</span><button type="button" class="switch ${state.forceColorPlaceholders?'on primary-action':''}" role="switch" aria-checked="${state.forceColorPlaceholders}" aria-label="Цветные заглушки" data-action="placeholders" ${state.uiState==='disabled'?'disabled':''}><i></i></button></div><p>Принудительно сохранять цвет<br>в любой палитре.</p></div><div class="card surface-scope"><div class="card-line"><span>Окраска от обложки</span><button type="button" class="switch ${state.tint?'on primary-action':''}" role="switch" aria-checked="${state.tint}" aria-label="Окраска от обложки" data-action="tint" ${state.uiState==='disabled'?'disabled':''}><i></i></button></div><p>Тон обложки на поверхностях.<br>Primary выбирается отдельно.</p></div></div>${mini()}`;}
function shade(){
 const buttons=[['wifi','Wi-Fi','Подключён','Выключен'],['bluetooth','Bluetooth','Наушники','Выключен'],['airplay','AirPlay','Включён','Выключен'],['mseb','MSEB','Включён','Выключен'],['eq','Эквалайзер','Body Voice','Выключен'],['fade','Плавный','переход','переход'],['gain','Усиление','Высокое','Низкое'],['lineout','Линейный','выход','выход']];
 return `<div class="underlay">${home()}</div><div class="shade-panel surface-scope"><div class="quickgrid">${buttons.map(([i,t,onText,offText])=>`<div class="quick-cell"><button type="button" class="quick-circle ${state.quick[i]?'active primary-action':''}" data-action="quick-${i}" aria-label="${t}" aria-pressed="${state.quick[i]}" ${state.uiState==='disabled'?'disabled':''}>${icon(i)}</button><div class="quick-label">${t}<small>${state.quick[i]?onText:offText}</small></div></div>`).join('')}</div><div class="brightness">${icon('sun')}<div class="brightness-track"><i></i></div><span class="settings-entry">${icon('quickSettings')}</span></div><div class="expand-area"><div class="handle"></div></div></div>`;
}
const pages={home:'Главная',list:'Список',player:'Плеер',settings:'Настройки',shade:'Шторка'};
function render(){
 const selected=document.activeElement;
 const focusedVariant=selected&&selected.closest('.variant');
 const focus=focusedVariant&&selected.dataset.action?{palette:focusedVariant.dataset.palette,theme:focusedVariant.dataset.theme,action:selected.dataset.action}:null;
 const scrolls=new Map([...document.querySelectorAll('.variant')].map(v=>[v.dataset.palette+v.dataset.theme,v.querySelector('.settings-body')?.scrollTop||0]));
 document.querySelector('#comparison').dataset.single=String(state.visible.length===1);
 document.querySelectorAll('[data-page]').forEach(b=>b.setAttribute('aria-pressed',String(state.page===b.dataset.page)));
 document.querySelectorAll('[data-pick]').forEach(b=>b.setAttribute('aria-pressed',String(state.visible.includes(b.dataset.pick))));
 document.querySelector('#comparison').innerHTML=state.visible.flatMap(palette=>['light','dark'].map(theme=>{
   const config={...state,palette,theme}, t=compute(config), title=`${palettes[palette].name} · ${theme==='dark'?'тёмная':'светлая'}`;
   const vars=Object.entries(t).filter(([k])=>k!=='ratios').map(([k,v])=>`--${k}:${v}`).join(';');
   const screen=state.page==='home'?home():state.page==='list'?list():state.page==='player'?player():state.page==='settings'?settings(config):shade();
   const caption=palette==='lcd'?(theme==='light'?'LCD · обычный':'LCD · инвертированный'):(theme==='dark'?'Тёмная тема':'Светлая тема');
   return `<article class="variant" data-palette="${palette}" data-theme="${theme}"><h2>${title}</h2><div class="caption">${caption} · ${state.tint?'окраска от обложки':'базовые цвета'}</div><div class="frame"><div class="device screen-${state.page}" data-mono-art="${state.monoArt}" data-force-placeholders="${state.forceColorPlaceholders}" data-ui-state="${state.uiState}" style="${vars}" aria-label="${title}: ${pages[state.page]}">${status()}${screen}</div></div><div class="swatches" aria-hidden="true">${['bg','surface','press','text','primary','secondary'].map(k=>`<span style="background:${t[k]}"></span>`).join('')}</div><details><summary>Цвета и контраст</summary><div class="metrics">Фон ${t.bg} · карточка ${t.surface}<br>Primary ${t.primary}<br>Текст ${t.ratios.text.toFixed(1)}:1 · вторичный ${t.ratios.muted.toFixed(1)}:1<br>На кнопке ${t.ratios.button.toFixed(1)}:1</div></details></article>`;
 })).join('');
 document.querySelectorAll('.variant').forEach(v=>{const scroller=v.querySelector('.settings-body');if(scroller)scroller.scrollTop=scrolls.get(v.dataset.palette+v.dataset.theme)||0;});
 if(focus){const target=document.querySelector(`.variant[data-palette="${focus.palette}"][data-theme="${focus.theme}"] [data-action="${focus.action}"]`);if(target&&!target.disabled)target.focus({preventScroll:true});}
}
for(const id of ['primary','cover','tint','backlight','monoArt','forceColorPlaceholders','uiState','sample'])document.getElementById(id).addEventListener('change',e=>{state[id]=['tint','monoArt','forceColorPlaceholders'].includes(id)?e.target.checked:e.target.value;render();});
document.querySelector('#pages').innerHTML=Object.entries(pages).map(([id,name])=>`<button data-page="${id}" aria-pressed="false">${name}</button>`).join('');
document.querySelector('#palette-picks').innerHTML=Object.entries(palettes).map(([id,p])=>`<button data-pick="${id}" aria-pressed="true">${p.name}</button>`).join('');
document.querySelector('#pages').addEventListener('click',e=>{const b=e.target.closest('[data-page]');if(b){state.page=b.dataset.page;render();}});
document.querySelector('#palette-picks').addEventListener('click',e=>{const b=e.target.closest('[data-pick]');if(!b)return;const id=b.dataset.pick;if(state.visible.includes(id)){if(state.visible.length===1)return;state.visible=state.visible.filter(v=>v!==id);}else state.visible=Object.keys(palettes).filter(v=>v===id||state.visible.includes(v));render();});
document.querySelector('#comparison').addEventListener('click',e=>{
 const button=e.target.closest('button[data-action]');if(!button||button.disabled)return;
 const action=button.dataset.action;
 if(action==='play')state.playing=!state.playing;
 else if(action==='heart')state.liked=!state.liked;
 else if(action==='placeholders'){state.forceColorPlaceholders=!state.forceColorPlaceholders;document.querySelector('#forceColorPlaceholders').checked=state.forceColorPlaceholders;}
 else if(action==='tint'){state.tint=!state.tint;document.querySelector('#tint').checked=state.tint;}
 else if(action.startsWith('quick-')){const key=action.slice(6);state.quick[key]=!state.quick[key];}
 else return;
 render();
});
render();
