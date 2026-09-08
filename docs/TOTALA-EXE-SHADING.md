# The SHADED unit rasterizer, part one: geometry and the per-vertex shade level

A second, independent read of `0x459C70` and everything it calls, down to but
not including the span fillers. Same binary as `docs/TOTALA-EXE.md`: GOG
release, 1,178,624 bytes, MD5 `8e74a1dffa1f5988624c52048f5b20cd`, image base
`0x400000`.

The headline, up front, because it inverts the previous pass's advice:

**The arithmetic in `FINDINGS-VISUALOPTIONS.md` section 05 is correct. Every
constant, the truncation, the mask, the un-renormalised average -- all of it
byte-verified again here, and it reproduces ARMSOLAR's black right panel
exactly. What is wrong is everything around it.** The exe computes the level
**once per vertex**, from a **smoothed** normal that it deliberately leaves
short, against a light vector it deliberately leaves **un-normalised**, and then
**Gouraud-interpolates the integer row number** across the polygon. RWE does
none of those three things: it flat-shades each triangle from a per-triangle
face normal, normalises both the normal and the light, and models the table as
a straight `0.06875 * k`. Section 15 lists the corrections one by one, with the
numbers each produces on ARMSOLAR.

---

## 01. Method, and what is VERIFIED versus INFERRED

Every instruction quoted below was read out of the flat `.text` listing and the
load-bearing sequences were then checked against the raw file bytes (section 07
prints them). Floats were read out of `.rdata`/`.data` and printed, not assumed.
The whole per-piece pipeline was then transcribed into a standalone Python
program and replayed against the real `armsolar.3do` and the real
`palettes\PALETTE.SHD`; the numbers in sections 09, 13 and 14 come from that
replay, not from arithmetic done in my head.

Labels used throughout:

- **VERIFIED** -- read off the instructions (and, where it matters, the bytes).
- **INFERRED** -- a name or purpose attached to something whose mechanism is
  verified but whose meaning is not directly evidenced.
- **NOT FOUND** -- looked for, not present. Stated as such rather than guessed.

Pivot addresses for redoing this:

| Address | What |
|---|---|
| `0x4586A0` | draw one model instance; the SHADING branch at `0x458744` |
| `0x459C70` | the shaded model driver -- this document |
| `0x459830` | the unshaded twin, same structure, 12-byte points |
| `0x4B6F00` | vector subtract, integer in, float out |
| `0x4B6F70` | cross product |
| `0x4B6FF0` | normalize, **no zero-length guard** |
| `0x4E43A0` | `_ftol`, truncate toward zero |
| `0x5065F8` / `FC` / `0x506600` | the light vector components |
| `0x4FD4CC` | the 5.0 multiplier |
| `0x4C8BB0` | shaded textured-quad rasterizer -- the gradient setup |
| `0x4C0C70` | shaded flat-colour n-gon -- same treatment of the shade field |
| `0x480DF0` / `0x480E70` | COB `SHADE`/`DONT_SHADE` setter and getter |
| `0x480DB0` | COB `CACHE`/`DONT_CACHE` setter |
| `0x45AEC0` | the piece-list builder, where the default flags are set |
| `0x4B1288` / `0x4B128E` | the COB opcode dispatch for `SHADE` / `DONT_SHADE` |

---

## 02. The call, and its four arguments

VERIFIED. `0x4586A0` picks the rasterizer and passes four arguments plus `this`:

```
458736:  mov  ecx,[ebp+0x110]              ; ebp = the unit
45873c:  test ecx,0x20000000               ; "this drawable has a unit record"
458742:  je   0x458779
458744:  mov  edx,ds:0x511de8
45874a:  test byte [edx+0x37f06],0x20      ; the SHADING option
458751:  je   0x458779
458753:  mov  ecx,[esp+0x28]               ; arg3 of 0x4586A0
458757:  xor  edx,edx
458759:  mov  dl,[ebp+0xff]                ; the owning player index
45875f:  push ecx
458760:  push edx
458761:  push edi                          ; the drawable
458762:  push eax                          ; the destination surface
458763:  mov  ecx,esi
458765:  call 0x459C70
```

so, inside `0x459C70`:

| Slot | Contents |
|---|---|
| `[esp+0x159E8]` | arg1, the destination surface |
| `[esp+0x159EC]` | arg2, the drawable |
| `[esp+0x159F0]` | arg3, `[unit+0xFF]`, the owning player index |
| `[esp+0x159F4]` | arg4, the *pass selector*: `-1`, `0` or `1` (section 12) |

All four callers of `0x4586A0` were checked: `0x45890C`, `0x45936E` and
`0x4595FC` pass arg3 = **1**; `0x459670` passes **-1**. There is no caller
passing 0 into the shaded path. VERIFIED.

Surface layout as this routine uses it, VERIFIED: `+0x00` width (word),
`+0x02` height (word), `+0x04` originX (word), `+0x06` originY (word),
`+0x10` the 8-bit pixel plane, `+0x14` the 8-bit height plane.

---

## 03. The frame: four arrays of exactly 2000

VERIFIED. `mov eax,0x159d4 / call 0x4e4b20` is the stack probe; the frame is
88,532 bytes and holds four per-piece arrays:

| Base | Stride | Count | What |
|---|---|---|---|
| `esp+0x224` | 12 | 2000 | `vertexNormal[]`, three float32 |
| `esp+0x5FE4` | 12 | 2000 | `faceNormal[]`, three float32, indexed by **primitive** |
| `esp+0xBDA4` | 4 | 2000 | `count[]`, int32, faces touching each vertex |
| `esp+0xDCE4` | 16 | 2000 | `point[]`, the projected vertex records |

The point array ends exactly at the first saved register, so 2000 is the real
capacity of all four. A piece with more than 2000 vertices or 2000 primitives
smashes this frame; the exe does not check.

A fifth, small array at `esp+0x94` (0x190 bytes, 25 records of 16) is the
scratch the current primitive's corners are copied into before the rasterizer
call.

Piece iteration runs **backwards**, last piece first (`sub ecx,0x36` at
`0x45A3ED`), stride `0x36` = 54 bytes, first piece at `drawable+0x22`.

Piece record, VERIFIED from this routine plus the COB accessors at
`0x480C30`-`0x480E70`:

| Offset | What |
|---|---|
| `+0x00` | pointer to the 3DO object |
| `+0x04`/`+0x08`/`+0x0C` | COB `MOVE` offsets, x/y/z, 16.16 |
| `+0x10`/`+0x12`/`+0x14` | COB `TURN` angles, words |
| `+0x16`...`+0x21` | accumulated transform offsets |
| `+0x22` | the working (transformed) vertex buffer |
| `+0x26` | word, "already transformed" |
| `+0x28` | **flags byte** -- bit 0 show, bit 1 cache, bit 2 shade |
| `+0x2A` | sibling piece |
| `+0x2E` | child piece |

3DO object record, as the exe uses it: `+0x04` vertex count (`0x459DDD`),
`+0x08` primitive count (`0x459F59`), `+0x0C` selection primitive
(`0x459F42`), `+0x24` vertex array (`0x45AEFF`), `+0x28` primitive array
(`0x459F48`), `+0x2C` sibling, `+0x30` child.

Primitive record, stride `0x20`, VERIFIED: `+0x00` colour index (pushed to the
flat filler at `0x45A3A3`), `+0x04` vertex count, `+0x0C` pointer to the
`uint16` vertex-index array, `+0x10` texture (or a word frame index),
`+0x18` GAF animation for team/animated textures, `+0x1C` flags.

**The vertex buffer is 16.16 fixed point.** VERIFIED two ways: every consumer
shifts right by 16 (`sar eax,0x10` at `0x459E37`, `0x458242`, `0x45837B`), and
the 3DO file itself stores 16.16 -- `armsolar.3do` vertex 24 is `2375680`
= 36.25. `0x45AEC0` copies the file's vertex array into the piece buffer with a
plain `rep movs`, and `0x45B030` resets it the same way; `0x45B0A0`/`0x45B150`
then rotate it in place (`0x4B6CC0` -> `0x4B7173`, `fsincos` and `fistp` back
to int32), adding **the drawable's own rotation `[drawable+0x18/0x1A/0x1C]` for
the root piece only** (`0x45B0DB`) and composing down the hierarchy.

So the normals are computed in a space that already carries the unit's heading.
A building facing north and the same building facing south shade differently.
VERIFIED.

---

## 04. Pass 1 -- projection, and the 16-byte vertex record

`0x459E27`-`0x459F38`. The unshaded twin `0x459830` writes 12 bytes per vertex
(`0x459A01`, `0x459A09`, `0x459A41` -- three stores and no fourth); this one
writes 16.

| Offset | Type | Contents | Written at |
|---|---|---|---|
| `+0x00` | int32 | screen x, **plus the surface origin x** | `0x459E7D`, origin added `0x459F05` |
| `+0x04` | int32 | screen y, **plus the surface origin y** | `0x459E89`, origin added `0x459F1C` |
| `+0x08` | int32 | height-buffer value | `0x459EC2` / `0x459EEF` |
| `+0x0C` | int32 | **shade level** | `0x459EF6` (placeholder), `0x45A2EF` (real) |

VERIFIED, with the projection being:

```
X = (x >> 16) sign-extended from 16 bits
Y = (y >> 16) sign-extended
Z = ((-z) >> 16) sign-extended

1x:  pt.x = X            pt.y = Z - (Y >> 1)        pt.height = Y + K
2x:  pt.x = 2X           pt.y = 2Z - Y              pt.height = Y + K
K = (unitdef[0x241] & 0x40000000) ? 0x7D (125) : 0x32 (50)
```

Note the 2x form: `pt.y = 2Z - Y` is `2*(Z - Y/2)` with the halving done
exactly rather than truncated, and the height is divided back down
(`cdq / sub eax,edx / sar eax,1` at `0x459E95`) so it stays in model units.

**The shade field's format.** VERIFIED: it is a **plain 32-bit integer holding a
row number 0..31**. Not 8.8, not 16.16, not a fraction. Two independent proofs:

1. Pass 1 seeds it with a *placeholder* `and edi,0x1F` where `edi` counts `+3`
   per vertex (`0x459E8E`, `0x459F1F`) -- i.e. `(3*i) & 31`, a rainbow. That is
   only meaningful as a row index. (The placeholder is dead: pass 3 overwrites
   `+0x0C` for every vertex of every drawn primitive. It survives in the binary
   as evidence of intent.)
2. `0x4C8BB0` shifts it left by 16 to make a 16.16 interpolant
   (`0x4C8D8A`: `mov ebp,[ebp+0xc] / shl ebp,0x10`). If it were already
   fixed-point that shift would be nonsense.

**Clamped, wrapped or saturated at write time?** VERIFIED: **wrapped**, by
`and eax,0x1F` on the truncated integer (`0x45A2EC`). There is no `cmp`, no
`cmov`, no clamp, and no second table lookup anywhere between the `_ftol` and
the store. Section 07 has the bytes.

---

## 05. Pass 2a -- the face normal, `0x459F79`-`0x45A133`

VERIFIED. The loop starts at primitive index 1 whenever the object declares a
selection primitive, and at 0 otherwise:

```
459f42:  mov  eax,[ebx+0xc]        ; obj->selectionPrimitive
459f45:  cmp  eax,0xffffffff
459f48:  mov  eax,[ebx+0x28]       ; obj->primitives
459f4b:  je   0x459f57
459f4d:  add  eax,0x20             ; skip primitive 0
459f50:  mov  edi,0x1
459f55:  jmp  0x459f59
459f57:  xor  edi,edi
```

Worth being exact about: the exe does **not** use the value of
`selectionPrimitive` as an index. It tests it against -1 and, if it is anything
else, skips primitive **0**. All stock models put the selection plate first
(ARMSOLAR declares `sel = 0`), so the distinction never shows, but a mod that
put it elsewhere would have primitive 0 dropped and the selection plate drawn.
VERIFIED; the same construction appears again at `0x45A133` and `0x45A22C`.

The selection primitive therefore contributes **no face normal and no
accumulation**, and its exclusive vertices keep `count == 0`. VERIFIED.

Three indices are read from every primitive regardless of its declared vertex
count:

```
459f79:  mov  edx,[esp+0x14]       ; &prim[0x0C]
459f7d:  mov  edx,[edx]            ; the uint16 index array
459f7f:  mov  cx,[edx]             ; i0
459f82:  mov  ax,[edx+0x2]         ; i1
459f86:  cmp  cx,ax
459f89:  je   0x45a101             ; i0 == i1 -> degenerate
459f8f:  mov  dx,[edx+0x4]         ; i2
459f93:  cmp  ax,dx
459f96:  je   0x45a0fd             ; i1 == i2 -> degenerate
459f9c:  cmp  dx,cx
459f9f:  je   0x45a0fd             ; i2 == i0 -> degenerate
```

Degenerate case, VERIFIED at `0x45A101`: the face normal is set to the literal
`(0.0, +1.0, 0.0)` -- a **unit** vector, model-space up. Because the exe's
normals point inward (below), that reads as a face pointing straight *down*,
and gives `5*dot = +5.0` -> level 5, a dark row. A primitive that repeats an
index is shaded dark, not skipped.

The non-degenerate case, with the argument order established by reading
`0x4B6F00` (`ret 0x1c`; it computes **b - a**, `fild`-ing integer differences
into floats):

```
45a027:  call 0x4B6F00   (ret, a = V[i1], b = V[i0])   ->  A = V[i0] - V[i1]
45a072:  call 0x4B6F00   (ret, a = V[i1], b = V[i2])   ->  B = V[i2] - V[i1]
45a0a9:  call 0x4B6F70   (ret, a = A,     b = B)       ->  N = A x B
45a0b5:  faceNormal[prim] = N                          ; the RAW cross
45a0dc:  call 0x4B6FF0   (ret, N)                      ->  normalize
45a0ed:  faceNormal[prim] = normalize(N)               ; overwrites it
```

`0x4B6F70` was decoded instruction by instruction from the `fxch` dance and is
the ordinary right-handed `a x b`. `0x4B6FF0` is an ordinary normalize --
`fsqrt` of the sum of squares, three `fdiv` -- **with no guard against zero
length**. A face whose indices are all distinct but whose points are collinear
yields `0/0`; with the CRT's masked exceptions that is the x87 indefinite NaN,
which propagates through the average and the dot, and `_ftol` of a NaN returns
`0x80000000`, so `& 0x1F` gives **0** -- pure black. VERIFIED as a mechanism; I
did **not** find such a face in stock data, so whether it ever fires is NOT
ESTABLISHED.

`A x B` with `A = V[i0]-V[i1]`, `B = V[i2]-V[i1]` is the **negative** of the
right-hand normal for the winding i0->i1->i2. Confirmed against real geometry:
`armsolar.3do` piece `base`, primitive 3 is the flat top plate of the skirt
(indices 18,17,21,22, all at y = 4.04, the outward face pointing up) and the
routine yields `(0, -1, 0)`. **The exe's normals point into the model.**
VERIFIED. Everything below is stated in those terms.

---

## 06. Pass 2b -- the average, `0x45A133`-`0x45A22C`

VERIFIED, and byte-verified in section 07. For every primitive from the start
index, for every one of its `[prim+0x04]` indices:

```
45a195:  fld  [esp+ecx+0x224]      ; vertexNormal[idx].x
45a19c:  fadd [edx-0x4]            ; + faceNormal[prim].x
45a19f:  fstp [esp+ecx+0x224]
45a1a6:  fld  [edx]                ; ... .y
45a1a8:  fadd [esp+ecx+0x228]
45a1af:  fstp [esp+ecx+0x228]
45a1b6:  fld  [esp+ecx+0x22c]      ; ... .z
45a1bd:  fadd [edx+0x4]
45a1c7:  fstp [ecx]
45a1c9:  mov  ecx,[eax]            ; count[idx]++
45a1cb:  inc  ecx
45a1cd:  mov  [eax],ecx
```

then, once per vertex of the piece:

```
45a1fd:  mov  edx,[ecx]            ; count[i]
45a1ff:  test edx,edx
45a205:  je   0x45a223             ; count == 0 -> leave the normal at (0,0,0)
45a207:  fild [esp+0x18]
45a20b:  fld  [eax-0x4] / fdiv st,st(1) / fstp [eax-0x4]
45a213:  fld  [eax]     / fdiv st,st(1) / fstp [eax]
45a219:  fld  [eax+0x4] / fdiv st,st(1) / fstp [eax+0x4]
45a221:  fstp st(0)
```

Three things that matter and that the previous write-up understated:

1. **There is no re-normalisation, and that is not a rounding detail -- it is
   most of the shading.** The stored value is the *mean* of unit face normals,
   so its length collapses toward 0 wherever adjacent faces disagree. On
   ARMSOLAR's `base` piece every vertex normal has length **0.727 to 0.760**
   (measured; section 14). The whole model's `5*dot` is therefore scaled by
   about three quarters relative to a unit normal.
2. **The accumulation and the divide are done in float32** -- each `fadd` is
   followed by an `fstp DWORD`, and the quotient is stored as `DWORD`. To match
   bit-for-bit, accumulate in `float`, round to `float` after each add and after
   the divide, and do the final dot in `double`.
3. **Zero-count vertices keep `(0,0,0)`**, which dots to 0 and lands on row 0,
   black. In stock data that only affects the selection plate's own vertices,
   which are never drawn.

The averaging is **per piece**. The arrays are reset per piece: `count[]` by the
`rep stos` at `0x459E25`, `vertexNormal[]` by the three stores inside the
projection loop (`0x459F0A`-`0x459F15`). A vertex at the same position in two
different pieces is shaded independently. VERIFIED.

---

## 07. Pass 3 -- the level, byte for byte

`0x45A256`-`0x45A305`. For each primitive, for each of its declared vertices,
the 16-byte point record is copied into the scratch quad and then `+0x0C` is
overwritten:

```
45a266:  lea  esi,[esp+0xa0]              ; &scratch[0].shade
45a26f:  lea  ecx,[esi-0xc]               ; &scratch[0]
45a272:  mov  dx,[ebp+0x0]                ; the vertex index
45a276:  shl  edx,0x4
45a279:  lea  eax,[esp+edx+0xdce4]        ; &point[idx]
45a280..45a298:  copy all 16 bytes
45a29b:  mov  ecx,[esp+0x1c]              ; the PIECE record
45a29f:  mov  dl,[ecx+0x28]               ; piece flags
45a2a2:  shr  dl,0x2
45a2a5:  test dl,0x1                      ; bit 2 -- the COB SHADE state
45a2a8:  je   0x45a2f3
45a2aa:  xor  eax,eax
45a2ac:  mov  ax,[ebp+0x0]
45a2b0:  lea  eax,[eax+eax*2]
45a2b3:  shl  eax,0x2                     ; idx * 12
45a2b6:  fld  [esp+eax+0x224]             ; n.x
45a2bd:  fmul ds:0x5065F8
45a2c3:  fld  [esp+eax+0x228]             ; n.y
45a2ca:  fmul ds:0x5065FC
45a2d0:  faddp st(1),st
45a2d2:  fld  [esp+eax+0x22c]             ; n.z
45a2d9:  fmul ds:0x506600
45a2df:  faddp st(1),st
45a2e1:  fmul ds:0x4FD4CC
45a2e7:  call 0x4E43A0                    ; _ftol
45a2ec:  and  eax,0x1F
45a2ef:  mov  [esi],eax
45a2f1:  jmp  0x45a2f9
45a2f3:  mov  dword [esi],0xF             ; DONT_SHADE -> row 15
45a2f9:  mov  eax,[edi+0x4]
45a2fc:  inc  ebx
45a2fd:  add  esi,0x10
45a300:  add  ebp,0x2
45a303:  cmp  ebx,eax
45a305:  jl   0x45a26d
```

The raw bytes, so there can be no argument about a missing term:

```
0045A29B  8b 4c 24 1c 8a 51 28 c0 ea 02 f6 c2 01 74 49 33
0045A2AB  c0 66 8b 45 00 8d 04 40 c1 e0 02 d9 84 04 24 02
0045A2BB  00 00 d8 0d f8 65 50 00 d9 84 04 28 02 00 00 d8
0045A2CB  0d fc 65 50 00 de c1 d9 84 04 2c 02 00 00 d8 0d
0045A2DB  00 66 50 00 de c1 d8 0d cc d4 4f 00 e8 b4 a0 08
0045A2EB  00 83 e0 1f 89 06 eb 06 c7 06 0f 00 00 00 8b 47
```

Between `d8 0d cc d4 4f 00` (`fmul [0x4FD4CC]`) and `83 e0 1f` (`and eax,0x1F`)
there is exactly one instruction, the call to `_ftol`. **No `fadd` of a bias, no
`fsub`, no ambient term, no second `fmul`, no `cmp`/`jcc` clamp, no table
lookup.** VERIFIED at the byte level.

**The constants**, read out of the image and printed:

| Address | Bytes | Value |
|---|---|---|
| `0x5065F8` | `cd cc 4c bf` | `-0.800000011920929f` |
| `0x5065FC` | `00 00 80 3f` | `1.0f` |
| `0x506600` | `00 00 80 3e` | `0.25f` |
| `0x4FD4CC` | `00 00 a0 40` | `5.0f` |

The 5.0 is CONFIRMED. The light vector is CONFIRMED as `(-0.8, 1.0, 0.25)` and
has exactly **two** references in the whole image: the reader above, and the
setter `0x459816`-`0x459824`, which multiplies three integer arguments by
`0.01f` (`[0x4FD4C8]`) and whose only caller is the console command handler
`0x4166C0`. Nothing normalises it, at startup or ever. Its length is
`sqrt(0.64 + 1 + 0.0625) = 1.304799...`. VERIFIED.

`0x4E43A0` is the MSVC `_ftol`:

```
4e43a6:  fstcw [ebp-0x2]
4e43af:  or    ah,0xc            ; RC = 11, round toward zero
4e43b6:  fldcw [ebp-0x4]
4e43b9:  fistp qword [ebp-0xc]
4e43bf:  mov   eax,[ebp-0xc]     ; low dword
```

**Truncates toward zero.** CONFIRMED, and the mask acts on the low 32 bits of
the 64-bit result, so a negative value wraps as two's complement.

So, exactly and finally:

```
level = ( (int32)( 5.0 * ( n.x*(-0.8) + n.y*1.0 + n.z*0.25 ) ) ) & 0x1F
```

with `n` the **un-normalised** mean of the piece's unit inward face normals, the
light vector **un-normalised**, C truncation toward zero, and 15 for a piece
whose flag bit 2 is clear.

---

## 08. What the level can actually be: thirteen rows out of thirty-two

VERIFIED by construction. `|n| <= 1` (the mean of unit vectors, by the triangle
inequality) and `|L| = 1.30480`, so

```
|5 * dot(n, L)|  <=  5 * 1.30480  =  6.5240
```

Truncation therefore lands in `[-6, +6]`, and the mask maps that to

| trunc | -6 | -5 | -4 | -3 | -2 | -1 | 0..6 |
|---|---|---|---|---|---|---|---|
| row | 26 | 27 | 28 | 29 | 30 | 31 | 0..6 |

**Only rows 0-6 and 26-31 are ever produced at a vertex -- thirteen of the
thirty-two.** Plus row 15 for a `DONT_SHADE` piece. The middle of the table
(rows 7-14 and 16-25) is unreachable at a vertex and exists *only* because the
level is interpolated across the polygon (section 10). That is a strong
internal argument that the interpolation is meant to be there and that the wrap
is not an accident: an implementation that quantises per pixel instead of per
vertex would never touch two thirds of the table the game ships.

---

## 09. The table those rows index -- measured, not assumed

The level is a row of `palettes\PALETTE.SHD`, an 8,192-byte table used as
`SHD[level*256 + texel]`. The previous pass established the plumbing
(`0x42E1D0` builds the path, `0x4BAB00` installs `0x800` dwords into
`[disp+0xC4]`, allocated under the name `SHADE TABLE`) and I have no correction
to make to any of that.

I **do** have a correction to make to the arithmetic. The previous pass read the
*fallback generator* `0x4BADF0` (`scale += 0.06875` per row) and reported the
table as "row k multiplies by 0.06875k". That is what the generator would
produce **before** its own `min(255, ...)` clamp and before the nearest-palette
snap, and it is not what the shipped file does. Measured directly from
`totala1\palettes\PALETTE.SHD` against `PALETTE.PAL` -- mean output/input
luminance over every palette entry brighter than 4:

| row | measured | `0.06875k` | | row | measured | `0.06875k` |
|---|---|---|---|---|---|---|
| 0 | **0.0000** | 0.000 | | 15 | **1.0009** | 1.031 |
| 1 | **0.0455** | 0.069 | | 26 | **1.6127** | 1.788 |
| 2 | **0.1180** | 0.138 | | 27 | **1.6711** | 1.856 |
| 3 | **0.1939** | 0.206 | | 28 | **1.6957** | 1.925 |
| 4 | **0.2676** | 0.275 | | 29 | **1.7392** | 1.994 |
| 5 | **0.3438** | 0.344 | | 30 | **1.7793** | 2.063 |
| 6 | **0.4085** | 0.413 | | 31 | **1.8070** | 2.131 |
| 14 | **0.9948** | 0.963 | | | | |

Two things fall out:

- **Identity is rows 14 *and* 15**, exactly -- probes at palette indices 100,
  180, 214 and 250 all come back unchanged through both rows. That is why
  `DONT_SHADE` hard-codes 15: it is the "x1.00" row. This independently
  confirms that `TOTALA-EXE.md` section 54's "identity at row 16" is wrong.
- **The bright end is compressed, hard.** Row 31 is 1.807x, not 2.131x, and for
  an already-saturated colour it is 1.000x -- palette index 250 (`0,255,0`)
  comes back as itself at every row from 14 to 31. A model painted in bright
  primaries barely brightens at all; a mid-grey one brightens by 80%. There is
  no linear multiplier that reproduces this. **Use the file.**

A census of the whole table says the same thing more sharply. Counting, for
each row, how many of the 256 entries it leaves exactly where they are, how
many it sends to a black entry and how many to a white one:

| row | unchanged | to black | to white | | row | unchanged | to black | to white |
|---|---|---|---|---|---|---|---|---|
| 0 | 1 | 256 | 0 | | 16 | 125 | 13 | 9 |
| 1 | 1 | 118 | 0 | | 18 | 27 | 13 | 19 |
| 4 | 1 | 30 | 0 | | 20 | 12 | 13 | 26 |
| 8 | 5 | 13 | 0 | | 24 | 8 | 13 | 41 |
| 13 | 59 | 13 | 0 | | 28 | 8 | 13 | 51 |
| 14 | 216 | 13 | 1 | | 31 | **8** | 13 | **58** |
| 15 | 232 | 13 | 3 | | | | | |

Row 31 leaves eight of the 256 entries exactly where they are and puts
fifty-eight of them on white; row 15, the identity row, is genuinely the
identity for 232 of them and within 12/255 for the other 24, most of which are
duplicate entries with the same RGB. A single multiplier does none of that. The
black count falling to a floor of exactly thirteen from row 8 upward is the same
thirteen entries every time -- the thirteen that `PALETTE.PAL` already holds as
`(0,0,0)`, indices 0, 10-15 and 240-245 -- which is section 25's finding
arrived at from the other end.

`PALETTE.LHT` (`[disp+0xC8]`, `LIGHT TABLE`) is a different 32x256 ramp and is
NOT used by this path. `PALETTE.ALP` (`[disp+0xC0]`) is the anti-alias blend
table. Both re-confirmed.

---

## 10. Interpolation: Gouraud, and it is the ROW that is interpolated

VERIFIED in `0x4C8BB0`, the shaded quad rasterizer. Its edge walk builds a span
table of `0x28`-byte rows carrying five interpolants a side:

| Offset | Left | Right |
|---|---|---|
| `+0x00` / `+0x04` | x (integer) | x (integer) |
| `+0x08` / `+0x10` | u 16.16 | u 16.16 |
| `+0x0C` / `+0x14` | v 16.16 | v 16.16 |
| `+0x18` / `+0x1C` | height 16.16 | height 16.16 |
| `+0x20` / `+0x24` | **shade 16.16** | **shade 16.16** |

The shade setup, for the left edge (the right edge at `0x4C8F40`-`0x4C8F98` is
identical):

```
4c8d87:  mov  ebp,[ebp+0xc]        ; point[i].shade, an integer 0..31
4c8d8a:  shl  ebp,0x10             ; -> 16.16
...
4c8dd2:  mov  eax,[esp+0x10]
4c8dd6:  mov  eax,[eax+0xc]        ; point[i-1].shade
4c8dd9:  shl  eax,0x10
4c8ddc:  sub  eax,ebp              ; delta
4c8dde:  cdq
4c8ddf:  idiv ecx                  ; / (y1 - y0)  -> per-scanline gradient
...
4c8e2e:  (per scanline)  mov [ecx+0x20],ebp ; store  /  add ebp,edx ; step
```

VERIFIED details:

- The shade interpolant is **16.16**, obtained by `shl 16` of the integer row.
- The gradient is `((row1 - row0) << 16) / dy`, `idiv`, truncating toward zero.
- There is **no** half-scanline rounding bias on shade. There *is* one on x
  (`add esi,0xffff` at `0x4C8D6A`), and only on x.
- When the top vertex is above the surface (`y0 < 0`) the interpolant is
  prestepped to scanline 0 by `start -= grad * y0` (`0x4C8DE1`-`0x4C8E2C`).
  Shade is prestepped along with everything else.
- **No mask, no clamp, no `and 0x1F`** anywhere in `0x4C8BB0`. The only `and`
  in the whole routine is `and esi,0x3` on a vertex index. Because both
  endpoints are in 0..31 and the interpolation is linear and truncating, the
  value cannot leave the range, so none is needed.

The flat-colour n-gon filler `0x4C0C70` treats `+0x0C` identically
(`0x4C0DD7`: `mov esi,[esi+0xc] / shl esi,0x10`), so a flat-coloured face is
Gouraud-shaded too.

**Consequence, and this is the visual point.** A quad whose corners came out at
rows 29 and 0 is drawn as a smooth ramp from 1.74x down to black, sweeping
through rows 28, 27, ... 2, 1 -- the "unreachable" middle of the table. It does
**not** wrap the short way round. That gradient is the original's look. Compute
the level per *pixel* from an interpolated normal instead and the same face
becomes a flat bright field with an abrupt black edge where the dot crosses
zero -- the same rows at the two corners, a completely different picture in
between.

Whether the span filler re-derives the row as `shade >> 16` per pixel, and how
it indexes the table, is `0x4C8020`'s business and belongs to the second half.

---

## 11. Per-piece and per-unit modifiers

**Piece flags bit 2 is the COB `SHADE` / `DONT_SHADE` state.** This is the
correction that matters most in this section -- the previous pass reported bit 2
as "set unconditionally, no writer that clears it found, dead code". There is a
writer, and it is a COB opcode.

VERIFIED chain:

```
; the COB interpreter, 0x4B1272
4b1272:  cmp  edx,0x1000e000       ; DONT_SHADE
4b1278:  ja   0x4b12a7
4b127a:  je   0x4b128e
4b127c:  cmp  edx,0x1000d000       ; SHADE
4b1282:  jne  0x4b1b60
4b1288:  mov  edx,[edi]  / push 0x1
4b128e:  mov  edx,[edi]  / push 0x0
4b1292:  mov  eax,[eax+ecx*4+0x4]  ; the piece index
4b1299:  call [edx+0x10]           ; -> 0x480DF0
```

```
480df0:  mov  eax,[esp+0x4]        ; pieceIndex
480df4:  mov  edx,[ecx+0x540]      ; the drawable
480dfb:  mov  bl,[esp+0xc]         ; the boolean
480dff:  lea  eax,[eax+eax*2]
480e02:  and  ebx,0x1
480e05:  shl  ebx,0x2              ; -> bit 2
480e08:  lea  eax,[eax+eax*8]      ; 27 * pieceIndex
480e0b:  lea  eax,[edx+eax*2+0x4a] ; &piece[i].flags
480e0f:  mov  dx,[eax]
480e12:  and  edx,0xfffb           ; clear bit 2
480e18:  or   edx,ebx
480e1b:  mov  [eax],dx
480e1e:  mov  eax,[ecx+0x540]
480e24:  mov  dword [eax+0x10],0x0 ; invalidate the cached bitmap
```

The vtable is at `0x4FD698`: `+0x08` = `0x480D50` (`SHOW`/`HIDE`, bit 0),
`+0x0C` = `0x480DB0` (`CACHE`/`DONT_CACHE`, bit 1), `+0x10` = `0x480DF0`
(`SHADE`/`DONT_SHADE`, bit 2). Getters at `+0x1C`/`+0x20`/`+0x24`
(`0x480E30`/`0x480E50`/`0x480E70`), reading the same three bits. The opcode
values match RWE's own `CobOpCode.h` (`SHADE = 0x1000D000`,
`DONT_SHADE = 0x1000E000`), so the naming is CONFIRMED, not guessed.

Defaults, set by the piece-list builder `0x45AEC0`, VERIFIED:

```
45aed4:  or   byte [ebx+eax*2+0x4a],0x2   ; CACHE on, always
45af1b:  cmp  dword [ebp+0x4],0x3
45af1f:  jl   0x45af27
45af21:  or   byte [ebx+0x28],0x1         ; SHOW, if the object has >= 3 vertices
45af27:  and  word [ebx+0x28],0xfffe      ; otherwise HIDE
45af31:  or   byte [ebx+0x28],0x4         ; SHADE on, always
```

So every piece of every model starts shaded, and a script has to say
`DONT_SHADE` to turn it off. When it does, that piece's vertices are pinned to
row 15 -- measured x1.0009, i.e. the texture drawn exactly as authored. This is
a real feature, not dead code.

**Whether any stock script uses it, answered 2026-09-06:** heavily. Scanning
the 714 shipped `.cob` files for the two opcodes, **290 call `DONT_SHADE` and
not one calls `SHADE`**. The data is written on the assumption that shading is
on and that a script turns it off where it is unwanted, which is exactly what
`0x45AF31` sets up. RWE cleared the flag for every mobile unit until
2026-09-06, so the whole of this document reached buildings and nothing
else.

**`[unitdef+0x241]`.** Read twice in this routine, both times the same bit and
both times for the same purpose:

```
459eaa:  mov  edx,[edx+0x241]
459eb0:  shr  edx,0x1e            ; bit 30
459eb3:  and  dl,0x1
459eb6:  neg  dl
459eb8:  sbb  edx,edx
459eba:  and  edx,0x4b            ; 75
459ebd:  add  edx,0x32            ; + 50  ->  125 or 50
459ec0:  add  eax,edx             ; the HEIGHT field, +0x08
```

**It changes the height-buffer bias and nothing else.** It does not touch the
shade. VERIFIED -- those are the only two reads of `+0x241` in `0x459C70`, and
the unshaded twin does the identical thing at `0x459A29`/`0x459A56`.

**Buildings versus mobile units: no separate path.** NOT FOUND. `0x459C70`
never reads a "is a building" flag, never branches on footprint or on
`unitdef`, and never reads the unit's world position, terrain height, or
current build fraction. Every unit and every feature with the `0x20000000`
drawable bit goes down the same code.

**Under construction: no separate path here either.** NOT FOUND in `0x459C70`.
The construction look comes from the height plane (`+0x14`), which this routine
writes but does not read.

**A global shade bias, a per-unit brightness, a damage or cloak modifier**:
NOT FOUND. The only writes to a point's `+0x0C` in the entire routine are the
two at `0x45A2EF` and `0x45A2F3`.

---

## 12. Everything else conditional in the routine

**(a) The pass selector, arg4, and the CACHE bit.** VERIFIED:

```
459d96:  test byte [ecx+0x28],0x1          ; SHOW; else skip the piece
459d9a:  je   0x45a3e9
459da0:  mov  eax,[esp+0x159f4]            ; arg4
459da7:  cmp  eax,0xffffffff
459daa:  je   0x459dd2                     ; -1: draw every piece
459dac:  mov  dl,[ecx+0x28]
459daf:  shr  edx,1
459db1:  and  edx,0x1                      ; the CACHE bit
459db4:  cmp  eax,edx
459db6:  je   0x459dd2                     ; matches this pass: draw
459db8:  mov  eax,[edi+0xc]                ; the unit
459dbb:  fld  dword [eax+0x104]
459dc1:  fcomp dword ds:0x4fd4c0           ; 0.0
459dc9:  test ah,0x40
459dcc:  jne  0x45a3e9                     ; == 0.0 -> skip the piece
```

The identical gate exists in the unshaded twin at `0x459958`-`0x459985`. Since
the shaded path is only ever called with arg4 = 1 or -1 (section 02), a piece
marked `DONT_CACHE` is skipped by the shaded renderer unless `[unit+0x104]` is
non-zero.

Those pieces are then drawn by a second, separate pass:

```
45960d:  mov  ecx,[ebp+0xc]                ; the unit
459610:  test dword [ecx+0x110],0x20000000
45961a:  je   0x45962f
45961c:  fld  dword [ecx+0x104]
459622:  fcomp dword ds:0x4fd4c0
45962d:  je   0x459646
45962f:  xor  eax,eax
459631:  push 0x0                          ; arg4 = 0 -> the DONT_CACHE pieces
459633:  mov  al,[ecx+0xff]
459639:  mov  ecx,[edi+0x10]               ; the SCREEN surface
45963e:  push ebp / push ecx
459641:  call 0x459830                     ; the UNSHADED rasterizer
```

**A `DONT_CACHE` piece is drawn unshaded, straight to the screen, every frame,
regardless of the SHADING option.** VERIFIED as control flow. The *reason* --
that the shaded path renders into a per-unit cached bitmap and a piece that
changes every frame cannot live in it -- is INFERRED but strongly supported:
`0x4586A0` only runs at all on a cache miss (`[drawable+0x10] == 0`), and both
COB setters that touch the piece flags null that pointer.

What `[unit+0x104]` is was NOT ESTABLISHED. It is a float, compared against 0.0
both here and in `0x4586A0` where it selects the non-caching variant `0x437BE0`
over `0x437B50`; there are far too many `+0x104` references across unrelated
structures to pin down by grep. Not guessed.

Because the shading is baked into a cached bitmap shared by every instance of
the same model in the same pose, **the shade level provably cannot depend on
world position, distance, terrain, or fog.** That is a structural proof, not an
absence of evidence.

**(b) The draw dispatch and the team-colour case.** VERIFIED as control flow at
`0x45A30B`-`0x45A3A1`:

```
45a30b:  mov  eax,[edi+0x1c]               ; primitive flags
45a30e:  test al,0x1
45a310:  jne  0x45a3a3                     ; bit 0 -> flat colour, 0x4C0C70
45a316:  cmp  dword [edi+0x4],0x4
45a31a:  jne  0x45a3bf                     ; textured non-quad -> NOT DRAWN
45a322:  shr  ecx,1  / test cl,0x1
45a327:  je   0x45a386                     ; bit 1 clear -> texture = [prim+0x10]
45a329:  shr  eax,0x2 / test al,0x1
45a32e:  je   0x45a363
45a330:  ... eax = game + 331*player + 0x1b8a ; the player record
45a354:  mov  cl,[eax+0x96]                ; the player colour index
45a35a:  push ecx / push [edi+0x18] / call 0x4B7F30    ; frame = colour index
45a363:  mov  eax,[esp+0x159f4]            ; arg4
45a36c:  je   0x45a37b
45a36e:  push 0 / push [edi+0x18] / call 0x4B7F30      ; frame 0
45a37b:  lea  ecx,[edi+0x10] / call 0x4B7EE0           ; the animation own frame
45a389:  push 0 / push scratch / push texture / push surface / call 0x4C8BB0
```

`0x4B7F30(anim, frame)` returns `[anim->frames + frame*8 + 0x28]`; `0x4B7EE0`
does the same with the animation current frame word. So the team-colour case
selects a **frame of a GAF animation**, and does nothing to the shade level --
a team-coloured face is shaded exactly like any other. VERIFIED. That bit 1
means "animated texture" and bit 2 "team colour" is INFERRED from the control
flow; the mechanism is verified either way. Note also that in the cached pass
(arg4 != 0) an animated non-team texture is frozen at frame 0.

`cmp dword [edi+0x4],0x4 / jne` confirms `TOTALA-EXE.md` section 58 item 4:
a textured primitive that is not a quad is not drawn at all.

**(c) ANTI.** `0x459C82` doubles the surface and sets a local 2x flag;
`0x45A3FF`-`0x45A462` box-filters back down through `0x4B95A0` and then
point-samples the height plane (`mov al,[ecx] / add ecx,0x2`). It affects the
*projection* (section 04) and nothing about the level. VERIFIED.

**(d) A second light, a distance term, fog.** NOT FOUND. `0x5065F8` has exactly
two references in the image; there is no second dot product, no attenuation, and
no reference to any fog global anywhere between `0x459C70` and `0x45A46C`.

---

## 13. ARMSOLAR, with numbers

The reference case: `armsolar.3do`, piece `base` (28 vertices, 15 primitives,
`selectionPrimitive = 0`). The four `CorSol1a` panels are primitives 11-14. In
model space +x is screen right, so:

- **primitive 13** (indices 7,5,1,3) is the **LEFT** panel;
- **primitive 12** (indices 6,4,0,2) is the **RIGHT** panel.

Replaying the decode -- face normals `normalize((V[i0]-V[i1]) x (V[i2]-V[i1]))`,
averaged over primitives 1-14 with no re-normalisation, dotted against the raw
`(-0.8, 1.0, 0.25)`, x5, truncated, `& 0x1F`:

| | vertex | len(n) | `5*dot` | trunc | **row** | PALETTE.SHD |
|---|---|---|---|---|---|---|
| **LEFT** (prim 13) | 7 | 0.7571 | -4.338 | -4 | **28** | x1.696 |
| | 5 | 0.7571 | -4.338 | -4 | **28** | x1.696 |
| | 1 | 0.7548 | -3.134 | -3 | **29** | x1.739 |
| | 3 | 0.7548 | -3.134 | -3 | **29** | x1.739 |
| **RIGHT** (prim 12) | 6 | 0.7548 | -0.583 | 0 | **0** | x0.000 |
| | 4 | 0.7548 | -0.583 | 0 | **0** | x0.000 |
| | 0 | 0.7526 | +0.621 | 0 | **0** | x0.000 |
| | 2 | 0.7526 | +0.621 | 0 | **0** | x0.000 |

**The left panel is drawn at x1.70-1.74 across its whole face. The right panel
is row 0 at all four corners, so the interpolant is 0 everywhere and every
texel maps to `SHD[0*256 + t]` -- pure black, uniformly.** That is exactly the
reference behaviour, and it falls out of the decode with no adjustment.

The rest of the piece, for completeness -- and this is where the current RWE
implementation parts company (section 15):

| prim | texture | exe rows at the four corners | RWE today (flat triangle) |
|---|---|---|---|
| 1 | stone2 | 28, 28, 29, 29 -> 1.70...1.74 | row 28 -> 1.925 |
| 2 | stone2 | 29, 29, 31, **0** -> bright, one black corner | row **0** -> 0.000, whole face |
| 3 | stone2 | 29, 28, 30, 31 | row 29 -> 1.994 |
| 4 | stone2 | **0**, 30, 28, 28 | row 30 -> 2.062 |
| 5 | stone2 | **0**, 31, 30, **0** | row 1 -> 0.069 |
| 6 | 32XGouraud | 28, 28, 29, 29 | row 28 -> 1.925 |
| 7 | 32XGouraud | 29, 29, 31, **0** | row **0** -> 0.000, whole face |
| 8 | 32XGouraud | 31, 29, 28, 30 | row 29 -> 1.994 |
| 9 | 32XGouraud | **0**, 30, 28, 28 | row 30 -> 2.062 |
| 10 | 32XGouraud | **0**, 31, 30, **0** | row 1 -> 0.069 |
| 11 | CorSol1a (front) | **0**, **0**, 29, 29 -- half-black gradient | row **0** -> 0.000, whole face |
| 12 | CorSol1a (right) | **0**, **0**, **0**, **0** -- solid black | row 1 -> 0.069 |
| 13 | CorSol1a (left) | 28, 28, 29, 29 | row 28 -> 1.925 |
| 14 | CorSol1a (back) | **0**, **0**, 28, 28 -- half-black gradient | row 30 -> **2.062** |

Read the two right-hand columns against each other. On prims 2, 7 and 11 RWE
paints black where the exe paints a bright face with one dark corner or a
gradient. On prim 14 RWE paints its brightest possible value where the exe
paints half the face black. On prims 3, 4, 8, 9 RWE exceeds 1.99x, a
brightness `PALETTE.SHD` cannot reach at all. Nothing about the left and right
panels is wrong; nearly everything between them is.

---

## 14. Why the normals are short, and why RWE two errors nearly cancel

Measured on `base`: every vertex normal has length between **0.727 and 0.760**,
because each vertex of this model is shared by two or three faces meeting at
roughly a right angle:

```
count 2 -> len(n) = 0.7526, 0.7543, 0.7548, 0.7571, 0.7600
count 3 -> len(n) = 0.7274, 0.7368
```

Now compare the two formulas:

```
exe:   5 * ( n . L )                       len(n) ~ 0.75,  len(L) = 1.3048
RWE:   5 * ( n/len(n) . L/len(L) )  =  exe / ( len(n) * 1.3048 )
```

For `len(n) = 0.7526` the correction factor is `0.7526 * 1.3048 = 0.982`.
**RWE two normalisations cancel to within 2% on this model**, which is precisely
why ARMSOLAR left and right panels came out looking right and everything else
did not. The cancellation is a coincidence of ARMSOLAR geometry. On a vertex
where the adjacent faces are nearly coplanar, `len(n) -> 1` and the exe value is
**1.30x** RWE -- a difference of one or two rows everywhere, and the difference
between black and bright at the wrap. On a vertex on the rim of a thin plate
where opposing faces meet, `len(n) -> 0` and the exe collapses to row 0 while
RWE does not.

Do not "fix" one of the two normalisations and leave the other. Remove both.

---

## 15. What the previous decode got wrong or missed

Each item is a concrete correction, in rough order of visible impact.

**1. Its implementation advice inverted its own finding, and RWE followed the
advice.** `FINDINGS-VISUALOPTIONS.md` section 16.1 says "**Do not** port the
`& 0x1F` wrap literally" and proposes a centred monotone ramp
`row = clamp(14.55 + 16*t, 0, 31)`. Section 08 of that document called its own
correct decode "harsher than the original is remembered to look". It is not
harsher. ARMSOLAR right panel really is solid black, and the wrap is the only
thing that produces that. The current `unitTexture.frag` has since restored the
wrap, so this one is already half-fixed; the note is here so it does not get
re-argued.

**2. It did not notice that the level is computed per VERTEX and the ROW is
what gets interpolated.** Section 07 of that document described the span table
correctly but drew the wrong conclusion -- "the level is Gouraud-interpolated
across the face and applied per pixel" is true, and RWE implemented it as
"compute the level per pixel from an interpolated normal", which is a different
thing. The exe quantises to an integer row **at the vertex** (`0x45A2EC`),
`shl 16` (`0x4C8D8A`), and interpolates linearly. Across a wrap boundary that
produces a smooth ramp through the middle of the table; per-pixel quantisation
produces a hard edge. This is the largest structural error.

**3. RWE does not average normals at all.** `src/rwe/mesh_util.cpp`:

```cpp
Vector3f getNormal(const Mesh::Triangle& t)
{
    auto v1 = t.b.position - t.a.position;
    auto v2 = t.c.position - t.a.position;
    return v1.cross(v2).normalizedOr(Vector3f(1.0f, 0.0f, 0.0f));
}
...
auto normal = getNormal(t);
texturedVerticesBuffer.emplace_back(t.a.position, t.a.textureCoord, normal);
texturedVerticesBuffer.emplace_back(t.b.position, t.b.textureCoord, normal);
texturedVerticesBuffer.emplace_back(t.c.position, t.c.textureCoord, normal);
```

One flat normal per triangle, assigned to all three vertices. The exe averages
the piece unit face normals per vertex index (`0x45A166`-`0x45A22A`). This is
what turns prims 2, 7, 11 and 14 of ARMSOLAR from gradients into flat fields
(section 13). It also means each quad two triangles can disagree when the quad
is not planar, adding a shade seam on the diagonal on top of the texture seam
already recorded in `TOTALA-EXE.md` section 57.

**4. The smoothed normal must NOT be normalised.** The previous document did
say the exe leaves it un-renormalised, but then listed normalising it as
"deliberate divergence #2" and RWE does `normalize(normal)`. Measured lengths
are 0.727-0.760 on ARMSOLAR (section 14); the shortening is 25% of the whole
signal.

**5. The light vector must NOT be normalised either.** This one is not in the
previous document at all. `(-0.8, 1.0, 0.25)` has length **1.30480** and the exe
uses it raw -- verified by the only two references to `0x5065F8` in the image.
RWE writes `normalize(vec3(-0.8, 1.0, -0.25))`, which divides every dot product
by 1.3048. On ARMSOLAR this happens to cancel error #4; on other geometry it
does not.

**6. `0.06875 * k` is not the table.** The previous document read the *fallback
generator* `0x4BADF0` and reported its pre-clamp constant. The shipped
`PALETTE.SHD` measures 1.807x at row 31, not 2.131x, and 1.696x at row 28, not
1.925x -- and for saturated colours it does not brighten at all (section 09).

*Half-fixed, 2026-09-06.* `unitTexture.frag` and `unitBuild.frag` now carry the
thirty-two measured multipliers as a table, generated by
`tools/exe/shading/shdramp.py` straight from the shipped `PALETTE.SHD` and
`PALETTE.PAL`. (They read between the two neighbouring rows for a day; since
2026-09-07 they truncate the interpolated row per pixel, as `0x4C8020` does --
section 21 -- and the strengths default to 100, so the shipped look is the
original's.) What they replaced
was neither `0.06875 * k` nor the real table but a two-segment line fitted by
hand, and that fit was wrong at the top: it put row 31 at 1.55 against the
measured 1.807, so every lit face came out dimmer than the original's while
the shadows stayed where they were. A model shaded that way reads as carrying
too much shadow, which is what prompted this pass.

What is still missing is that **the saturation is per colour, not uniform**.
Averaging a row into one multiplier throws that away: palette index 250, a pure
green, comes back unchanged at every row from 14 to 31, while a mid grey
brightens by 80%. Reproducing it needs the texel's own palette index at the
fragment, which means carrying an index channel through the texture atlas
rather than the RGBA the atlas holds now -- the exact lookup is then
`PALETTE.PAL[PALETTE.SHD[row * 256 + index]]` and nothing has to be fitted at
all. That is the remaining work on this item.

**7. Identity is rows 14 AND 15, exactly, and 15 is not dead code.** The
previous document put identity "at row 14-15" but called the level-15 branch
"dead code on stock data" because it could not find a writer that clears piece
flag bit 2. There is one: `0x480DF0`, reached from the COB `SHADE`
(`0x1000D000`) and `DONT_SHADE` (`0x1000E000`) opcodes through vtable slot
`+0x10` (section 11). Bit 1 is likewise `CACHE`/`DONT_CACHE` via `0x480DB0`,
which resolves the mystery gate at `0x459DAC` that the previous pass left
unexplained.

**8. A `DONT_CACHE` piece is drawn by the UNSHADED rasterizer.** `0x45962F`
calls `0x459830` with arg4 = 0 into `[disp+0x10]`, the screen surface, with no
SHADING check. Not mentioned previously. *Ported 2026-09-07:* the COB `CACHE`
and `DONT_CACHE` opcodes, which RWE had been swallowing, now set a `cached`
flag on the piece, and a finished unit's uncached pieces are drawn with the
shade strength at zero. 137 of the 357 shipped scripts use `DONT_CACHE`; the
lab's nanolathe arms are typical.

**9. The selection primitive is skipped by INDEX 0, not by its declared
index.** `0x459F4D` / `0x45A231` add `0x20` to the primitive pointer; neither
uses the value of `[obj+0x0C]` as an offset. Stock data is unaffected; mods
would be.

**10. A degenerate primitive gets the unit vector `(0, +1, 0)`, not zero.**
`0x45A101`. It shades as though facing straight down: `5*dot = +5.0` -> row 5,
x0.344. And `0x4B6FF0` has no zero-length guard, so a collinear-but-distinct
face produces a NaN whose `_ftol` is `0x80000000` and whose masked row is 0.

**11. Vertices with `count == 0` keep `(0,0,0)` and land on row 0.** The divide
is guarded (`0x45A1FF`), the dot is not. Only the selection plate exclusive
vertices are affected in stock data.

**12. Two minor slips.** `[prim+0x0C]`, not `+0x08`, is the vertex-index array
(`0x459F7D`, `0x45A179`, `0x45A259`). And the vector helper `0x4B6F00` returns
**b - a**, so the calls are `A = V[i0] - V[i1]`, `B = V[i2] - V[i1]` -- which is
what the previous document wrote, but by luck: the sign cancels in the cross
product, so both readings give the same normal. Recorded so nobody "fixes" it
in the wrong direction.

**13. Precision.** The exe accumulates and divides in float32 with a round to
float32 after every step, then does the dot at x87 precision. Truncation of a
value sitting exactly on an integer boundary changes the row. Accumulate in
`float`, dot in `double`.

---

## 16. Still unknown

**Answered by part two, below.** Everything in this list that was about the
pixel half is now decoded -- see section 26 for the item-by-item disposal. The
short version: the lookup really is `SHD[row][texel]` with the row truncated
per pixel and nothing else applied, and the one thing part one could not see is
that a unit whose FBI says `ZBuffer=0` is drawn through a path with no shading
at all.

- What `[unit+0x104]` is. It gates the render cache in `0x4586A0` and the piece
  pass in `0x459DAC`. NOT ESTABLISHED; deliberately not guessed.
- Whether any stock `.cob` actually issues `DONT_SHADE`. A data question, not
  an exe question, and not checked.
- Whether the missing zero-length guard in `0x4B6FF0` ever fires on stock
  geometry.
- Everything from `0x4C8020` inward: the per-pixel row derivation, the height
  test, and the exact table index. That is the second half job. The only thing
  this half needs from it is confirmation that no further mask or clamp is
  applied to the interpolated value -- there is none in `0x4C8BB0`.

---

# The SHADED unit rasterizer, part two: the span filler and the table lookup

Everything from `0x4C8020` inward — the job section 16 named. Same binary as
part one: GOG release, 1,178,624 bytes, MD5 `8e74a1dffa1f5988624c52048f5b20cd`,
image base `0x400000`, `.text` file offset = VA − `0x400C00`.

The headline, up front, because it adds a gate that part one could not see:

**The lookup is exactly `SHD[(shade >> 16) * 256 + texel]` and nothing else.
`sar 16`, `shl 8`, add, one byte load. No rounding, no dither, no second mask,
no clamp, no ambient, no fog, no attenuation, no blend, no second table. The
whole span filler references exactly one table pointer in the whole of its 1546
bytes, and it is `[display+0xC4]`, `PALETTE.SHD`.** So part one's conclusion
stands unaltered: a shaded face's brightness is `PALETTE.SHD[row][texel]` with
`row` Gouraud-interpolated between the vertices.

**But** the textured filler only does that lookup when the destination surface
has a **height plane**, and the destination surface only gets one when the
unit's FBI says `ZBuffer=1`. A completed `ZBuffer=0` unit's textured quads are
drawn **raw and unshaded**. In stock data that is exactly two units, CORFAV and
CORTRUCK — but it is a real branch, and it is the one thing in this half that
changes what an implementation has to do.

---

## 17. Pivot addresses for this half

| Address | What |
|---|---|
| `0x4C8020` | **the shaded textured span filler** — one scanline, `ret 0x10` |
| `0x4C8630` / `0x4C8648` | its width jump table and byte index table (with height plane) |
| `0x4C86C4` / `0x4C86DC` | the same, for the no-height-plane path |
| `0x4C8175` / `0x4C820C` / `0x4C829E` / `0x4C8330` / `0x4C83C2` / `0x4C8454` | the six shaded inner loops: widths 128, 64, 32, 16, 8, generic |
| `0x4C84F4`…`0x4C862A` | the **unshaded** variants taken when there is no height plane |
| `0x4CD896` / `0x4CD8DA` / `0x4CD91E` / `0x4CD962` | hand-written 68-byte raw-texel span blitters called by those |
| `0x4C0B10` | the flat-colour span filler (called from `0x4C0C70`), same lookup |
| `0x4B6220` | `return ds:0x51FBD0` — the display object |
| `0x4BAB00` | installs 0x800 dwords into `[display+0xC4]` — the SHADE TABLE |
| `0x437BE0` | allocate a drawable's cached bitmap **with** a height plane |
| `0x437B50` | allocate one **without** a height plane |
| `0x4B90A0` | composite a cached bitmap onto another surface, colour-keyed |
| `0x42C5A6` | `zbuffer` → bit 7 of `def+0x241` |
| `0x485ACB` | `def+0x241` bit 7 → `unit+0x114` bit 0 |
| `0x4586CE`…`0x458719` | which of the two allocators `0x4586A0` uses |

Labels as in section 01: **VERIFIED** (read off the instructions and, where it
matters, the bytes), **CONFIRMED** (independently corroborated), **INFERRED**,
**NOT FOUND**.

One caveat about tooling. `objdump -d` on this image is a linear sweep and it
desynchronises in several places; `0x485AC4` and `0x42C58C` both disassemble to
nonsense (`ror DWORD PTR [ecx+0x11086],0x0`) until you restart the sweep on the
right boundary. Two of the findings below were invisible in the flat listing and
only appeared after searching the **raw bytes** for the displacement
(`14 01 00 00` for `+0x114`) and re-syncing. Anything load-bearing here is
quoted as bytes for that reason.

---

## 18. The way in is `0x4C8020`, confirmed, and it is called once per scanline

VERIFIED. `0x4C8BB0` ends at `0x4C90AC` with `ret 0x10`, and its only call other
than the stack probe is to `0x4C8020`:

```
4c9068:  mov  esi,[esp+0x34]          ; y = first scanline
4c906c:  mov  ebx,[esp+0x28]          ; last scanline
4c9070:  cmp  esi,ebx
4c9072:  lea  edi,[esp+0x70]          ; &span[0]
4c9076:  jge  0x4c90a2
4c9078:  mov  eax,[edi+0x4]           ; span.xRight
4c907b:  mov  edx,[edi]               ; span.xLeft
4c907d:  sub  eax,edx
4c907f:  test eax,eax
4c9081:  jle  0x4c909a                ; empty span -> skip
4c9083:  mov  ecx,[esp+0x7d78]        ; arg2 of 0x4C8BB0 = the texture
4c908a:  mov  edx,[esp+0x7d74]        ; arg1 of 0x4C8BB0 = the destination surface
4c9091:  push ecx
4c9092:  push edx
4c9093:  push edi                     ; &span[y]
4c9094:  push esi                     ; y
4c9095:  call 0x4c8020
4c909a:  add  edi,0x28
4c909d:  inc  esi
4c909e:  cmp  esi,ebx
4c90a0:  jl   0x4c9078
```

So `0x4C8020(y, &span, surface, texture)`, `__stdcall`. The argument identities
come from the call site of `0x4C8BB0` itself, `0x45A389`-`0x45A39C`
(`push 0 / push scratch / push texture / push surface`), giving
`0x4C8BB0(surface, texture, points, uvQuad)`. The fourth argument being NULL is
what makes `0x4C8BB0` synthesise a default UV quad `(0,0)–(w−1,0)–(w−1,h−1)–(0,h−1)`
from the texture's own dimensions at `0x4C8BF8`-`0x4C8C33`. VERIFIED.

Prologue, VERIFIED, and it is the source of the two pointers everything below
turns on:

```
004C8020  83 ec 28 8b 44 24 34 53 55 56 8b 70 10 8b 68 14
004C8030  8b 44 24 44 57 8b 48 10 89 4c 24 1c e8 df e1 fe
```

```
4c8020:  sub  esp,0x28
4c8023:  mov  eax,[esp+0x34]     ; arg3, the DESTINATION surface
4c8027:  push ebx / push ebp / push esi
4c802a:  mov  esi,[eax+0x10]     ; dest pixel plane
4c802d:  mov  ebp,[eax+0x14]     ; dest HEIGHT plane          <-- see section 23
4c8030:  mov  eax,[esp+0x44]     ; arg4, the TEXTURE
4c8034:  push edi
4c8035:  mov  ecx,[eax+0x10]     ; texture pixel plane
4c8038:  mov  [esp+0x1c],ecx
4c803c:  call 0x4b6220           ; -> eax = ds:0x51FBD0, the display object
4c8041:  mov  [esp+0x2c],eax
```

`0x4B6220` is five bytes long: `a1 d0 fb 51 00 c3` — `mov eax,ds:0x51fbd0 / ret`.
CONFIRMED as the same object `0x4BAB00` installs the shade table into:

```
4bab00:  call 0x4b6220
4bab05:  mov  cl,[eax+0xf0]
4bab0b:  shr  cl,0x6
4bab11:  je   0x4bab28
4bab14:  mov  edi,[eax+0xc4]      ; the SHADE TABLE
4bab1f:  mov  ecx,0x800           ; 0x800 dwords = 8192 bytes
4bab24:  rep movs dword
```

Same global, same `+0xC4`. That closes the loop from part one's section 09: the
byte the filler reads really is a byte of `palettes\PALETTE.SHD`.

---

## 19. Per-span setup: five gradients, one left prestep, one right clamp

VERIFIED, `0x4C8041`-`0x4C810D`. `dx = span.xRight − span.xLeft`, then four
`idiv` by `dx`, in this order:

```
4c804e:  mov  ecx,[ebx+0x8]   / 4c8053: mov eax,[ebx+0x10] / sub / cdq / idiv edi   ; du
4c805f:  mov  edx,[ebx+0xc]   / 4c8068: mov eax,[ebx+0x14] / sub / cdq / idiv edi   ; dv
4c8074:  mov  edx,[ebx+0x18]  / 4c807f: mov eax,[ebx+0x1c] / sub / cdq / idiv edi   ; dheight
4c8087:  mov  edx,[ebx+0x20]  / 4c8092: mov eax,[ebx+0x24] / sub / cdq / idiv edi   ; dSHADE
```

which re-confirms part one's section 10 span layout exactly: `+0x00/+0x04` x,
`+0x08/+0x10` u, `+0x0C/+0x14` v, `+0x18/+0x1C` height, `+0x20/+0x24` **shade**.
All four gradients are `((right − left) << 0) / dx` on values that are already
16.16, `idiv`, truncating toward zero. VERIFIED.

Left clip, `0x4C80A2`-`0x4C80E5`, when `span.xLeft < 0`:

```
4c80a4:  edi = du      / imul edi,xLeft / u      -= edi   -> [ebx+0x08]
4c80af:  edi = dv      / imul edi,xLeft / v      -= edi   -> [ebx+0x0c]
4c80bf:  edi = dheight / imul edi,xLeft / height -= edi   -> [ebx+0x18]
4c80cf:  edi = dshade  / imul edi,xLeft / shade  -= edi   -> [ebx+0x20]
4c80df:  mov  dword [ebx],0x0
```

Shade is prestepped exactly like u, v and height, with no mask. VERIFIED.

Right clamp, `0x4C80E8`-`0x4C80F9`: `xRight = min(xRight, surface.width − 1)`.
Then `count = xRight − xLeft`; `jle` out if empty. VERIFIED.

**This is what makes the "no clamp is needed" argument airtight.** Both span
endpoints are 16.16 values that themselves came from vertical interpolation
between two vertex rows, each of which part one pins to `0..31` by `and eax,0x1F`
(or the literal 15 for `DONT_SHADE`). The horizontal gradient is
`trunc((right − left)/dx)`, so `|grad| * dx <= |right − left|` and the walk
cannot overshoot either endpoint. The left prestep moves by `|xLeft|` steps, and
`|xLeft| < dx` whenever `xRight >= 0` — which the `jle` guarantees, because
`xLeft` has already been set to 0. So the interpolated shade is provably inside
`[0<<16, 31<<16]` at every pixel, the index is provably inside `[0, 8191]`, and
the exe checks neither. VERIFIED by construction.

---

## 20. Six inner loops, dispatched on **texture width**, identical in the shade

VERIFIED:

```
004C8155  33 c0 66 8b 02 83 c0 f8 83 f8 78 0f 87 ee 02 00
004C8165  00 33 d2 8a 90 48 86 4c 00 ff 24 95 30 86 4c 00
```

```
4c8155:  xor  eax,eax
4c8157:  mov  ax,[edx]              ; TEXTURE width
4c815a:  add  eax,0xfffffff8        ; -8
4c815d:  cmp  eax,0x78              ; 120
4c8160:  ja   0x4c8454              ; outside [8,128] -> the generic loop
4c8166:  xor  edx,edx
4c8168:  mov  dl,[eax+0x4c8648]
4c816e:  jmp  [edx*4+0x4c8630]
```

Tables read out of the image:

```
0x4C8630:  c2 83 4c 00  30 83 4c 00  9e 82 4c 00  0c 82 4c 00  75 81 4c 00  54 84 4c 00
```

| case | entry | selected by texture width | texel address |
|---|---|---|---|
| 0 | `0x4C83C2` | 8 | `sar v,0xd / and al,0xf8` |
| 1 | `0x4C8330` | 16 | `sar v,0xc / and al,0xf0` |
| 2 | `0x4C829E` | 32 | `sar v,0xb / and al,0xe0` |
| 3 | `0x4C820C` | 64 | `sar v,0xa / and al,0xc0` |
| 4 | `0x4C8175` | 128 | `sar v,0x9 / and ecx,0xffffff80` |
| 5 | `0x4C8454` | every other width in 8..128, and via `0x4C8160` everything outside it | `sar v,0x10 / imul eax,texWidth` |

**Those `and` masks are on the texture row, not on the shade.** `sar v,9` then
`& ~127` is just `(v >> 16) * 128` with the multiply folded into the shift; the
same trick at every power of two. It is worth being explicit about because
`and ecx,0xffffff80` sitting three instructions from the shade lookup is exactly
the kind of thing that gets misread as a second mask on the row. It is not.
VERIFIED.

All six loops derive the row identically. Grepping the whole function for the
sequence gives six sites and no seventh:

| loop | `sar shade,0x10` | `shl ,0x8` | `mov ,[disp+0xC4]` |
|---|---|---|---|
| w128 | `0x4C81AF` | `0x4C81B2` | `0x4C81B7` |
| w64 | `0x4C8245` | `0x4C8248` | `0x4C824D` |
| w32 | `0x4C82D7` | `0x4C82DA` | `0x4C82DF` |
| w16 | `0x4C8369` | `0x4C836C` | `0x4C8371` |
| w8 | `0x4C83FB` | `0x4C83FE` | `0x4C8403` |
| generic | `0x4C8497` | `0x4C849A` | `0x4C849F` |

---

## 21. The lookup, byte for byte

The width-128 loop in full. Raw bytes first, so there can be no argument about a
missing term:

```
004C817D  8b 44 24 40 8a 55 00 c1 f8 10 3a d0 89 44 24 44
004C818D  77 3e 8b 44 24 1c 8b cb c1 f9 09 83 e1 80 8b d7
004C819D  c1 fa 10 03 c1 33 c9 8a 0c 02 8b 54 24 10 8b 44
004C81AD  24 2c c1 fa 10 c1 e2 08 03 ca 8b 90 c4 00 00 00
004C81BD  8a 04 11 8a 4c 24 44 88 06 88 4d 00 8b 4c 24 28
```

```
4c817d:  mov  eax,[esp+0x40]        ; height, 16.16
4c8181:  mov  dl,[ebp+0x0]          ; the height buffer at this pixel
4c8184:  sar  eax,0x10              ; newHeight = height >> 16
4c8187:  cmp  dl,al
4c8189:  mov  [esp+0x44],eax
4c818d:  ja   0x4c81cd              ; existing > new (UNSIGNED) -> skip the pixel

4c818f:  mov  eax,[esp+0x1c]        ; texture pixel plane
4c8193:  mov  ecx,ebx               ; v, 16.16
4c8195:  sar  ecx,0x9
4c8198:  and  ecx,0xffffff80        ; = (v >> 16) * 128
4c819b:  mov  edx,edi               ; u, 16.16
4c819d:  sar  edx,0x10              ; = u >> 16
4c81a0:  add  eax,ecx
4c81a2:  xor  ecx,ecx
4c81a4:  mov  cl,[edx+eax*1]        ; texel = the RAW palette index from the texture

4c81a7:  mov  edx,[esp+0x10]        ; SHADE, 16.16
4c81ab:  mov  eax,[esp+0x2c]        ; the display object
4c81af:  sar  edx,0x10              ; row = shade >> 16
4c81b2:  shl  edx,0x8               ; row * 256
4c81b5:  add  ecx,edx               ; row*256 + texel
4c81b7:  mov  edx,[eax+0xc4]        ; PALETTE.SHD
4c81bd:  mov  al,[ecx+edx*1]        ; SHD[row*256 + texel]
4c81c0:  mov  cl,[esp+0x44]
4c81c4:  mov  [esi],al              ; write the pixel
4c81c6:  mov  [ebp+0x0],cl          ; write the height buffer
```

then the step, `0x4C81CD`-`0x4C81FC`: `u += du`, `v += dv`, `height += dheight`,
`shade += dshade`, `esi++`, `ebp++`, `--count`.

That answers questions 2, 3, 5 and 6 outright.

**Truncated, not rounded, not dithered.** `c1 fa 10` is `sar edx,0x10` — an
arithmetic shift, floor for the values that occur. There is no `add 0x8000`
anywhere in `0x4C8020`; the only two large immediates in the whole function are
`0xfffffff8` (the width-dispatch offset) and `0xffffff80` (the texture-row mask),
and neither touches shade. There is no dither table, no per-pixel ordered
pattern, no `xor` of a screen coordinate into the value. VERIFIED at the byte
level.

**`shl edx,0x8` — row × 256, and the texel is the raw palette index.**
`c1 e2 08` is the multiply; `8a 0c 02` at `0x4C81A4` reads one byte straight out
of the texture's pixel plane into a register that `xor ecx,ecx` has just
zeroed, so the texel is 0..255 with no remapping. `add ecx,edx / mov al,[ecx+edx*1]`
— one add, one load, into `[display+0xC4]`. VERIFIED.

**No second `and 0x1F`, no clamp, neither.** Between `sar edx,0x10` and the byte
load there is exactly `shl edx,0x8`, `add ecx,edx` and `mov edx,[eax+0xc4]`.
No `cmp`, no `jcc`, no `and`, no `cmov`. VERIFIED.

The **flat-colour** filler `0x4C0B10` — the per-scanline routine of `0x4C0C70`,
which part one showed treats `point+0x0C` identically — does the same thing:

```
004C0BE2  8b d3 c1 fa 10 38 17 77 2a 8b 6c 24 14 8b d9 c1
004C0BF2  fb 10 8b ad c4 00 00 00 c1 e3 08 03 eb 8b 5c 24
004C0C02  2c 81 e3 ff 00 00 00 8a 5c 1d 00 88 1e 8b 5c 24
```

```
4c0be2:  mov  edx,ebx / sar edx,0x10 / cmp [edi],dl / ja skip   ; same height test
4c0bef:  mov  ebx,ecx               ; shade, 16.16
4c0bf1:  sar  ebx,0x10
4c0bf4:  mov  ebp,[ebp+0xc4]        ; PALETTE.SHD
4c0bfa:  shl  ebx,0x8
4c0bfd:  add  ebp,ebx
4c0bff:  mov  ebx,[esp+0x2c]        ; the primitive's flat colour index
4c0c03:  and  ebx,0xff              ; masked -- the COLOUR, not the row
4c0c09:  mov  bl,[ebp+ebx*1]
4c0c0d:  mov  [esi],bl
```

Note where the only mask in that routine actually lands: on the *colour index*,
to make the byte-sized flat colour safe as an array subscript. Not on the row.
VERIFIED.

---

## 22. The height test

VERIFIED, and it is the same three instructions in all six shaded loops:

| loop | `sar eax,0x10` | `cmp dl,al` | `ja` |
|---|---|---|---|
| w128 | `0x4C8184` | `0x4C8187` | `0x4C818D` |
| w64 | `0x4C821B` | `0x4C821E` | `0x4C8224` |
| w32 | `0x4C82AD` | `0x4C82B0` | `0x4C82B6` |
| w16 | `0x4C833F` | `0x4C8342` | `0x4C8348` |
| w8 | `0x4C83D1` | `0x4C83D4` | `0x4C83DA` |
| generic | `0x4C8463` | `0x4C8466` | `0x4C846C` |

Semantics, VERIFIED:

- The buffer is **one unsigned byte per pixel**, in the destination surface's
  second plane at `[surface+0x14]`, `width*height` bytes immediately after the
  pixel plane.
- The test is `cmp dl,al` / `ja skip`, an **unsigned byte** compare of the
  existing value against the *low byte* of `height >> 16`. The pixel is drawn
  iff `existing <= new`. **Greater wins, and ties draw** — so among coplanar
  surfaces the later primitive overwrites, and since part one's section 03 shows
  piece iteration runs backwards (last piece first, `sub ecx,0x36` at
  `0x45A3ED`), "later" means *earlier in the piece list*.
- Both the compare and the store use only the low byte (`mov cl,[esp+0x44]`,
  `mov [ebp+0x0],cl`). A height outside 0..255 **wraps**. That is what the
  `+125`/`+50` bias in part one's section 04 exists to prevent.
- The buffer is cleared to **0** when the surface is allocated (`0x437C45`:
  `xor eax,eax` then `rep stos` over the height plane), so the first primitive
  to touch a pixel always passes.

**What it means for overlapping pieces.** The value being tested is
`pt.height = Y + K` from part one's section 04 — the vertex's **model-space Y**,
not a camera-space depth. So a unit's pieces are sorted against each other by
*height above the model origin*. In TA's fixed dimetric view that is a workable
proxy for depth, and it is what makes a turret draw over the hull it sits on
without any per-face sorting. It is **not** a Z-buffer in the usual sense: two
pieces at the same model height that overlap on screen do not sort at all, they
just paint in list order. And because the interpolant is per-pixel 16.16 across
the quad, the resolution is per-pixel, not per-face.

**`K` is `digger`.** Part one's section 11 found `[unitdef+0x241]` bit 30
selecting 125 over 50 but did not name the bit. Bit 30 of `def+0x241` is the FBI
key **`digger`** (string at `0x503AF0`, parsed at `0x42C880`/`0x42C895`,
`shl eax,0x1e`) — cross-referenced from `docs/TOTALA-EXE.md`'s field table.
CONFIRMED against the shipped data: in the v3.1 unit set (189 FBIs) exactly two
declare `Digger=1`, **ARMAMB** and **CORTOAST** — the two pop-up defences, whose
models sink below the origin. A larger bias is exactly what a model that extends
downward needs to keep `Y + K` inside an unsigned byte. The purpose stated in
part one as unexplained is therefore CONFIRMED as "keep the height byte from
wrapping on models that go below zero".

---

## 23. The height plane is conditional — and `ZBuffer=0` turns shading OFF

This is the one substantive thing part one could not have seen, and it is the
only place in either half where something outside `0x459C70` can change whether
a face is shaded at all.

**The branch.** VERIFIED, `0x4C8141`:

```
4c813f:  add  esi,edx
4c8141:  test ebp,ebp                ; ebp = [destSurface+0x14], the height plane
4c8143:  je   0x4c84f4               ; NO height plane -> the unshaded variants
```

and the `0x4C84F4` family is a *second* width dispatch (tables at `0x4C86C4` /
`0x4C86DC`, same six cases) into loops that do **no height test and no SHD
lookup**. The width-8 one entire:

```
4c85a6:  mov  edx,[esp+0x18]
4c85aa:  mov  [esp+0x48],edx
4c85ae:  mov  ebp,[esp+0x1c]         ; texture pixels
4c85b2:  mov  eax,ebx / sar eax,0xd / and al,0xf8    ; (v>>16)*8
4c85b9:  mov  edx,edi / sar edx,0x10                 ; u>>16
4c85be:  add  ebp,eax
4c85c0:  inc  esi
4c85c1:  add  edi,ecx
4c85c3:  mov  al,[edx+ebp*1]         ; the raw texel
4c85c6:  mov  edx,[esp+0x14]
4c85ca:  mov  [esi-0x1],al           ; straight to the destination
```

The 128/64/32/16 cases call out to four hand-written 68-byte blitters at
`0x4CD896`, `0x4CD8DA`, `0x4CD91E`, `0x4CD962`, which do the same thing with the
row multiply folded into a shift (`shl ebx,0x7 / and ebx,0xff800000 / add ebx,ecx
/ shr ebx,0x10`). None of them touches `[display+0xC4]`. VERIFIED.

**Which surface gets a height plane.** VERIFIED. The shaded model driver is
called with the drawable's *cached bitmap* as its destination (part one, section
12a), and `0x4586A0` allocates that bitmap through one of two routines:

```
004586CE  8b 44 24 24 85 c0 75 33 f6 85 14 01 00 00 01 75
004586DE  2a d9 85 04 01 00 00 d8 1d c0 d4 4f 00 df e0 f6
004586EE  c4 40 74 17 ...
```

```
4586ce:  mov  eax,[esp+0x24]              ; arg2 of 0x4586A0
4586d2:  test eax,eax
4586d4:  jne  0x458709                    ; nonzero -> 0x437BE0
4586d6:  test byte [ebp+0x114],0x1        ; the unit's ZBUFFER flag
4586dd:  jne  0x458709                    ; set    -> 0x437BE0
4586df:  fld  dword [ebp+0x104]
4586e5:  fcomp dword ds:0x4fd4c0          ; 0.0
4586ed:  test ah,0x40
4586f0:  je   0x458709                    ; != 0  -> 0x437BE0
4586f2:  ...  call 0x437b50               ; otherwise 0x437B50
```

`0x437BE0` allocates `2*w*h + 0x18` and sets **both** planes:

```
00437C31  8d 42 18 8b ce 66 89 1a 66 89 7a 02 8b d9 89 42
00437C41  10 8d 3c 30 33 c0 c1 e9 02 89 7a 14 f3 ab 8b cb
00437C51  83 e1 03 f3 aa 8b 7a 10 8b ce c6 42 08 01 8b d1
00437C61  b8 01 01 01 01 c1 e9 02 f3 ab 8b ca 83 e1 03 f3
```

```
437c31:  lea  eax,[edx+0x18]        ; pixel plane
437c3f:  mov  [edx+0x10],eax
437c42:  lea  edi,[eax+esi*1]       ; height plane = pixels + w*h
437c45:  xor  eax,eax
437c4a:  mov  [edx+0x14],edi
437c4d:  rep  stos dword            ; height plane cleared to 0
437c56:  mov  edi,[edx+0x10]
437c5b:  mov  byte [edx+0x8],0x1    ; the transparent colour key
437c61:  mov  eax,0x1010101
437c69:  rep  stos dword            ; pixel plane filled with index 1
```

`0x437B50` allocates `w*h + 0x18`, sets the same key and the same fill, and
**writes zero to `[surface+0x14]`** (`437b8c: mov [eax+0x14],ecx` with
`ecx = 0`). VERIFIED.

**What `[unit+0x114]` bit 0 is.** VERIFIED — and this is one the flat listing
hides, because `objdump` desynchronises just before it. Searching the raw `.text`
for the displacement `14 01 00 00` finds a write at `0x485AE1`:

```
00485ACB  8b 8a 41 02 00 00 8b 86 14 01 00 00 c1 e9 07 24
00485ADB  fe 83 e1 01 0b c1 89 86 14 01 00 00
```

```
485acb:  mov  ecx,[edx+0x241]      ; the unit definition's flags word A
485ad1:  mov  eax,[esi+0x114]
485ad7:  shr  ecx,0x7              ; bit 7
485ada:  and  al,0xfe
485adc:  and  ecx,0x1
485adf:  or   eax,ecx
485ae1:  mov  [esi+0x114],eax      ; unit+0x114 bit 0 = def+0x241 bit 7
```

and bit 7 of `def+0x241` is the FBI key **`zbuffer`** — CONFIRMED at the parser,
which also needs a re-sync to read:

```
42c5a6:  push 0x503bd0             ; the string "zbuffer"
42c5ab:  mov  ecx,[esp+0x1c]
42c5b6:  call 0x4c46c0             ; read the boolean
42c5bb:  and  eax,0x1
42c5be:  mov  ecx,[ebp+0x241]
42c5c5:  shl  eax,0x7
42c5c8:  and  cl,0x7f
42c5d0:  or   eax,ecx
42c5d6:  mov  [ebp+0x241],eax
```

`0x503BD0` reads `"zbuffer"`. CONFIRMED, and it agrees with the field table in
`docs/REVERSE-ENGINEERING-PRIORITIES.md` (`zbuffer` = bit 7, parsed `0x42C5A6`).

**`[unit+0x104]`** — part one left this NOT ESTABLISHED and I am only narrowing
it, not closing it. It is set at unit creation, `0x485B09`-`0x485B37`: one branch
writes `+0x104 = 0.0` and `+0x108 = word[def+0x1FA]`, the other writes
`+0x104 = 1.0f`, `+0x100 = 0` and `+0x108 = 0`. `unit+0x108` is current hit
points and `def+0x1FA` is `maxdamage` (both established in `docs/TOTALA-EXE.md`),
so the first branch is "spawn complete, full health" and the second is "spawn as
a nanoframe, zero health". **`[unit+0x104]` is therefore a build-state float that
is 0.0 for a finished unit and non-zero while it is being built** — INFERRED, but
corroborated by `0x458DD0`, the routine that draws the construction wipe, which
returns immediately unless the cached bitmap **has** a height plane
(`458dda: mov eax,[ebp+0x14] / test / jne`) *and* `+0x104 != 0.0`. The build
effect needs the height plane; that is why an incomplete unit is given one
regardless of `ZBuffer`.

**So, put together.** The cached bitmap gets a height plane — and therefore the
textured spans get shaded — when **any** of:

1. `0x4586A0`'s arg2 is nonzero (the attached-drawable call at `0x459670`), or
2. the unit's FBI has `ZBuffer=1`, or
3. the unit is still under construction.

It gets `0x437B50` and **no** height plane only for a *completed* `ZBuffer=0`
unit drawn as the main drawable — and then its textured quads are drawn raw,
unsorted and unshaded, while its flat-coloured n-gons are still shaded, because
`0x4C0B10`'s own no-height path at `0x4C0C38` **does** keep the lookup:

```
4c0c46:  mov  ebx,[esp+0x14]          ; the display object
4c0c4a:  mov  edx,ecx / sar edx,0x10  ; row
4c0c4f:  mov  ebx,[ebx+0xc4]          ; PALETTE.SHD
4c0c57:  shl  edx,0x8
4c0c5a:  add  edx,edi                 ; + the flat colour
4c0c5e:  mov  dl,[edx+ebx*1]
```

**How much data this affects.** CONFIRMED by counting the shipped FBIs. Of the
189 unit definitions in the v3.1 set, **187 declare `ZBuffer=1` and exactly two
declare `ZBuffer=0`: `CORFAV.FBI` and `CORTRUCK.FBI`.** ARMSOLAR — part one's
whole reference case — is `ZBuffer=1`, so every number in part one's section 13
stands. The asymmetry is a real behaviour of the original, not a bug: the two
cheapest Core vehicles skip both the per-pixel sort and the shading, and RWE
should reproduce it or consciously choose not to. INFERRED that this was a speed
optimisation; NOT ESTABLISHED whether it is visible in play. *Ported
2026-09-07:* `ZBuffer` is read from the FBI and a finished `ZBuffer=0` unit is
drawn with the shade strength at zero; its flat-colour n-gons go unshaded with
the rest, a difference recorded in TOTALA-EXE.md S:88.

---

## 24. Per-pixel modifiers: none. And where transparency actually happens

**Question 5, answered by enumeration.** Every memory operand in the whole of
`0x4C8020`-`0x4C862A`, tabulated, contains exactly **one** table pointer:
`[edx+0xc4]` / `[eax+0xc4]`, six occurrences, one per shaded loop. There is:

- **no reference to `[display+0xC0]`** — `PALETTE.ALP`, the anti-alias blend
  table. NOT FOUND.
- **no reference to `[display+0xC8]`** — `PALETTE.LHT`, the light table.
  NOT FOUND.
- no second byte load from any other array, no `fog`-like global, no distance
  term, no reference to the unit's world position or to any per-player or
  per-frame value. The only globals the routine touches at all are the two jump
  tables and `ds:0x51FBD0` via `0x4B6220`. NOT FOUND.
- no alpha blend: the pixel write is `mov [esi],al`, a plain store, in every one
  of the six loops. The destination byte is never read. VERIFIED.

Part one's structural argument — that because the shading is baked into a cached
bitmap shared by every instance of the model in the same pose, the level provably
cannot depend on world position, distance, terrain or fog — is now matched by a
direct one: there is nothing in the pixel loop that *could* carry such a term.

**Question 6, the special-cased index — and it is not 0.** VERIFIED: there is no
transparency test anywhere in `0x4C8020`. A texel of any value, index 0 included,
is put through the table and written, and it writes the height buffer too. So
inside the unit's own bitmap a "transparent" texel is fully opaque and does
occlude.

Transparency is applied **later**, at the composite, and the key is **index 1**,
not 0. `[surface+0x08]` is the surface's transparent colour, both allocators set
it to `1` (`c6 42 08 01` at `0x437C5B`, `c6 40 08 01` at `0x437BB4`) and both
prefill the pixel plane with `0x01010101`. `0x4B90A0` is the compositor:

```
004B9130  8b 54 24 1c 8a 0a 8a 55 08 3a ca 74 1e 8b 5c 24
004B9140  2c 33 d2 8a 17 03 d3 33 db 8a 18 3b da 7f 0c 8a
004B9150  54 24 2c 88 0e 8a 0f 02 ca 88 08 8b 4c 24 1c 46
```

```
4b9130:  mov  edx,[esp+0x1c]     ; the source pixel pointer
4b9134:  mov  cl,[edx]           ; the source pixel
4b9136:  mov  dl,[ebp+0x8]       ; the SOURCE surface's transparent key
4b9139:  cmp  cl,dl
4b913b:  je   0x4b915b           ; equal -> skip
4b913d:  mov  ebx,[esp+0x2c]     ; arg5, a per-unit height bias
4b9143:  mov  dl,[edi]           ; the source height byte
4b9145:  add  edx,ebx
4b9149:  mov  bl,[eax]           ; the destination height byte
4b914b:  cmp  ebx,edx
4b914d:  jg   0x4b915b           ; destination in front -> skip
4b914f:  mov  dl,[esp+0x2c]
4b9153:  mov  [esi],cl           ; the pixel
4b9155:  mov  cl,[edi]
4b9157:  add  cl,dl
4b9159:  mov  [eax],cl           ; source height + bias into the screen's height plane
```

So the model's height buffer propagates into the screen's, offset by a per-unit
bias, and the whole scene y-sorts through the same mechanism. VERIFIED. (This is
the *attached* drawable's composite — `0x4596D8` is `0x4B90A0`'s only caller in
the image. The main unit's composite goes through a different routine which I did
not chase; the key and plane semantics come from the surface record and the
allocators, which are shared. NOT ESTABLISHED which routine composites the main
unit's bitmap.)

**And this has a real consequence for the shading.** Because the transparent key
is index 1 and the table's output is an index, a lit pixel can be punched
transparent by the table itself. Measured directly on the shipped
`palettes\PALETTE.SHD`, the entries whose output is index 1 are:

| row | texel | the texel's own colour |
|---|---|---|
| 7, 8 | 249 | `(255, 0, 0)` |
| 13, 14, 15, 16, 17 | 1 | `(128, 0, 0)` — the key mapping to itself |
| 19, 20, 21, 22, 23, 24, 25 | 215 | `(87, 0, 0)` |

Fourteen entries out of 8192. Rows 14 and 15 mapping texel 1 to itself is the
expected identity. The other nine — rows 7, 8, 13, 16, 17 and 19–25 on texels
249 and 215 — are **holes**: a dark red texel shaded into the middle of the table
comes out as the transparency key and the unit becomes see-through at that pixel.
And the middle of the table is precisely the region that only Gouraud
interpolation can reach (part one, section 08). So the artefact only exists
because the row is interpolated, and only on the mid-red palette entries. CONFIRMED
by measurement of the shipped file. Whether it is ever visible in play is NOT
ESTABLISHED — I did not look for a texture using index 215 or 249.

**Index 0 is not special anywhere in this path.** It is ordinary black,
`pal[0] = (0,0,0)`, and row 0 of the table maps **all 256 texels** to it — the
one row that is a constant. That is what makes part one's ARMSOLAR right panel
uniformly black rather than a dark version of the texture, and it is fully
opaque. VERIFIED (the row-0 slice of `PALETTE.SHD` is 256 zero bytes).

---

## 25. Saturation, and how dark or bright it can get

Collecting the answers to question 6:

- **No saturation and no clamp on the index.** The row is not clamped, the
  product `row*256 + texel` is not clamped, and the byte that comes out of the
  table is written verbatim. VERIFIED.
- **The bounds are structural, not enforced.** Every reachable index is inside
  the 8192-byte table because both span endpoints are in `0..31` and every
  interpolation step is a truncating linear walk between them (section 19). The
  exe relies on that and checks nothing. VERIFIED by construction.
- **Everything about how bright or dark the result is lives in the table.** The
  pixel loop applies no gain, no bias and no gamma. Part one's section 09
  measurements — identity at rows 14 and 15, 1.807× at row 31, saturated colours
  not brightening at all — are therefore the complete story of the brightness
  range. Nothing in the second half widens or narrows it.
- **The darkest possible result is index 0, and only row 0 forces it for every
  texel.** Rows 1–31 map 13 to 118 of their 256 texels to index 0, decreasing
  monotonically to a floor of exactly 13 from row 8 upward. Those thirteen are
  not arbitrary: `PALETTE.PAL` has exactly thirteen entries that are already
  `(0,0,0)` — indices 0, 10–15 and 240–245 — and from row 8 up the set of texels
  mapping to index 0 **is** that set, entry for entry. CONFIRMED by measurement.
  Nothing else ever goes fully black above row 7.

---

## 26. What this settles for part one's section 16

Section 16 asked for one thing from this half: *"confirmation that no further
mask or clamp is applied to the interpolated value."*

**Confirmed, twice over.** There is none in `0x4C8BB0` — the whole edge-walk
contains exactly one `and` instruction, `and esi,0x3` at `0x4C8ECD`, on a vertex
index — and there is none in `0x4C8020` either. The interpolated 16.16 value goes
`sar 16`, `shl 8`, into the table, unmodified.

So the model to implement is exactly:

```
row_at_vertex = ( (int32)( 5.0 * dot(n_unnormalised, (-0.8, 1.0, 0.25)) ) ) & 0x1F      [part one]
row(x, y)     = Gouraud interpolate the INTEGER row, 16.16, truncating           [part one §10]
pixel         = PALETTE_SHD[ (row >> 16) * 256 + texel ]                         [this half §21]
```

with, additionally:

- an 8-bit height buffer per drawable, cleared to 0, tested unsigned as
  `draw iff existing <= (height >> 16)`, and written on draw (§22);
- `height = modelY + (digger ? 125 : 50)`, wrapping in a byte (§22);
- the whole lookup **skipped**, and the raw texel drawn, for the textured path
  when the unit's FBI has `ZBuffer=0` and it is finished building — two units in
  stock data (§23);
- index 1, not 0, as the transparent colour of the composited bitmap (§24).

## 27. Still unknown, second half

- Which routine composites the **main** unit's cached bitmap onto the screen, and
  what height bias it passes. `0x4B90A0` is verified but has only one caller,
  the attached drawable at `0x4596D8`. NOT ESTABLISHED; deliberately not guessed.
- Whether the `SHD → index 1` holes (§24) ever fire on a shipped texture. It is a
  data question — does any unit texture use palette index 215 or 249 — and I did
  not check it.
- What sets `[unit+0x104]` to its intermediate values during construction, and
  whether it is a 0..1 fraction or a countdown. Narrowed to "a build-state float,
  0.0 when finished" (§23) but not pinned.
- The width-128 case of the **unshaded** dispatch, `0x4C8518`, calls `0x4CD896`
  and then **falls straight through** into the width-64 case at `0x4C8537`, which
  blits the same span again with a 64-wide stride. Byte-verified
  (`… e8 66 53 00 00  8b 4c 24 44  83 c4 1c  8b 54 24 14 …` — no `jmp` between
  them). It looks like a missing jump, i.e. a genuine bug, but it only affects a
  128-wide texture on a surface with no height plane, so in stock data only
  CORFAV and CORTRUCK could ever reach it. NOT ESTABLISHED whether it is
  reachable in practice; not corrected here.
