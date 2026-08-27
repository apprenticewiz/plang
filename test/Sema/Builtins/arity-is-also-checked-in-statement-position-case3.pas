(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(* issue #207: pack's own arm was gated on `S.Args.size() == 3`, so a wrong
   count skipped it entirely and fell to the same unchecked generic path as
   reset -- reaching emitUserProcCall instead of a diagnostic. checkBuiltinArity
   now catches this before pack's arm is even reached. *)

program p;
var a: array[1..5] of char; z: packed array[1..5] of char;
begin pack(a, z) end.

(*
CHECK: 'pack' expects 3 argument(s), got 2
*)
