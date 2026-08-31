// Turbo Tier 4, Cluster C item 7: the `Printer` unit.
//
// Real Borland Turbo Pascal's Printer unit exports one predefined `Text`
// file variable, Lst, pre-bound to the parallel-port LPT1 device -- there is
// no POSIX equivalent of that (no direct parallel-port access, no universal
// spooler API), so real `fpc` on Linux/macOS does not try to reproduce it:
// its own Printer unit (source/packages/printer, confirmed against the
// published FPC RTL docs rather than assumed) auto-assigns Lst to
// `/tmp/<PID>.lst` -- an ordinary temp file, NOT a live pipe into `lpr`/CUPS
// -- and leaves actually printing it to whatever the user does with that
// file afterward (FPC's own AssignLst also documents a `|lpr ...` pipe form
// for a caller who wants one, but that is opt-in, not what Lst defaults to).
// This unit matches that exact field practice: Lst is auto-bound to a plain
// temp file, not a live process pipe, which is also what keeps this unit's
// own lit test CI-safe -- nothing here ever talks to lpr/CUPS/a real spooler
// unless a caller explicitly pipes the resulting file there themselves,
// exactly as real `fpc`'s own Lst leaves it.
//
// HOW Lst ACTUALLY GETS BOUND: see share/plang/units/Strings.pas' own header
// comment for the full account of why this file's IMPLEMENTATION section
// (below) is never compiled or linked for a program that merely `uses
// Printer` via the shipped search path -- the identical reasoning applies
// to a real Turbo/FPC unit's own `begin ... end` initialization section,
// which is exactly the mechanism real Lst auto-binding depends on and which
// this project's own shipped/fallback unit loading does not run (Codegen::
// Impl::emitUnitInitFn's own comment). So the auto-bind cannot be Pascal
// code here either: runtime/plang_printer.cpp defines Lst's own storage
// directly (as a real PascalFile, sharing plang/Basic/PascalFileLayout.h
// with every other Text file variable this project has), under the exact
// mangled name a reference to Lst resolves to (`pasg_printer$Lst` --
// CGLinkage's PlangGlobalPrefix + moduleScope("printer") + "Lst"), and a
// C++ global constructor calls this project's OWN plang_tp_assign/
// plang_tp_rewrite (the same two entry points TP's real `Assign`/`Rewrite`
// statements already call) once, before main(), to open it -- so Lst is a
// real, live, already-open Text file by the time any user code runs,
// exactly matching real Borland/FPC's own promise that Lst needs no Assign/
// Rewrite of its own before first use. PLANG_LST_PATH, if set, overrides the
// default `/tmp/plang_lst_<pid>.txt` path -- this is what makes the shipped
// unit's own lit test deterministic without ever faking Lst's real binding
// mechanism.
//
// A caller who wants Lst to go somewhere else entirely (a real `lpr` pipe,
// a fixed path, ...) can still `Assign(Lst, ...); Rewrite(Lst);` again
// themselves at any time -- rebinding a file variable that is already open
// is ordinary, already-supported Tier 3 Assign/Rewrite behavior, not
// anything this unit needs to add.
unit Printer;

interface

var
  Lst: Text;

implementation

// Deliberately empty -- see this file's own header comment for exactly why,
// and where Lst's own real binding actually happens.

end.
