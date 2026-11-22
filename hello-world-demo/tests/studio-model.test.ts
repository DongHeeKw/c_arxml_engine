import test from 'node:test';
import assert from 'node:assert/strict';
import {SCENES, DEFAULT_SCENE, stepScene, countEnabled, getSelectedEcu, sceneAssetPath, ECU_METADATA} from '../react/studio-model';

test('scene navigation advances, reverses, and wraps without invalid states',()=>{
 assert.equal(stepScene(DEFAULT_SCENE,1),'topology-map');
 assert.equal(stepScene('studio-day',-1),'live-3d');
 assert.equal(stepScene('live-3d',1),'studio-day');
 assert.equal(stepScene('module-detail',-1),'topology-map');
});
test('all static scenes map to public assets; live geometry has no image asset',()=>{
 for(const scene of SCENES){
   const path=sceneAssetPath(scene.id);
   if(scene.id==='live-3d') assert.equal(path,null);
   else assert.equal(path,'./assets/'+scene.id+'.svg');
 }
});
test('ECU display state is derived and not stored twice',()=>{
 const ecus=[{name:'ADAS',enabled:true},{name:'VCU',enabled:false}];
 assert.equal(countEnabled(ecus),1);
 assert.equal(getSelectedEcu(ecus,'VCU')?.enabled,false);
 assert.equal(getSelectedEcu(ecus,'UNKNOWN'),undefined);
 assert.equal(ecus[0].enabled,true);
});
test('every known ECU has a display metadata definition',()=>{
 for(const name of ['ADAS','BCM','CGW','VCU']){
   assert.ok(Object.hasOwn(ECU_METADATA,name));
   assert.ok(ECU_METADATA[name as keyof typeof ECU_METADATA].modules.length>0);
 }
});
