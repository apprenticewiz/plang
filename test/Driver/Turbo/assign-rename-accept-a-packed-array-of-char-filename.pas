(*
Issue #671: Assign(f, name)/Rename(f, name) accept a §6.4.3.2
char-string-type (`packed array[1..n] of char`) name argument just as
readily as a `string` one -- Sema's own isCharStringType (SemaStmt.cpp) has
always allowed it for this position, and real Turbo Pascal/fpc both accept
and run it -- but StringCallMarshalling::emitCStrArg (CodeGen) had no case
for this argument shape and fell through to a generic EmitExpr(e), which
for a plain array-typed variable loads the WHOLE FIXED-SIZE CHAR ARRAY AS
AN LLVM AGGREGATE VALUE rather than taking its address: an LLVM IR
verifier failure ("[n x i8] ... passed to a ptr parameter") on every such
Assign/Rename call, not merely a wrong filename.  Exercises both Assign and
Rename with a packed-array-of-char name/newname, confirming the compiler no
longer crashes and that the rename actually took effect on disk (the same
three checks
rename-renames-the-bound-file-on-disk-and-updates-fs-own-bound-name.pas's
own string-argument version uses: old gone, new present with the right
size, and a reopen through the SAME file variable reads back what was
written before the rename).

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
RUN: not test -f t671old.bin
RUN: wc -c < t671new.bin | tr -d ' ' | FileCheck --check-prefix=SIZE %s
*)

(*
CHECK:assign IOResult=0
CHECK-NEXT:rename IOResult=0
CHECK-NEXT:value after reopening the renamed file=42
SIZE:1
*)

var
  f: file of Byte;
  v: Byte;
  oldName: packed array[1..11] of char;
  newName: packed array[1..11] of char;
begin
  oldName := 't671old.bin';
  newName := 't671new.bin';

  assign(f, oldName);
  writeln('assign IOResult=', IOResult);
  rewrite(f);
  write(f, Byte(42));
  close(f);

  rename(f, newName);
  writeln('rename IOResult=', IOResult);

  reset(f);
  read(f, v);
  writeln('value after reopening the renamed file=', v);
  close(f);
end.
