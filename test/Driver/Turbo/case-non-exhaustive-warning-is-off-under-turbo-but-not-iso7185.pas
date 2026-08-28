(*
warn_case_not_exhaustive fires under ISO 7185/Extended Pascal because a case
with no else/otherwise part that does not obviously cover its selector's
whole type traps at run time if it is ever reached with an uncovered value --
see case-non-exhaustive-warning-is-off-under-turbo-but-not-iso7185's ISO
sibling check below.  Under Turbo the same shape does not trap (it falls
through -- see case-with-no-matching-arm-and-no-else-falls-through.pas), so
the warning's own premise is false there and it must not fire, with no -W
flag able to bring it back.  Same source, both dialects: only the -std flag
changes what's expected.
*)

(*
RUN: %plang -std=turbo %s -o %t.turbo 2>&1 | FileCheck --check-prefix=TURBO --allow-empty %s
RUN: %plang -std=iso7185 %s -o %t.iso 2>&1 | FileCheck --check-prefix=ISO %s
*)

(*
TURBO-NOT: does not cover
ISO: warning: case does not cover Green or Blue of type 'Color', and has no 'otherwise' part
*)

program p(output);
type
  Color = (Red, Green, Blue);
var
  c: Color;
begin
  c := Red;
  case c of
    Red: writeln('red')
  end;
  writeln('after')
end.
