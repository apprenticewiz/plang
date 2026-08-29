(*
The single most important non-regression check for Gap 2's fix
(checkDeref's new Opts.turbo() check, file-buffer-variable-caret-is-isos-
file-model-turbo-rejects-it.pas): checkDeref's Pointer arm -- an ordinary
p^ for a `^SomeType` pointer -- is a completely separate `if` from the File
arm the new check lives in (PtrTy->Kind can only ever be Pointer or File,
never both), so an ordinary pointer dereference must compile and run
identically under all three implemented dialects, exactly as before the
File arm grew a dialect check of its own.
*)

(*
RUN: %plang_run %s | FileCheck %s
RUN: %plang_ep_run %s | FileCheck %s
RUN: %plang -std=turbo %s -o %t && %run %t | FileCheck %s
*)

(*
CHECK:42
*)

program p(output);
type PInt = ^integer;
var p1: PInt;
begin
  new(p1);
  p1^ := 42;
  writeln(p1^);
  dispose(p1)
end.
