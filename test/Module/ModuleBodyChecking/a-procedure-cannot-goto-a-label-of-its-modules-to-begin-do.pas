(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
RUN: FileCheck --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR: goto '5' cannot reach a label of a module's block
*)
(*
ERR-ABSENT-NOT: LLVM ERROR
*)

(* Issue #211: a module's 'to begin do' returns once its own statement
   finishes, unlike a program's block or an enclosing procedure, which stay
   on the call stack for as long as anything nested in them could still run.
   A procedure the module declares (bump, here) stays callable for the rest
   of the program's life -- including from another compilation unit that
   only imports the module -- so a non-local goto reaching from one back into
   the module's own block could resume an activation that has already ended.
   Sema rejects it outright rather than let codegen plant a landing pad that
   would only sometimes be safe to use. *)
module M;
  label 5;
  var v: integer;
  procedure bump;
  begin
    v := v + 1;
    if v < 3 then goto 5
  end;
  to begin do begin
    v := 0;
    5: bump;
    if v < 3 then goto 5
  end;
end.
