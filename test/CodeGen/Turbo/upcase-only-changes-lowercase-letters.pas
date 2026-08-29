(*
System-unit string routines item: UpCase(ch): Char -- real Turbo Pascal 7's
UpCase takes and returns a CHAR, not a string (Builtins.def's own comment on
why the later Delphi/SysUtils string-argument overload is out of scope for
this milestone).  Only 'a'..'z' change; every other character passes through
unchanged, digits included.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
begin
  writeln(UpCase('a'));
  writeln(UpCase('A'));
  writeln(UpCase('1'));
  writeln(UpCase('z'));
  writeln(UpCase('!'));
end.

(*
CHECK:A
CHECK-NEXT:A
CHECK-NEXT:1
CHECK-NEXT:Z
CHECK-NEXT:!
*)
