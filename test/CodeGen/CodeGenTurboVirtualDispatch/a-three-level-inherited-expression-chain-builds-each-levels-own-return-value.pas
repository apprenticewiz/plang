(*
Issue #509: 'inherited' used as an expression generalizes beyond a single
2-level override -- TLeaf.Describe calls 'inherited Describe()' to reach
TMid's own body, which ITSELF calls 'inherited Describe()' to reach TBase's
own body, each level concatenating the ancestor's own value onto its own
suffix as part of building its OWN return value (never as a bare
statement).  A bug that resolved the wrong ancestor at either level, or that
redispatched through the VMT instead of calling statically, would show up
here as a wrong string -- e.g. 'base+mid+mid' (TLeaf's own 'inherited'
reaching itself instead of TMid) or an infinite loop, rather than the exact
'base+mid+leaf' below.

Cross-checked against a local `fpc -Mtp` build of this exact program: same
output, 'base+mid+leaf'.
*)

(*
RUN: %plang -std=turbo -O0 %s -o %t.O0
RUN: %run %t.O0 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -std=turbo -O2 %s -o %t.O2
RUN: %run %t.O2 | FileCheck --strict-whitespace --match-full-lines %s
*)

program threelevelinheritedexpr;
type
  TBase = object
    function Describe: string;
  end;
  TMid = object(TBase)
    function Describe: string;
  end;
  TLeaf = object(TMid)
    function Describe: string;
  end;

function TBase.Describe: string;
begin
  Describe := 'base';
end;

function TMid.Describe: string;
begin
  Describe := inherited Describe() + '+mid';
end;

function TLeaf.Describe: string;
begin
  Describe := inherited Describe() + '+leaf';
end;

var
  L: TLeaf;
begin
  WriteLn(L.Describe());
end.

(*
CHECK:base+mid+leaf
*)
