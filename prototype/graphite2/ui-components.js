(function(root,factory){
  if(typeof module==='object'&&module.exports)module.exports=factory;
  else root.SonixUI=factory(root.React,root.SonixTokens);
})(typeof window==='undefined'?globalThis:window,function(React,T){
  'use strict';
  const h=React.createElement;
  const cx=(...names)=>names.filter(Boolean).join(' ');
  const Icon=({name,size=28})=>name==='playOutline'?h('svg',{width:size,height:size,viewBox:'0 0 24 24',fill:'none',stroke:'currentColor',strokeWidth:2,strokeLinejoin:'round','aria-hidden':true},h('path',{d:'M7 4 21 12 7 20Z'})):name?h('sx-i',{n:name,s:size,'aria-hidden':true}):null;
  function Artwork({background,placeholder=false,icon='music',iconSize=28,className,style,label,children,...props}){
    return h('span',{'data-ui':'Artwork','data-placeholder':placeholder,'data-art':!placeholder,className:cx('sx-artwork',className),role:label?'img':undefined,'aria-label':label,'aria-hidden':label?undefined:true,style:{background,...style},...props},children??(placeholder?h(Icon,{name:icon,size:iconSize}):null));
  }
  function Equalizer({moving}){
    const ref=React.useRef(null),[visible,setVisible]=React.useState(false);
    React.useEffect(()=>{let inside=false;const update=()=>setVisible(inside&&!document.hidden),observer=new IntersectionObserver(([entry])=>{inside=entry.isIntersecting;update();});observer.observe(ref.current);document.addEventListener('visibilitychange',update);return()=>{observer.disconnect();document.removeEventListener('visibilitychange',update);};},[]);
    return h('span',{ref,className:'sx-equalizer','aria-hidden':true},[0,1,2].map(k=>h('i',{key:k,style:{animationPlayState:moving&&visible?'running':'paused'}})));
  }
  function Button({label,icon,children,onClick,variant='ghost',size='primary',className,pressed,disabled,style,...props}){
    return h('button',{type:'button','data-ui':'Button',className:cx('sx-button','sx-button--'+variant,'sx-button--'+size,className),'aria-label':label,'aria-pressed':pressed,disabled,onClick,style,...props},icon?h(Icon,{name:icon}):null,children);
  }
  function Choice({items,label,className,disabled=false}){
    return h('div',{'data-ui':'Choice',role:'group','aria-label':label,'aria-disabled':disabled||undefined,className:cx('sx-choice',className)},items.map((p,i)=>h(Button,{key:p.t||i,variant:'choice',disabled,pressed:!!p.selected,onClick:p.go,style:{'--button-bg':p.bg,'--button-ink':p.fg}},h('span',{className:'sx-choice-label'},p.t))));
  }
  function Card({b}){
    const toggle=b.hasSwitch;
    const interactive=typeof b.tap==='function';
    const nav=interactive&&b.hasChev&&!toggle;
    const content=[
      h('div',{className:'sx-card-line',key:'line'},b.tone?h('span',{className:'sx-card-notice-icon'},h(Icon,{name:'info',size:28})):null,h('div',{className:'sx-card-copy'},b.title?h('div',{className:'sx-card-title'},b.title):null,b.hasSub&&!toggle?h('div',{className:'sx-card-sub',style:{color:b.subColor}},b.sub):null),b.value?h('div',{className:'sx-card-value'},b.value):null,toggle?h('span',{className:'sx-switch','data-on':!!b.cardChecked,'aria-hidden':true},h('i')):b.hasChev?h(Icon,{name:'chevR',size:26}):null),
      toggle&&b.hasSub?h('div',{key:'note',className:'sx-card-sub',style:{color:b.subColor}},b.sub):null,
      b.hasPills?h(Choice,{key:'choices',items:b.pills,label:b.title,disabled:b.choicesDisabled}):null,
      b.hasSwatches?h('div',{key:'swatches',className:'sx-accent-grid',role:'group','aria-label':'Акцентный цвет'},b.swatches.map(a=>h(Button,{key:a.label,label:a.label,pressed:a.selected,onClick:a.go,variant:'swatch',className:'accent-choice'},h('span',{className:'sx-accent-dot',style:{background:a.c,color:a.ink}},h(Icon,{name:a.g})),h('span',{className:'sx-accent-name'},a.label)))):null,
      b.hasBar?h('div',{key:'bar',className:'sx-meter','aria-hidden':true},h('i',{style:{width:b.pct}})):null,
      b.hasAction?h(Button,{key:'action',onClick:b.act,variant:'surface',className:'sx-card-action'},b.actLabel):null
    ];
    if(interactive)return h('button',{type:'button','data-ui':toggle?'SwitchCard':'NavCard',role:toggle?'switch':undefined,'aria-checked':toggle?!!b.cardChecked:undefined,disabled:b.disabled,'data-muted':b.muted,'data-tone':b.tone,className:cx('sx-card','settings-card','sx-card--interactive',nav&&'sx-card--nav'),onClick:b.tap,style:{background:nav?undefined:b.bg,opacity:b.op}},content);
    return h('section',{'data-ui':'Card','data-muted':b.muted,'data-tone':b.tone,className:'sx-card settings-card',style:{background:b.bg,opacity:b.op}},content);
  }
  function Row({b}){
    const eq=b.eq?h(Equalizer,{moving:b.moving}):null;
    return h('div',{'data-ui':'ListRow',className:'sx-row-shell',style:{height:b.h,transform:b.otf,zIndex:b.oz,transition:b.otr}},
      h('div',{className:'sx-swipe-underlay',style:{display:b.layerShow,background:b.layerBg,color:b.layerFg,justifyContent:b.layerJustify}},h(Icon,{name:b.layerIc}),b.layerLabel),
      h('div',{className:'sx-row track-row','data-title':b.title,'data-playing':b.active,role:'group',onContextMenu:b.context,onPointerDown:b.pd,onPointerMove:b.pm,onPointerUp:b.pu,onPointerCancel:b.pc,style:{height:b.h,background:b.bg,transform:b.tf,transition:b.tr}},
        h(Button,{label:b.title,onKeyDown:b.key,size:'fill',variant:'row',className:'row-primary sx-row-primary'},
          b.isNum?h('span',{className:'sx-row-number',style:{color:b.numColor}},b.num,eq):h(Artwork,{'data-art':b.hasArt,placeholder:b.placeholder,className:'sx-row-art',background:b.cover,style:{borderRadius:b.thR,width:b.th,height:b.th,color:b.leadColor}},b.glyph,h(Icon,{name:b.gic,size:26}),eq),
          h('span',{className:'sx-row-copy'},h('span',{className:'sx-row-title',style:{color:b.titleColor}},h('span',null,b.title),b.durTop?h('small',null,b.durTop):null),b.sub||b.q||b.dur?h('span',{className:'sx-row-sub',style:{color:b.subColor}},h('span',null,b.sub),b.q?h('span',{className:'sx-row-badge',style:{color:b.qc}},b.q.replace(/^\s*·\s*/,'')):null,b.dur?h('span',{className:'sx-duration'},b.dur):null):null)),
        b.hasTrail?h(Button,{'data-act':'1',label:b.trailLabel,icon:b.trail,pressed:/star/.test(b.trail)?b.trail==='starf':undefined,onClick:b.onStar,onPointerDown:b.hd,onPointerMove:b.hm,onPointerUp:b.hu,onPointerCancel:b.hcancel,className:'sx-row-action',style:{color:b.trailColor,touchAction:b.ta}}):null));
  }
  function TileGrid({b}){
    return h('div',{'data-ui':'TileGrid',className:'sx-tile-grid',style:{'--tile-height':b.tileH+'px'}},b.tiles.map(t=>h(Button,{key:t.t,onClick:t.go,variant:'tile',size:'fill',className:'home-tile'},h('span',{className:'sx-tile-icon'},h(Icon,{name:t.g,size:34})),h('span',{className:'tile-title'},h('span',null,t.t)),h('span',{className:'tile-subtitle'},/\d/.test(t.sub)?t.sub:''))));
  }
  function AlbumHero({b,v}){
    return h('section',{'data-ui':'AlbumHero',className:'album-hero'},h('div',{className:'album-heading'},h(Artwork,{className:'album-cover',background:b.cover,placeholder:b.placeholder,icon:'disc',iconSize:56,style:{color:b.artInk},label:(b.placeholder?'Альбом без обложки: ':'Обложка альбома: ')+b.title}),h('div',{className:'album-copy'},h('div',{className:'album-title'},b.title),h('div',{className:'album-artist'},b.artist),h('div',{className:'album-meta'},b.meta))),h('div',{className:'album-actions'},h(Button,{onClick:b.play,icon:'play',variant:'accent'},'Слушать'),h(Button,{onClick:b.shuffle,icon:'shuffle',label:'Перемешать альбом',variant:'surface'}),h(Button,{onClick:b.menu,icon:'more',label:'Меню альбома',variant:'surface'})));
  }
  function EmptyState({b}){
    return h('section',{'data-ui':'EmptyState',className:'sx-state'},h('div',{className:'sx-state-content'},h('span',{className:'sx-state-icon'},h(Icon,{name:b.glyph,size:32})),h('h2',null,b.title),h('p',null,b.text)),b.label1?h('div',{className:cx('sx-state-actions',b.has2&&'sx-state-actions--pair')},h(Button,{onClick:b.go1,variant:'accent'},b.label1),b.has2?h(Button,{onClick:b.go2,variant:'surface'},b.label2):null):null);
  }
  function Block({b,v}){
    if(b.isRow)return h(Row,{b});
    if(b.isCard)return h(Card,{b});
    if(b.isTiles)return h(TileGrid,{b});
    if(b.isHero)return h(AlbumHero,{b,v});
    if(b.isState)return h(EmptyState,{b});
    if(b.isGroup)return h('div',{'data-ui':'SectionHeading',className:'section-label'},b.t);
    if(b.isNote)return h('p',{'data-ui':'Note',className:'sx-note'},b.t);
    if(b.isSpacer)return h('div',{style:{height:b.h}});
    if(b.isGestureHint)return h('aside',{'data-ui':'GestureHint',className:'gesture-hint'},h('div',null,b.next?'← Лайк · Следующим →':'Свайп влево — лайк',h('br'),'Удержание — все действия'),h(Button,{onClick:b.dismiss,label:'Скрыть подсказку жестов',icon:'x'}));
    return null;
  }
  function SearchControls({v,input,filters}){
    return h('div',{'data-ui':'SearchControls',className:'sx-search-controls'},h('div',{className:'sx-search-field'},h('span',{className:'sx-search-icon'},h(Icon,{name:'search'})),h('input',{className:'search-input',type:'search','aria-label':'Поиск музыки',placeholder:'Название или исполнитель',value:v.query,onChange:v.queryChanged,onFocus:input.focus,onKeyDown:v.searchKey,autoComplete:'off',spellCheck:false}),v.query?h(Button,{onClick:input.clear,label:'Очистить поиск',icon:'x'}):null),!v.kbOn&&filters?h(Choice,{items:filters.pills,label:'Тип результатов',className:'sx-search-filters'}):null);
  }
  function Header({v}){
    return h('header',{'data-ui':'Header',className:'sx-header',style:{transform:'translateX('+v.navX+'px)',transition:v.navTr}},h(Button,{onClick:v.back,icon:'chevL',label:'Назад'}),h('div',{className:'heading-copy'},h('h1',{className:'page-title',title:v.title},v.title),v.headerHint?h('div',{className:'header-hint'},v.headerHint):null),v.hdrBtns.map((b,i)=>h(Button,{key:b.label||i,onClick:b.go,icon:b.g,label:b.label})));
  }
  function Content({v}){
    const search=v.screenId==='search'||v.screenId==='zsearch';
    const input=search?v.blocks.find(b=>b.isInput):null;
    const filters=search?v.blocks.find(b=>b.isCard&&b.hasPills):null;
    const blocks=search?v.blocks.filter(b=>b!==input&&b!==filters):v.blocks;
    const stateOnly=blocks.length===1&&blocks[0].isState;
    return h('main',{'data-ui':'Content',className:cx('sx-content',search&&'sx-content--search',v.kbOn&&'sx-content--typing'),style:{top:v.bodyTop,bottom:v.bodyBottom,transform:'translateX('+v.navX+'px)',transition:v.navTr}},
      input?h(SearchControls,{v,input,filters}):null,
      h('div',{ref:v.listRef,'data-testid':'screen-content',onScroll:v.onListScroll,className:cx('sx-scroller','scr',stateOnly&&'sx-scroller--state',v.hasLane&&'sx-scroller--indexed')},blocks.map((b,i)=>h('div',{key:b.instanceKey||i,className:cx('sx-block',b.isState&&'sx-block--state',b.isCard&&b.hasChev&&b.tap&&!b.hasSwitch&&'sx-block--nav')},h(Block,{b,v})))),
      v.hasLane?h('div',{className:'sx-alphabet',onPointerDown:v.scrubDown,onPointerMove:v.scrubMove,onPointerUp:v.scrubUp,onPointerCancel:v.scrubUp},v.letters.map(l=>h('span',{key:l.ch,style:{color:l.color}},l.ch))):null,
      v.scrubOn?h('div',{className:'sx-letter-preview'},v.scrubLetter):null,
      v.canBack?h('div',{className:'sx-edge',onPointerDown:v.edgeDown,onPointerMove:v.edgeMove,onPointerUp:v.edgeUp,onPointerCancel:v.edgeCancel,'aria-hidden':true}):null);
  }
  function Footer({v}){
    if(!v.showMini&&!v.showCta)return null;
    return h('div',{'data-ui':'FooterSlot',className:'sx-footer-slot'},v.showCta?h(Button,{variant:'surface',size:'fill',className:'sx-empty-player',onClick:v.shuffleAll},h('span',{className:'sx-empty-player-icon'},h(Icon,{name:'shuffle'})),h('span',{className:'sx-footer-copy'},h('strong',null,'Перемешать всю музыку'),h('small',null,v.libraryCount+' треков · '+v.albumCount+' альбомов'))):h('div',{'data-ui':'MiniPlayer',className:'mini-player',onPointerDown:v.mDown,onPointerUp:v.mUp,onPointerCancel:v.mCancel},h(Artwork,{className:'sx-mini-art',background:v.miniCover,placeholder:v.npArtMissing,icon:v.npArtIcon,iconSize:28,style:{color:v.npArtInk}}),h('button',{type:'button',className:'sx-mini-open','aria-label':'Открыть плеер',onKeyDown:v.miniKey},h('strong',{className:'mini-title'},v.npTitle),h('span',{className:'mini-artist'},v.npArtist)),h(Button,{'data-act':'1',label:v.pauseLabel,onClick:v.togglePause,icon:v.pauseGlyph,variant:'mini',className:'sx-mini-play'}),h('div',{className:'sx-mini-progress','aria-hidden':true},h('i',{style:{width:v.progW,background:v.miniFill}}))));
  }
  function Slider({label,value,max=100,min=0,onChange,onStart,onEnd,disabled,icon}){
    const pct=max>min?Math.max(0,Math.min(100,(value-min)/(max-min)*100)):0;
    const stop=e=>{e.stopPropagation();onEnd&&onEnd();};
    return h('div',{'data-ui':'Slider',className:'sx-slider'},icon?h(Icon,{name:icon,size:30}):null,h('div',{className:'sx-slider-hit'},h('div',{className:'sx-slider-rail','aria-hidden':true},h('i',{style:{width:pct+'%'}})),h('input',{type:'range',min,max,value,disabled,'aria-label':label,onChange:e=>onChange(Number(e.target.value)),onPointerDown:e=>{e.stopPropagation();onStart&&onStart();},onPointerUp:stop,onPointerCancel:stop,onBlur:onEnd})));
  }
  function Player({v}){
    return h('section',{'data-ui':'Player',className:'now-playing',style:{transform:'translateY('+v.npY+'px)',transition:v.navTr,background:v.npBg,'--sx-accent':v.accent,'--sx-accent-ink':v.onAcc},onPointerDown:v.npDown,onPointerMove:v.npMove,onPointerUp:v.npUp,onPointerCancel:v.npCancel},
      h('div',{'data-cover':'1',className:'player-cover',style:{height:v.coverH}},h(Artwork,{className:'sx-player-art',background:v.npArt,placeholder:v.npArtMissing,icon:v.npArtIcon,iconSize:80,style:{left:v.npArtL,top:v.npArtTop,width:v.npArtS,height:v.npArtS,color:v.npArtInk},label:(v.npArtMissing?'Без обложки: ':'Обложка: ')+v.npTitle})),
      h('div',{className:'sx-player-top','data-noswipe':'1'},h(Button,{size:'secondary',variant:'surface',icon:'chevD',label:'Свернуть плеер',onClick:v.collapse,onPointerDown:v.stop,onPointerUp:v.stop}),h(Button,{size:'secondary',variant:'surface',label:'Открыть очередь',onClick:v.toQueue,className:'sx-up-next',onPointerDown:v.stop,onPointerUp:v.stop},h('span',null,v.upLbl?h('small',null,v.upLbl):null,h('strong',null,v.upNext)),h(Icon,{name:'chevR',size:22}))),
      v.chainOpen?h('section',{className:'sx-chain',role:'region','aria-label':'Аудиотракт'},v.chain.map(c=>h('div',{key:c.k},h('span',null,c.k),h('strong',null,c.v)))):null,
      h('div',{className:'player-panel',style:{background:v.npPanel}},h('div',{className:'sx-player-meta'},h('div',{className:'sx-player-title',title:v.npTitle},v.npTitle),h('div',{className:'sx-player-artist'},v.npArtist),v.npCanStar?h(Button,{label:'Избранное',pressed:v.npStarred,icon:v.npStarG,onClick:v.npStar,className:'sx-player-favourite',style:{color:v.npStarC}}):null),
        h('div',{className:'audio-badge',style:{color:v.chipC}},h('i'),' '+v.audioBadge),
        h(Slider,{label:'Позиция трека',value:v.progress,max:v.duration||1,onChange:v.setProgress,onStart:v.seekBegin,onEnd:v.seekCancel,disabled:v.isLive}),
        h('div',{className:'sx-player-times'},h('span',null,v.tCur),h('span',null,v.npPos),h('span',null,v.tTot)),
        h('div',{className:'transport'},h(Button,{size:'secondary',label:'Режим повтора',pressed:v.repActive,icon:v.repGlyph,onClick:v.cycleRepeat,style:{color:v.repActive?v.textAccent:v.ts}}),h(Button,{label:'Предыдущий трек',icon:'skipB',onClick:v.prev}),h(Button,{label:v.pauseLabel,icon:v.pauseGlyph,onClick:v.togglePause,size:'play',variant:'accent',style:{'--button-bg':v.npAcc,'--button-ink':v.npInk}}),h(Button,{label:'Следующий трек',icon:'skipF',onClick:v.next}),h(Button,{size:'secondary',label:'Меню трека',icon:'more',onClick:v.openNpMenu}))));
  }
  function Sheet({v}){
    if(v.sheetHidden)return null;
    return h(React.Fragment,null,h('div',{className:'sheet-scrim',onClick:v.closeSheet}),h('section',{'data-ui':'Sheet',className:'action-sheet',role:'dialog','aria-modal':true,'aria-label':v.sheetTitle,style:{transform:v.sheetTf,transition:v.shTr},onPointerDown:v.shDown,onPointerMove:v.shMove,onPointerUp:v.shUp,onPointerCancel:v.shCancel},h('div',{className:'sheet-heading'},h('div',{className:'sheet-handle','aria-hidden':true}),h(Button,{label:'Закрыть меню',icon:'x',size:'secondary',onClick:v.closeSheet,className:'sheet-close'}),h('h2',null,v.sheetTitle),v.sheetSub?h('p',null,v.sheetSub):null),h('div',{className:'sx-sheet-items'},v.sheetItems.map((b,i)=>h(Button,{key:b.label||i,variant:'menu',onClick:b.go,style:{color:b.fg}},h('span',{style:{color:b.ic}},h(Icon,{name:b.icon==='play'?'playOutline':b.icon})),b.label))),v.sheetInput?h('div',{className:'sx-form-field'},h('label',{htmlFor:'playlist-name'},'Название плейлиста'),h('input',{id:'playlist-name',className:'playlist-name-input',value:v.playlistName,onChange:v.playlistNameChanged,maxLength:60,'aria-invalid':!!v.playlistError,'aria-describedby':v.playlistError?'playlist-name-error':undefined}),v.playlistError?h('p',{id:'playlist-name-error',role:'alert'},v.playlistError):null):null,v.sheetConfirm?h('div',{className:'sx-sheet-actions'},h(Button,{onClick:v.closeSheet,variant:'surface'},'Отмена'),h(Button,{onClick:v.confirmGo,variant:'accent',style:{'--button-bg':v.confirmBg,'--button-ink':v.confirmFg}},v.confirmLabel)):null));
  }
  function Toast({v}){
    if(!v.toastOn)return null;
    return h('div',{'data-ui':'Toast',className:'sx-toast',role:'status','aria-live':'polite',style:{bottom:v.toastBottom},onMouseEnter:v.pauseToast,onMouseLeave:e=>{if(!e.currentTarget.contains(document.activeElement))v.resumeToast();},onFocusCapture:v.pauseToast,onBlurCapture:e=>{if(!e.currentTarget.contains(e.relatedTarget)&&!e.currentTarget.matches(':hover'))v.resumeToast();},onPointerDown:e=>{v.pauseToast();v.tDown(e);},onPointerUp:e=>{v.tUp(e);v.resumeToast();},onPointerCancel:v.resumeToast},h('span',{className:'sx-toast-message'},v.toastMsg),v.toastUndo?h(Button,{onClick:v.undo,variant:'toast',className:'sx-toast-undo'},'Отменить'):null,v.toastUndo?h('span',{className:'sx-toast-timer','aria-hidden':true},h('i',{style:{width:v.toastW+'%',transition:v.toastTr}})):null);
  }
  function Keyboard({v}){
    return v.kbOn?h('div',{'data-ui':'Keyboard',className:'sx-keyboard',onPointerDown:v.kbDown,onPointerUp:v.kbUp},v.kbRows.map((row,i)=>h('div',{key:i,className:'sx-keyboard-row'},row.keys.map((key,j)=>h(Button,{key:j,size:'key',variant:'key',onClick:key.go,style:{flex:key.flex,'--button-bg':key.bg,'--button-ink':key.fg}},key.t))))):null;
  }
  function ControlPanel({v}){
    if(!v.ccOpen)return null;
    return h(React.Fragment,null,h('div',{className:'control-scrim',onClick:v.toggleCC}),h('section',{'data-ui':'ControlPanel',className:'control-panel',role:'dialog','aria-modal':true,'aria-label':'Пункт управления',style:{transform:v.ccTransform,background:v.miniBg,color:v.miniFg,'--sx-accent-text':v.miniFg,'--sx-muted':v.miniFg},onPointerDown:v.ccDown,onPointerUp:v.ccUp,onPointerCancel:v.ccCancel},h('div',{className:'sx-quick-grid'},v.ccBtns.map(c=>h(Button,{key:c.l,pressed:c.pressed,onClick:c.go,onPointerDown:c.down,onPointerMove:c.move,onPointerUp:c.up,onPointerCancel:c.cancel,onContextMenu:c.context,size:'fill',className:'sx-quick-toggle'},h('span',{className:'sx-quick-disc',style:{background:c.bg,color:c.fg}},h(Icon,{name:c.g,size:32})),h('span',{className:'sx-quick-label'},c.l)))),h(Slider,{icon:'sun',label:'Яркость',min:10,max:100,value:v.brightness,onChange:v.setBrightness})));
  }
  function Device(v){
    return h(React.Fragment,null,
      h('button',{type:'button','data-ui':'StatusBar',className:'statusbar','aria-label':'Пункт управления',style:{background:v.sbBg,color:v.sbFg,zIndex:v.ccOpen?24:7},onClick:v.topClick,onPointerDown:v.topDown,onPointerMove:v.topMove,onPointerUp:v.topUp,onPointerCancel:v.topCancel},h('span',null,h(Icon,{name:v.sbVolIc,size:20}),h('span',{style:{color:v.sbWarn,background:v.sbWarnBg,borderRadius:4,padding:v.sbWarnBg?'0 2px':undefined}},v.sbVol),v.sbClockL),h('span',null,v.sbC),h('span',null,v.sbBt?h(Icon,{name:'bt',size:20}):null,v.sbWifi?h(Icon,{name:'wifi',size:20}):null,v.sbR,h(Icon,{name:'battery',size:24}))),
      !v.isNp&&v.showHeader?h(Header,{v}):null,
      !v.isNp&&v.isAlbum?h('div',{className:cx('sx-album-bar',v.albBarShow==='flex'&&'sx-album-bar--solid')},h(Button,{onClick:v.back,label:'Назад',icon:'chevL'}),v.albBarShow==='flex'?h(React.Fragment,null,h('strong',null,v.albTitle),h(Button,{onClick:v.albumMenu,label:'Меню альбома',icon:'more'})):null):null,
      v.isNp?h(Player,{v}):h(Content,{v}),
      h(Footer,{v}),h(Keyboard,{v}),h(Sheet,{v}),h(Toast,{v}),
      h('div',{className:'sx-volume','aria-hidden':true,style:{transform:'translateX('+v.volX+'px)',background:v.miniBg,color:v.miniFg,'--sx-accent-text':v.miniFg,'--sx-muted':v.miniFg}},h('strong',{style:v.vol>55?{background:T.feedback.dangerFill,color:'#fff',borderRadius:6,padding:'0 4px'}:undefined},v.vol),h('span',{className:'sx-volume-rail'},h('i',{style:{height:v.volH}})),h(Icon,{name:v.volG,size:24})),
      h(ControlPanel,{v}));
  }
  return Object.freeze({Artwork,Button,Choice,Card,Row,TileGrid,AlbumHero,EmptyState,SearchControls,Header,Footer,Slider,Player,Sheet,Toast,Keyboard,ControlPanel,Device,createDevice:view=>h(Device,view)});
});
