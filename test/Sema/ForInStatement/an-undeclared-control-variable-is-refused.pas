(*
Issue #689: ISO 10206 §6.9.3.9.1's control variable in `for v in set do`
is a variable-access, exactly like the to/downto form's (ISO §6.8.3.9) --
it names a DECLARED variable, and is not implicitly declared by the loop
itself.  checkForIn used to push a fresh scope and silently `define` q
there whenever it was not already visible, so an entirely undeclared q
compiled without complaint.

RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: undefined variable 'q' in for statement
*)

program p(output);
var s: set of 1..5;
begin s := [1, 2]; for q in s do writeln(q) end.
