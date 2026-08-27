(*
ISO §6.9.1 / §6.9.2: readln(v) is defined in terms of read(v), which is
v := f^ -- a named constant is not a variable-access and may not receive
an assignment any more than it may stand on an assignment's left-hand
side.  A bare literal is caught by not being any kind of variable-access
at all; this checks the companion case of an identifier that IS one, but
whose symbol is a constant rather than a variable.
*)

(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: read/readln target must be a variable
*)

program t(input, output);
const c = 5;
begin readln(c) end.
