(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck %s
*)

(* The Extended Pascal sibling of a-file-field-in-a-record-does-not-corrupt-
   adjacent-fields-iso7185.pas, right next to this test -- see its own
   comment for what this actually proves.  EP shares the exact same
   PascalFile layout and reset/rewrite entry points ISO 7185 does (Turbo's
   Name/Mode/RecSize growth is behaviorally inert here, but the struct is
   still physically bigger), so this is the identical stress under the
   other non-Turbo dialect. *)

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
  rewrite(w.f, 'a-file-field-in-a-record-ep.txt');
  writeln(w.f, 'hello');
  reset(w.f, 'a-file-field-in-a-record-ep.txt');
  writeln(w.guard1);
  writeln(w.guard2)
end.
