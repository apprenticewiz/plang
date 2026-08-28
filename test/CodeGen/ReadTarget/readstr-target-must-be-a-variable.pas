(*
Issue #412: readstr's read-target arguments (v1..vn in
readstr(e, v1,...,vn)) were never checked for assignability, unlike
read/readln's targets (#224).  A non-variable target fell through to the
generic "evaluate for side effects only" fallback and reached codegen with
no address to assign into, aborting the compiler instead of being refused
at the call site.

RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: readstr target must be a variable
*)

program t;
var s: string(20);
begin s := '42'; readstr(s, 5) end.
