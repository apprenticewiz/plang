(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: duplicate field name 'x'
*)

(*
ISO Sec6.4.3.3: a record's field identifiers are distinct, across the fixed
part and EVERY variant alike -- a variant selects which fields exist, not
which of two same-named fields is meant.  The check existed for the fixed
part and not for the variant part, seventy lines apart in one file.

This is the same-offset case, which an earlier offset-mismatch gate could
not see: it used to compile successfully, silently leaving the second
declaration of the same field name unreachable instead of being rejected.
*)

program p(output);
type r = record case b: boolean of true: (x: integer); false: (x: real) end;
var v: r;
begin v.b := true; v.x := 5; writeln(v.x:1) end.
