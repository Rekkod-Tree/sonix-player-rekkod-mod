(function(root){
  const tokens = Object.freeze({
    space:Object.freeze({xs:4,sm:8,md:12,page:16,tile:20,card:24,section:32}),
    size:Object.freeze({width:480,status:44,header:88,row:88,rowLarge:104,thumb:56,thumbLarge:64,tap:88,secondary:72,play:96,footer:112,footerSlot:128,keyboard:268,playerPanel:352,rail:28,railGutter:40,albumCover:156,tileR1:188,tileR3:164}),
    radius:Object.freeze({surface:20,media:12,sheet:24}),
    type:Object.freeze({heading:32,title:28,track:26,control:24,secondary:22,compact:20}),
    color:Object.freeze({darkCanvas:'#1c1e1a',darkSurface:'#272a24',darkPress:'#363c31',darkText:'#f4f1e9',darkMuted:'#b7bcaf',lightCanvas:'#f2eee5',lightSurface:'#faf7f0',lightPress:'#e6e2d8',lightText:'#2e342b',lightMuted:'#535f4e'}),
    feedback:Object.freeze({darkDanger:'#ff9b9f',lightDanger:'#a12e38',dangerFill:'#ae3540',dangerInk:'#ffffff',darkWarning:'#e0bd79',lightWarning:'#75551a',darkLine:'rgba(174,187,198,.18)',lightLine:'rgba(42,57,70,.18)',darkOverlay:'rgba(255,255,255,.08)',lightOverlay:'rgba(0,0,0,.06)',scrim:'rgba(0,0,0,.56)',mediaInk:'#ffffff',mediaBadge:'#151b22'}),
    artwork:Object.freeze({ink:'#ffffff',categoryInk:'#17232b',neutral:'#aab8c2'}),
    coverShades:Object.freeze(['#984f47','#866329','#5a7139','#36715d','#3b6483','#4e5f8a','#6c5286','#884e71']),
    time:Object.freeze({transition:180,toast:6000}),
    palettes:Object.freeze({
      warm:Object.freeze({name:'Тёплая',darkAccent:'#c6b89f',lightAccent:'#705b3c',color:null}),
      graphite:Object.freeze({name:'Графит',darkAccent:'#bcc9d1',lightAccent:'#425b6b',color:Object.freeze({darkCanvas:'#181b20',darkSurface:'#242930',darkPress:'#343d47',darkText:'#edf0f3',darkMuted:'#b1bbc6',lightCanvas:'#e9ecef',lightSurface:'#f7f8fa',lightPress:'#dbe1e6',lightText:'#222c37',lightMuted:'#4e5d6b'})}),
      graphite2:Object.freeze({name:'Графит 2',darkAccent:'#366c8c',darkAccentText:'#8fbad4',lightAccent:'#295c7a',lightAccentText:'#295c7a',color:Object.freeze({darkCanvas:'#11151a',darkSurface:'#1e252d',darkPress:'#303b46',darkText:'#f1f4f7',darkMuted:'#aebbc6',lightCanvas:'#e5e9ec',lightSurface:'#f7f9fa',lightPress:'#d4dde3',lightText:'#1a252e',lightMuted:'#465663'})}),
      midnight:Object.freeze({name:'Ночной',darkAccent:'#bfa77b',lightAccent:'#70592f',color:Object.freeze({darkCanvas:'#121c29',darkSurface:'#1e2b3c',darkPress:'#2c3e51',darkText:'#f1eee7',darkMuted:'#b5becb',lightCanvas:'#eceef0',lightSurface:'#f8f8f6',lightPress:'#dce2e8',lightText:'#253140',lightMuted:'#505f70'})})
    })
  });
  if(typeof module==='object'&&module.exports)module.exports=tokens;
  else root.SonixTokens=tokens;
})(typeof window==='undefined'?globalThis:window);
