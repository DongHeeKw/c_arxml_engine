/**
 * Studio UI domain data and pure view state transformations.
 * Keep one file for the "scene selection" feature, not one file per control.
 * No React, Electron, or WebGL dependencies -> fast deterministic tests.
 */
export const ECU_METADATA = {
  ADAS:{controller:'TC397',network:'CAN FD',role:'Advanced driver assistance',modules:['CanIf','PduR','Com','Dcm']},
  BCM:{controller:'TC375',network:'CAN FD',role:'Body control',modules:['CanIf','PduR','Com','Dem']},
  CGW:{controller:'TC397',network:'CAN FD / Ethernet',role:'Central gateway',modules:['CanIf','PduR','SoAd','Dcm']},
  VCU:{controller:'TC387',network:'CAN FD',role:'Vehicle control',modules:['CanIf','PduR','Com','Dcm']}
} as const;
export type EcuName = keyof typeof ECU_METADATA;
export type Ecu = { name:string; enabled:boolean };
export type HistoryEntry = {id:number;ecu:string;before:boolean;after:boolean;at:string};

export const SCENES = [
 {id:'studio-day',name:'Studio',description:'Light / vehicle cutaway'},
 {id:'topology-map',name:'Topology',description:'Top-down network map'},
 {id:'module-detail',name:'ECU Detail',description:'Component inspection'},
 {id:'inspection-night',name:'Night',description:'Low-light inspection'},
 {id:'live-3d',name:'Live 3D',description:'Interactive geometry'}
] as const;
export type SceneId=typeof SCENES[number]['id'];
export const DEFAULT_SCENE:SceneId='studio-day';

/** Choosing prev/next always wraps, so navigation has no invalid state. */
export function stepScene(current:SceneId, direction:-1|1):SceneId {
 const index=SCENES.findIndex(scene=>scene.id===current);
 if(index<0) return DEFAULT_SCENE;
 return SCENES[(index+direction+SCENES.length)%SCENES.length].id;
}
export function countEnabled(ecus:readonly Ecu[]):number {
 return ecus.filter(ecu=>ecu.enabled).length;
}
export function getSelectedEcu(ecus:readonly Ecu[], selected:string):Ecu|undefined {
 return ecus.find(ecu=>ecu.name===selected);
}
export function sceneAssetPath(scene:SceneId):string|null {
 return scene==='live-3d'?null:'./assets/'+scene+'.svg';
}
