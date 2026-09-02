(*
Issue #603: {$J-} (Switch::WritableConst off, CompilerSwitches.def) is
supposed to make a Turbo typed constant truly read-only, matching FPC
3.2.2 -Mtp's "Can't assign values to const variable".  Symbol::IsTypedConst
was set but never consulted by any assignment check, so the switch was
parsed and recorded and then simply had no effect -- every typed constant
stayed assignable no matter what.  checkNotProtected (SemaStmt.cpp) now
asks Opts.switchOn(Switch::WritableConst, Loc) whenever the write's base
symbol is a typed constant, so this direct assignment under {$J-} is
correctly refused.  See j-plus-keeps-a-typed-constant-writable.pas for the
default-on sibling.

RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: can't assign values to const variable 'x'
*)

program p;
{$J-}
const x: integer = 1;
begin
  x := 2
end.
