// SPDX-License-Identifier: GPL-3.0-or-later
// Uses only disposable mock commands and child processes; never Kindle services.
import {mkdtemp, mkdir, writeFile, copyFile, readFile, access} from 'node:fs/promises';
import {spawn} from 'node:child_process';
import {once} from 'node:events';
import path from 'node:path';
import assert from 'node:assert/strict';
const root=await mkdtemp(path.resolve('build/launcher-test-'));
async function run(mode) {
  const dir=path.join(root,mode);
  await mkdir(dir,{recursive:true});
  await copyFile('launch.sh',path.join(dir,'launch.sh'));
  const awesome=spawn('/bin/sleep',['60']);
  const pid=awesome.pid;
  await mkdir(path.join(dir,'proc',String(pid)),{recursive:true});
  await writeFile(path.join(dir,'proc',String(pid),'status'),'State:\tS (sleeping)\n');
  const mock=path.join(dir,'mock.sh');
  await writeFile(mock,`#!/bin/sh
printf '%s %s\\n' "$(basename "$0")" "$*" >> "$FW_TEST_EVENTS"
case "$(basename "$0")" in
 pidof) printf '%s\\n' "$FW_TEST_AWESOME";;
 status) printf '%s\\n' 'statusbar start/running, process 123';;
 lipc) if [ "$FW_TEST_MODE" = fail-disable ] && [ "$3" = disable ]; then exit 1; fi;;
esac
`,{mode:0o755});
  const env={...process.env,FW_LAUNCH_TEST:'1',FW_TEST_MODE:mode,
    FW_TEST_AWESOME:String(pid),FW_TEST_EVENTS:path.join(dir,'events'),
    FW_TEST_LOCK:path.join(dir,'lock'),FW_TEST_PROC:path.join(dir,'proc'),
    FW_TEST_BINARY:path.join(dir,'binary'),FW_TEST_SYNC_BINARY:path.join(dir,'sync'),FW_TEST_READY:path.join(dir,'ready')};
  for(const name of ['status','start','stop','lipc','pidof','power']) {
    const file=path.join(dir,name); await copyFile(mock,file); env[`FW_TEST_${name.toUpperCase()}`]=file;
  }
  await writeFile(env.FW_TEST_BINARY,`#!/bin/sh
case "$1" in
 --probe) [ "$FW_TEST_MODE" != fail-probe ]; exit $?;;
 --progress) cat >/dev/null; exit 0;;
 --bounded-command) shift; exec "$@";;
esac
printf '%s\\n' "$PPID" > "$FW_TEST_READY"
case "$FW_TEST_MODE" in
 crash) exit 139;;
 signal) exec /bin/sleep 30;;
 *) exit 0;;
esac
`,{mode:0o755});
  await writeFile(env.FW_TEST_SYNC_BINARY,'#!/bin/sh\nexit 0\n',{mode:0o755});
  let stderr='';
  const child=spawn('/bin/sh',[path.join(dir,'launch.sh')],{env});
  child.stderr.on('data',d=>stderr+=d);
  const finished=once(child,'exit');
  const timeout=setTimeout(()=>child.kill('SIGKILL'),8000);
  try {
    if(mode==='signal') {
      for(let i=0;i<200;i++) {
        try {await access(env.FW_TEST_READY);break;} catch {}
        await new Promise(r=>setTimeout(r,10));
      }
      child.kill('SIGTERM');
    }
    const [code,signal]=await finished;
    assert.equal(signal,null,stderr);
    assert.equal(code,mode==='normal'?0:mode==='crash'?139:mode==='signal'?143:1);
    let events=''; try {events=await readFile(env.FW_TEST_EVENTS,'utf8');} catch {}
    /* The screen is taken before sync, so even a failed probe restores it. */
    {
      assert.match(events,/stop statusbar/);
      assert.match(events,/lipc com.lab126.pillow disableEnablePillow disable/);
      assert.match(events,/lipc com.lab126.pillow disableEnablePillow enable/);
      assert.match(events,/start statusbar/);
      if(mode!=='fail-disable') assert.match(events,/lipc com.lab126.appmgrd start app:\/\/com.lab126.booklet.home/);
      if(mode!=='fail-disable' && mode!=='fail-probe') {
        assert.match(events,/lipc com.lab126.powerd preventScreenSaver 1/);
        assert.match(events,/lipc com.lab126.powerd preventScreenSaver 0/);
      }
    }
    await assert.rejects(access(env.FW_TEST_LOCK));
    console.log(`launcher ${mode}: cleanup and ownership restoration passed`);
  } finally {
    clearTimeout(timeout);
    // Only the disposable fake Awesome process belongs to this test.
    awesome.kill('SIGCONT'); awesome.kill('SIGTERM');
    child.kill('SIGKILL');
  }
}
for(const mode of ['normal','crash','signal','fail-probe','fail-disable']) await run(mode);
