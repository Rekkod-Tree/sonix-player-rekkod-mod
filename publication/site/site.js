/* Presentation-only controls. No connection to the player or service accounts. */
const $=s=>document.querySelector(s);
const all=s=>[...document.querySelectorAll(s)];
const escapeHTML=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const normalize=s=>s.toLocaleLowerCase('ru').replaceAll('ё','е').trim();

const rows=all('.feature');
const groups=all('.catalog-group');
const searchable=new Map(rows.map(row=>[row,normalize(row.textContent)]));
function filterFeatures(){
  const words=normalize($('#feature-search').value).split(/\s+/).filter(Boolean);
  const category=$('#category-filter').value,origin=$('#origin-filter').value,status=$('#status-filter').value;
  const filtering=words.length>0||category!=='all'||origin!=='all'||status!=='all';
  let found=0;
  for(const row of rows){
    const show=(category==='all'||row.dataset.category===category)&&(origin==='all'||row.dataset.origin===origin)&&(status==='all'||row.dataset.status===status)&&words.every(word=>searchable.get(row).includes(word));
    row.hidden=!show;if(show)found++;
  }
  $('#feature-count').textContent=`Найдено ${found} из ${rows.length}`;
  $('#empty-results').hidden=found!==0;
  for(const group of groups){
    const count=[...group.querySelectorAll('.feature')].filter(row=>!row.hidden).length;
    group.hidden=count===0;group.querySelector('.group-count').textContent=`${count} пунктов`;
    if(filtering&&count)group.open=true;
  }
  const expanded=rows.filter(row=>!row.hidden).every(row=>row.open);
  $('#expand-features').textContent=found&&expanded?'Свернуть найденные':'Развернуть найденные';
}
function resetFilters(){
  $('#feature-search').value='';
  for(const id of ['category-filter','origin-filter','status-filter'])$('#'+id).value='all';
  for(const group of groups)group.open=false;
  filterFeatures();
}
$('#feature-search').addEventListener('input',filterFeatures);
for(const id of ['category-filter','origin-filter','status-filter'])$('#'+id).addEventListener('change',filterFeatures);
$('#reset-filters').addEventListener('click',resetFilters);
$('#empty-reset').addEventListener('click',()=>{resetFilters();$('#feature-search').focus();});
$('#expand-features').addEventListener('click',()=>{
  const visible=rows.filter(row=>!row.hidden);const open=!visible.every(row=>row.open);
  for(const group of groups)if(!group.hidden)group.open=open;
  for(const row of visible)row.open=open;
  $('#expand-features').textContent=open?'Свернуть найденные':'Развернуть найденные';
});
function revealHash(){
  if(!location.hash.startsWith('#feature-'))return;
  const target=document.getElementById(location.hash.slice(1));
  if(target?.classList.contains('feature')){resetFilters();target.closest('.catalog-group').open=true;target.open=true;requestAnimationFrame(()=>target.scrollIntoView({block:'start',behavior:'instant'}));}
}
window.addEventListener('hashchange',revealHash);
filterFeatures();revealHash();

const gestureSets={
  list:[['→','Вправо — играть следующим','Начните внутри строки трека. Явное действие «следующим» не зависит от настройки добавления по умолчанию.'],['←','Влево — избранное','Добавить или убрать лайк там, где источник поддерживает избранное.'],['···','Удержание — меню','В том числе добавление в конец очереди.']],
  albums:[['→','Вправо — альбом следующим','В поддерживаемом списке альбомов в очередь добавляются его треки.'],['←','Влево — альбом в конец','Это правило строки альбома. Для плиток главной, исполнителей и книг такой универсальный жест не заявляется.']],
  mini:[['↑','Вверх или тап — полный плеер','Тапните по названию/обложке либо проведите вверх. Кнопка паузы действует отдельно.'],['←','Влево — следующий трек','Здесь действие отличается от свайпа по строке списка.'],['→','Вправо — предыдущий трек','В компактном виде привычные направления сохраняются.']],
  player:[['↓','Вниз — вернуться на предыдущую страницу','Потяните обложку или свободную область вниз. Музыка продолжает играть.'],['↑','Снизу вверх — на главную','Начните в нижней области, в том числе на транспортных кнопках. Это выход к разделам плеера без остановки музыки.'],['↔','По обложке — сменить трек','Влево — следующий, вправо — предыдущий. Для книги действует отдельное правило глав.'],['Ⅱ','Тап по обложке — пауза или Play','Промах рядом с полосой прогресса не должен ставить воспроизведение на паузу.']],
  book:[['↔','По обложке — переключить главу','Свайпы выбирают части книги. Кнопки транспорта остаются перемоткой на настроенный интервал.'],['···','Удержать «Продолжить книгу» — детали','Показать сохранённую главу и время. Обычный тап продолжает прослушивание.']],
  queue:[['←','Влево — удалить предстоящий трек','После удаления доступна отмена. Текущий трек не переставляется.'],['↕','За ручку — изменить порядок','Перетаскивайте предстоящие треки в очереди. Это отдельное действие от горизонтального свайпа.']],
  navigation:[['→','От левого края — Назад','Начните у самого края. Внутри строки вправо выполняется действие со строкой.'],['↓','От верхнего края — шторка','Быстрые настройки и общий таймер сна доступны поверх текущей страницы.'],['···','Удержать плитку — настроить главную','В редакторе меняйте порядок за ручку; свайп влево убирает ярлык с возможностью отмены.'],['↑','В «Музыке» — карусель альбомов','Если карусель включена, проведите вверх от нижней области. Закрытие — за верхнюю ручку вниз.']]
};
function renderGestures(){
  $('#gesture-cards').innerHTML=gestureSets[$('#gesture-context').value].map(([arrow,title,body])=>`<article class="gesture-card"><span class="gesture-arrow" aria-hidden="true">${escapeHTML(arrow)}</span><div><h3>${escapeHTML(title)}</h3><p>${escapeHTML(body)}</p></div></article>`).join('');
}
$('#gesture-context').addEventListener('change',renderGestures);renderGestures();

const upgradeNotes={
  sonix:['Полный дифф относительно Sonix 1.3.0: собственный интерфейс, Яндекс Музыка, Звук, OpenSubsonic и офлайн-книги.','Свои главная, палитры, жесты и доработки звука, очереди, карты и фоновой работы.','В каталоге отдельно обозначены собственные изменения и функции Sonix, перенесённые с адаптацией.'],
  alpha10:['После alpha10: настраиваемая главная, продолжение книги и быстрые действия Яндекса.','Офлайн-книги Яндекса/Звука, подкасты, названия ленивых треков и общий таймер сна.','Дополнительные палитры, большие плейлисты, новая системная база и текущая доводка v1.4.0.'],
  alpha20:['После alpha20: восемь дополнительных палитр и независимые настройки обложек и заглушек.','Быстрое продолжение книги, свайпы глав, крупные плейлисты и сохранение места в каталогах.','К v1.4.0: компактный мини-плеер, доработки AutoEQ, EQ, поиска, OTA и фоновой работы. Промежуточные alpha не считаются релизами.']
};
function renderUpgrade(){const selected=$('#upgrade-origin')?.value;if(selected)$('#upgrade-notes').innerHTML=upgradeNotes[selected].map(text=>`<li>${escapeHTML(text)}</li>`).join('');}
$('#upgrade-origin')?.addEventListener('change',renderUpgrade);renderUpgrade();

const rgbHex=hex=>[1,3,5].map(i=>parseInt(hex.slice(i,i+2),16)/255);
const toHex=rgb=>'#'+rgb.map(c=>Math.round(c*255).toString(16).padStart(2,'0')).join('');
const mixHex=(a,b,t)=>toHex(rgbHex(a).map((v,i)=>v*(1-t)+rgbHex(b)[i]*t));
function renderPalette(){
  const form=$('#palette-form'),data=new FormData(form),theme=data.get('theme'),family=data.get('palette'),dark=theme==='dark';
  const options={palette:family==='graphite'?'mono':family,theme,primary:data.get('primary'),tint:data.has('tint'),cover:data.get('cover'),backlight:data.get('backlight'),forceColorPlaceholders:data.has('forceColorPlaceholders')};
  const t=PalettePreview.compute(family==='graphite'?{...options,forceColorPlaceholders:true}:options);
  if(family==='graphite'){
    Object.assign(t,dark?{bg:'#11151a',surface:'#202a32',press:'#2e3b45',text:'#edf3f7',muted:'#b0bec9',line:'#42505c'}:{bg:'#e5e9ec',surface:'#f6f8fa',press:'#d0dce3',text:'#182832',muted:'#465d6b',line:'#b9c8d1'});
    if(options.tint){const cover={teal:'#328e91',amber:'#b57c36',rose:'#b83f75'}[options.cover];t.bg=mixHex(t.bg,cover,dark?.15:.11);t.surface=mixHex(t.surface,cover,dark?.15:.11);}
    if(options.primary==='palette'){t.primary=dark?'#b5d7ea':'#315a72';t.onPrimary=dark?'#132832':'#ffffff';}
  }
  const map={bg:'bg',surface:'surface',text:'text',muted:'muted',primary:'primary','on-primary':'onPrimary',line:'line',press:'press'};
  const specimen=$('#color-specimen');for(const [css,key] of Object.entries(map))specimen.style.setProperty('--d-'+css,t[key]);
  const familyName=family==='graphite'?'Графит 2':PalettePreview.palettes[family].name;
  $('#specimen-name').textContent=familyName;$('#specimen-theme').textContent=dark?'Тёмная тема':'Светлая тема';
  $('#lamp-label').hidden=family!=='lcd';
  all('[data-placeholder]').forEach(el=>{
    const i=el.dataset.placeholder;let bg=t['placeholder'+i],ink=t['placeholderInk'+i];
    if(data.has('monoCovers')&&!options.forceColorPlaceholders){const rgb=rgbHex(bg);const y=.2126*rgb[0]+.7152*rgb[1]+.0722*rgb[2];bg=toHex([y,y,y]);ink=y>.46?'#000000':'#ffffff';}
    el.style.background=bg;el.style.color=ink;
  });
  $('#palette-swatches').innerHTML=[['Фон',t.bg],['Поверхность',t.surface],['Текст',t.text],['Акцент',t.primary]].map(([role,color])=>`<span class="swatch" style="background:${color}" title="${role}: ${color}" aria-label="${role}: ${color}"></span>`).join('');
}
$('#palette-form').addEventListener('change',renderPalette);$('#palette-form').addEventListener('submit',event=>event.preventDefault());renderPalette();

const dialog=$('#image-dialog');let imageTrigger=null;
all('[data-image]').forEach(button=>button.addEventListener('click',()=>{
  imageTrigger=button;$('#image-date').textContent='Реальный HiBy R1 · '+button.dataset.date;$('#large-image').src=button.dataset.image;$('#large-image').alt=button.dataset.caption+' — реальный HiBy R1';$('#image-dialog-title').textContent=button.dataset.caption;
  dialog.showModal();
}));
$('#close-image').addEventListener('click',()=>dialog.close());
dialog.addEventListener('close',()=>imageTrigger?.focus({preventScroll:true}));

all('[data-copy]').forEach(button=>button.addEventListener('click',async()=>{
  const text=button.dataset.copy;let copied=false;
  try{if(navigator.clipboard&&window.isSecureContext){await navigator.clipboard.writeText(text);copied=true;}}
  catch{/* Fall back to a user-selectable field on plain HTTP. */}
  if(copied){$('#copy-status').textContent='Скопировано.';return;}
  let field=button.parentElement.querySelector('.copy-fallback');
  if(!field){field=document.createElement('textarea');field.className='copy-fallback';field.readOnly=true;field.setAttribute('aria-label','Текст для ручного копирования');button.after(field);}
  field.value=text;field.focus();field.select();$('#copy-status').textContent='Выделили текст. Нажмите Ctrl+C или используйте «Копировать» на телефоне.';
}));
