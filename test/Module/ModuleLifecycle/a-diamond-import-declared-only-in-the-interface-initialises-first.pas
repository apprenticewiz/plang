(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 -c %t.dir/d.pas -o %t.dir/d.o
RUN: %plang -std=iso10206 -I%t.dir -c %t.dir/a.pas -o %t.dir/a.o
RUN: %plang -std=iso10206 -I%t.dir -c %t.dir/b.pas -o %t.dir/b.o
RUN: %plang -std=iso10206 -I%t.dir %t.dir/prog.pas %t.dir/a.o %t.dir/b.o %t.dir/d.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(* Issue #126: D is diamond-imported by both A and B, and A/B each declare
   'import D' only on their interface half, never repeating it on the
   implementation half.  When A, B and D are each their own separate
   translation unit (compiled with -c and linked together, not all visible
   in one compilation), D's initialiser has to still be found and called
   before A's and B's own -- the mangled name of an imported module's own
   init function is resolved the same way whether the import was written on
   the interface or the implementation. *)

(*
CHECK:init D
CHECK-NEXT:init A
CHECK-NEXT:init B
CHECK-NEXT:called d
CHECK-NEXT:called d
CHECK-NEXT:body
CHECK-NEXT:fini B
CHECK-NEXT:fini A
CHECK-NEXT:fini D
*)

//--- d.pas
module D interface;
  export d;
  procedure d;
end.
module D implementation;
  procedure d; begin writeln('called d') end;
  to begin do writeln('init D');
  to end do writeln('fini D');
end.

//--- a.pas
module A interface;
  import D;
  export a;
  procedure a;
end.
module A implementation;
  procedure a; begin d end;
  to begin do writeln('init A');
  to end do writeln('fini A');
end.

//--- b.pas
module B interface;
  import D;
  export b;
  procedure b;
end.
module B implementation;
  procedure b; begin d end;
  to begin do writeln('init B');
  to end do writeln('fini B');
end.

//--- prog.pas
program p(output);
  import A;
  import B;
begin
  a;
  b;
  writeln('body')
end.
