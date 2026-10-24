const M=MiniDesign;
const ids=['theme','accent','state','tint','targets'];
function options(){return Object.fromEntries(ids.map(id=>[id,document.getElementById(id).type==='checkbox'?document.getElementById(id).checked:document.getElementById(id).value]));}
function render(){
 const o=options();
 document.getElementById('variants').innerHTML=M.concepts.map((c,i)=>{
  const normal=M.footer(c.id,'normal',o),compact=M.footer(c.id,'compact',o);
  return `<article class="variant" data-id="${c.id}"><div class="variant-header"><h2>${String(i+1).padStart(2,'0')} · ${c.name}</h2><small>${c.short}</small></div><div class="mini-pair"><div><span class="mode-caption">Обычный · 112 / 128 px</span>${M.svgDocument(480,normal.height,normal.svg,c.name+' — обычный')}</div><div><span class="mode-caption">Компактный · 56 / 72 px</span>${M.svgDocument(480,compact.height,compact.svg,c.name+' — компактный')}</div></div><p>${c.note}</p></article>`;
 }).join('');
 renderContext();
}
function renderContext(){
 const o=options(),id=document.getElementById('context-concept').value;
 document.getElementById('context').innerHTML=['normal','compact'].map(mode=>`<figure><figcaption>${mode==='normal'?'Обычный':'Компактный'} · резерв ${o.state==='empty'?0:M.geometry[mode].slot} px</figcaption>${M.device(id,mode,o)}</figure>`).join('');
}
ids.forEach(id=>document.getElementById(id).addEventListener('change',render));
document.getElementById('context-concept').addEventListener('change',renderContext);
render();
