(*
EP §6.11.3: a name imported `qualified` is written M.name wherever the
unqualified name would be written, and that includes a type-denoter -- the
grammar gives qualified-import-name no narrower a role than any other type
identifier.  A var declared with one used to be unparsable: the type parser
read the module name, found a '.' where it expected ';' or nothing at all,
and reported a syntax error instead of resolving the type.

RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42
*)

//--- test.pas
module m1;
  type t = 0..100;
end.
program p;
  import m1 qualified;
  var x: m1.t;
begin
  x := 42;
  writeln(x)
end.
