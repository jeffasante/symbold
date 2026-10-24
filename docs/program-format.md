# symbold program format, version 1

This document is the contract between whatever produces programs and every
runtime that executes them (the C runtime in `c/` and its WebAssembly build).
All implementations must accept exactly the same files, reject exactly the
same files, and produce the same label for the same input.

A learned classifier has two parts:

1. A **feature set**: a deterministic function from a 28 x 28 image to a
   vector of signed 32-bit integers. Version 1 defines feature set `1`.
2. A **program**: a sequence of instructions that reads that vector and
   returns a label.

The file stores the program and names the feature set it expects. It does not
contain the feature extractor.

## 1. File layout

All integers are little-endian. A file is a 32-byte header followed by
`n_instructions` 12-byte instructions and nothing else. The file length must
be exactly `32 + 12 * n_instructions`.

### Header (32 bytes)

| Offset | Type      | Field            | Rule                                        |
|-------:|-----------|------------------|---------------------------------------------|
| 0      | u8[4]     | `magic`          | ASCII `SDPG`                                |
| 4      | u16       | `version`        | `1`                                         |
| 6      | u16       | `header_size`    | `32`                                        |
| 8      | u32       | `feature_set_id` | Feature set the program reads (see §4)      |
| 12     | u32       | `n_features`     | Length of the feature vector, 1..65536      |
| 16     | u32       | `n_instructions` | 1..1048576                                  |
| 20     | u16       | `n_labels`       | `10`                                        |
| 22     | u16       | `flags`          | `0`                                         |
| 24     | u32       | `crc32`          | CRC-32 (IEEE 802.3, the zlib/PNG CRC) of the instruction bytes |
| 28     | u32       | `reserved`       | `0`                                         |

### Instruction (12 bytes)

| Offset | Type | Field       |
|-------:|------|-------------|
| 0      | u8   | `op`        |
| 1      | u8   | `label`     |
| 2      | u16  | `feature`   |
| 4      | i32  | `threshold` |
| 8      | u32  | `target`    |

## 2. Instruction set

The machine has a program counter `pc` (starting at 0) and one Boolean
register `flag` (starting false). `x` is the feature vector.

| op | Name      | Operands                    | Effect                                               |
|---:|-----------|-----------------------------|------------------------------------------------------|
| 1  | `RETURN`  | `label`                     | Halt and return `label`                              |
| 2  | `BR_GT`   | `feature, threshold, target`| If `x[feature] > threshold` then `pc = target`, else `pc += 1` |
| 3  | `LOAD_LE` | `feature, threshold`        | `flag = x[feature] <= threshold`                     |
| 4  | `AND_LE`  | `feature, threshold`        | `flag = flag and x[feature] <= threshold`            |
| 5  | `OR_LE`   | `feature, threshold`        | `flag = flag or x[feature] <= threshold`             |
| 6  | `NOT`     | —                           | `flag = not flag`                                    |
| 7  | `JUMP_IF` | `target`                    | If `flag` then `pc = target`, else `pc += 1`         |
| 8  | `JUMP`    | `target`                    | `pc = target`                                        |

Comparisons are between signed 32-bit integers. There is no arithmetic, so
there is nothing to overflow and no floating point.

`BR_GT` is a fused compare-and-branch used by compiled decision trees. The
`flag` instructions express rule lists of the form
`if (a and b) or not c: return k`.

### Labels

`label` is `0`..`9` for a digit, or `255` for `UNKNOWN` ("no sufficiently
supported decision"). `UNKNOWN` is a valid prediction and is never correct.

## 3. Validation

A loader must reject the file, before executing anything, unless all of the
following hold:

1. The header rules in §1 hold and the length is exact.
2. The CRC-32 matches.
3. `op` is 1..8.
4. Every operand an instruction does **not** use is zero (see the table in §2;
   for example `NOT` has all four operands zero, `RETURN` has `feature`,
   `threshold` and `target` zero).
5. `feature < n_features` for `BR_GT`, `LOAD_LE`, `AND_LE`, `OR_LE`.
6. `label` is 0..9 or 255 for `RETURN`.
7. Every `target` satisfies `pc < target < n_instructions` (jumps go strictly
   forward).
8. The last instruction is `RETURN`.

Rules 7 and 8 together guarantee that every execution terminates after at most
`n_instructions` steps and never runs off the end of the program, so the
interpreter needs no step limit and no bounds checks once a program is valid.
Rule 4 keeps every program's encoding canonical, so two loaders cannot
disagree about a file one of them would have ignored bytes in.

The **cost** of an execution is the number of instructions executed,
including the final `RETURN`. The **size** of a program is `n_instructions`.

## 4. Feature set 1

Input: a 28 x 28 image of unsigned bytes in row-major order, `0` = background
and `255` = full ink (MNIST convention).

Binarise first: `B[r][c] = 1` if `pixel >= 128`, else `0`. Every feature
below is computed from `B` alone. Rows `r` and columns `c` run 0..27.
Division is integer division of non-negative values (truncation). Where a
feature is undefined because there is no ink (or no hole), its value is `0`.

| Index       | Count | Name             | Definition                                                      |
|-------------|------:|------------------|-----------------------------------------------------------------|
| 0..783      | 784   | `px[r,c]`        | `B[r][c]`, index `28r + c`                                      |
| 784..832    | 49    | `cell[i,j]`      | Ink count in the 4 x 4 block at rows `4i..4i+3`, cols `4j..4j+3`; index `784 + 7i + j` |
| 833..860    | 28    | `row_ink[r]`     | Ink count in row `r`                                            |
| 861..888    | 28    | `col_ink[c]`     | Ink count in column `c`                                         |
| 889..916    | 28    | `row_runs[r]`    | Number of maximal horizontal runs of ink in row `r`             |
| 917..944    | 28    | `col_runs[c]`    | Number of maximal vertical runs of ink in column `c`            |
| 945         | 1     | `ink`            | Total ink count                                                 |
| 946         | 1     | `components`     | Connected components of ink, 8-connectivity                     |
| 947         | 1     | `holes`          | Connected components of background, 4-connectivity, that do not touch the image border |
| 948         | 1     | `bbox_height`    | `max r - min r + 1` over ink pixels                             |
| 949         | 1     | `bbox_width`     | `max c - min c + 1` over ink pixels                             |
| 950         | 1     | `centroid_row`   | `(sum of r over ink pixels) / ink`                              |
| 951         | 1     | `centroid_col`   | `(sum of c over ink pixels) / ink`                              |
| 952         | 1     | `mirror_lr`      | Ink pixels whose left-right mirror `B[r][27-c]` is also ink     |
| 953         | 1     | `mirror_tb`      | Ink pixels whose top-bottom mirror `B[27-r][c]` is also ink     |
| 954         | 1     | `hole_area`      | Number of background pixels belonging to holes                  |
| 955         | 1     | `hole_row`       | `(sum of r over hole pixels) / hole_area`                       |

`n_features` is `956`. A runtime implementing feature set 1 must refuse a
program whose header names a different feature set or feature count.

The 8-connected ink / 4-connected background pairing is deliberate: it is
the pairing under which a closed loop of diagonally touching pixels encloses
a hole.

## 5. Changing the format

Any change to the header, the instruction encoding, an opcode's meaning or
the validation rules requires a new `version`. Any change to a feature's
definition, order or the binarisation threshold requires a new
`feature_set_id`. Old programs must then be rejected, not reinterpreted.
