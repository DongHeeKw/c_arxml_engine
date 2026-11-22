import test from 'node:test';
import assert from 'node:assert/strict';
import {ecuVisualState,pointerToNdc} from '../three/Vehicle';

test('enabled ECU has positive color',()=>{
 assert.equal(ecuVisualState(true,false).body,0x08785c);
 assert.equal(ecuVisualState(false,false).body,0x46586e);
});
test('selection outline wins over status outline',()=>{
 assert.equal(ecuVisualState(false,true).edge,0xffcf45);
 assert.equal(ecuVisualState(true,true).edge,0xffcf45);
 assert.notEqual(ecuVisualState(true,false).edge,ecuVisualState(false,false).edge);
});
test('canvas pixels map to normalized raycasting coordinates',()=>{
 const rect={left:10,top:20,width:200,height:100};
 assert.deepEqual(pointerToNdc(110,70,rect),{x:0,y:0});
 assert.deepEqual(pointerToNdc(10,20,rect),{x:-1,y:1});
 assert.deepEqual(pointerToNdc(210,120,rect),{x:1,y:-1});
 assert.throws(()=>pointerToNdc(2,3,{left:0,top:0,width:0,height:100}));
});
