(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: two different types that share a name
*)

program p(output);
type vec(n: integer) = array[1..n] of integer;
var g: vec(3); i: integer;
procedure q(var x: vec(3));
type vec(n: integer) = array[1..n*10] of integer;
var l: vec(3); j: integer;
begin for j := 1 to 30 do l[j] := 7000+j; x := l end;
begin for i := 1 to 3 do g[i] := i; q(g); writeln(g[1]:1) end.
