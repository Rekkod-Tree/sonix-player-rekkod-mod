const {palettes,covers,compute}=PalettePreview;
const state={primary:'blue',tint:false,cover:'teal',page:'home'};
const paths={music:'M9 18V5l12-2v13 M9 9l12-2 M9 18a3 3 0 1 1-3-3h3 M21 16a3 3 0 1 1-3-3h3',
 stream:'M5 18a10 10 0 0 1 0-12 M9 15a6 6 0 0 1 0-6 M19 6a10 10 0 0 1 0 12 M15 9a6 6 0 0 1 0 6 M12 12h.01',
 book:'M3 4h7l2 2 2-2h7v15h-7l-2 2-2-2H3z M12 6v15',folder:'M3 5h7l2 3h9v12H3z',
 settings:'M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8 M10 2h4l1 3 3 2 3 0 2 4-2 2-1 3 0 3-4 2-2-2-3-1-3 0-2-4 2-2 1-3-1-3 4-2z',
 more:'M5 12h.01 M12 12h.01 M19 12h.01',search:'M10 3a7 7 0 1 0 0 14 7 7 0 0 0 0-14 M15 15l6 6',
 back:'M15 4l-8 8 8 8',chevron:'M9 5l7 7-7 7',down:'M5 9l7 7 7-7',play:'M8 5l11 7-11 7z',pause:'M8 5v14 M16 5v14',
 next:'M5 5l11 7-11 7z M20 5v14',previous:'M19 5L8 12l11 7z M4 5v14',
 queue:'M3 5h18 M3 11h18 M3 17h10 M18 15l4 3-4 3z',heart:'M12 21L3 12C-2 4 8 0 12 7c4-7 14-3 9 5z',
 volume:'M3 9h4l5-4v14l-5-4H3z M16 8a6 6 0 0 1 0 8 M19 5a10 10 0 0 1 0 14',
 battery:'M2 6h18v12H2z M23 10v4 M5 9h12v6H5z',wifi:'M2 8a17 17 0 0 1 20 0 M6 12a11 11 0 0 1 12 0 M10 16a4 4 0 0 1 4 0 M12 20h.01',
 bluetooth:'M7 7l10 10-5 4V3l5 4L7 17',sun:'M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8 M12 1v3 M12 20v3 M1 12h3 M20 12h3 M4 4l2 2 M18 18l2 2 M4 20l2-2 M18 6l2-2',shuffle:'M3 5h3l12 14h3 M17 15l4 4-4 4 M3 19h3l4-5 M14 9l4-4h3 M17 1l4 4-4 4'};
function icon(name){return `<svg class="ico" viewBox="0 0 24 24" aria-hidden="true"><path d="${paths[name]||paths.music}"/></svg>`;}
const art=()=>`assets/${covers[state.cover].image}`;
const thumb=()=>`<img class="thumb" src="${art()}" alt="">`;
function status(){return `<div class="status"><span>${icon('volume')}40</span><span>18:46</span><span>78% ${icon('battery')}</span></div>`;}
function mini(){return `<div class="mini">${thumb()}<div class="copy"><b>Quiet Structures</b><small>Alpha</small></div><div class="play">${icon('pause')}</div></div>`;}
function head(title){return `<div class="head">${icon('back')}<h3>${title}</h3>${icon('search')}</div>`;}
function home(){return `<div class="body home">${[['music','Музыка','1 248 треков'],['stream','Стриминг','Яндекс Музыка · Звук'],['book','Аудиокниги','Продолжить слушать'],['folder','Папки','Карта памяти'],['settings','Настройки',''],['more','Ещё','']].map(([i,t,s])=>`<div class="tile"><span>${icon(i)}</span><div class="tile-copy"><b>${t}</b><small>${s||'&nbsp;'}</small></div></div>`).join('')}</div>${mini()}`;}
function list(){return `${head('Треки')}<div class="body"><div class="group">Все треки · 1 248</div>${[['Quiet Structures','Alpha','4:32'],['Open Water','North','3:48'],['Тёплый свет','Север','5:06'],['Evening Lines','Alpha','3:21'],['Невесомость','Север','4:18']].map(([title,artist,time],i)=>`<div class="row ${i===0?'now':''}">${i===0?thumb():`<div class="thumb placeholder" style="--coverBg:${['','#866329','#36715d','#4e5f8a','#884e71'][i]}">${icon('music')}</div>`}<div class="copy"><b>${title}</b><small>${artist}</small></div><span class="duration">${time}</span></div>`).join('')}</div>${mini()}`;}
function player(){return `<div class="head">${icon('down')}<div class="next"><div><small>Далее</small>Open Water</div>${icon('chevron')}</div></div><img class="disc" src="${art()}" alt="Демонстрационная обложка"><h3 class="track-title">Quiet Structures</h3><p class="artist">Alpha</p><div class="seek"><i></i></div><div class="times"><span>1:46</span><span>4:32</span></div><div class="transport">${icon('shuffle')}${icon('previous')}<div class="play">${icon('pause')}</div>${icon('next')}${icon('queue')}</div><div class="bottom-actions"><span>FLAC · 44,1 кГц</span>${icon('heart')}${icon('more')}</div>`;}
function settings(theme){return `${head('Внешний вид')}<div class="body settings-body"><div class="card"><div class="card-line">Палитра</div><div class="segment"><span class="${theme.palette==='mono'?'selected':''}">Монохром</span><span class="${theme.palette==='vivid'?'selected':''}">Контраст</span></div></div><div class="card"><div class="card-line">Тема</div><div class="segment"><span class="${theme.theme==='light'?'selected':''}">Светлая</span><span class="${theme.theme==='dark'?'selected':''}">Тёмная</span></div></div><div class="card"><div class="card-line"><span>Primary-цвет</span><span style="background:var(--primary);width:32px;height:32px;border-radius:50%"></span></div><p>${document.querySelector('#primary').selectedOptions[0].textContent}</p></div><div class="card"><div class="card-line"><span>Окраска от обложки</span><span class="switch ${state.tint?'on':''}"><i></i></span></div><p>Лёгкий тон на фоне и карточках.<br>Primary выбирается отдельно.</p></div></div>${mini()}`;}
function shade(){return `<div class="underlay">${home()}</div><div class="shade-panel"><div class="shade-title">Пункт управления ${icon('settings')}</div><div class="quickgrid"><div class="quick active">${icon('wifi')}<span>Wi-Fi<br>Подключён</span></div><div class="quick active">${icon('bluetooth')}<span>Bluetooth<br>Наушники</span></div><div class="quick">${icon('shuffle')}Перемешать</div><div class="quick">${icon('volume')}Аудиовыход</div></div><div class="slider"><div class="slider-fill"></div><span>${icon('sun')}<strong>52%</strong></span></div><div class="slider"><div class="slider-fill" style="right:60%"></div><span>${icon('volume')}<strong>40</strong></span></div><div class="handle"></div></div>`;}
const pages={home:'Главная',list:'Список',player:'Плеер',settings:'Настройки',shade:'Шторка'};
function render(){
 document.querySelector('#pages').innerHTML=Object.entries(pages).map(([id,name])=>`<button data-page="${id}" aria-pressed="${state.page===id}">${name}</button>`).join('');
 document.querySelector('#comparison').innerHTML=['mono','vivid'].flatMap(palette=>['light','dark'].map(theme=>{
   const config={...state,palette,theme}, t=compute(config), title=`${palettes[palette].name} · ${theme==='dark'?'тёмная':'светлая'}`;
   const vars=Object.entries(t).filter(([k])=>k!=='ratios').map(([k,v])=>`--${k}:${v}`).join(';');
   const screen=state.page==='home'?home():state.page==='list'?list():state.page==='player'?player():state.page==='settings'?settings(config):shade();
   return `<article class="variant" data-palette="${palette}" data-theme="${theme}"><h2>${title}</h2><div class="caption">${state.tint?'С окраской от обложки':'Нейтральные поверхности'} · ${pages[state.page]}</div><div class="frame"><div class="device ${state.page}" style="${vars}" aria-label="${title}: ${pages[state.page]}">${status()}${screen}</div></div><div class="swatches" aria-hidden="true">${['bg','surface','press','text','primary'].map(k=>`<span style="background:${t[k]}"></span>`).join('')}</div><details><summary>Цвета и контраст</summary><div class="metrics">Фон ${t.bg} · карточка ${t.surface}<br>Primary ${t.primary}<br>Текст ${t.ratios.text.toFixed(1)}:1 · вторичный ${t.ratios.muted.toFixed(1)}:1<br>На кнопке ${t.ratios.button.toFixed(1)}:1</div></details></article>`;
 })).join('');
}
for(const id of ['primary','cover','tint'])document.getElementById(id).addEventListener('change',e=>{state[id]=id==='tint'?e.target.checked:e.target.value;render();});
document.querySelector('#pages').addEventListener('click',e=>{const b=e.target.closest('[data-page]');if(b){state.page=b.dataset.page;render();}});
function preset(primary,tint){state.primary=primary;state.tint=tint;document.querySelector('#primary').value=primary;document.querySelector('#tint').checked=tint;render();}
document.querySelector('#strict').addEventListener('click',()=>preset('gray',false));
document.querySelector('#colorful').addEventListener('click',()=>preset('cover',true));
render();
