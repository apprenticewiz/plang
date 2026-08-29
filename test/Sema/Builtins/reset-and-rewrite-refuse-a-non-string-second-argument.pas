(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(* issue: reset/rewrite's optional second argument names the external file
   (EP §6.7.5.2), and CodeGen's marshalling (StringCallMarshalling::
   emitCStrArg) only knows how to turn a char or string value into the
   `const char *` the runtime open call takes.  Nothing checked the
   argument's type before codegen saw it, so a non-string second argument
   -- an integer literal, a boolean variable, a whole array, a whole record
   -- reached emitCStrArg shaped nothing like a string and made LLVM's own
   IR verifier abort the entire compiler with an internal error ("Call
   parameter type does not match function signature!") instead of plang
   ever refusing the program with an ordinary diagnostic. *)

program p;
type rt = record x, y: integer end;
var f: text;
    b: boolean;
    r: real;
    a: array[1..5] of integer;
    rec: rt;
begin
  reset(f, 42);
  reset(f, b);
  rewrite(f, r);
  rewrite(f, a);
  reset(f, rec)
end.

(*
CHECK: 'reset' external file name must be char or string, got 'integer'
CHECK: 'reset' external file name must be char or string, got 'boolean'
CHECK: 'rewrite' external file name must be char or string, got 'real'
CHECK: 'rewrite' external file name must be char or string, got 'array[1..5] of integer'
CHECK: 'reset' external file name must be char or string, got 'rt'
*)
