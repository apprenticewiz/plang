// Turbo Tier 4, Cluster C item 7: the `Strings` unit -- C-style
// null-terminated PChar utilities.
//
// HOW THIS FILE IS ACTUALLY USED (read before touching the IMPLEMENTATION
// section below): this unit is resolved the same way InstallProbeUnit.pas
// proved the shipped-RTL search path works (see that file's own header
// comment, and CMakeLists.txt's `install(DIRECTORY share/plang/units/...)`
// rule) -- a program's `uses Strings` finds THIS SOURCE FILE via
// Sema::loadUnitInterfaceExports' own .pas-source fallback (no .tui is
// published for it, and none is built by this project's CMake either) and
// reads its INTERFACE section ONLY, for type-checking.  Sema deliberately
// never compiles a unit's IMPLEMENTATION when it was reached that way
// (checkUnitInterfaceOnly's own comment), and Codegen never emits an
// automatic call into one either (Codegen::Impl::emitUnitInitFn's own
// comment) -- so a call to, say, StrLen from a program that merely `uses
// Strings` becomes an ordinary cross-object EXTERN reference to the mangled
// symbol `pas_strings$StrLen` (CGLinkage's own scheme: PlangProcPrefix +
// moduleScope("strings") + the call's own spelling of the name -- see
// CGLinkage.h's top comment), which nothing this file's own IMPLEMENTATION
// section could ever produce even if it were compiled.
//
// So the REAL bodies live in runtime/plang_strings.cpp, built into
// libplang.a (which every plang-built program already links unconditionally,
// runtime/CMakeLists.txt), each one exported under the exact mangled name
// above via an `asm("...")` link-name override -- verified empirically
// end-to-end (probe unit + hand-written extern "C" definitions, real link,
// real run) before this file was written, not assumed. This mirrors real
// Borland/FPC field practice much more closely than it might look: real
// Turbo/FPC never ship a compilable Strings.pas at all -- Strings.tpu/.ppu
// is precompiled, and the source (when published) is a reference, not
// something a user's own build ever compiles again. Do NOT `plang -c` this
// file and link its .o into a normal build: it would define
// `pas_strings$...` symbols a SECOND time, colliding with the ones
// libplang.a already provides.
//
// Every exported name below keeps its real Borland/FPC arity and return
// type, with one deliberate exception: StrPCopy and StrPLCopy take their
// Pascal `string` argument as `var` rather than Borland's `const`. This is
// forced, not stylistic -- a `string` (ShortString, 256 bytes) passed BY
// VALUE across this exact extern-declaration boundary is lowered (confirmed
// by reading the emitted LLVM IR, `pas_probestrings$StrPCopy(ptr, <{ i8,
// [255 x i8] }>)`, and its further legalization -- 256 individual scalar
// register/stack slots, empirically dumped from the generated assembly) into
// a shape no ordinary hand-written C++ function signature can reproduce; a
// `var` parameter is passed as a plain pointer instead (confirmed the same
// way: `pas_probestrings2$StrPCopy(ptr, ptr)`), which both matches this
// project's own file-model runtime calling convention throughout
// (plang_sstr.cpp) and is trivial to bind correctly. The cost is that a
// caller must pass an actual string VARIABLE, not a string literal, to
// StrPCopy/StrPLCopy -- unlike real Borland/FPC, where `const` also accepts
// a literal.  Every other export here keeps its real by-value/PChar/ordinal
// signature untouched, because none of the rest ever puts a `string` in an
// argument position.
unit Strings;

interface

function StrLen(Str: PChar): Cardinal;
function StrCopy(Dest, Source: PChar): PChar;
function StrLCopy(Dest, Source: PChar; MaxLen: Cardinal): PChar;
function StrCat(Dest, Source: PChar): PChar;
function StrLCat(Dest, Source: PChar; MaxLen: Cardinal): PChar;
function StrComp(Str1, Str2: PChar): Integer;
function StrLComp(Str1, Str2: PChar; MaxLen: Cardinal): Integer;
function StrIComp(Str1, Str2: PChar): Integer;
function StrPos(Str1, Str2: PChar): PChar;
function StrScan(Str: PChar; Chr: Char): PChar;
function StrRScan(Str: PChar; Chr: Char): PChar;
function StrUpper(Str: PChar): PChar;
function StrLower(Str: PChar): PChar;
function StrNew(Str: PChar): PChar;
procedure StrDispose(Str: PChar);
function StrPCopy(Dest: PChar; var Source: string): PChar;
function StrPLCopy(Dest: PChar; var Source: string; MaxLen: Cardinal): PChar;

implementation

// Deliberately empty -- see this file's own header comment for exactly why
// nothing here is ever compiled or linked in the shipped-unit path, and
// where the real bodies actually live.

end.
