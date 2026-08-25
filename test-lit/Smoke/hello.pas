(*
RUN: %plang -dump-ast %s | FileCheck --check-prefix=AST %s
RUN: %plang_run | FileCheck --check-prefix=OUT %s
*)

program Hello(output);
begin
  writeln('hello, lit')
end.

(*
AST: (program Hello
AST: (call writeln "hello, lit")
OUT: hello, lit
*)
