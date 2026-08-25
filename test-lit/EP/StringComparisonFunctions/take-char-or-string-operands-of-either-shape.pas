(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:true true true true
CHECK-NEXT:true true true
*)

program p(output);
var s: string(10); c: char;
begin s := 'abc'; c := 'a';
  writeln(eq(s, 'abc'), ' ', lt('ab','ac'), ' ', gt('b','a'), ' ', ne(s,'x'));
  writeln(eq(c, 'a'), ' ', le('a','a'), ' ', ge('b','a')) end.
