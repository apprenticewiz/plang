(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(* ISO §6.9.3.1: the width after the first colon is an integer-expression, so
   a char here is refused instead of quietly widening to its ordinal value
   (issue #256: write(x:'a') used to write field 97 wide, no diagnostic). *)
(*
ERR: write field-width must be an integer expression, got 'char'
*)

program p(output);
begin writeln(5: 'a') end.
