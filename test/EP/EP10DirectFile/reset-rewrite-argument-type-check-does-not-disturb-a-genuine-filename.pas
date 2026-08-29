(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:one
CHECK-NEXT:two
CHECK-NEXT:three
*)

(* Non-regression for the reset/rewrite second-argument type check (see
   reset-and-rewrite-refuse-a-non-string-second-argument.pas): the new
   Sema-level guard must reject a non-string second argument without
   disturbing any of the three call shapes that were already valid --
   reset(f, 'literal') with a string literal, reset(f, v) with an EP
   string(n) variable, and reset(f) with no name at all (which reuses
   whatever name the most recent explicit rewrite/reset gave it). *)

program p;
var f: text;
    name: string(30);
    line: string(30);
begin
  (* 2-arg, string literal *)
  rewrite(f, 'plang_resettypecheck_1.txt');
  writeln(f, 'one');
  close(f);
  reset(f, 'plang_resettypecheck_1.txt');
  readln(f, line); writeln(line);
  close(f);

  (* 2-arg, EP string(n) variable *)
  name := 'plang_resettypecheck_2.txt';
  rewrite(f, name);
  writeln(f, 'two');
  close(f);
  reset(f, name);
  readln(f, line); writeln(line);
  close(f);

  (* 1-arg, no filename -- reuses the name the rewrite just above gave it *)
  rewrite(f, 'plang_resettypecheck_3.txt');
  writeln(f, 'three');
  close(f);
  reset(f);
  readln(f, line); writeln(line);
  close(f)
end.
