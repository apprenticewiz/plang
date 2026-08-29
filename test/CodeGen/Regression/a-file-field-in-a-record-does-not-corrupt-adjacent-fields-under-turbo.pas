(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(* The Turbo sibling of a-file-field-in-a-record-does-not-corrupt-adjacent-
   fields-iso7185.pas, right next to this test -- see its own comment for
   what this proves.  This is the dialect that actually exercises the new
   Name/Mode/RecSize fields' own storage (not just their inert presence),
   through Assign/Rewrite/Close/Reset, immediately before reading guard2
   back.  guard1/guard2 are kept within Turbo's own 16-bit Integer range
   (Width stamped 16 under -std=turbo, unlike ISO/EP's 64) -- a first draft
   of this test used the ISO/EP sibling's 111111/222222 unchanged, which
   Sema truncates to fit at compile time and so still round-tripped
   correctly, but proved nothing about corruption; small values make a
   genuine corruption show up as a wrong value instead of a
   quietly-truncated-either-way one. *)

(*
CHECK-DAG: 11111
CHECK-DAG: 22222
*)

type
  Wrapper = record
    guard1: integer;
    f: text;
    guard2: integer;
  end;
var w: Wrapper;
begin
  w.guard1 := 11111;
  w.guard2 := 22222;
  assign(w.f, 'a-file-field-in-a-record-turbo.txt');
  rewrite(w.f);
  writeln(w.f, 'hello');
  close(w.f);
  reset(w.f);
  writeln(w.guard1);
  writeln(w.guard2)
end.
