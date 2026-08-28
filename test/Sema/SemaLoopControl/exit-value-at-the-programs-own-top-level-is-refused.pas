(*
Exit(value)'s mirror of exit-value-inside-a-procedure-is-refused.pas for
the OTHER context CurrentRetType is null in: a program's own top level is
not a function either, and `fpc -Mtp` refuses the identical program the
same way (confirmed empirically) -- a bare `Exit;` there is fine (it ends
the program, like falling off the end of the body), but a value has nowhere
to go.
*)

(*
RUN: not %plang -std=turbo -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 'exit' may only take a value inside a function; a procedure cannot return one
*)

program p;
begin
  Exit(5)
end.
