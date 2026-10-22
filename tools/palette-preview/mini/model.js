/* Visual proposal only. No device, audio, settings or gesture API. */
(function(root){
 const modes={
  normal:{name:'Обычный',height:112,slot:128,hit:112,playWidth:88,description:'Текущий вид. Обложка, название и исполнитель.'},
  compact:{name:'Компактный',height:56,slot:72,hit:64,playWidth:64,description:'Одна строка с названием и исполнителем, без обложки.'},
  minimal:{name:'Свёрнутый',height:32,slot:64,hit:64,playWidth:64,description:'Название и состояние. Исполнитель — в полном плеере.'}
 };
 function geometry(mode,hasTrack=true,height=800){
  const m=modes[mode],reserved=hasTrack?m.slot:0;
  return {reserved,bodyTop:132,bodyBottom:height-reserved,bodyHeight:height-reserved-132,
   visibleHeight:hasTrack?m.height:0,hitTop:height-(m.slot+m.hit)/2,hitHeight:m.hit,
   gained:128-reserved};
 }
 const api={modes,geometry};
 if(typeof module!=='undefined')module.exports=api;else root.MiniProposal=api;
})(typeof window==='undefined'?globalThis:window);
