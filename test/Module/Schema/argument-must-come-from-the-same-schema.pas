(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: must be a 'vec'
*)

program p;
type vec(n: integer) = array[1..n] of integer;
     other(n: integer) = array[1..n] of integer;
var a: other(3);
procedure f(var v: vec);
begin v[1] := 1 end;
begin f(a) end.
