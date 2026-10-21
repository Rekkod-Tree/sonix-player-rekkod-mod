const {palettes,covers,compute}=PalettePreview;
const state={primary:'palette',tint:false,cover:'teal',page:'home',backlight:'olive',monoArt:false,visible:Object.keys(palettes)};
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
const nativeIcons={music:'ui3-music',stream:'ui3-radio',network:'ui3-globe',book:'ui3-book',settings:'ui3-sliders',more:'ui3-grid',wifi:'wifi',bluetooth:'bluetooth',airplay:'airplay-status',mseb:'mseb',eq:'equalizer',fade:'fade-track',gain:'low-gain',lineout:'lineout',sun:'sun'};
function icon(name){if(nativeIcons[name])return `<span class="ico native-icon" style="--icon:url('assets/native/${nativeIcons[name]}.svg')" aria-hidden="true"></span>`;return `<svg class="ico" viewBox="0 0 24 24" aria-hidden="true"><path d="${paths[name]||paths.music}"/></svg>`;}
const art=()=>`assets/${covers[state.cover].image}`;
const thumb=()=>`<img class="thumb" src="${art()}" alt="">`;
function status(){return `<div class="status"><span>${icon('volume')}40</span><span>18:46</span><span>78% ${icon('battery')}</span></div>`;}
function mini(){return `<div class="mini">${thumb()}<div class="copy"><b>Quiet Structures</b><small>Alpha</small></div><div class="play">${icon('pause')}</div></div>`;}
function head(title){return `<div class="head">${icon('back')}<h3>${title}</h3>${icon('search')}</div>`;}
function home(){return `<div class="body home-grid">${[['music','Музыка','1 248 треков'],['stream','Стриминг','2 сервиса'],['network','Сеть','Wi-Fi · Передача файлов'],['book','Аудиокниги','3 книги'],['settings','Настройки',''],['more','Ещё','']].map(([i,t,s],n)=>`<div class="tile" style="background:var(--tile${n});color:var(--tileInk${n})"><span>${icon(i)}</span><div class="tile-copy"><b>${t}</b><small>${s||'&nbsp;'}</small></div></div>`).join('')}</div>${mini()}`;}
function list(){return `${head('Треки')}<div class="body"><div class="group">Все треки · 1 248</div>${[['Quiet Structures','Alpha','4:32'],['Open Water','North','3:48'],['Тёплый свет','Север','5:06'],['Evening Lines','Alpha','3:21'],['Невесомость','Север','4:18']].map(([title,artist,time],i)=>`<div class="row ${i===0?'now':''}">${i===0?thumb():`<div class="thumb placeholder" style="--coverBg:${['','#866329','#36715d','#4e5f8a','#884e71'][i]}">${icon('music')}</div>`}<div class="copy"><b>${title}</b><small>${artist}</small></div><span class="duration">${time}</span></div>`).join('')}</div>${mini()}`;}
function player(){return `<div class="head">${icon('down')}<div class="next"><div><small>Далее</small>Open Water</div>${icon('chevron')}</div></div><img class="disc" src="${art()}" alt="Демонстрационная обложка"><h3 class="track-title">Quiet Structures</h3><p class="artist">Alpha</p><div class="seek"><i></i></div><div class="times"><span>1:46</span><span>4:32</span></div><div class="transport">${icon('shuffle')}${icon('previous')}<div class="play">${icon('pause')}</div>${icon('next')}${icon('queue')}</div><div class="bottom-actions"><span>FLAC · 44,1 кГц</span>${icon('heart')}${icon('more')}</div>`;}
function settings(theme){return `${head('Внешний вид')}<div class="body settings-body"><div class="card"><div class="card-line">Палитра ${icon('chevron')}</div><p style="color:var(--primaryText)">${palettes[theme.palette].name}</p></div><div class="card"><div class="card-line">Тема</div><div class="segment"><span class="${theme.theme==='light'?'selected':''}">Светлая</span><span class="${theme.theme==='dark'?'selected':''}">Тёмная</span></div></div><div class="card"><div class="card-line"><span>Primary-цвет</span><span style="background:var(--primary);width:32px;height:32px;border-radius:50%"></span></div><p>${document.querySelector('#primary').selectedOptions[0].textContent}</p></div><div class="card"><div class="card-line"><span>Окраска от обложки</span><span class="switch ${state.tint?'on':''}"><i></i></span></div><p>Лёгкий тон на фоне и карточках.<br>Primary выбирается отдельно.</p></div></div>${mini()}`;}
function shade(){
 const buttons=[['wifi','Wi-Fi','Подключён',true],['bluetooth','Bluetooth','Наушники',true],['airplay','AirPlay','Выключен',false],['mseb','MSEB','Включён',true],['eq','Эквалайзер','Body Voice',true],['fade','Плавный','переход',false],['gain','Усиление','Низкое',false],['lineout','Линейный','выход',false]];
 return `<div class="underlay">${home()}</div><div class="shade-panel"><div class="quickgrid">${buttons.map(([i,t,sub,on])=>`<div class="quick-cell"><div class="quick-circle ${on?'active':''}">${icon(i)}</div><div class="quick-label">${t}<small>${sub}</small></div></div>`).join('')}</div><div class="brightness">${icon('sun')}<div class="brightness-track"><i></i></div><span class="settings-entry">${icon('settings')}</span></div><div class="expand-area"><div class="handle"></div></div></div>`;
}
const pages={home:'Главная',list:'Список',player:'Плеер',settings:'Настройки',shade:'Шторка'};
function render(){
 document.querySelector('#comparison').dataset.single=String(state.visible.length===1);
 document.querySelectorAll('[data-page]').forEach(b=>b.setAttribute('aria-pressed',String(state.page===b.dataset.page)));
 document.querySelectorAll('[data-pick]').forEach(b=>b.setAttribute('aria-pressed',String(state.visible.includes(b.dataset.pick))));
 document.querySelector('#comparison').innerHTML=state.visible.flatMap(palette=>['light','dark'].map(theme=>{
   const config={...state,palette,theme}, t=compute(config), title=`${palettes[palette].name} · ${theme==='dark'?'тёмная':'светлая'}`;
   const vars=Object.entries(t).filter(([k])=>k!=='ratios').map(([k,v])=>`--${k}:${v}`).join(';');
   const screen=state.page==='home'?home():state.page==='list'?list():state.page==='player'?player():state.page==='settings'?settings(config):shade();
   const caption=palette==='lcd'?(theme==='light'?'LCD · обычный':'LCD · инвертированный'):(theme==='dark'?'Тёмная тема':'Светлая тема');
   return `<article class="variant" data-palette="${palette}" data-theme="${theme}"><h2>${title}</h2><div class="caption">${caption} · ${state.tint?'окраска от обложки':'базовые цвета'}</div><div class="frame"><div class="device screen-${state.page}" data-mono-art="${state.monoArt}" style="${vars}" aria-label="${title}: ${pages[state.page]}">${status()}${screen}</div></div><div class="swatches" aria-hidden="true">${['bg','surface','press','text','primary','secondary'].map(k=>`<span style="background:${t[k]}"></span>`).join('')}</div><details><summary>Цвета и контраст</summary><div class="metrics">Фон ${t.bg} · карточка ${t.surface}<br>Primary ${t.primary}<br>Текст ${t.ratios.text.toFixed(1)}:1 · вторичный ${t.ratios.muted.toFixed(1)}:1<br>На кнопке ${t.ratios.button.toFixed(1)}:1</div></details></article>`;
 })).join('');
}
for(const id of ['primary','cover','tint','backlight','monoArt'])document.getElementById(id).addEventListener('change',e=>{state[id]=['tint','monoArt'].includes(id)?e.target.checked:e.target.value;render();});
document.querySelector('#pages').innerHTML=Object.entries(pages).map(([id,name])=>`<button data-page="${id}" aria-pressed="false">${name}</button>`).join('');
document.querySelector('#palette-picks').innerHTML=Object.entries(palettes).map(([id,p])=>`<button data-pick="${id}" aria-pressed="true">${p.name}</button>`).join('');
document.querySelector('#pages').addEventListener('click',e=>{const b=e.target.closest('[data-page]');if(b){state.page=b.dataset.page;render();}});
document.querySelector('#palette-picks').addEventListener('click',e=>{const b=e.target.closest('[data-pick]');if(!b)return;const id=b.dataset.pick;if(state.visible.includes(id)){if(state.visible.length===1)return;state.visible=state.visible.filter(v=>v!==id);}else state.visible=Object.keys(palettes).filter(v=>v===id||state.visible.includes(v));render();});
render();
