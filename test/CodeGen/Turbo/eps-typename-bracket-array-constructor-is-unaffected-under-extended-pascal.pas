(*
EP §6.8.7's structured value constructor is written 'TypeName[...]', with
SQUARE brackets -- a completely separate production from the new Turbo
'TypeName(...)' typecast, which uses parens and does not exist under
-std=iso10206 at all (parseFactor's and parseStatement's cast arms are
gated on Opts.turbo()).  This is the regression check that adding the new
paren form left the existing bracket form alone: same TypeNames_/VarNames_
disambiguation machinery in the parser backs both, and this exercises the
bracket path specifically, under the dialect that owns it.
*)

(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:10 20 30
*)

program p;
type
  Vec = array[1..3] of Integer;
var
  v: Vec;
begin
  v := Vec[1: 10; 2: 20; 3: 30];
  writeln(v[1], ' ', v[2], ' ', v[3]);
end.
