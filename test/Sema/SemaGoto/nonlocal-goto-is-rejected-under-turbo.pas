(*
Turbo Pascal (matching FPC) has no non-local goto at all: a goto may only
reach a label declared in the SAME routine's block, never one of an
enclosing procedure/function/program.  checkGoto (SemaStmt.cpp) reaches this
diagnostic only after the two structural checks ISO 7185/EP still apply
first (err_goto_module_block, err_goto_outer_block) have already passed --
this program trips neither of those (the label is at the outermost level of
a plain program block, not a module's to-begin-do and not itself nested in a
structured statement), so under ISO 7185/EP it is a perfectly legal
non-local goto (see iso7185-and-extended-pascal-still-allow-non-local-goto.pas,
test/Driver/Turbo, for the identical source compiling and actually jumping
under both).  Only -std=turbo's own extra check rejects it.
*)

(*
RUN: not %plang -std=turbo -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: goto '1' cannot leave this routine for a label of an enclosing one
*)

program p(output);
label 1;
procedure outer;
  procedure inner;
  begin writeln('inner'); goto 1 end;
begin inner end;
begin
  outer;
1:
  writeln('landed')
end.
