(*
FileMode -- the second predefined mutable Var this project registers under
-std=turbo, following ExitCode's own mechanism exactly (Sema::registerBuiltins'
FileMode Symbol comment).  Real Borland/FPC defaults it to 2 (read-write),
confirmed against `fpc -Mtp` (a fresh program's own FileMode reads 2 before
anything touches it); this checks both that default and that it is a real,
working, assignable variable and not merely a read-only constant.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:2
CHECK-NEXT:42
*)

begin
  writeln(FileMode);
  FileMode := 42;
  writeln(FileMode);
end.
