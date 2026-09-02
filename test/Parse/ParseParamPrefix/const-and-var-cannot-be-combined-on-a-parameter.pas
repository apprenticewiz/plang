(*
Issue #712.  parseParamGroup (Parse/ParseDecl.cpp) treated `const` and `var`
as independent flags: `const` set G.IsConst and simply fell through to the
ordinary `var` match right below it, so `const var x: Integer` set BOTH
flags.  Real Turbo Pascal makes them mutually exclusive -- a local
`fpc -Mtp` build rejects the identical program outright ("identifier
expected but VAR found").

The combination wasn't just a cosmetic over-acceptance: `x := 1` was still
refused (err_const_param_assigned), but G.IsVar being true meant CodeGen
passed x BY REFERENCE the same way an ordinary var parameter is (unlike a
plain `const x: Integer`, which is passed by value for a scalar type).  A
mutating builtin that only checks isLValue rather than checkNotProtected
(the class of gap #710/#711 fixed for other builtins) would have reached
straight through to the caller's own storage via this "by-reference const"
hole.

RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: 'const' and 'var' cannot be combined on the same parameter
*)

program p;
procedure Q(const var x: Integer);
begin
end;
begin
end.
