/* Color-only proposal. No firmware state or network access. */
(function(root) {
  const palettes = {
    mono: {name:'Монохром', light:[.95,.99,.89,.23,.46], dark:[.19,.255,.32,.95,.74], tint:.018, primary:.105},
    vivid: {name:'Контраст', light:[.94,1,.86,.17,.40], dark:[.145,.245,.34,.98,.79], tint:.035, primary:.19}
  };
  const covers = {
    teal:{name:'Бирюзовая', hue:185, chroma:.10, image:'teal.svg'},
    amber:{name:'Янтарная', hue:55, chroma:.14, image:'amber.svg'},
    rose:{name:'Малиновая', hue:350, chroma:.16, image:'rose.svg'}
  };
  function rgb(L,C,h) {
    const a=C*Math.cos(h*Math.PI/180), b=C*Math.sin(h*Math.PI/180);
    const l=(L+.3963377774*a+.2158037573*b)**3;
    const m=(L-.1055613458*a-.0638541728*b)**3;
    const s=(L-.0894841775*a-1.291485548*b)**3;
    return [4.0767416621*l-3.3077115913*m+.2309699292*s,
      -1.2684380046*l+2.6097574011*m-.3413193965*s,
      -.0041960863*l-.7034186147*m+1.707614701*s].map((v,i)=>{
        const srgb=v<=.0031308?12.92*v:1.055*Math.pow(v,1/2.4)-.055;
        const levels=i===1?63:31;
        return Math.round(Math.max(0,Math.min(1,srgb))*levels)/levels;
      });
  }
  const hex=r=>'#'+r.map(v=>Math.round(v*255).toString(16).padStart(2,'0')).join('');
  const luminance=r=>r.reduce((v,c,i)=>v+[.2126,.7152,.0722][i]*(c<=.04045?c/12.92:((c+.055)/1.055)**2.4),0);
  function contrast(a,b) {const x=luminance(a),y=luminance(b);return (Math.max(x,y)+.05)/(Math.min(x,y)+.05);}
  function compute({palette,theme,primary,tint,cover}) {
    const p=palettes[palette], dark=theme==='dark', levels=p[theme], art=covers[cover];
    const C=tint?p.tint*Math.min(1,art.chroma/.10):0;
    const channels=levels.map((L,i)=>rgb(L,i<3?C:0,art.hue));
    const hue=primary==='cover'?art.hue:({blue:255,orange:55,green:155,purple:305,gray:0}[primary]);
    const chroma=primary==='gray'?0:p.primary*(primary==='cover'?Math.min(1,art.chroma/.1):1);
    let fill, ink, actionText;
    for(let i=0;i<=40;i++) {
      fill=rgb((dark?.73:.50)+(dark?1:-1)*i*.004,chroma,hue);
      ink=contrast(fill,[0,0,0])>contrast(fill,[1,1,1])?[0,0,0]:[1,1,1];
      if(contrast(fill,channels[1])>=3 && contrast(fill,ink)>=4.5)break;
    }
    for(let i=0;i<=40;i++) {
      actionText=rgb((dark?.77:.44)+(dark?1:-1)*i*.004,chroma,hue);
      if(contrast(actionText,channels[1])>=4.5)break;
    }
    const mix=(a,b,t)=>a.map((v,i)=>v*(1-t)+b[i]*t);
    const active=mix(channels[1],fill,palette==='vivid'?.23:.14);
    return {bg:hex(channels[0]),surface:hex(channels[1]),press:hex(channels[2]),
      text:hex(channels[3]),muted:hex(channels[4]),primary:hex(fill),onPrimary:hex(ink),
      primaryText:hex(actionText),active:hex(active),line:hex(mix(channels[1],channels[3],.18)),
      ratios:{text:contrast(channels[3],channels[0]),muted:contrast(channels[4],channels[1]),
        button:contrast(fill,ink),actionText:contrast(actionText,channels[1])}};
  }
  const api={palettes,covers,compute,rgb,contrast};
  if(typeof module!=='undefined')module.exports=api;else root.PalettePreview=api;
})(typeof window==='undefined'?globalThis:window);
