const RM=RefinedMini;
const fields=['family','theme','primary','state','tint','targets','screen'];
function refinedOptions(){return Object.fromEntries(fields.map(id=>{const e=document.getElementById(id);return [id,e.type==='checkbox'?e.checked:e.value];}));}
function paintRefined(){
 const o=refinedOptions();
 document.getElementById('refined-pair').innerHTML=['normal','compact'].map(mode=>{const f=RM.footer(mode,o);return `<div><span class="mode-caption">${mode==='normal'?'Обычный':'Компактный'}</span>${MiniDesign.svgDocument(480,f.height,f.svg,'Доведённая крупная обложка — '+mode)}</div>`;}).join('');
 document.getElementById('refined-context').innerHTML=['normal','compact'].map(mode=>`<figure><figcaption>${mode==='normal'?'Обычный':'Компактный'} · резерв ${o.state==='empty'?0:MiniDesign.geometry[mode].slot} px</figcaption>${RM.device(mode,o)}</figure>`).join('');
}
fields.forEach(id=>document.getElementById(id).addEventListener('change',paintRefined));
paintRefined();
