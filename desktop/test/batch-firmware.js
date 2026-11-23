const fs=require('node:fs/promises'),path=require('node:path');
const pause=ms=>new Promise(r=>setTimeout(r,ms));
async function run({win,session,store,root,output,inventory}){
 const plan=JSON.parse(await fs.readFile(inventory,'utf8'));
 const destination=path.join(root,'output','firmware-batch');await fs.mkdir(destination,{recursive:true});
 const results=[],seen=new Map();let frame,lastState;
 session.events.on('frame',value=>{frame=value});session.events.on('status',value=>{lastState=value});
 for(const candidate of plan.candidates){
  const result={source:candidate.source};const started=Date.now();frame=null;
  try{
   const item=await store.importFile(candidate.source);result.sha256=item.sha256;
   if(seen.has(item.id)){result.status='duplicate';result.sameAs=seen.get(item.id);results.push(result);continue;}
   seen.set(item.id,candidate.source);result.name=item.name;result.id=item.id;
   session.addFirmware({id:item.id,path:store.imagePath(item)});
   await win.webContents.executeJavaScript(`addImported(${JSON.stringify(item)});switchTo(${JSON.stringify(item.id)})`);
   let seconds=0;
   while(seconds++<35){await pause(1000);if(lastState?.state==='error')break;}
   const log=await fs.readFile(path.join(output,'serial.log'),'utf8').catch(()=>''),stderr=await fs.readFile(path.join(output,'qemu.stderr.log'),'utf8').catch(()=>'');
   const directory=path.join(destination,item.id);await fs.mkdir(directory,{recursive:true});
   await fs.writeFile(path.join(directory,'serial.log'),log);await fs.writeFile(path.join(directory,'qemu.stderr.log'),stderr);
   await fs.writeFile(path.join(directory,'desktop.png'),(await win.webContents.capturePage()).toPNG());
   let lit=0,colors=new Set();if(frame)for(let i=0;i<frame.rgb.length;i+=3){const color=(frame.rgb[i]<<16)|(frame.rgb[i+1]<<8)|frame.rgb[i+2];if(color)lit++;colors.add(color);}
   result.frames=session.frameCount;result.litPixels=lit;result.colors=colors.size;
   result.resetCount=(log.match(/ESP-ROM:/g)||[]).length;
   result.fatal=/Guru Meditation|abort\(\)|assert failed|ESP_ERROR_CHECK failed/i.test(log);
   result.status=lastState?.state==='error'?'emulator-error':result.fatal?'firmware-error':result.resetCount>2?'reset-loop':lit>1000&&colors.size>4?'displayed':'no-display';
   result.screenshot=path.relative(destination,path.join(directory,'desktop.png'));
   result.errors=log.split('\n').filter(line=>/^E \(/.test(line)).map(line=>line.replace(/\x1b\[[0-9;]*m/g,'')).slice(-8);
  }catch(e){result.status='import-rejected';result.error=e.message;}
  finally{await session.stop();}
  result.seconds=Math.round((Date.now()-started)/1000);results.push(result);
  await fs.writeFile(path.join(destination,'results.json'),JSON.stringify(results,null,2));
  console.log(JSON.stringify({name:result.name||path.basename(path.dirname(candidate.source)),status:result.status,seconds:result.seconds}));
 }
 await fs.writeFile(path.join(destination,'results.json'),JSON.stringify(results,null,2));
}
module.exports={run};
