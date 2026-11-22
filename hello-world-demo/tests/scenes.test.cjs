const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const root=path.resolve(__dirname,'..');
const scenes=['studio-day','topology-map','module-detail','inspection-night'];
test('all four scene assets are real standalone SVG files',()=>{
 for(const name of scenes){
  const file=path.join(root,'public','assets',name+'.svg');
  assert.ok(fs.existsSync(file),'missing scene '+name);
  const xml=fs.readFileSync(file,'utf8');
  assert.match(xml,/<svg\s/);
  assert.match(xml,/<path\s/);
  assert.match(xml,/<text\s/);
  assert.match(xml,/ADAS/);
  assert.match(xml,/CGW/);
  assert.match(xml,/BCM/);
  assert.match(xml,/VCU/);
 }
});
test('scene picker references each existing asset and retains live 3D',()=>{
 const model=fs.readFileSync(path.join(root,'react','studio-model.ts'),'utf8');
 const ui=fs.readFileSync(path.join(root,'react','App.tsx'),'utf8');
 for(const name of scenes)assert.ok(model.includes(name),'missing catalog entry '+name);
 assert.ok(model.includes('live-3d'),'missing interactive scene');
 assert.ok(ui.includes('SCENES.filter'),'UI must render scene catalog');
 assert.ok(ui.includes('sceneAssetPath'),'UI must resolve images through catalog');
 assert.ok(ui.includes('stepScene'),'UI must use tested navigation rule');
});
