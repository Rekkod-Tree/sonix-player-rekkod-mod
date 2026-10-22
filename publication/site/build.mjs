import {readFileSync, writeFileSync, mkdirSync, copyFileSync, existsSync, readdirSync, statSync} from 'node:fs';
import {resolve, dirname, join, relative} from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';

const here=dirname(fileURLToPath(import.meta.url));
const repo=resolve(here,'../..');
const dist=join(here,'dist');
const data=JSON.parse(readFileSync(join(here,'content.json'),'utf8'));
const esc=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const docs='https://github.com/Rekkod-Tree/sonix-player-rekkod-mod/blob/main/';
const ids=new Set();
const origins={mod:'Доработка мода',adapted:'Адаптация Sonix'};
const states={available:'Реализовано',preparing:'Дорабатывается'};
const renderedFeatures=data.features.map(row=>{
  const [id,category,title,body,origin,status,source]=row;
  if(row.length!==7||ids.has(id)||!data.categories[category]||!origins[origin]||!states[status]||!existsSync(join(repo,source)))throw new Error(`Invalid feature: ${id}`);
  ids.add(id);
  return `<details class="feature" id="feature-${esc(id)}" data-category="${esc(category)}" data-origin="${origin}" data-status="${status}"><summary><span class="feature-category">${esc(data.categories[category])}</span><span class="feature-title">${esc(title)}</span><span class="feature-state ${status}">${states[status]}</span><span class="disclosure" aria-hidden="true">+</span></summary><div class="feature-body"><p>${esc(body)}</p><div class="feature-foot"><span>${origins[origin]}</span><a href="${docs+source}">Подробности в репозитории ↗</a><a href="#feature-${esc(id)}" aria-label="Ссылка на пункт: ${esc(title)}">Ссылка на пункт</a></div></div></details>`;
});
const features=Object.entries(data.categories).map(([category,name],index)=>{
  const entries=renderedFeatures.filter((_,i)=>data.features[i][1]===category);
  return `<details class="catalog-group" data-group="${category}"><summary><span class="group-index">${String(index+1).padStart(2,'0')}</span><span class="group-title">${esc(name)}</span><span class="group-count">${entries.length} пунктов</span><span class="disclosure" aria-hidden="true">+</span></summary><div class="group-items">${entries.join('\n')}</div></details>`;
}).join('\n');
const gallery=[
 ['r1-home.png','Главная под себя','Пять ярлыков и постоянное «Ещё».'],
 ['r1-player.png','Полный плеер','Обложка, транспорт и данные записи.'],
 ['r1-quick-panel.png','Быстрые настройки','Шторка в общей палитре интерфейса.'],
 ['r1-album.png','Страница альбома','Обложка, данные и действия с треками.'],
 ['r1-local-search.png','Светлая тема и поиск','Настоящие результаты, обложки и клавиатура.'],
 ['r1-palettes.png','Все палитры рядом','Выбор оформления на настоящем R1.'],
 ['r1-mini-compact.png','Компактный мини-плеер','Новая настройка в прогоне кандидата.'],
 ['r1-autoeq.png','Профили под наушники','Результаты поиска AutoEQ.'],
 ['r1-peq.png','PEQ и кривая отклика','Настройка обработки на устройстве.'],
 ['r1-album-list.png','Список альбомов','Цветные обложки и компактная панель.'],
 ['r1-audiobook.png','Книга и сохранённое место','Архивный кадр реального устройства.'],
 ['r1-home-editor.png','Редактор главной','Архивный кадр: порядок разделов за ручку.']
];
const imageManifest=JSON.parse(readFileSync(join(repo,'screenshots/release-site/sources.json'),'utf8'));
mkdirSync(join(dist,'assets'),{recursive:true});
const provenance=[];
for(const [file,title] of gallery){
  const entry=imageManifest.images.find(i=>i.file===file);
  const source=join(repo,'screenshots/release-site',file);
  const hash=createHash('sha256').update(readFileSync(source)).digest('hex');
  if(!entry||entry.synthetic_data!==false||hash!==entry.sha256)throw new Error(`Unverified device image: ${file}`);
  copyFileSync(source,join(dist,'assets',file));
  provenance.push({...entry,title});
}
const poster=join(repo,'screenshots/release-site/video-poster.png');
const posterEntry=imageManifest.images.find(i=>i.file==='video-poster.png');
if(createHash('sha256').update(readFileSync(poster)).digest('hex')!==posterEntry.sha256)throw new Error('Video poster hash mismatch');
copyFileSync(poster,join(dist,'assets/video-poster.png'));provenance.push(posterEntry);
copyFileSync(join(repo,'screenshots/demos/r1-graphite2-gestures-alpha19.mp4'),join(dist,'assets/gestures-r1.mp4'));
copyFileSync(join(repo,'tools/palette-preview/colors.js'),join(dist,'assets/palette-colors.js'));
for(const file of ['site.css','site.js'])copyFileSync(join(here,file),join(dist,file));
const galleryHTML=gallery.map(([file,title,caption])=>{
  const date=provenance.find(i=>i.file===file).date;
  const shownDate=date.split('-').reverse().join('.')+(date==='2026-10-22'?' · прогон кандидата':' · архив R1');
  return `<figure class="screen"><button type="button" class="screen-button" data-image="assets/${file}" data-caption="${esc(title)}" data-date="${esc(shownDate)}" aria-label="Увеличить: ${esc(title)}"><img loading="lazy" decoding="async" src="assets/${file}" width="480" height="800" alt="${esc(title)} — снимок реального HiBy R1"></button><figcaption><strong>${esc(title)}</strong><span>${esc(caption)}</span><small>${esc(shownDate)}</small></figcaption></figure>`;
}).join('\n');
let html=readFileSync(join(here,'template.html'),'utf8');
for(const [token,value] of Object.entries({FEATURES:features,COUNT:data.features.length,GALLERY:galleryHTML,CATEGORIES:Object.entries(data.categories).map(([id,name])=>`<option value="${id}">${esc(name)}</option>`).join(''),REVIEWED:esc(data.reviewed)}))html=html.replaceAll(`{{${token}}}`,String(value));
if(/\{\{\w+\}\}/.test(html))throw new Error('Unresolved template placeholder');
writeFileSync(join(dist,'index.html'),html);
writeFileSync(join(dist,'assets/sources.json'),JSON.stringify({note:'Unmodified physical R1 captures. Ten images are from recorded native candidate runs on 22 October; two gallery images and the video poster are archival. These are not captures of the final published v1.4.0 image.',images:provenance},null,2)+'\n');
const files=[];
const allowed=new Set(['index.html','site.css','site.js','manifest.json','assets/sources.json','assets/gestures-r1.mp4','assets/palette-colors.js','assets/video-poster.png',...gallery.map(([file])=>'assets/'+file)]);
function inventory(dir){for(const name of readdirSync(dir)){const path=join(dir,name);if(statSync(path).isDirectory())inventory(path);else {const file=relative(dist,path).replaceAll('\\','/');if(!allowed.has(file))throw new Error(`Unexpected output file: ${file}`);if(file!=='manifest.json')files.push({file,bytes:statSync(path).size,sha256:createHash('sha256').update(readFileSync(path)).digest('hex')});}}}
inventory(dist);
writeFileSync(join(dist,'manifest.json'),JSON.stringify({release:data.release,status:data.state,reviewed:data.reviewed,featureCount:data.features.length,files},null,2)+'\n');
console.log(`Built ${data.features.length} catalog items, ${gallery.length} verified R1 screenshots, ${files.reduce((n,f)=>n+f.bytes,0)} bytes.`);
