(*
Issue #606.  EP §6.4.3.3 makes `string` itself a schema, so `type
s(n: integer) = string(n); type t = s(8) value 'hi'; var a: t` denotes a
string just as much as `var a: string(8) value 'hi'` does -- but t's own
denoter is a schema instantiation, and declaredStrCapacity() (CodeGenProcs.cpp)
asked only whether the resolved type's Kind was directly VarString, which a
SchemaInstance whose body resolves to VarString is not.  storeInitialValue
then fell to the scalar/pointer store path and wrote the string literal's
OWN ADDRESS into a's length field instead of initializing it: length(a)
read back whatever bit pattern the pointer happened to be, and printing a
walked off the end of its 8-byte buffer into adjacent memory.
*)

(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[hi] 2
*)

program p;
type s(n: integer) = string(n);
     t = s(8) value 'hi';
var a: t;
begin
  writeln('[', a, '] ', length(a))
end.
