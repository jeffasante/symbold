# symbold

A handwritten-digit classifier that is a small **program**, not a neural network.

`models/tree.sdp` is 1407 bytecode instructions (16.9 KB) that read integer
features of a 28×28 image and branch on them until they emit a digit. This repo
contains the runtime that executes such programs: a dependency-free C99
interpreter, its WebAssembly build, and a minimal browser UI where you draw a
digit and see which instructions were executed.

**Try it in the browser: https://jeffasante.github.io/symbold/**

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

## Why symbolic reasoning?

*Symbolic* means the model is made of things a person can name and read:
"how much ink is in row 25", "is there a closed loop", "is the digit wider
than 7 pixels". The model reasons by asking these questions in order, like a
person following a checklist.

A neural network works differently. It answers with millions of
multiplications on decimal numbers. You cannot read them, and nobody can say in
words why it gave its answer.

symbold answers with a short list of yes/no questions about the image. Here is
the full path for the first test image, which is a 7:

```
step  question                 answer
   0  row_ink[25]  =  3 > 0 ?  yes   ink near the bottom
1132  hole_area    =  0 > 1 ?  no    no closed loop
1133  px[14,12]    =  0 > 0 ?  no    centre is empty
1134  px[16,11]    =  0 > 0 ?  no
1135  px[13,13]    =  0 > 0 ?  no
1136  px[15,12]    =  0 > 0 ?  no
1137  px[17,10]    =  0 > 0 ?  no
1138  col_runs[14] =  2 > 2 ?  no
1139  bbox_width   = 16 > 7 ?  yes   digit is wide
1143  row_runs[17] =  1 > 2 ?  no
1144  cell[3,3]    =  0 > 8 ?  no
1145  answer 7
```

(`px[r,c]` is one pixel, `row_ink[r]` counts ink in row `r`, `cell[i,j]`
counts ink in a 4×4 block, `bbox_width` is the width of the digit.)

That is 12 steps out of 1407. Every answer has a path like this, so a
person can check *why* the program decided.

What you get from this:

- **You can read it.** Each step compares one named number with one whole
  number. No hidden weights.
- **It is tiny and fast.** 17 KB. About 12 steps per image. Whole numbers
  only: no decimals, no matrix maths, no libraries.
- **Same answer everywhere.** Whole numbers only, so the C build, the
  WebAssembly build and the training code give exactly the same answer and the
  same step count on every image. This was checked on all 10,000 test images.
- **Safe to run.** A program is checked once when it is loaded. After that it
  cannot read outside its data, cannot loop, and always stops.

What you give up: accuracy. This program gets 92.7% on MNIST. A small neural
network gets above 99%. symbold trades some accuracy for a model
you can read, check and run anywhere.

## The maths

### 1. Features: turning the image into whole numbers

First every pixel becomes 0 or 1:

$$
B[r][c] = \begin{cases} 1 & \text{if pixel} \ge 128 \\ 0 & \text{otherwise} \end{cases}
$$

Then 956 whole numbers are computed from $B$: the pixels themselves, ink
counts per row, column and $4 \times 4$ block, how many separate strokes cross
each row and column, the number of closed loops (*holes*, as in 0, 6, 8, 9),
the size of the digit's box, its centre, and how symmetric it is. The exact
list is in [docs/program-format.md §4](docs/program-format.md#4-feature-set-1).
Call this list $x = (x_0, x_1, \dots, x_{955})$.

### 2. The program: a list of questions

The main instruction is `BR_GT f t j`:

$$
\text{if } x_f > t \text{ then go to step } j \text{, else go to the next step}
$$

and `RETURN k` stops with the answer $k$. Jumps only go **forward**
($j > \text{current step}$), so a program with $n$ steps always stops after at
most $n$ steps.

### 3. Learning: grow a big tree

Training builds a decision tree on 50,000 MNIST images. It starts with all
images in one group. It picks the question "$x_f > t$?" that best splits the
group, then does the same again for each half, up to 24 questions deep.

"Best" means each half is as close as possible to holding only one digit. This
is measured by the *Gini impurity* of a group:

$$
G = 1 - \sum_{k=0}^{9} p_k^2
$$

where $p_k$ is the share of digit $k$ in the group. $G = 0$ means only one
digit is left. $G$ is large when the digits are well mixed. A split into a left group ($n_L$ images) and a right group
($n_R$ images) is scored by

$$
\frac{n_L}{n}\,G_L + \frac{n_R}{n}\,G_R
$$

and the split with the lowest score wins. Thresholds $t$ are only values that
actually occur in the training data, so they are whole numbers.

The grown tree had 4755 instructions. That is too big, and it has learned
noise.

### 4. Pruning: trade accuracy against size

For a program $P$, let

$$
J(P) = \underbrace{\frac{\text{training mistakes}}{N}}_{\text{error rate}} + \lambda \cdot \underbrace{\text{size}(P)}_{\text{instructions}}
$$

$\lambda$ is the price of one instruction. A question is only kept if it fixes
enough mistakes to pay for itself.

For a fixed $\lambda$ the best way to cut the tree down can be found
**exactly** in one pass from the bottom of the tree up. At each question
compare

$$
J_{\text{leaf}} = \frac{e}{N} + \lambda
\qquad\text{and}\qquad
J_{\text{branch}} = J(\text{left}) + J(\text{right}) + \lambda
$$

where $e$ is the number of mistakes if the node just returns its most common
digit, and the extra $\lambda$ pays for the one `BR_GT` instruction. Keep
whichever is smaller.

Training tries 26 values of $\lambda$ (0, and 25 values from $10^{-6}$ to
$10^{-2}$) and keeps the program with the best accuracy on 10,000 separate
validation images. The 10,000 test images are never used for any choice. The
winner was $\lambda \approx 2.15 \times 10^{-5}$: 4755 instructions down to
1407.

### 5. Tree to bytecode

The tree is written out top to bottom, each question followed by its "no"
side and then its "yes" side. So "no" means "go to the next step" and "yes"
means "jump forward". A tree with $L$ answers has $L - 1$
questions, so

$$
\text{size} = 2L - 1 \qquad (1407 = 2 \cdot 704 - 1)
$$

The number of steps for one image is the depth of the answer it reaches, plus
one. Here that is 12.4 on average and never more than 21.

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
