/* Rasterize our SVG drawings, without a browser or access to any browser tab. */
const fs=require('node:fs');
const path=require('node:path');
const sharp=require('sharp');
const model=require('./model.js');
const refined=require('./refined.js');
const root=__dirname;
const out=path.join(root,'illustrations');
fs.mkdirSync(out,{recursive:true});
// SVG text uses the installed Segoe UI fonts; font files are not redistributed.
const data='data:image/png;base64,'+fs.readFileSync(path.join(root,'assets/cover-source.png')).toString('base64');
const designs={
 'comparison-dark':()=>model.comparison({theme:'dark'}),
 'comparison-light':()=>model.comparison({theme:'light'}),
 'context':()=>model.contextBoard(),
 'states':()=>model.statesBoard(),
 'refined':()=>refined.overview(),
 'refined-context':()=>refined.contextBoard(),
 'refined-palettes':()=>refined.paletteBoard(),
 'refined-states':()=>refined.statesBoard()
};
(async()=>{
 for(const [name,make] of Object.entries(designs)){
  const svg=make();
  fs.writeFileSync(path.join(out,name+'.svg'),svg.replaceAll('assets/cover-source.png','../assets/cover-source.png'));
  const embedded=svg.replaceAll('assets/cover-source.png',data);
  const result=await sharp(Buffer.from(embedded)).png().toFile(path.join(out,name+'.png'));
  console.log(name,result.width+'x'+result.height);
 }
})().catch(error=>{console.error(error.message);process.exitCode=1;});
