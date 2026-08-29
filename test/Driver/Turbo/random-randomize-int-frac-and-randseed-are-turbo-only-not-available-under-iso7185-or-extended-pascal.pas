(*
Regression gate: Random, Randomize, Int, Frac (Builtins.def, Dialects=TP)
and RandSeed (a predefined Var registered exactly the way ExitCode is --
see that Symbol's own comment in Sema::registerBuiltins) are all registered
only under Opts.turbo(), so under -std=iso7185 or -std=iso10206 none of
them means anything: the four Builtins.def names are required identifiers
refused BY NAME (err_turbo_required_name's wording, "is a Turbo Pascal
extension and is only available under -std=turbo" -- the same message
RunError/Assert already get), while RandSeed, never declared at all outside
Turbo, is an ordinary undefined identifier, the same as ExitCode's own
identical gate test.

Random's BARE (no-parens) spelling gets its own line here specifically: it
is Sema's first dialect-restricted, zero-argument Func builtin, and
checkIdent's generic SymbolKind::Builtin case originally had no dialect
check of its own at all -- only checkCallExpr's checkEPOnly, reached only by
the parenthesized CallExpr form, did.  Before checkIdent grew its own
checkEPOnly call, `b := Random > 0.5;` under -std=iso7185 silently compiled
and RAN Random's own generator instead of being refused the way
`r := Random(5);`, immediately below it, already correctly was.  This test
is as much a regression gate for that fix as for the dialect gate itself.
*)

(*
RUN: not %plang -std=iso7185 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err

RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK-DAG: 'random' is a Turbo Pascal extension and is only available under -std=turbo
CHECK-DAG: 'randomize' is a Turbo Pascal extension and is only available under -std=turbo
CHECK-DAG: 'int' is a Turbo Pascal extension and is only available under -std=turbo
CHECK-DAG: 'frac' is a Turbo Pascal extension and is only available under -std=turbo
CHECK-DAG: undefined identifier 'RandSeed'
*)

program p;
var
  r: Real;
  b: Boolean;
begin
  b := Random > 0.5;
  r := Random(5);
  Randomize;
  r := Int(1.5);
  r := Frac(1.5);
  RandSeed := 1
end.
