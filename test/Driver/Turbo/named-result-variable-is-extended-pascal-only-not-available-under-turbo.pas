(*
EP §6.6's named-result form ('function F = r: integer') is Extended
Pascal's alone -- Turbo spells no such thing, and had no dialect gate at
all until now (test/Sema/DialectGating/named-result-variable-case1.pas
and -case2.pas cover the ISO 7185/Extended Pascal half of the same fix).
*)

(*
RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

program namedresultturbo;
function F = r: integer;
begin
  r := 5;
end;
begin
  writeln(F);
end.

(*
CHECK: a named result variable ('function name(...) = result: type') is an Extended Pascal extension and is not available under -std=iso7185
*)
