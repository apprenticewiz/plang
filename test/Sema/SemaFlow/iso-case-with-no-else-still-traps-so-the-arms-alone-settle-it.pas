(*
Regression gate for turbo-case-with-no-else-can-leave-a-value-unassigned.pas:
under ISO 7185 and ISO/EP 10206, a case-statement with no else/otherwise
part is still an error (§6.8.3.5) when the selector matches no arm, so
"every surviving path went through some arm" is still true and this must
stay exactly as quiet as it always has -- checkDefiniteAssignment's CaseStmt
arm only folds in the unmodified pre-case state under Opts.turbo().
*)

(*
RUN: %plang -std=iso7185 -dump-ast %s 2> %t.iso7185.err
RUN: FileCheck --allow-empty --check-prefix=ERR-ABSENT %s < %t.iso7185.err
RUN: %plang -std=iso10206 -dump-ast %s 2> %t.iso10206.err
RUN: FileCheck --allow-empty --check-prefix=ERR-ABSENT %s < %t.iso10206.err
*)

(*
ERR-ABSENT-NOT: is read here before it has been given a value
*)

program p(output);
var i, x: integer;
begin
  i := 1;
  case i of
    1: x := 1
  end;
  writeln(x)
end.
