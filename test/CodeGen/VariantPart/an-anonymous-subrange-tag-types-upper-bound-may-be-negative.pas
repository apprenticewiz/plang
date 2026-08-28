(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:-10 100
CHECK-NEXT:-7 200
CHECK-NEXT:-5 300
*)

(*
Issue #419: a variant part's own anonymous-subrange tag type (`case lo..hi
of`, used when the selector is written as a bare subrange rather than a
named type or a named tag field) parsed its upper bound with a bare
parseFactor() call, which has no sign production of its own -- so
`case NegTen..-5 of` failed to parse the `-5` upper bound with "expected
expression, got '-'".  This is the identical root cause #257 fixed for
case-statement labels and variant-part case-CONSTANT labels, via
Parser::parseCaseConstant() (optional sign, then parseFactor) -- but
parseVariantPart's separate tag-TYPE disambiguation branch (ParseType.cpp,
reached only via the identifier-then-DotDot lookahead below) was not one of
the sites #257 routed through it. Fix: route this upper bound through
parseCaseConstant() too.
*)

program p(output);
const NegTen = -10;
type
  r = record
        case NegTen..-5 of
          -10: (a: integer);
           -7: (b: integer);
           -5: (c: integer)
      end;
var v: r;
begin
  v.a := 100; writeln(-10:1, ' ', v.a:1);
  v.b := 200; writeln(-7:1, ' ', v.b:1);
  v.c := 300; writeln(-5:1, ' ', v.c:1)
end.
