(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: not compatible with tag type 'boolean'
*)

(*
ISO Sec6.4.3.3: each case-constant of a variant is required to be a value of
the tag field's own type, exactly as a case-statement's labels are held to
the type of its selector (checkCase, SemaStmt.cpp).  walkVariantFields folded
every label only to find duplicates AMONG each other -- it never checked what
type a label actually was, so a boolean tag accepted 5 and 7 as case-
constants even though neither is one of boolean's two values.
*)

program p(output);
type
  r = record
        case b: boolean of
          5: (y: integer);
          7: (z: integer)
      end;
var v: r;
begin writeln('unreachable') end.
