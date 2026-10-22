const assert=require('node:assert/strict');
const {modes,geometry}=require('./model.js');
for(const height of [800,720])for(const id of Object.keys(modes)){
 const g=geometry(id,true,height),m=modes[id];
 assert.equal(g.bodyBottom,height-m.slot);
 assert.equal(g.visibleHeight,m.height);
 assert(g.hitTop>=g.bodyBottom,'Touch band must not cover scrollable content');
 assert(g.hitTop+g.hitHeight<=height,'Touch band must stay on screen');
 assert(g.hitHeight>=64&&m.playWidth>=64,'Compact controls need a real touch area');
 assert(g.visibleHeight<=m.slot,'Visible panel must fit inside the reserved slot');
 assert.equal(geometry(id,false,height).bodyBottom,height,'No track returns the footer space');
}
assert.equal(geometry('compact').gained,56);
assert.equal(geometry('minimal').gained,64);
assert.equal(geometry('minimal').gained-geometry('compact').gained,8);
console.log('Mini-player geometry: 3 modes, 2 heights, no overlap and no-track space passed');
