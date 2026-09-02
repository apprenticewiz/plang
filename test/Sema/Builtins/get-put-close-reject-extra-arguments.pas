(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(* Issue #605: get, put, and close each take exactly one file argument
   (ISO 7185 6.6.5.2/6.6.5.3); Builtins.def used to give them MaxArgs -1
   so the generic arity check imposed no upper bound at all, and codegen
   silently used only the first argument.  Now the catalogue says 1,1 and
   the same generic checkBuiltinArity used everywhere else catches it. *)
program p(input, output);
begin
  get(input, input);
  put(output, output);
  close(output, output)
end.

(*
CHECK: 'get' expects 1 argument(s), got 2
CHECK: 'put' expects 1 argument(s), got 2
CHECK: 'close' expects 1 argument(s), got 2
*)
