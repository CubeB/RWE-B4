# TDF parsing, record access, typed fields and translations parity audit

Spec-room finding, 2026-10-17. Like `BT-WEAPONS-WEAPONS-PARITY.md`,
`BT-ORDERS-UNIT-ORDERS-PARITY.md` and `TA-SYSTEMS-PARITY.md`, it allocates no section
number in the main register. This is the finding that implementation work cites for
how the original reads a TDF-syntax text file into records and fields, how it turns a
field into a number or a string, how duplicates resolve, and how it localises. It is
the layer under every data loader (FBI, weapon TDF, feature TDF, OTA, sound, GUI, side
data), so a difference here changes a loaded value everywhere at once. Per `AGENTS.md`
and ADR-0004 a change to `src/rwe/io/tdf` cites this audit.

What each loader *does* with its keys (which defaults, which units) is the business of
`TOTALA-EXE-DATA.md` and the `BT-*-PARITY.md` files for each subject, and is not
repeated. The register's open row **BT-31** (duplicate blocks, non-numeric values) is
answered here from shipped data: see rules 5 and 6.

## Source and evidence class

- **Source.** One prose chapter of the TA: Byte Tactics systems manual, at commit
  `97d7ac59b460417bcdfa629cf90da99f31818c65` (worktree path
  `D:\bytetactics\.worktrees\naval-reference\docs\systems\modules\util-tdf.md`). Nothing
  else under that tree was opened: no source, no decompilation, and none of its source
  links were followed. Addresses are the ones the chapter names; rules are restated in
  prose. The spec-room discrepancy register
  (`D:\RWE-spec-room\docs\BYTE-TACTICS-DISCREPANCIES.md`) was read, read-only; the row
  this audit answers is BT-31, and BT-29 and BT-30 (archive ranking and language
  directories) are the neighbouring open rows.
- **Evidence class.** A **static reading** by that project: nothing in it was executed.
  Weaker than `TOTALA-EXE*.md`, which were read from the retail executable. Disagreements
  are marked **CONFLICT** with no winner picked.
- **No executed observation of the original was used.** No original-game play and no
  demo oracle. The Gaps table says which items an original check settles.
- **The RWE half** is a read-only audit of `src/rwe/io/tdf/` (`TdfParser.h`,
  `TdfBlock.*`, `tdf.cpp`, the three adapters), `src/rwe/util/rwe_string.cpp`'s trimming
  and `src/rwe/game/GameMediaDatabase.cpp` at the working tree of 2026-10-17. Nothing
  was built, run or changed, and tests were not run. Another agent had uncommitted edits
  in the tree, so every line number is "about".
- **A census of shipped data, with a model of each parser.** The chapter's grammar and
  RWE's parser were each written down as a small scratch program (`tdfcensus.py` in the
  session scratchpad; throw-away, not checked in) and run over every `.fbi`, `.tdf` and
  `.ota` entry in every archive of the retail install (`totala1-4`, `tactics1-8`,
  `worlds`, `ccdata`, `ccmaps`, `ccmiss`, `btdata` and `btmaps` `.ccx`, `rev31.gp3`, the
  `.ufo` files), read with `tools/hpi.py`'s reader: **1941 entries**, counted per
  archive and not de-duplicated by path. The model of the original is **only as good as
  the chapter's prose**, so it can show where RWE and *the chapter* part company on real
  files, not where RWE and the executable do. `tools/hpi.py` is a second implementation of the
  engine's HPI reader (its own header says to believe the C++), checked there byte for byte
  against the engine's on the retail archives.

Status words as in the earlier audits: **MATCHES**, **DIFFERS**, **MISSING**,
**UNCLEAR**, **DIVERGENCE (documented)**, **CONFLICT**.

## Index

1. Shape and what the register already holds
2. The grammar: records, fields, delimiters and what is fatal
3. Comments
4. Stray semicolons, top-level fields and a stray closing brace
5. Duplicate fields and duplicate blocks (answering BT-31)
6. Typed getters: integer, float, boolean, fixed, string (answering BT-31)
7. Case, whitespace and encoding
8. Record navigation and the record checksum
9. Translations and localised fields
10. Loading and failure
11. Gaps to fix

---

## 1. Shape and what the register already holds

| Chapter section | Register or earlier audit | Verdict |
| --- | --- | --- |
| 2, 7, 8 tree, navigation | none (storage model) | not applicable: RWE's `TdfBlock` is a map of blocks and a map of properties |
| 4 comment removal | none | rule 3 |
| 5 grammar and errors | none | rule 2, rule 4 |
| 6 duplicates | register row BT-31, open | rule 5 |
| 6 record checksum | `TOTALA-EXE-DATA.md` section 117 | **MATCHES** (rule 8) |
| 8 typed getters | register row BT-31, open | rule 6 |
| 9, 10 translations | none | rule 9 |

## 2. The grammar: records, fields, delimiters and what is fatal

**Reference (sections 3, 5).** A loaded file becomes a root record named "root". Inside any
record the parser skips space, tab, carriage return and newline; then:

- an opening square bracket starts a child: the name runs to the **next closing bracket**
  and is trimmed, an opening brace must follow after whitespace, and the child is built
  recursively and appended in source order;
- a closing brace ends the record;
- anything else is a **field**: the key is everything from here to the **first equals sign
  found anywhere in the remaining text**, trimmed; the value is everything after it to the
  **next semicolon**, trimmed.

The searches for the equals sign and the semicolon are `strchr` over the rest of the
buffer, so they are **not bounded to the field, the line or the block**. Newlines are
whitespace and do not end a field. No quotes, escapes, includes or schema exist. Empty
trimmed keys and values are accepted. The fatal cases (the process ends with an error
message naming record and file) are: no closing bracket, no opening brace after a child
name, end of text inside a nested record, no equals sign, no semicolon. End of text at the
root is ordinary completion.

**RWE.** `TdfParser.h` is a hand-written recursive-descent over the same four ideas, with
three differences in how it finds the ends of things.

- **Key termination: DIFFERS in form, not on shipped data.** RWE's key runs to the first
  equals sign **or semicolon or end of file** (`expectPropertyName`, about 260), then
  demands an equals sign. A semicolon inside what the original would treat as the key
  (a `name;` with no equals sign, followed on a later line by `other=1;`) is a parse error in
  RWE and, per the chapter, in the original the key would be the whole span including the
  semicolon and the newline. Both lose the *intended* `other` property in the second case
  (its name is mangled into the first key); RWE refuses the file, the original loads it.
  **Census: one shipped file has a key that spans a line.** `armscorp.fbi` (in
  `ccdata.ccx`) reads `Scorpione` on one line, with no `=` or `;`, and `canguard=1;` on the
  next. Both parsers produce the one key `Scorpione\ncanguard` (the original's carries a
  carriage return as well) and neither yields a `canguard` property; the unit has no
  `CanGuard` in either engine. This is also why the spanning key is **not** a difference
  RWE has missed: RWE's key scan does not stop at a newline.
- **Value termination: MATCHES.** Both run to the next semicolon wherever it is, so a
  forgotten semicolon swallows the next line into the value. Census: `corals.tdf` (in
  `ccdata.ccx` and `btdata.ccx`) has `footprintz=10` with no semicolon, followed by
  `height=120;`, giving the value `10\nheight=120` in both; an integer read gives 10 and
  `height` is lost in both.
- **Empty key: DIFFERS.** RWE throws on a key that begins with an equals sign, newline,
  semicolon or end of file (about 262); the original stores an empty key. Census: none.
- **The block name: MATCHES.** To the next closing bracket, trimmed (comments inside it are
  also dropped in RWE).
- **Hard failure: DIFFERS in effect, MATCHES in kind.** The original ends the process; RWE
  throws a `TdfParserException` with line and column and each loader reports and skips
  that content (`CLAUDE.md`, the security rule that a fault in content costs that content).
  This is the right replacement and is the reason a mod with a typo does not take the game
  down.
- **Nesting depth: DIFFERS (RWE only).** RWE refuses more than 64 levels
  (`MaxBlockDepth`, about 136) because a file of nothing but opening brackets overran the
  stack (issue #75). The original has no limit and recurses to whatever the stack allows.
  The parser's own comment says TA's deepest files nest "a handful of levels".

## 3. Comments

**Reference (section 4).** A separate pass runs before parsing and **blanks** comments in
place: a double slash to the end of line (the newline kept), a slash-star pair through its
close (including the pair and **including any newlines inside it**). An unclosed block
comment consumes the rest of the text with **no error**. The pass does not recognise quotes,
so a comment opener inside a value is a comment.

**RWE.** Comments are consumed inline by the parser (`acceptBlockComment`, about 340;
`acceptLineComment`) wherever whitespace or a character can appear.

- **A comment in the middle of a key, value or block name: DIFFERS.** The original
  replaces the comment with blanks, so `x=ab/* note */cd;` has the value `ab` then eight
  spaces then `cd`. RWE removes the comment and joins `ab` and `cd`. The two differ for any
  value with a comment inside it that is later read as text (a string with spaces) or as a
  number (`atoi` stops at the first blank, so `12 /* x */ 34` is **12** in the original and
  **1234** in RWE). **Census: no shipped file has a comment in the interior of a key, value or block
  name** (a comment at the edge of a token, such as `x=5 /* c */;`, trims to the same text
  either way; every difference between the two models on shipped data is accounted for in
  rules 4, 5 and 7). So this is a mod-data difference, not a retail
  one.
- **An unclosed block comment: DIFFERS.** The original quietly drops the rest of the file;
  RWE throws "Expected */, got end of file" (about 349) and the loader skips the file.
  Census: none unclosed.
- **Quotes: MATCHES.** Neither understands them; a `//` in a value starts a comment in both.
- **Newlines inside a block comment: DIFFERS, invisible.** The original blanks them. RWE's
  line numbers count them, which only changes the text of an error message.

## 4. Stray semicolons, top-level fields and a stray closing brace

**Reference (section 5).** After a child's closing brace the parser is back at the field
branch. A semicolon there is not whitespace and not a bracket or brace, so on the chapter's
reading it begins a *field*, whose key runs to the next equals sign. When parsing the root
without a parent, a closing brace **returns early and ignores the rest of the text** rather
than failing. A field may appear at the root (outside any block); it is stored on the root
record.

**RWE.**

- **A semicolon after `}` or between statements: DIFFERS, by design.** `TdfParser.h` accepts
  a stray semicolon at the top level (about 163) and inside a block (about 232) and skips it.
  The comment there says the original's own files do it ("UNITVIEW.TDF and Translate.tdf
  both close their last block with `};`"). On the chapter's reading the original's field
  branch would try to find an equals sign after it and, finding none, end the process.
  **Census: the model of the original rejects exactly one shipped file, `gamedata/unitview.tdf`
  in `totala1.hpi`** (293 bytes: a `[RESOURCES]` block with `website=`, `logo=`,
  `armbackground=`, `corebackground=`, closed with `};`), with "missing equals sign". The retail
  game starts, so either **the original never loads that file through this parser** (the
  keys read like a Cavedog tool's resource list, not a game table) or **the chapter's
  grammar leaves out a rule for a stray semicolon**. Which is unknown; the census cannot say.
  Separately, the code comment is slightly wrong about the second file: `Translate.tdf`
  contains a single `};`, but it sits **inside a value** (`French = ~ (touche};` in the entry
  for the tilde key, a typo of Cavedog's), not at a block end, and both models read it as part
  of that value. No shipped file contains `;;` or `{;`.
- **A field at the top level: DIFFERS.** RWE requires an opening bracket at the top and
  throws on anything else. The original stores the field on the root. Census: none; the
  scan of the 1941 entries found no top-level field. A mod file written as a flat list of
  `key=value;` lines would load in the original (readable only through a root-level getter)
  and fail in RWE.
- **A stray closing brace at the top level: DIFFERS.** The original stops and silently
  ignores the rest of the file. RWE throws on the expected bracket. Census: none.
- **A stray closing brace inside a block's text: MATCHES.** Both end the block.

## 5. Duplicate fields and duplicate blocks (answering BT-31)

**Reference (sections 2, 6, 7).** Fields are held in a vector sorted by a case-insensitive
key comparison. Insertion does a lower-bound search by that comparison and **reuses a slot
only when the key at the found position is exactly equal (case-sensitively)**; otherwise it
inserts there, which is *before* any case-variant already present. A lookup is a
lower-bound search and returns the element it lands on. Children are **not** merged: they
stay in source order, and `FindSubRecord` and `SelectRecord` return the **first**
case-insensitive match, while a walk by index sees all of them.

**Register row BT-31 (open).** "Repeated field updates" for the reference; "duplicate block
replaces the earlier" for RWE. The two halves concern different things; the row mixes
fields and blocks.

**RWE.** `insertOrAssignProperty` and `createBlock` (`TdfBlock.cpp`, about 186-200) are
`insert_or_assign` into a case-insensitive map, so for both fields and blocks **the last one
wins** and the earlier is discarded.

- **Duplicate fields, same spelling: MATCHES.** The original replaces the value; RWE
  assigns. Last wins in both.
- **Duplicate fields, different spelling: MATCHES on the chapter's reading, UNCLEAR on the
  original.** On the insert-before reading above, the newest variant sits first and is what
  a lookup returns, so the last also wins. The chapter says only that "later lookup returns
  its first member", which is the sentence that depends on how the insertion places equals;
  if the original places a new variant *after* the existing ones, the first-written
  spelling wins instead. **Census: 12 distinct shipped aircraft FBIs carry both
  `MaxWaterDepth=0;` and `maxwaterdepth=255;`** (`armcsa`, `armfig`, `armlance`,
  `armseap`, `armsehak`, `armsfig`, `corcsa`, `corhunt`, `corseap`, `corsfig`, `cortitan`,
  `corveng`, in `ccdata.ccx` and `btdata.ccx`), in that order. RWE reads 255. Whether
  the original reads 0 or 255 is not decided by the chapter. For an aircraft the value is
  almost certainly dead (a flyer does not wade), so this changes nothing in a game, but
  it is a clean test of the rule and the shipped evidence the spec room can use.
  Forty entries (per archive copy) repeat a key at all: twenty-nine with a different
  spelling and twenty-two with a different value (twenty with both, the aircraft above); the
  other eighteen repeat the same value (for example `armaas` in `cortron_weapon.tdf` and
  `edgeeffectiveness` in `armyork_weapon.tdf`), where nothing can differ.
- **Duplicate blocks: DIFFERS, consumer by consumer.** RWE keeps the last block of a name and
  drops the earlier entirely, contents and all. The original keeps all of them in order, so
  its result depends on how the caller reads:
  - a caller that **selects by name** (a unit's `[UNITINFO]`, an OTA's `[GlobalHeader]`) gets
    the **first** block in the original, the **last** in RWE;
  - a caller that **walks the root's children** (the translation loader, the useonly list
    applier at `0x431740`, sound and weapon tables when they iterate) sees **every** block in
    the original; for a keyed table each later block's fields overwrite the earlier's, so
    the effective result is **last wins per key but a union across blocks**, where RWE keeps
    only the whole last block.
  **Census: six entries repeat a block name**: `gamedata/translate.tdf` in `totala1.hpi`,
  `ccdata.ccx`, `btdata.ccx` and `rev31.gp3` (translation keys `kills` four times, `kill` and
  `losses` twice), and two mission unit lists, `camps/useonly/raid on vetes.tdf` and
  `camps/useonly/ice base assault.tdf`. The two unit lists are sets of **empty** blocks
  (`[ARMCA]{}` twice), so a repeated name changes nothing. RWE does not read
  `Translate.tdf` at all (rule 9). **So on retail data BT-31's block half changes no loaded
  value**; it would for a mod that repeats an `[UNITINFO]`, a weapon block or a feature
  block inside one file, in which case the original's name selection and RWE's last-wins
  parting company is the first thing to check.

## 6. Typed getters: integer, float, boolean, fixed, string (answering BT-31)

**Reference (section 8).** The integer and float getters apply `atoi` and `atof` to the found
value; **the caller's default is used only when the key is absent.** A value that is present
but malformed (empty, letters) is converted by the C routines, so it becomes **zero** or a
numeric prefix. The fixed getter applies `atof`, multiplies by 65536 and truncates to an
integer; its absent-key default is already a raw fixed value and is not multiplied. The string
getter copies at most `size` bytes, terminates at `size - 1`, and returns 1 for any present
key including an empty value; for an absent key it copies the default with no length check
and returns 0.

**Register row BT-31 (open).** "Malformed number goes through C conversion, giving zero or a
prefix" against RWE's "no-prefix value falls back to the default".

**RWE.** `TdfBlock::extract<T>` reads the value through an input string stream
(`TdfBlock.h`, about 17-27); `readOrDefault` takes the default on **any** failure.

- **A numeric prefix: MATCHES.** `13O`, `3.6.`, `12abc`, `1.5f` give 13, 3.6, 12, 1.5 in both.
  Census: the only malformed-looking numbers in the weapon, unit and feature tables are
  `maxvelocity=3.6.` in `corpt.fbi` (3.6 in both) and
  `weaponacceleration=13O` (a letter O) in `missiles.tdf` (four occurrences across the
  archives; 13 in both). **Every
  such value in the shipped tables parses to the same number either way.**
- **A value with no numeric prefix: DIFFERS.** Empty, or letters, or a lone sign or dot:
  `atoi` and `atof` give **0**; RWE's stream fails and the call falls back to the
  **caller's default**, and `expect<T>` throws. They agree when the default is zero.
  **Census: one shipped value is empty where a number is read**, `reproducearea=;` in
  `smudges.tdf` (`totala1.hpi`), and the feature reader's default is zero, so both give zero.
  Everything else is a mod question: a mod's `key=;` or `key=none;` on a field whose
  default is not zero (a speed, a range, a `duration`, which `BT-WEAPONS-WEAPON-TYPES-PARITY.md`
  rule 3 records defaults to 1/30) loads **zero** in the original and the **default** in RWE.
- **A value past the integer range: DIFFERS, trivial.** The stream fails; the C
  routines clamp or wrap. Census: none.
- **Boolean: DIFFERS in the same way.** RWE's boolean is "the integer is nonzero"; a word such
  as `true` fails and falls back to the default, while `atoi("true")` is zero (false).
  `BT-UNITS-UNIT-TYPES-PARITY.md` and the FBI table carry the per-flag defaults.
- **Fixed point: representation.** The original truncates `value * 65536` to an integer at
  load (a velocity of 2.3 becomes the integer 150732, which is 2.29998779 as a
  number); RWE keeps a float. The register's x65536 table
  (`TOTALA-EXE-MOVEMENT.md` near 1842 and `-DATA.md` weapon table) already lists which keys
  take the conversion. The difference is under a hundred-thousandth per tick and is the
  `CLAUDE.md` determinism rule's territory, not a gap.
- **Strings: DIFFERS by capacity, UNCLEAR by caller.** The original truncates a value to the
  caller's buffer, which is different for each key. RWE never truncates. Census of the
  longest shipped values: `yardmap` 89 characters, `copyright` 61, `category` 59,
  `description` 30, `name` under 30, weapon names 19, `initialmission` 1329 (an OTA
  script), `missiondescription` 187. Whether any of those exceeds its destination buffer in
  the original is not decidable without the buffers' sizes; the register's FBI table
  (`TOTALA-EXE-DATA.md`) lists the string offsets and several lengths, and nothing there
  suggests shipped data overflows one.
- **Absent default: MATCHES.** Absent keys take the default in both.

## 7. Case, whitespace and encoding

- **Case: MATCHES for ASCII.** Keys and block names compare case-insensitively
  (`CaseInsensitiveEquals`, via `std::toupper` over unsigned characters, ASCII under the C
  locale). The chapter's mismatch (case-insensitive ordering with exact-match reuse) is
  rule 5.
- **Whitespace and trimming: DIFFERS in the set, invisibly.** The original trims space, tab,
  carriage return and newline. RWE's `isSpaceCodePoint` (about 148) uses the C library's
  space test, which adds vertical tab and form feed. Census: no shipped `.fbi`, `.tdf` or
  `.ota` contains either. (`endgame.gui` does, and also a NUL and an end-of-transmission
  byte, but it is a binary resource, not text; RWE does not parse it as TDF.)
- **Carriage returns: DIFFERS, invisibly.** RWE reads a carriage-return and line-feed pair as
  one newline, and a lone carriage return as a newline; the original keeps both bytes in a
  value that spans lines. This is the only difference in all the keys and values of the
  1941 entries other than those in rules 5 and 6 (`corals.tdf`, `barriers.tdf` and
  `armscorp.fbi`, each a multi-line value or key).
- **Text encoding: DIVERGENCE (representation).** RWE decides per file whether the bytes are
  valid UTF-8 and otherwise reads them as Latin-1 into UTF-8 (`tdf.cpp`, about 14-21;
  `rwe_string.cpp`, about 287). 615 of the 2311 `.fbi`, `.tdf`, `.ota` and `.gui` entries in the
  install are not valid UTF-8 (the French, German and Spanish descriptions), all of them
  read as Latin-1 by this rule. The original treats every file as bytes. Names that are
  plain ASCII compare the same. The conversion is lossless, so a non-ASCII byte is still the
  same character when drawn; it would matter only if a high byte were compared against a
  literal.
- **Embedded NUL: DIFFERS, invisibly.** The original's C string routines stop at a NUL, so
  one ends the file. RWE treats it as a character and the end-of-transmission byte (4) as the
  end of the text (`TdfEndOfFile`). No shipped text file has either.

## 8. Record navigation and the record checksum

**Reference (sections 6, 7, 8).** The record checksum is computed over the body of a record
from just after its opening brace to **two bytes before the closing brace**, after comments
were blanked. Selecting a missing child clears the current record and the next selection
starts at the root again. Iterating siblings needs a reset between indexed selects. Getting a
record name copies at most the given count without forcing termination.

**Register.** `TOTALA-EXE-DATA.md` section 117: the OTA half of the map checksum is the
`[GlobalHeader]` block's body hashed from the byte after `{` to **two bytes before the matching
`}`**, "the two trailing bytes are always dropped, whatever they are".

- **Checksum range: MATCHES.** The chapter's "length end minus text minus one" is the register's
  two dropped bytes; `globalHeaderChecksum` (`TaChecksums.cpp`, about 65-122) implements the
  register's rule, including the nested body.
- **Comments inside a hashed block: UNCLEAR in principle, empty on shipped data.** The chapter
  computes the checksum after comments were blanked; the register and `TaChecksums.cpp` hash
  the **raw** bytes, and RWE finds the closing brace by counting raw braces, so a brace inside
  a comment would end the block early in RWE and not in the original. The two agree wherever
  the block holds no comment. **Census: of 275 distinct shipped `.ota` paths (all archives, the
  Byte Tactics `.ccx` included), every one has a `[GlobalHeader]` and none has a comment
  anywhere inside it**, and the raw and comment-blanked body are byte-identical in every
  one. So no shipped map is exposed; a mod map with a comment in its header would hash
  differently from a TA host's if the original hashes the blanked text.
- **Navigation: not applicable.** RWE has no current-record cursor; loaders hold block
  references. The chapter's fault modes (a null current, a failed select resetting the
  cursor) do not exist.

## 9. Translations and localised fields

**Reference (sections 9, 10).** Loading a language compares the requested language against
the remembered one case-insensitively and **returns at once if equal, before looking at the
file**. Otherwise it frees the table, **records the new language before the file is read**
(so a failed load leaves an empty table and a remembered language, and a retry is skipped),
and builds a sorted table from the file's top-level records: each record name is a key, and
its field named by the language is the value; empty values are left out; duplicates replace.
Lookup is exact and case-sensitive and returns the **input pointer when nothing matches**
(the English text is the key, so English needs no table). A reverse lookup scans values
case-insensitively. The localised-field getter forms **language name plus field key with no
separator** (so `French` plus `Description`) and reads that first, falling back to the plain
key only when the localised one is **missing**; a present empty localised value suppresses
the fallback.

**RWE: MISSING.** RWE does not read `gamedata/translate.tdf` (the only mention is a comment in
`MainMenuScene.cpp` about 1300) and has no language setting. It reads each unit's plain `Name`
and `Description` and ignores the `FrenchName`, `GermanDescription` and similar keys present in
the FBIs. Every user-visible string is the English text, which is the key in the
table, so **on an English install the missing feature is invisible**; the register's BT-30
(language-suffixed data directories) is the sibling row.

- **If a language selector is ever added**, the chapter's rules are the specification: a
  table with one column per language (English is the key; the shipped table and the FBIs
  carry German, French, Italian, Spanish and Pig Latin, as `german...`, `french...`,
  `italian...`, `spanish...` and `piglatin...` keys), the early return that makes a second load of the same language free, and
  the fact that a bad file wedges the language until the next change.
- **The two duplicate-key findings** the census turned up in `Translate.tdf` (rule 5) mean the
  table the original builds from the shipped file has *the last block's* values for `kills`,
  `kill` and `losses`, because the loader walks every block and replaces; a port that stored
  blocks by name and kept the last would agree on those, and one that kept the first would not.

## 10. Loading and failure

**Reference (sections 3, 4).** `LoadFile` returns failure for an unopenable file or a
non-positive length. It frees the old tree **before** the new text is parsed; a parse error
is fatal, so there is no partial tree. A short read is parsed anyway with whatever was in the
buffer. `LoadBuffer` frees the old tree first and takes the caller's provenance flag.

**RWE.** `parseTdfFromString` builds a fresh `TdfBlock` and returns it; a thrown exception
leaves the caller's old data untouched.

- **Failure atomicity: DIFFERS, better.** A failed parse cannot damage a previously loaded
  tree in RWE. Not a gap.
- **An empty file: DIFFERS, invisibly.** The original reports failure for a file of length
  zero; RWE returns an empty tree. Every loader treats "nothing in it" and "absent" the same
  way in effect.
- **Archive provenance flag: not applicable.** The original records whether the file came out
  of an archive. Nothing in RWE's tree stores it.
- **Memory:** the chapter's allocation-failure faults (unchecked allocations) do not exist.

---

## 11. Gaps to fix

Ranked by how likely each is to change a loaded value on **retail or widely used data**
first, then by mod data. The census says no retail file separates the two parsers by value
except in the single unresolved case of rank 1.

| Rank | Gap | Kind | Who settles it |
| --- | --- | --- | --- |
| 1 | **Which spelling wins when a key is written twice in different case**: 12 distinct shipped aircraft FBIs write `MaxWaterDepth=0;` then `maxwaterdepth=255;`. RWE reads 255. The original's choice (255, or 0 if equal spellings are placed after) is not decided by the chapter. For an aircraft the value is dead, so the shipped result is unaffected. | UNCLEAR, no shipped effect | spec-room read of the field insert routine (`0x4c51e0` region) |
| 2 | **A comment inside a key, a value or a block name** is blanked by the original and removed by RWE; `12 /* x */ 34` reads 12 versus 1234. Nothing shipped does it. | DIFFERS, mod data only | code change (replace a comment with a space inside a token) plus a parser test; the lower-risk of the two directions |
| 3 | **A value with no numeric prefix** reads as zero in the original and as the field's default in RWE (and throws for a required key). One shipped value, and it agrees (default zero). Mods with `key=;` on a non-zero-default field differ. | DIFFERS, mod data only | decision: match the original's zero for the numeric getters, or keep the safer default and say so in `compatibility.md` |
| 4 | **Stray semicolons**: RWE skips them; the chapter's reading of the original would stop on them; the one shipped file with one (`unitview.tdf`) is presumably never loaded by the parser. The code comment's claim about `Translate.tdf` is wrong. | UNCLEAR, comment fix | spec-room check that the parser reaches `unitview.tdf`; fix the comment in `TdfParser.h` regardless |
| 5 | **Duplicate block names within one file**: first wins by name in the original, last wins in RWE; walk-all consumers see a union in the original. No retail effect (translation and empty unit lists only). | DIFFERS, mod data only | add a note to `TOTALA-EXE-DATA.md`; change only if a mod demands it |
| 6 | **The map checksum over a block containing a comment**: the original may hash the blanked text; RWE hashes raw bytes and counts raw braces. No shipped map has a comment in its header (275 checked). | UNCLEAR, no shipped effect | spec-room read of the hash call's argument, only if mod maps matter |
| 7 | **Top-level fields, a stray top-level brace, an unclosed block comment, an empty key**: loaded or ignored by the original, refused by RWE. | DIFFERS, mod data only | none needed; RWE's refusal is the safer behaviour |
| 8 | **Translations are not read.** Invisible on an English install. | MISSING | only when a language selector is wanted; BT-30 is the sibling |
| 9 | **String capacity truncation**: not modelled; no retail overflow evidence. | UNCLEAR | per-caller buffer sizes from the register's FBI table |
| 10 | Whitespace set (vertical tab, form feed), embedded NUL, carriage-return pairs in values. | DIFFERS, no data | none |

No source file was changed.
