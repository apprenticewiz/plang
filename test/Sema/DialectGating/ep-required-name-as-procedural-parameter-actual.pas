(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

program p(output);
type charset = set of char;
procedure caller(function f(s: charset): integer);
begin writeln(f(['a'])) end;
begin caller(card) end.

(*
CHECK: 'card' is an Extended Pascal extension and is only available under -std=iso10206
*)
