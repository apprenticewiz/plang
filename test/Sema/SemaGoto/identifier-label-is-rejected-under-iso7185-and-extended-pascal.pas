(*
Regression gate, the exact contrast the Turbo identifier-label feature
above depends on: Sema's Phase 1 label loop (Sema.cpp) only skips
err_label_must_be_integer when Opts.turbo() -- ISO 7185 §6.1.6 requires a
label to be a digit-sequence, and Extended Pascal never widened that, so the
identical source has to stay a hard error under both of the other two
dialects, not silently start compiling as a label the way it would if the
Turbo gate were ever dropped or inverted.  (The parser itself accepts an
identifier token in a label section unconditionally, in every dialect --
canonicalLabel, ParserInternal.h -- so this is purely a Sema-level check.)
*)

(*
RUN: not %plang -std=iso7185 -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err

RUN: not %plang -std=iso10206 -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: label 'done' must be an unsigned integer
*)

program p;
label Done;
var x: integer;
begin
Done: x := 0
end.
