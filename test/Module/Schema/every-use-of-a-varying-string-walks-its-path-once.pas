(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t < %t.dir/stdin.txt | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:compare 1
CHECK-NEXT:hi
CHECK-NEXT:write   1
CHECK-NEXT:length  1
CHECK-NEXT:rhs     1
CHECK-NEXT:substr  1
CHECK-NEXT:read    1
CHECK-NEXT:concat  1 [xyhello]
CHECK-NEXT:subval  1 [he]
CHECK-NEXT:substrfn1 [he]
CHECK-NEXT:trimfn  1 [hello]
CHECK-NEXT:[hello] b=true k=2 z=[hello]
*)

//--- test.pas
program p(input, output);
type t(n: integer) = record a: array[1..n] of record s: string(n) end end;
var q: ^t; calls: integer; b: boolean; k: integer; z: string(8);
function next: integer;
begin calls := calls + 1; next := 1 end;
begin
  new(q, 8);
  q^.a[1].s := 'hi';
  calls := 0; b := q^.a[next].s = 'hi';   writeln('compare ', calls:1);
  calls := 0; writeln(q^.a[next].s);      writeln('write   ', calls:1);
  calls := 0; k := length(q^.a[next].s);  writeln('length  ', calls:1);
  calls := 0; z := q^.a[next].s;          writeln('rhs     ', calls:1);
  calls := 0; q^.a[next].s[1..2] := 'ab'; writeln('substr  ', calls:1);
  calls := 0; read(q^.a[next].s);         writeln('read    ', calls:1);
  { B20: concatenation's right operand, a substring rvalue, and substr/trim
    each used to walk a varying string's access path a second time -- see
    strAddrAndCap's own callers in CGBinaryOps.cpp/CGExprCore.cpp/
    SchemaAccess.cpp for what each of these four routes through now. }
  calls := 0; z := 'xy' + q^.a[next].s;         writeln('concat  ', calls:1, ' [', z, ']');
  calls := 0; z := q^.a[next].s[1..2];          writeln('subval  ', calls:1, ' [', z, ']');
  calls := 0; z := substr(q^.a[next].s, 1, 2);  writeln('substrfn', calls:1, ' [', z, ']');
  calls := 0; z := trim(q^.a[next].s);          writeln('trimfn  ', calls:1, ' [', z, ']');
  writeln('[', q^.a[1].s, '] b=', b, ' k=', k:1, ' z=[', z, ']');
  dispose(q)
end.

//--- stdin.txt
hello
