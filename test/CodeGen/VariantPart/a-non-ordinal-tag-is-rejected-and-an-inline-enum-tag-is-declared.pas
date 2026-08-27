(*
RUN: split-file %s %t.dir
RUN: not %plang %t.dir/non-ordinal.pas -o %t.non-ordinal 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
RUN: %plang %t.dir/inline-enum.pas -o %t.inline-enum
RUN: %run %t.inline-enum | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
ERR: variant tag type must be an ordinal type, got 'real'
*)

(*
CHECK:10
*)

(*
ISO Sec6.4.3.3: tag-type is an ordinal-type, and EP Sec6.4.3.3 widens that to
an anonymous ordinal type spelled out in place -- but walkVariantFields only
ever resolved the tag's TYPE at all when a tag FIELD NAME was also written
('case b: boolean of'); an anonymous selector ('case real of', or an inline
enumeration 'case (aa, bb) of') skipped resolveType entirely.  That let a
non-ordinal tag such as 'real' through with no diagnostic, and separately
meant an inline enumeration's values were never declared as symbols at all
-- referring to 'aa' or 'bb' anywhere read as "undefined identifier", and
the duplicate-label check (which folds each label to compare it) could not
fold them either, so it too was silently defeated.
*)

//--- non-ordinal.pas
program p(output);
type r = record case real of 0: (x: integer); 1: (y: integer) end;
var v: r;
begin writeln('unreachable') end.

//--- inline-enum.pas
program p(output);
type r = record case (aa, bb) of aa: (x: integer); bb: (y: integer) end;
var v: r;
begin v.x := 10; writeln(v.x:1) end.
