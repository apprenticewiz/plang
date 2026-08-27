(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(* ISO §6.9.3.1: a real width is refused instead of being truncated to an
   integer with no diagnostic (issue #256: write(x:2.5) used to silently
   truncate to a field 2 wide). *)
(*
ERR: write field-width must be an integer expression, got 'real'
*)

program p(output);
begin writeln(5: 2.5) end.
