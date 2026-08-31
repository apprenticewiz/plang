(*
Turbo Tier 5, Cluster A item 0: parsing only.  'object()' with nothing
between the parens is not a legal ancestor clause -- an ancestor, when the
parens are written at all, must name a real type.  This just checks the
parser reports one clean diagnostic and does not, say, silently treat an
empty ancestor as "no ancestor" or crash.
*)

(*
RUN: not %plang_ir -std=turbo -dump-parse-tree %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program MissingAncestorName;

type
  TDog = object()
    Name: string;
  end;

begin
end.

(*
CHECK: expected identifier, got ')'
*)
