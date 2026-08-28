(*
addPredefinedConditionalSymbols (lib/Frontend/Frontend.cpp) seeds
LangOptions::Defines from the compilation's target before any -d/-u or
in-source DEFINE is applied.  UNIX and CPU64 are checked here rather than
LINUX/DARWIN or CPUX86_64/CPUAARCH64 specifically because they are the two
predefined symbols true on every platform/architecture combination this
project actually supports (see README.md: Linux and macOS, both Unix-like,
both only ever built 64-bit) -- so this test passes on any of plang's own
supported CI hosts without needing a lit REQUIRES directive naming a
specific one. FPC is
checked NOT to be predefined: plang's Turbo dialect must never claim to be
FPC (see addPredefinedConditionalSymbols's own comment in Frontend.cpp) --
an IFDEF FPC guard is how real-world source selects branches using
FPC-only features this milestone does not implement, and predefining that
name would let such a branch compile as though it did.

-uUNIX (see the sibling command-line-d-and-u-flags-behave-like... test for
-d/-u's basic round trip) proves a predefined symbol is an ordinary member
of the same set a user's own -d/-u or DEFINE/UNDEF can still override --
not a separate, unreachable kind of "defined".
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s

RUN: %plang -std=turbo -uUNIX %s -o %t.no_unix
RUN: %run %t.no_unix | FileCheck --check-prefix=OVERRIDDEN --strict-whitespace --match-full-lines %s
*)

program p;
begin
  {$IFDEF UNIX}
  writeln('unix-yes');
  {$ELSE}
  writeln('unix-no');
  {$ENDIF}
  {$IFDEF CPU64}
  writeln('cpu64-yes');
  {$ELSE}
  writeln('cpu64-no');
  {$ENDIF}
  {$IFDEF FPC}
  writeln('fpc-yes');
  {$ELSE}
  writeln('fpc-no');
  {$ENDIF}
end.

(*
CHECK:unix-yes
CHECK-NEXT:cpu64-yes
CHECK-NEXT:fpc-no
OVERRIDDEN:unix-no
OVERRIDDEN-NEXT:cpu64-yes
OVERRIDDEN-NEXT:fpc-no
*)
