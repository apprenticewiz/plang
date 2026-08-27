(*
RUN: %plang -dump-ast %s
*)

(*
ISO §6.10: a program-parameter's own 'var' declaration in the program
block IS its defining occurrence, not a shadow of some other binding of
the same name (issue #292) -- so this must be accepted with no
diagnostic at all.
*)

program t(f);
var f : integer;
begin
  f := 1
end.
