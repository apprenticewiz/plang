(*
Issue #649: a bare reference (no parentheses) to a FUNCTION-typed
procedural variable, in an ordinary VALUE context, was rejected --
'Writeln(fn)' errored "'function(): integer' cannot be written" even
though plang already auto-calls a bare PLAIN function name the same way
('Writeln(G)' worked) -- an inconsistency between the two spellings of
the identical idea.  fpc -Mtp auto-calls fn and prints the result;
'fn()' (with parens) already worked in both.

Sema::checkIdent's SymbolKind::Var/VarParam arm now auto-calls a bare,
FUNCTION-typed procedural variable exactly like the SymbolKind::Proc arm
already does for a plain function name (ISO Sec6.7.3's function-designator),
UNLESS this exact occurrence is one of the few spellings that want the
variable's own STORED value instead: the assignment idiom
'f2 := f1' (copying one procedural value into another, not calling f1 and
assigning its result to f2), '@f1' (address of the variable itself, not
of its call result's type), Assigned(f1) (checking whether f1 currently
holds a routine at all), and -- the one that is NOT an ordinary "read"
at all -- 'f1 := G' itself, where f1 is the ASSIGNMENT TARGET: computing
its own type must not auto-call it either, or the target's type comes back
as its RETURN type instead of its own procedural type, and CodeGen would
store a call's result over the variable's own pointer-shaped storage.

G's own counter increments on every REAL call, so a wrong count below
would mean the auto-call fired an extra (or zero) times, not just that the
disambiguation compiled.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:viaG=1
CHECK-NEXT:copied=2
CHECK-NEXT:assigned=true
CHECK-NEXT:notassigned=false
*)

program BareProcVarAutoCallsInValueContext;

type
  TFn = function: Integer;

var
  Counter: Integer;
  fn, fn2: TFn;
  addr: ^TFn;

function G: Integer;
begin
  Counter := Counter + 1;
  G := Counter;
end;

begin
  Counter := 0;

  { 'fn := G' is the ASSIGNMENT-TARGET case: computing fn's own type must
    not auto-call fn itself, and must still read G as a routine reference
    (issue #649's own pre-existing sibling rule) rather than a call. }
  fn := G;

  { Bare read in an ordinary value context -- the auto-call this issue is
    about. Prints 1 (G's first real call), not G's own procedural type. }
  Writeln('viaG=', fn);

  { 'fn2 := fn' copies fn's own procedural VALUE (must NOT auto-call fn) ;
    a bare read of fn2 afterward auto-calls THROUGH it, giving 2 (the
    SECOND real call to G, proving fn2 truly holds a live reference to G
    rather than some frozen or default value). }
  fn2 := fn;
  Writeln('copied=', fn2);

  { '@fn' takes the address of the variable itself, not of its call
    result's type -- addr^ read back and called explicitly still reaches
    the SAME live G, not a corrupted or unrelated value. }
  addr := @fn;
  if Assigned(addr^) then
    Writeln('assigned=true')
  else
    Writeln('assigned=false');

  fn := nil;
  if Assigned(fn) then
    Writeln('notassigned=true')
  else
    Writeln('notassigned=false');
end.
