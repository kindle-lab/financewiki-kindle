// SPDX-License-Identifier: GPL-3.0-or-later
import {readFile} from 'node:fs/promises';
import assert from 'node:assert/strict';
const b=await readFile(process.argv[2]);
assert.equal(b.subarray(0,4).toString('hex'),'7f454c46');
assert.equal(b[4],1,'ELF32'); assert.equal(b[5],1,'little endian');
assert.equal(b.readUInt16LE(18),40,'ARM machine');
const flags=b.readUInt32LE(36);
assert.equal(flags>>>24,5,'EABI5'); assert.ok(flags&0x400,'hard-float ABI');
const off=b.readUInt32LE(28),size=b.readUInt16LE(42),count=b.readUInt16LE(44);
for(let i=0;i<count;i++) assert.notEqual(b.readUInt32LE(off+i*size),3,'No dynamic interpreter');
console.log(`ELF verified: 32-bit ARM little-endian EABI5 hard-float, static; ${b.length} bytes`);
