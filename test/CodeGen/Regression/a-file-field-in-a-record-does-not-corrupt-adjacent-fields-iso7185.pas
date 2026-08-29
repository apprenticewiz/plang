(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck %s
*)

(* The bare-stack-local sibling of this test, right next to it
   (file-variable-does-not-corrupt-adjacent-stack.pas), covers a PascalFile
   grown to make room for Turbo's Name/Mode/RecSize fields (PascalFileLayout.h)
   not corrupting two plain adjacent locals.  This is the harder case that
   growth actually risks: a file variable as a RECORD FIELD, where the field
   AFTER it (guard2) only lands at the right offset if Sema::byteSizeOf's
   File case, codegen's fileStructType() and the runtime's own sizeof(PascalFile)
   all still agree on the file's real size after the growth -- three
   independent computations of one fact, per PascalFileLayout.h's own top
   comment. *)

(*
CHECK-DAG: 111111
CHECK-DAG: 222222
*)

program p;
type
  Wrapper = record
    guard1: integer;
    f: text;
    guard2: integer;
  end;
var w: Wrapper;
begin
  w.guard1 := 111111;
  w.guard2 := 222222;
  rewrite(w.f, 'a-file-field-in-a-record-iso7185.txt');
  writeln(w.f, 'hello');
  reset(w.f, 'a-file-field-in-a-record-iso7185.txt');
  writeln(w.guard1);
  writeln(w.guard2)
end.
