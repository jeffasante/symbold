// node web/test.mjs  -> checks the wasm build against samples/expected.txt (label and cost).
import { readFileSync } from "node:fs";
import { createRuntime } from "./symbold.js";

const at = (p) => new URL(p, import.meta.url);
const rt = await createRuntime(readFileSync(at("./symbold.wasm")));
rt.loadProgram(readFileSync(at("../models/tree.sdp")));
const images = readFileSync(at("../samples/mnist-test-500.u8"));
const expected = readFileSync(at("../samples/expected.txt"), "utf8").trim().split("\n");

let bad = 0;
expected.forEach((line, i) => {
  const { label, cost } = rt.predict(images.subarray(i * 784, (i + 1) * 784));
  if (`${label} ${cost}` !== line) {
    if (bad++ < 5) console.error(`image ${i}: expected "${line}", got "${label} ${cost}"`);
  }
});

const corrupt = Uint8Array.from(readFileSync(at("../models/tree.sdp")));
corrupt[corrupt.length - 1] ^= 1;
let rejected = false;
try { rt.loadProgram(corrupt); } catch (e) { rejected = /CRC mismatch/.test(e.message); }
if (!rejected) { console.error("corrupt program was not rejected"); bad++; }

if (bad) { console.error(`FAIL: ${bad} mismatches`); process.exit(1); }
console.log(`ok: wasm matches the reference on ${expected.length} images`);
