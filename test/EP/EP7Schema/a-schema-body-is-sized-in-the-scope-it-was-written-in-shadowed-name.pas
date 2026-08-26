(*
EP section 6.4.7.  A schema body's bound expressions are written where the
schema is DECLARED, and the only names in scope there are its own
discriminants and compile-time constants.  new() re-emits those
expressions where the ALLOCATION happens, and that put the allocating
procedure's own variables in front of the names the body meant: a
const k used in a bound was captured by any unrelated var k at the
call site, which sized the object from a run-time variable.

This is the regression shape: alloc's own local k (unrelated to the
type) shadows the const k the schema body actually means.  Paired with
...-distinct-name.pas, which differs only in that local's spelling and
must produce the identical output -- the object's layout must not
depend on the name of an unrelated local.

RUN: %plang_ep %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:10 20 30 40 50 60 70 
*)

program p(output);
const k = 3;
type t(n: integer) = array[1..n+k] of integer;
var q: ^t; i: integer;
procedure alloc;
var k: integer;
begin k := 1; new(q, 4) end;
begin alloc;
  for i := 1 to 7 do q^[i] := i * 10;
  for i := 1 to 7 do write(q^[i]:1, ' ');
  writeln end.
