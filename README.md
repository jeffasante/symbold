# symbold

A handwritten-digit classifier that is a small **program**, not a neural network.

`models/tree.sdp` is 1407 bytecode instructions (16.9 KB) that read integer
features of a 28×28 image and branch on them until they emit a digit. This repo
contains the runtime that executes such programs: a dependency-free C99
interpreter, its WebAssembly build, and a minimal browser UI where you draw a
digit and see which instructions were executed.

| | |
|---|---|
| MNIST test accuracy (10,000 images) | 92.66% |
| Program size | 1407 instructions, 16,916 bytes |
| Instructions executed per image | 12.4 mean, 21 max |
| Feature extraction | ~5.5 µs per image (C, -O2, laptop CPU) |
| Program execution | ~0.15 µs per image |

The program was produced offline by a separate training pipeline, which is not
part of this repository. The bytecode format is fully specified in
[docs/program-format.md](docs/program-format.md), so any producer can target
this runtime.

## Layout

```
c/include/symbold.h     public API
c/src/interpreter.c     program validation (CRC-32, bounds, branch targets) and execution
c/src/features.c        feature set 1: the 956 integer features programs read
c/src/main.c            command-line tool
c/src/wasm.c            WebAssembly exports
web/                    browser UI, JS loader, prebuilt symbold.wasm, Node test
docs/program-format.md  bytecode format spec (v1)
models/tree.sdp         the trained program
samples/                first 500 MNIST test images and labels, plus expected outputs
```

## C runtime

Needs only a C99 compiler.

```sh
make -C c                    # builds c/build/symbold
make -C c test               # ASan/UBSan build, checks outputs against samples/expected.txt

c/build/symbold check   models/tree.sdp
c/build/symbold predict models/tree.sdp samples/mnist-test-500.u8
c/build/symbold bench   models/tree.sdp samples/mnist-test-500.u8 samples/mnist-test-500.labels
```

Image files are raw: 784 bytes per image, row-major, one byte per pixel
(0 = background). Label files are one byte per image.

| Command | Output |
|---|---|
| `check MODEL` | validates the program and prints its shape |
| `features IMAGES OUT` | writes 956 little-endian int32 features per image |
| `run MODEL FEATURES` | runs on precomputed features; prints `label cost` per image |
| `predict MODEL IMAGES` | features + run; prints one label per image |
| `bench MODEL IMAGES LABELS` | accuracy, latency and peak memory as JSON |

A label of `255` means UNKNOWN (the program declined to answer). `cost` is the
number of instructions executed.

Library use:

```c
#include "symbold.h"

sd_program p;
char err[256];
if (sd_program_load("models/tree.sdp", &p, err, sizeof err) != 0) { /* err says why */ }

int32_t x[SD_N_FEATURES];
sd_features(image /* 784 bytes */, x);
uint32_t cost;
uint8_t label = sd_run(&p, x, &cost);
sd_program_free(&p);
```

Programs are validated once at load time (header, CRC-32, every opcode, feature
index and branch target), so `sd_run` does no checks and cannot read out of
bounds or loop.

## WebAssembly

A prebuilt `web/symbold.wasm` (33 KB) is checked in. To rebuild it you need an
LLVM clang with the wasm32 target, `wasm-ld`, and a WASI sysroot:

```sh
brew install llvm lld wasi-libc wasi-runtimes   # macOS
make -C c wasm                                  # builds and copies to web/symbold.wasm
node web/test.mjs                               # checks wasm outputs against samples/expected.txt
```

On other systems, override `WASM_CC`, `WASI_SYSROOT` and `WASM_RESOURCE_DIR`
in [c/Makefile](c/Makefile). The module needs no real WASI host;
[web/symbold.js](web/symbold.js) provides stubs.

```js
import { createRuntime } from "./symbold.js";
const rt = await createRuntime(wasmBytes);
rt.loadProgram(programBytes);              // throws with the validator's message on a bad program
const { label, cost } = rt.predict(image); // image: 784 bytes
```

## Browser UI

Serve the repository root with any static file server and open `/web/`:

```sh
npx serve .        # or: python3 -m http.server
```

Draw a digit, or press **MNIST test sample** to load one of the bundled test
images. The drawing is scaled into a 20×20 box and centred by mass in 28×28,
as MNIST digits are. The page shows the predicted digit, the number of
instructions executed, and the binarized input the program saw.

## Verifying

`samples/expected.txt` holds the reference `label cost` for each of the 500
sample images, produced by the training pipeline's own interpreter. Both
`make -C c test` and `node web/test.mjs` must reproduce it exactly. Accuracy on
these 500 images is 91.2%.

## Data

`samples/` contains the first 500 images of the MNIST test set
(Yann LeCun, Corinna Cortes and Christopher J.C. Burges), converted to raw bytes.
