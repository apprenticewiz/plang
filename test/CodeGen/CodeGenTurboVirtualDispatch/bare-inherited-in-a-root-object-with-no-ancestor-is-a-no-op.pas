(*
Issue #624: a bare 'inherited;' (no method name) used as a full STATEMENT
inside a ROOT object's own method -- one with no ancestor at all -- is a
legal no-op, confirmed against a local `fpc -Mtp` build: this is the common
defensive "always call inherited at the top of every constructor/
destructor" idiom, written even on the root of a hierarchy where it has
nothing to reach.  Previously this was refused outright
(err_inherited_no_ancestor), even though the identical idiom is completely
routine one level further down a real hierarchy.  Both TRoot's constructor
and destructor open with a bare 'inherited;' and still run their own body
afterward, proving CodeGen emits nothing for the no-op call rather than
tripping the "unresolved 'inherited'" ICE it still guards against for every
other case.
*)

(*
RUN: %plang -std=turbo -O0 %s -o %t.O0
RUN: %run %t.O0 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -std=turbo -O2 %s -o %t.O2
RUN: %run %t.O2 | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
type
  TRoot = object
    constructor Init;
    destructor Done;
  end;

constructor TRoot.Init;
begin
  inherited;
  writeln('Init ran');
end;

destructor TRoot.Done;
begin
  inherited;
  writeln('Done ran');
end;

var
  R: TRoot;
begin
  R.Init;
  R.Done;
end.

(*
CHECK:Init ran
CHECK-NEXT:Done ran
*)
