/* SPDX-License-Identifier: MIT: temporary local TCP/UDP test peer, no packages. */
'use strict';
const net=require('node:net'),dgram=require('node:dgram');
let active=0;
const tcp=net.createServer(s=>{
 if(active>=4){s.destroy();return;}active++;let count=0,ended=false;
 s.setTimeout(10000,()=>s.destroy());s.on('error',()=>{});s.on('close',()=>active--);
 s.on('data',b=>{if(ended)return;count+=b.length;for(let i=0;i<b.length;i+=17)s.write(b.subarray(i,i+17));if(count>=512){ended=true;s.end();}});
});
const udp=dgram.createSocket('udp4');
tcp.on('error',e=>{console.error(e.message);process.exit(1);});udp.on('error',e=>{console.error(e.message);process.exit(1);});
udp.on('message',(data,peer)=>{if(data.length<=1200)udp.send(data,peer.port,peer.address);});
tcp.listen(17780,'0.0.0.0',()=>console.log('TCP echo :17780'));
udp.bind(17781,'0.0.0.0',()=>console.log('UDP echo :17781'));
setTimeout(()=>process.exit(0),600000);
