import { useEffect, useRef } from 'react';
import * as THREE from 'three';
import type { Ecu } from '../react/App';
const locations:Record<string,[number,number]>={ADAS:[0,-2.6],BCM:[-0.78,-0.55],CGW:[0.78,-0.55],VCU:[0,2.45]};
// Pure, testable view-model: React state decides colors; Three.js only draws them.
export function ecuVisualState(enabled:boolean,selected:boolean){return {body:enabled?0x08785c:0x46586e,edge:selected?0xffcf45:enabled?0x50e6ad:0x8292a6};}
export function pointerToNdc(clientX:number,clientY:number,rect:{left:number;top:number;width:number;height:number}){if(rect.width<=0||rect.height<=0)throw Error('Invalid canvas bounds');return {x:(clientX-rect.left)/rect.width*2-1,y:-(clientY-rect.top)/rect.height*2+1};}
export function Vehicle({ecus,selected,onSelect}:{ecus:Ecu[];selected:string;onSelect:(name:string)=>void}){
 const host=useRef<HTMLDivElement>(null);
 const labels=useRef(new Map<string,{mesh:THREE.Mesh<THREE.BoxGeometry,THREE.MeshStandardMaterial>;outline:THREE.LineSegments<THREE.EdgesGeometry,THREE.LineBasicMaterial>}>());
 const callback=useRef(onSelect); callback.current=onSelect;
 useEffect(()=>{
  const node=host.current!; let renderer:THREE.WebGLRenderer;
  try{renderer=new THREE.WebGLRenderer({antialias:true});}catch(e){node.textContent='WebGL unavailable: '+String(e);return;}
  const scene=new THREE.Scene();scene.background=new THREE.Color('#101827');
  const camera=new THREE.OrthographicCamera(-4,4,5,-5,0.1,100);
  camera.position.set(3,11,5);camera.lookAt(0,0,0);
  renderer.setPixelRatio(Math.min(window.devicePixelRatio,2));node.appendChild(renderer.domElement);
  scene.add(new THREE.HemisphereLight(0xffffff,0x556677,2));
  const light=new THREE.DirectionalLight(0xffffff,2);light.position.set(3,8,-5);scene.add(light);
  const grid=new THREE.GridHelper(12,24,0x34465b,0x1e2b3c);grid.position.y=-0.4;scene.add(grid);
  function box(w:number,h:number,d:number,x:number,y:number,z:number,color:number,opacity=1){const m=new THREE.Mesh(new THREE.BoxGeometry(w,h,d),new THREE.MeshStandardMaterial({color,transparent:opacity<1,opacity}));m.position.set(x,y,z);scene.add(m);return m;}
  // Vehicle body, cabin, windshield, rear window and wheels: simple box geometry.
  box(2.5,.35,6.6,0,0,0,0x8296ad,.38);
  box(2.1,.7,3,0,.42,.05,0x6f9dbb,.25);
  box(2.1,.2,1.1,0,.26,-2.5,0xa2b8c9,.25);
  box(2.1,.2,1,0,.26,2.5,0xa2b8c9,.25);
  for(const x of [-1.32,1.32])for(const z of [-2.15,2.15]){const wheel=new THREE.Mesh(new THREE.CylinderGeometry(.48,.48,.36,24),new THREE.MeshStandardMaterial({color:0x090e16}));wheel.rotation.z=Math.PI/2;wheel.position.set(x,0,z);scene.add(wheel);}
  // CAN bus: independent line geometry.
  const points=[new THREE.Vector3(0,.65,-2.6),new THREE.Vector3(0,.65,2.45),new THREE.Vector3(0,.65,-.55),new THREE.Vector3(-.78,.65,-.55),new THREE.Vector3(.78,.65,-.55)];
  const bus=new THREE.LineSegments(new THREE.BufferGeometry().setFromPoints([points[0],points[1],points[3],points[4]]),new THREE.LineDashedMaterial({color:0x22d3ee,dashSize:.15,gapSize:.08}));bus.computeLineDistances();scene.add(bus);
  for(const [name,[x,z]] of Object.entries(locations)){
   const mesh=box(.95,.28,.6,x,.68,z,0x46586e);mesh.userData.name=name;
   const outline=new THREE.LineSegments(new THREE.EdgesGeometry(mesh.geometry),new THREE.LineBasicMaterial({color:0x8da1b5}));mesh.add(outline);
   labels.current.set(name,{mesh,outline});
   const canvas=document.createElement('canvas');canvas.width=256;canvas.height=96;const ctx=canvas.getContext('2d')!;ctx.fillStyle='#172434';ctx.fillRect(0,0,256,96);ctx.font='bold 46px sans-serif';ctx.fillStyle='#ffffff';ctx.textAlign='center';ctx.textBaseline='middle';ctx.fillText(name,128,48);
   const sprite=new THREE.Sprite(new THREE.SpriteMaterial({map:new THREE.CanvasTexture(canvas),depthTest:false}));sprite.position.set(x,1.15,z);sprite.scale.set(1.05,.4,1);scene.add(sprite);
  }
  const ray=new THREE.Raycaster(),pointer=new THREE.Vector2();
  function click(e:MouseEvent){const bounds=renderer.domElement.getBoundingClientRect();const normalized=pointerToNdc(e.clientX,e.clientY,bounds);pointer.set(normalized.x,normalized.y);ray.setFromCamera(pointer,camera);const hit=ray.intersectObjects([...labels.current.values()].map(v=>v.mesh),false)[0];if(hit)callback.current(hit.object.userData.name);}
  renderer.domElement.addEventListener('click',click);
  function resize(){const w=Math.max(1,node.clientWidth),h=Math.max(1,node.clientHeight);renderer.setSize(w,h);const aspect=w/h,half=Math.max(4.8,3.6/aspect);camera.left=-half*aspect;camera.right=half*aspect;camera.top=half;camera.bottom=-half;camera.updateProjectionMatrix();}
  const observer=new ResizeObserver(resize);observer.observe(node);resize();
  renderer.setAnimationLoop(()=>renderer.render(scene,camera));
  return ()=>{observer.disconnect();renderer.setAnimationLoop(null);renderer.domElement.removeEventListener('click',click);scene.traverse(o=>{if(o instanceof THREE.Mesh||o instanceof THREE.LineSegments){o.geometry.dispose();const mats=Array.isArray(o.material)?o.material:[o.material];mats.forEach(m=>m.dispose());}if(o instanceof THREE.Sprite){o.material.map?.dispose();o.material.dispose();}});renderer.dispose();renderer.domElement.remove();labels.current.clear();};
 },[]);
 // React state -> actual Three.js material update, only after backend response.
 useEffect(()=>{for(const [name,{mesh,outline}] of labels.current){const enabled=ecus.find(e=>e.name===name)?.enabled??false;const style=ecuVisualState(enabled,name===selected);mesh.material.color.set(style.body);outline.material.color.set(style.edge);}console.log('[Three.js] synchronized state',ecus,selected);},[ecus,selected]);
 return <div ref={host} style={{width:'100%',height:'100%'}}/>;
}
