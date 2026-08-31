/// plang_printer.cpp — Turbo Tier 4, Cluster C item 7: the real binding
/// behind the shipped `Printer` unit's Lst (share/plang/units/Printer.pas).
///
/// See that file's own header comment for the full account of why Lst's own
/// storage and its auto-open both have to live here rather than in a real
/// Pascal `var Lst: Text` initializer -- summary: this project's shipped/
/// fallback unit loading never compiles or links a used unit's own
/// implementation, so nothing Pascal-level this file could write would ever
/// run.  Lst is instead a real PascalFile (this project's own file-variable
/// layout, plang/Basic/PascalFileLayout.h, shared with codegen so every
/// Text/file(of X) variable this project has already agrees on it) defined
/// directly here under the mangled name a reference to Lst resolves to, and
/// opened by a C++ global constructor that runs before main() -- calling
/// this project's OWN plang_tp_assign/plang_tp_rewrite, the exact two entry
/// points a real `Assign(Lst, ...); Rewrite(Lst);` already goes through
/// (runtime/plang_file.cpp), so Lst ends up in precisely the state either
/// call would have left it in, not some hand-rolled approximation.
///
/// Field practice this matches: real Borland Turbo Pascal's Printer unit
/// binds Lst to the parallel port (LPT1), which has no POSIX equivalent; real
/// `fpc` does not try to fake one either -- its own Printer unit auto-
/// assigns Lst to a plain temp file, `/tmp/<PID>.lst` (confirmed against the
/// published FPC RTL docs for AssignLst/Lst), and leaves turning that file
/// into an actual print job to whatever the caller does with it afterward
/// (FPC's own AssignLst also accepts an explicit `|lpr ...` pipe form, but
/// that is opt-in, never Lst's own default). This is also what keeps this
/// CI-safe: nothing here ever spawns lpr or talks to CUPS on its own, so a
/// CI runner with no print spooler installed is never involved.
///
/// PLANG_LST_PATH, if set in the environment, overrides the default
/// `/tmp/plang_lst_<pid>.txt` path -- not real Borland/FPC behavior (their
/// own path is unconditional), but a small, clearly-documented addition:
/// with no override, the exact output path is only known at runtime (it is
/// keyed on the process's own PID), which a lit RUN line has no way to
/// predict ahead of time to FileCheck against. The override changes nothing
/// about HOW Lst is bound (still plang_tp_assign + plang_tp_rewrite, the
/// same two real entry points), only WHERE.

#include "plang/Basic/PascalFileLayout.h"

#include <cstdio>
#include <cstdlib>

#include <unistd.h> // getpid()

// See plang_strings.cpp's own identical macro for the full rationale: an
// asm-label needs an explicit leading underscore on Mach-O (macOS), which
// the compiler would otherwise add automatically for a non-asm-labelled
// symbol but does NOT add on top of an explicit asm(...) label.
#if defined(__APPLE__)
#define PLANG_ASM_NAME(name) asm("_" name)
#else
#define PLANG_ASM_NAME(name) asm(name)
#endif

namespace plang {

extern "C" {

// ---- Tier 3's own Assign/Rewrite, reused rather than duplicated
// (runtime/plang_file.cpp) -- the exact two entry points a real
// `Assign(Lst, path); Rewrite(Lst);` already calls. --------------------------
void plang_tp_assign(PascalFile *F, const char *Name);
void plang_tp_rewrite(PascalFile *F);

} // extern "C"

// Lst's own storage, under the exact mangled name CGLinkage::mangledGlobal
// resolves a reference to Printer's Lst to: PlangGlobalPrefix ("pasg_") +
// moduleScope("printer") ("printer$") + the reference's own spelling of the
// name ("Lst") -- confirmed empirically the same way Strings.pas' own
// header comment describes for its own mangled procedure names, not
// assumed. Zero-initialized: PascalFile's own default member initializers
// (PascalFileLayout.h) already leave it in the same all-zero, unopened
// state any other file variable starts in, which plang_tp_assign/
// plang_tp_rewrite below then open for real before anything else can touch
// it.
PascalFile PlangPrinterLst PLANG_ASM_NAME("pasg_printer$Lst");

namespace {

/// Runs before main(): opens PlangPrinterLst for writing so that a program
/// which does nothing but `uses Printer; ... Writeln(Lst, ...)` finds it
/// already live, exactly as real Borland/FPC's own Lst needs no Assign/
/// Rewrite of its own before first use.
///
/// Only ever linked in when something in THIS translation unit is actually
/// referenced -- i.e., only when a program's own `uses Printer` reaches
/// Lst at all (a static archive, libplang.a, pulls in a member .o only when
/// one of its symbols is needed elsewhere in the link; nothing else in this
/// file is referenced from any other translation unit) -- so a plang build
/// that never touches Printer never creates this file at all.
void initPrinterLst() {
    // A plain stack buffer + snprintf, not std::string: this project's
    // runtime is linked into every compiled program without libstdc++
    // (plang_*.cpp elsewhere in this directory is C++ syntactically, but
    // sticks to C-library facilities for exactly this reason), so pulling
    // in std::string here would leave every program that merely `uses
    // Printer` with undefined references to operator new/delete and
    // libstdc++'s exception-support symbols.
    char Path[PlangFileNameCap];
    if (const char *Override = std::getenv("PLANG_LST_PATH"); Override && *Override) {
        std::snprintf(Path, sizeof(Path), "%s", Override);
    } else {
        std::snprintf(Path, sizeof(Path), "/tmp/plang_lst_%ld.txt",
                      static_cast<long>(::getpid()));
    }
    plang_tp_assign(&PlangPrinterLst, Path);
    plang_tp_rewrite(&PlangPrinterLst);
}

struct PrinterLstInit {
    PrinterLstInit() { initPrinterLst(); }
} PlangPrinterLstInit;

} // namespace

} // namespace plang
