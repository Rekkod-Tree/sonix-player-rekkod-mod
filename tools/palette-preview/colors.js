/* Color-only proposal. No firmware state or network access. */
(function(root) {
  const palettes = {
    mono: {name:'Строгий монохром', light:[.95,.99,.89,.20,.43], dark:[.19,.255,.32,.96,.76], tint:.018, primary:.105, defaultHue:0, defaultChroma:0, fixed:{light:['#efefef','#ffffff','#d6d6d6','#212121','#525252'],dark:['#101010','#212121','#313131','#f7f7f7','#b8b8b8']}},
    vivid: {name:'Яркий контраст', light:[1,.965,.88,.13,.36], dark:[0,.20,.29,.98,.80], tint:.045, primary:.29, defaultHue:255, defaultChroma:.29, primaryL:{light:.50,dark:.72}, preserveCanvas:true, fixed:{light:['#ffffff','#f3f4f6','#dce0e5','#101319','#4a5058'],dark:['#000000','#101216','#252a30','#f9fbff','#b7bdc5']},fixedPrimary:{light:'#005beb',dark:'#008cff'}},
    lcd: {name:'Ретро LCD', light:[.81,.85,.73,.19,.35], dark:[.16,.22,.29,.87,.73], tint:.055, primary:.13, defaultChroma:.10, lcd:true, textChroma:.035},
    terminal: {name:'Терминал Ч/Б', light:[.99,.99,.87,.10,.34], dark:[.045,.08,.17,.98,.79], tint:.012, primary:.12, defaultHue:0, defaultChroma:0, outline:true, fixed:{light:['#ffffff','#ffffff','#dedede','#101010','#525252'],dark:['#000000','#080808','#212121','#ffffff','#b5b5b5']}},
    terminalColor: {name:'Цветной терминал', light:[.95,.99,.85,.16,.35], dark:[.10,.16,.24,.91,.77], tint:.025, primary:.22, defaultHue:145, defaultChroma:.21, baseHue:155, baseChroma:.015, textChroma:.10, secondaryHue:195, outline:true, fixed:{light:['#e9f0e7','#f6fbef','#cad8c6','#14331f','#3e6046'],dark:['#080f0a','#101a12','#1b2b1e','#6ce99b','#86bb96']}},
    cyber: {name:'Найт-Сити', light:[.94,.99,.86,.15,.37], dark:[.10,.185,.28,.97,.80], tint:.055, primary:.27, defaultHue:335, defaultChroma:.25, baseHue:295, baseChroma:.040, secondaryHue:195, tiles:true},
    cyber2077: {name:'2077 · Жёлтый / графит', light:[.94,.99,.86,.15,.37], dark:[.10,.185,.28,.97,.80], tint:.030, primary:.24, defaultHue:100, defaultChroma:.19, secondaryHue:195,
      fixed:{light:['#eee736','#f8f34d','#d4ce2a','#101a22','#414732'],dark:['#0b1018','#18212b','#263744','#e8e28c','#a2b7ba']},
      component:{light:{bg:'#1b2730',press:'#30404b',text:'#eee8b1'}},
      fixedPrimary:{light:'#14212b',dark:'#eee736'},fixedTiles:{light:['#1b2730','#25333a'],dark:['#18212b','#1c2a32']},fixedTileInk:'#eee8b1'},
    netrunner: {name:'Нетраннер · Красный / циан', light:[.94,.99,.86,.15,.37], dark:[.10,.185,.28,.97,.80], tint:.025, primary:.22, defaultHue:25, defaultChroma:.20, secondaryHue:195,
      fixed:{light:['#eee8e6','#faf6f3','#ddd0cc','#302129','#66525a'],dark:['#110e16','#211921','#34232b','#edc4bd','#ba9ba0']},
      fixedPrimary:{light:'#9f303f',dark:'#ff7772'},fixedTiles:{light:['#faf6f3','#f1e7e5'],dark:['#211921','#271c25']}}
  };
  const backlights={olive:{name:'Оливковая',hue:115,chroma:.065},amber:{name:'Янтарная',hue:80,chroma:.085},ice:{name:'Ледяная',hue:215,chroma:.055},white:{name:'Белая',hue:0,chroma:0},pink:{name:'Розовая',hue:345,chroma:.055}};
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
  const parseHex=h=>[1,3,5].map(i=>parseInt(h.slice(i,i+2),16)/255);
  const quantize=a=>a.map((v,i)=>{const n=i===1?63:31;return Math.round(v*n)/n;});
  const mix=(a,b,t)=>a.map((v,i)=>v*(1-t)+b[i]*t);
  function readable(L,C,h,surfaces,target=4.65) {
    const direction=L>.6?1:-1;
    let result;
    for(let i=0;i<=100;i++) {
      result=rgb(Math.max(.02,Math.min(.99,L+direction*i*.005)),C,h);
      if(surfaces.every(bg=>contrast(result,bg)>=target))return result;
    }
    const black=[0,0,0],white=[1,1,1];
    return Math.min(...surfaces.map(bg=>contrast(black,bg)))>
      Math.min(...surfaces.map(bg=>contrast(white,bg)))?black:white;
  }
  function compute({palette,theme,primary,tint,cover,backlight='olive',forceColorPlaceholders=false}) {
    const p=palettes[palette],dark=theme==='dark',levels=p[theme],art=covers[cover],lamp=backlights[backlight];
    const baseHue=tint?art.hue:p.lcd?lamp.hue:(p.baseHue||0);
    const baseC=tint?p.tint*Math.min(1,art.chroma/.10):p.lcd?lamp.chroma:(p.baseChroma||0);
    const channels=p.fixed?p.fixed[theme].map(h=>quantize(parseHex(h))):levels.map((L,i)=>rgb(L,i<3?baseC:(p.textChroma||0),i<3?baseHue:(p.lcd?baseHue:p.defaultHue||0)));
    if(p.fixed&&tint)for(let i=p.preserveCanvas?1:0;i<3;i++)channels[i]=quantize(mix(channels[i],rgb(levels[i],baseC,baseHue),.32));
    if(!p.fixed||channels.slice(0,3).some(bg=>contrast(channels[3],bg)<4.6))channels[3]=readable(levels[3],p.textChroma||0,p.lcd?baseHue:p.defaultHue||0,channels.slice(0,3));
    if(!p.fixed||channels.slice(0,2).some(bg=>contrast(channels[4],bg)<4.6))channels[4]=readable(levels[4],p.textChroma||0,p.lcd?baseHue:p.defaultHue||0,[channels[0],channels[1]]);
    const hue=primary==='palette'?(p.lcd?lamp.hue:p.defaultHue):primary==='cover'?art.hue:({blue:255,orange:55,yellow:100,green:155,purple:305,pink:350,gray:0}[primary]);
    const chroma=primary==='palette'?(p.lcd&&lamp.chroma===0?0:p.defaultChroma):primary==='gray'?0:p.primary*(primary==='cover'?Math.min(1,art.chroma/.1):1);
    // Primary is derived independently of cover tint. Theme and palette may
    // tune lightness/chroma; neither surface tint nor browsing route does.
    const fill=primary==='palette'&&p.fixedPrimary?quantize(parseHex(p.fixedPrimary[theme])):rgb(p.primaryL?p.primaryL[theme]:(dark?.76:.46),chroma,hue);
    const ink=contrast(fill,[0,0,0])>contrast(fill,[1,1,1])?[0,0,0]:[1,1,1];
    const active=quantize(mix(channels[1],fill,p.tiles ? .22 : .14));
    const actionText=readable(dark?.83:.37,chroma,hue,[channels[0],channels[1],active]);
    const line=quantize(mix(channels[1],channels[3],p.outline ? .57 : .18));
    const tileHues=palette==='cyber'?[290,290,295,290,295,290]:[255,255,255,255,255,255];
    const tileChannels=Array.from({length:6},(_,i)=>p.fixedTiles?quantize(tint?mix(parseHex(p.fixedTiles[theme][i%2]),rgb(dark?.28:.88,baseC,baseHue),.32):parseHex(p.fixedTiles[theme][i%2])):p.tiles?rgb(palette==='cyber'?(dark?.25:.91):(dark?.34:.87),tint?baseC:(palette==='cyber'?.045:.075),tint?baseHue:tileHues[i]):channels[1]);
    const secondarySurfaces=palette==='cyber2077'&&!dark?[channels[0],channels[1]]:[channels[0],channels[1],...tileChannels];
    const secondary=readable(dark?.84:.36,p.secondaryHue ? .16 : 0,p.secondaryHue||0,secondarySurfaces);
    const tileInks=tileChannels.map(bg=>p.fixedTileInk&&contrast(quantize(parseHex(p.fixedTileInk)),bg)>=4.65?quantize(parseHex(p.fixedTileInk)):contrast(channels[3],bg)>=4.65?channels[3]:readable(levels[3],0,0,[bg]));
    const tileMuted=tileChannels.map((bg,i)=>readable(luminance(bg)<.3?.77:.37,p.secondaryHue ? .035 : 0,p.secondaryHue||baseHue,[bg]));
    const tileIcons=tileChannels.map((bg,i)=>palette==='vivid'?(contrast(fill,bg)>=4.65?fill:readable(luminance(bg)<.3?.83:.36,chroma,hue,[bg])):p.secondaryHue?readable(luminance(bg)<.3?.83:.36,.11,p.secondaryHue,[bg]):tileInks[i]);

    // Shared panels may have the opposite polarity to the page, such as the
    // dark 2077 cards on the yellow canvas. Foregrounds belong to a surface.
    const spec=p.component&&p.component[theme];
    const panel=spec?quantize(tint?mix(parseHex(spec.bg),rgb(.28,baseC,baseHue),.25):parseHex(spec.bg)):channels[1];
    const panelPress=spec?quantize(tint?mix(parseHex(spec.press),rgb(.36,baseC,baseHue),.25):parseHex(spec.press)):channels[2];
    const panelDark=luminance(panel)<.3;
    const panelFill=contrast(fill,panel)>=3?fill:readable(panelDark?.78:.43,chroma,hue,[panel],3.2);
    const panelOnFill=contrast(panelFill,[0,0,0])>contrast(panelFill,[1,1,1])?[0,0,0]:[1,1,1];
    const panelActive=quantize(mix(panel,panelFill,.18));
    const panelBackgrounds=[panel,panelPress,panelActive];
    const candidate=spec?quantize(parseHex(spec.text)):channels[3];
    const panelText=panelBackgrounds.every(bg=>contrast(candidate,bg)>=4.65)?candidate:readable(panelDark?.96:.16,p.textChroma||0,p.lcd?baseHue:p.defaultHue||0,panelBackgrounds);
    const panelMuted=readable(panelDark?.79:.36,p.secondaryHue ? .035 : p.textChroma||0,p.secondaryHue||baseHue,panelBackgrounds);
    const panelAction=readable(panelDark?.85:.34,chroma,hue,panelBackgrounds);
    const panelLine=quantize(mix(panel,panelText,p.outline ? .52 : .18));
    const panelDisabled=quantize(mix(panel,panelText,.09));
    const panelDisabledInk=readable(panelDark?.70:.42,0,0,[panelDisabled]);
    const disabled=quantize(mix(channels[0],channels[3],.10));
    const disabledInk=readable(dark?.70:.42,0,0,[disabled]);
    const pressed=(background,foreground)=>quantize(mix(background,foreground[0]===1?[0,0,0]:[1,1,1],.14));
    const panelBorder=p.outline?hex(panelLine):['cyber','netrunner','cyber2077'].includes(palette)?hex(quantize(mix(panel,panelText,.20))):'transparent';

    // Missing-art thumbnails are UI-generated content. Keep identity variation
    // inside each palette's gamut rather than borrowing unrelated fixed RGBs.
    const placeholderHues=palette==='cyber'?[285,310,195,335]:palette==='netrunner'?[20,345,195,280]:palette==='cyber2077'?[100,195,240,75]:palette==='terminalColor'?[145,175,155,195]:[55,155,255,320];
    const neutral=palette==='mono'||palette==='terminal';
    const placeholders=placeholderHues.map((h,i)=>{
      if(forceColorPlaceholders){const forcedHue=[30,155,255,315][i];const bg=rgb(.51,.115,forcedHue);return {bg,fg:readable(.96,0,0,[bg])};}
      const toneHue=palette==='vivid'?hue+(i-1.5)*15:p.lcd?baseHue:h;
      const toneC=neutral?0:p.lcd?baseC:palette==='vivid'?.065:.045;
      const bg=rgb((panelDark?.34:.84)+i*.025,toneC,toneHue);
      const fg=readable(panelDark?.94:.19,neutral?0:p.lcd?.03:.02,toneHue,[bg]);
      return {bg,fg};
    });
    // A list is one plane, not a stack of settings cards. Only an inverted
    // component scheme adds one shared panel behind the entire list.
    const listBase=spec?panel:channels[0];
    const listPress=spec?panelPress:channels[2];
    const listFill=spec?panelFill:fill;
    const listDark=luminance(listBase)<.3;
    const listActive=quantize(mix(listBase,listFill,.16));
    const listBackgrounds=[listBase,listPress,listActive];
    const listCandidate=spec?panelText:channels[3];
    const listText=listBackgrounds.every(bg=>contrast(listCandidate,bg)>=4.65)?listCandidate:readable(listDark?.96:.16,p.textChroma||0,p.lcd?baseHue:p.defaultHue||0,listBackgrounds);
    const listMuted=readable(listDark?.79:.36,p.secondaryHue ? .035 : p.textChroma||0,p.secondaryHue||baseHue,listBackgrounds);
    const listAction=readable(listDark?.85:.34,chroma,hue,listBackgrounds);
    const listDisabled=quantize(mix(listBase,listText,.09));
    const listDisabledInk=readable(listDark?.70:.42,0,0,[listDisabled]);
    const result={bg:hex(channels[0]),surface:hex(channels[1]),press:hex(channels[2]),
      text:hex(channels[3]),muted:hex(channels[4]),primary:hex(fill),onPrimary:hex(ink),
      primaryPressed:hex(pressed(fill,ink)),disabledFill:hex(disabled),disabledInk:hex(disabledInk),
      primaryText:hex(actionText),active:hex(active),line:hex(line),secondary:hex(secondary),
      border:p.outline?hex(line):['cyber','netrunner','cyber2077'].includes(palette)?hex(quantize(mix(channels[1],secondary,.20))):'transparent',
      panel:hex(panel),panelText:hex(panelText),panelMuted:hex(panelMuted),panelPress:hex(panelPress),
      panelPrimary:hex(panelFill),panelOnPrimary:hex(panelOnFill),panelAction:hex(panelAction),panelActive:hex(panelActive),
      panelLine:hex(panelLine),panelBorder,
      listSurface:spec?hex(panel):'transparent',listBorder:spec?panelBorder:'transparent',
      listText:hex(listText),listMuted:hex(listMuted),listPress:hex(listPress),listActive:hex(listActive),listAction:hex(listAction),
      listDisabledFill:hex(listDisabled),listDisabledInk:hex(listDisabledInk),panelPrimaryPressed:hex(pressed(panelFill,panelOnFill)),
      panelDisabledFill:hex(panelDisabled),panelDisabledInk:hex(panelDisabledInk),
      ratios:{text:contrast(channels[3],channels[0]),muted:contrast(channels[4],channels[1]),
        button:contrast(fill,ink),actionText:contrast(actionText,active),secondary:Math.min(...secondarySurfaces.map(bg=>contrast(secondary,bg))),
        panelText:Math.min(...panelBackgrounds.map(bg=>contrast(panelText,bg))),
        panelMuted:Math.min(...panelBackgrounds.map(bg=>contrast(panelMuted,bg))),
        panelButton:contrast(panelFill,panelOnFill),panelAction:contrast(panelAction,panelActive),
        panelPressed:contrast(pressed(panelFill,panelOnFill),panelOnFill),
        pressed:contrast(pressed(fill,ink),ink),disabled:contrast(disabled,disabledInk),panelDisabled:contrast(panelDisabled,panelDisabledInk),listText:Math.min(...listBackgrounds.map(bg=>contrast(listText,bg))),listMuted:Math.min(...listBackgrounds.map(bg=>contrast(listMuted,bg))),listAction:Math.min(...listBackgrounds.map(bg=>contrast(listAction,bg))),listDisabled:contrast(listDisabled,listDisabledInk)}};
    tileChannels.forEach((tile,i)=>{
      result['tile'+i]=hex(tile);result['tileInk'+i]=hex(tileInks[i]);result['tileMuted'+i]=hex(tileMuted[i]);result['tileIcon'+i]=hex(tileIcons[i]);
      result.ratios['tile'+i]=contrast(tile,tileInks[i]);result.ratios['tileMuted'+i]=contrast(tile,tileMuted[i]);result.ratios['tileIcon'+i]=contrast(tile,tileIcons[i]);
    });
    placeholders.forEach((t,i)=>{result['placeholder'+i]=hex(t.bg);result['placeholderInk'+i]=hex(t.fg);result.ratios['placeholder'+i]=contrast(t.bg,t.fg);});
    return result;
  }
  const api={palettes,covers,backlights,compute,rgb,contrast};
  if(typeof module!=='undefined')module.exports=api;else root.PalettePreview=api;
})(typeof window==='undefined'?globalThis:window);
