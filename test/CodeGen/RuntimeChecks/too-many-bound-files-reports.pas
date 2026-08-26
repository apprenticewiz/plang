(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: too many bound files
*)

program p(output);
var
  f1, f2, f3, f4, f5, f6, f7, f8, f9, f10,
  f11, f12, f13, f14, f15, f16, f17, f18, f19, f20,
  f21, f22, f23, f24, f25, f26, f27, f28, f29, f30,
  f31, f32, f33, f34, f35, f36, f37, f38, f39, f40,
  f41, f42, f43, f44, f45, f46, f47, f48, f49, f50,
  f51, f52, f53, f54, f55, f56, f57, f58, f59, f60,
  f61, f62, f63, f64, f65: bindable text;
  b: BindingType;
begin
  b.name := 'f';
  bind(f1, b);
  bind(f2, b);
  bind(f3, b);
  bind(f4, b);
  bind(f5, b);
  bind(f6, b);
  bind(f7, b);
  bind(f8, b);
  bind(f9, b);
  bind(f10, b);
  bind(f11, b);
  bind(f12, b);
  bind(f13, b);
  bind(f14, b);
  bind(f15, b);
  bind(f16, b);
  bind(f17, b);
  bind(f18, b);
  bind(f19, b);
  bind(f20, b);
  bind(f21, b);
  bind(f22, b);
  bind(f23, b);
  bind(f24, b);
  bind(f25, b);
  bind(f26, b);
  bind(f27, b);
  bind(f28, b);
  bind(f29, b);
  bind(f30, b);
  bind(f31, b);
  bind(f32, b);
  bind(f33, b);
  bind(f34, b);
  bind(f35, b);
  bind(f36, b);
  bind(f37, b);
  bind(f38, b);
  bind(f39, b);
  bind(f40, b);
  bind(f41, b);
  bind(f42, b);
  bind(f43, b);
  bind(f44, b);
  bind(f45, b);
  bind(f46, b);
  bind(f47, b);
  bind(f48, b);
  bind(f49, b);
  bind(f50, b);
  bind(f51, b);
  bind(f52, b);
  bind(f53, b);
  bind(f54, b);
  bind(f55, b);
  bind(f56, b);
  bind(f57, b);
  bind(f58, b);
  bind(f59, b);
  bind(f60, b);
  bind(f61, b);
  bind(f62, b);
  bind(f63, b);
  bind(f64, b);
  bind(f65, b);
  writeln('should not get here')
end.
