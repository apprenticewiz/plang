// Turbo Tier 4, Cluster C item 6: the real Dos unit -- POSIX field practice
// (Linux/macOS), not DOS/Windows semantics.  Signatures and constants below
// were confirmed against real Borland Turbo Pascal 7 documentation and, for
// the POSIX-specific reinterpretations (Drive, SearchRec's own private
// fields, SetDate/SetTime's no-permission behaviour), against a local real
// `fpc` install's own Unix Dos unit (/usr/lib/fpc/src/rtl/unix/dos.pp +
// rtl/inc/dosh.inc) -- the closest real, field-tested POSIX Pascal Dos unit
// there is, since Borland's own Dos unit only ever targeted real DOS.
//
// IMPORTANT -- how this file is actually used: unlike a unit a real program
// writes and compiles itself, this file is NEVER passed to `plang -c`, and
// its own `implementation` bodies below are NEVER what runs.  A program
// that 'uses Dos' reaches this file purely through Sema::
// loadUnitInterfaceExports' own `.pas`-fallback path (Turbo Tier 4, Cluster
// A item 2's own report) -- read for its INTERFACE, to type-check calls
// against, and never compiled.  The bodies below exist only because the
// parser requires every interface heading to have a matching
// implementation definition (Cluster A item 0's own grammar); each one is
// a placeholder that is syntactically real Pascal and never executed.
//
// The REAL implementation is runtime/plang_dos.cpp, linked into every
// program via libplang.a exactly like every other runtime entry point.
// Two different techniques bind an export here to that real code, chosen
// per export by whether its own parameters/result can cross the
// C++/Pascal boundary as plain scalars (an integer, a Boolean, a Pointer)
// or need one of Turbo's own aggregate string/record shapes:
//
//   * SCALAR-ONLY exports (GetDate/GetTime/SetDate/SetTime, DiskFree/
//     DiskSize, GetDir, FindNext/FindClose, DosExitCode, and DosError's own
//     storage) bind directly: CodeGen's ordinary "not defined in this
//     compile, so declare it extern and call it by its mangled name" path
//     (CGProcCall::emitUserProcCall / CGFuncCall's own twin) produces a
//     call to "pas_dos$<ExactName>" -- and runtime/plang_dos.cpp defines a
//     same-shaped C++ function under that EXACT linker symbol, via the
//     `asm("...")` label extension every C++ compiler this project builds
//     with (GCC, Clang) supports.  Confirmed empirically (this item's own
//     report): a scalar/pointer parameter list lowers identically whether
//     the two sides of the call were compiled by plang or by a real C++
//     compiler, because ISO C's own x86-64 SysV calling convention is what
//     decides a scalar or pointer argument's register/stack placement, and
//     both compilers implement that convention identically.
//
//   * AGGREGATE-CROSSING exports (ChDir/MkDir/RmDir/GetEnv/Exec/FindFirst --
//     everywhere a real Turbo `string` VALUE parameter or result crosses
//     into C++) do NOT bind this way: confirmed empirically (this item's
//     own report) that plang's OWN calling convention for a `string`
//     passed or returned BY VALUE is not the standard x86-64 SysV
//     aggregate-classification convention at all (it is LLVM's generic,
//     target-agnostic "flatten to scalars" default, observed to scatter a
//     256-byte ShortString one BYTE at a time across argument registers
//     and the stack) -- self-consistent between two plang-compiled
//     functions, but not reproducible from hand-written C++ without
//     fragile raw assembly.  So these six go through a small, explicit
//     CodeGen recognizer instead (CGProcCall.cpp's/CGFuncCall.cpp's own
//     "own Dos-unit intrinsics" dispatch, gated on the call's resolved
//     import owner being exactly this unit -- see those files' own
//     comments), which marshals the Turbo string arguments through the
//     SAME { length, bytes }-address / NUL-terminated-C-string helpers
//     (StringCallMarshalling's emitCStrArg, plang_sstr_from_bytes) every
//     OTHER runtime string entry point in this compiler already uses, and
//     calls a plain, scalar/pointer-only runtime entry point of its own
//     (plang_dos_chdir, plang_dos_mkdir, ...) beneath it.
unit Dos;

interface

const
  { Bitmasks for file attribute -- real Borland-documented values (TP7
    Language Guide, "SearchRec"/FindFirst); confirmed identical in FPC's
    own dosh.inc (readonly/hidden/sysfile/volumeid/directory/archive/
    anyfile constants).  Unprefixed, matching real TP7/`fpc -Mtp` field
    practice -- NOT the `fa`-prefixed spellings (`faReadOnly` etc.), which
    are a Delphi/SysUtils (`TSearchRec`) convention this unit does not
    implement. }
  ReadOnly  = $01;
  Hidden    = $02;
  SysFile   = $04;
  VolumeID  = $08;
  Directory = $10;
  Archive   = $20;
  AnyFile   = $3F;

type
  { Real TP7/FPC field practice: the Dos unit's own DateTime record, used by
    PackTime/UnpackTime to translate SearchRec's own packed 32-bit Time
    field to/from ordinary Year/Month/Day/Hour/Min/Sec fields. }
  DateTime = record
    Year, Month, Day, Hour, Min, Sec: Word;
  end;

  { POSIX reinterpretation of real TP7's SearchRec (Fill: array[1..21] of
    Byte; Attr: Byte; Time: LongInt; Size: LongInt; Name: string[12]) and
    FPC unix/dos.pp's own cross-platform SearchRec -- see this unit's own
    header comment for the fpc source consulted.  The historical name is
    "SearchRec", not "TSearchRec" (that Delphi-style alias belongs to
    SysUtils, a wholly different unit this project does not implement);
    matching real Borland/FPC Dos-unit field practice is exactly why this
    file uses the un-prefixed name.
    The first three fields are this implementation's own PRIVATE
    replacement for real TP's opaque `Fill` bytes -- carrying the OS-level
    directory-iteration state between FindFirst/FindNext/FindClose
    (real FPC's own unix SearchRec does exactly this: DirPtr/SearchAttr/
    NamePos of its own, in place of DOS's meaningless Fill bytes) -- a
    program reading/writing them itself is relying on an implementation
    detail no more portable than reading DOS's own Fill bytes would have
    been.  Attr/Time/Size/Name are the real, documented, portable fields. }
  SearchRec = record
    DirHandle:   Pointer;  { private: opendir()'s DIR*, or Nil }
    SearchAttr:  Word;     { private: the Attr FindFirst was called with }
    PathPrefix:  string;   { private: the original FindFirst search path,
                             verbatim (directory component plus wildcard
                             pattern) -- kept per-instance, not in a shared
                             global, so nested/interleaved FindFirst/
                             FindNext loops over different directories do
                             not clobber one another's search state }
    Attr:        Byte;
    Time:        LongInt;  { packed DOS-style date/time -- see PackTime/UnpackTime }
    Size:        LongInt;
    Name:        string;
  end;

{ DosError: Integer -- real TP7/FPC field practice: FindFirst/FindNext (and,
  here, ChDir/MkDir/RmDir/GetDir) report failure through this global rather
  than a return value or an exception, exactly the file-model's own
  InOutRes/dollar-I-plus pattern but a SEPARATE register -- confirmed against real
  `fpc -Mtp`'s own Dos unit (rtl/inc/dosh.inc's own `DosError : integer`).
  0 means the last Dos-unit call that reports through it succeeded. }
var
  DosError: Integer;

{ Info/Date/Time -- GetDate/GetTime read the real wall clock (reusing this
  project's own existing plang_time.cpp foundation, not a separate clock
  access of their own -- see runtime/plang_dos.cpp's own comment).
  SetDate/SetTime attempt to set it and, confirmed against real `fpc -Mtp`'s
  own SetDate/SetTime (rtl/unix/dos.pp): silently do nothing further when
  the underlying settimeofday(2) fails for lack of privilege -- neither
  raises nor sets DosError; a real permission failure is simply not
  reported by these two, field-practice-confirmed rather than assumed. }
procedure GetDate(var Year, Month, Day, DayOfWeek: Word);
procedure GetTime(var Hour, Minute, Second, Sec100: Word);
procedure SetDate(Year, Month, Day: Word);
procedure SetTime(Hour, Minute, Second, Sec100: Word);
procedure PackTime(const T: DateTime; var P: LongInt);
procedure UnpackTime(P: LongInt; var T: DateTime);

{ Exec -- real TP7/FPC field practice: synchronous (the calling program
  blocks until the child exits), confirmed against real `fpc -Mtp`'s own
  Exec (rtl/unix/dos.pp): fork + exec + waitpid, not a detached spawn.  The
  child's exit code is read back afterward through DosExitCode, a separate
  FUNCTION call -- not a var parameter of Exec itself -- exactly matching
  real Borland/FPC's own two-call contract (dosh.inc's own `Function
  DosExitCode: word;`).  ComLine is split on whitespace into the child's
  own argv, with Path substituted for argv[0] -- real FPC's own
  StringToPPChar-based splitting, reproduced here; no quoting support, the
  same real-world limitation real Exec has always had. }
procedure Exec(const Path, ComLine: string);
function DosExitCode: Word;

{ Disk -- Drive is a real DOS drive-letter index (0=default/current,
  1=A:, 2=B:, ...) on real Borland/FPC, meaningless on POSIX.  Real FPC
  Unix field practice (confirmed against rtl/unix/dos.pp's own DiskFree/
  DiskSize): Drive=0 queries the CURRENT WORKING DIRECTORY's own
  filesystem via statvfs(2); every other Drive value is ALSO treated as
  "the current directory" here (this implementation keeps no per-Drive
  registry the way real FPC's own AddDisk/DriveStr array does -- a
  deliberate scope cut, documented in this item's own report: nothing
  in this project ever calls AddDisk, so a second registered "drive"
  is not reachable from Pascal source in the first place).  Returns -1
  on failure, matching real field practice. }
function DiskFree(Drive: Byte): Int64;
function DiskSize(Drive: Byte): Int64;

{ FindFirst/FindNext/FindClose -- directory iteration via opendir/readdir/
  closedir.  Report failure through DosError (0 on success), never a
  return value -- see DosError's own comment. }
procedure FindFirst(const Path: string; Attr: Word; var F: SearchRec);
procedure FindNext(var F: SearchRec);
procedure FindClose(var F: SearchRec);

{ Environment }
function GetEnv(const EnvVar: string): string;

{ GetDir/ChDir/MkDir/RmDir -- Drive is the same real-DOS-drive-letter
  index DiskFree/DiskSize take, and gets the identical POSIX
  reinterpretation: every value means "the current drive", so GetDir
  always reports the real current working directory (getcwd(3))
  regardless of what Drive was passed -- confirmed against real FPC
  field practice (its own System-unit GetDir does the same; this
  project has no other exposure of directory operations yet, so this
  item places all four here rather than leaving them unimplemented).
  ChDir/MkDir/RmDir report failure through DosError, same as FindFirst. }
procedure GetDir(Drive: Byte; var Dir: string);
procedure ChDir(const Dir: string);
procedure MkDir(const Dir: string);
procedure RmDir(const Dir: string);

implementation

{ Every body below is a placeholder never compiled/executed -- see this
  unit's own header comment. }

procedure GetDate(var Year, Month, Day, DayOfWeek: Word);
begin
end;

procedure GetTime(var Hour, Minute, Second, Sec100: Word);
begin
end;

procedure SetDate(Year, Month, Day: Word);
begin
end;

procedure SetTime(Hour, Minute, Second, Sec100: Word);
begin
end;

procedure PackTime(const T: DateTime; var P: LongInt);
begin
  P := 0;
end;

procedure UnpackTime(P: LongInt; var T: DateTime);
begin
  T.Year := 1980; T.Month := 1; T.Day := 1;
  T.Hour := 0; T.Min := 0; T.Sec := 0;
end;

procedure Exec(const Path, ComLine: string);
begin
end;

function DosExitCode: Word;
begin
  DosExitCode := 0;
end;

function DiskFree(Drive: Byte): Int64;
begin
  DiskFree := -1;
end;

function DiskSize(Drive: Byte): Int64;
begin
  DiskSize := -1;
end;

procedure FindFirst(const Path: string; Attr: Word; var F: SearchRec);
begin
end;

procedure FindNext(var F: SearchRec);
begin
end;

procedure FindClose(var F: SearchRec);
begin
end;

function GetEnv(const EnvVar: string): string;
begin
  GetEnv := '';
end;

procedure GetDir(Drive: Byte; var Dir: string);
begin
end;

procedure ChDir(const Dir: string);
begin
end;

procedure MkDir(const Dir: string);
begin
end;

procedure RmDir(const Dir: string);
begin
end;

end.
