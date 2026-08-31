(*
Turbo Tier 4, Cluster C item 6: Dos.GetEnv is a thin wrapper over getenv(3)
(runtime/plang_dos.cpp's own plang_dos_getenv, reached through CodeGen's own
Dos-intrinsic recognizer rather than a plain linker-symbol alias -- this
item's own report explains why GetEnv is one of the six exports that
needs that treatment: it has a `string` VALUE parameter and result).
Uses a REAL environment variable, set on the RUN line itself, and a second
lookup of a name deliberately not set, checking real `fpc -Mtp` field
practice (GetEnv returns '' for an unset variable, not an error).

RUN: %plang -std=turbo -I%plang_unit_dir %s -o %t
RUN: env PLANG_DOS_TEST_VAR=real-value %run %t | FileCheck %s
*)

program DosGetEnv;
uses Dos;
begin
  Writeln('[', GetEnv('PLANG_DOS_TEST_VAR'), ']');
  Writeln('[', GetEnv('PLANG_DOS_TEST_VAR_DEFINITELY_UNSET'), ']');
end.

(*
CHECK:[real-value]
CHECK-NEXT:[]
*)
