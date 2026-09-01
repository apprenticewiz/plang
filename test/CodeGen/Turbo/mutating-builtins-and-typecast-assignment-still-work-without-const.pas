(*
Issues #710 and #711's fix routes FillChar/Move/Include/Exclude/Inc/Dec/
Delete/Insert/SetLength/Str's write-target argument, and a variable
typecast's operand, through the SAME checkNotProtected/protectedBaseOf
mechanism that already refuses an ordinary `x := v` or `x.f := v` on a
`const` parameter.  That mechanism asks a real question (is this
particular symbol marked const/protected/a conformant bound?), not "is
this argument shaped like one of these builtins' targets?", so it must
answer "no" just as reliably as it answers "yes" -- a `var` parameter, an
ordinary (copied) value parameter, and a plain global variable are none of
those three things, and every one of these calls has to keep compiling AND
keep actually mutating the storage it always did.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
type
  TPoint  = record x, y: Integer end;
  TPoint2 = record a, b: Integer end;

{ var parameter: FillChar and a variable typecast both reach the CALLER's
  own storage, exactly as they did before #710/#711 existed. }
procedure ZeroThenStampViaVar(var r: TPoint);
begin
  FillChar(r, SizeOf(r), 0);
  TPoint2(r).a := 42;
end;

{ ordinary value parameter: same two operations, but only on the callee's
  own copy -- the caller must see no change at all. }
procedure ZeroThenStampViaValue(r: TPoint);
begin
  FillChar(r, SizeOf(r), 0);
  TPoint2(r).a := 42;
end;

var
  v: TPoint;
  s: set of Char;
  n: Integer;
  buf: string;
begin
  v.x := 7; v.y := 9;
  ZeroThenStampViaVar(v);
  writeln(v.x, ' ', v.y);

  v.x := 7; v.y := 9;
  ZeroThenStampViaValue(v);
  writeln(v.x, ' ', v.y);

  s := ['a'];
  Include(s, 'z');
  Exclude(s, 'a');
  if ('z' in s) and not ('a' in s) then writeln('set ok');

  n := 5;
  Inc(n);
  Inc(n, 3);
  Dec(n);
  writeln('n=', n);

  buf := 'hello world';
  Delete(buf, 1, 6);
  Insert('XX-', buf, 1);
  SetLength(buf, 6);
  writeln(buf);
  Str(n:0, buf);
  writeln(buf);

  { plain global variable, not a parameter at all }
  v.x := 1; v.y := 2;
  TPoint2(v).a := 99;
  writeln(v.x, ' ', v.y);
end.

(*
CHECK:42 0
CHECK-NEXT:7 9
CHECK-NEXT:set ok
CHECK-NEXT:n=8
CHECK-NEXT:XX-wor
CHECK-NEXT:8
CHECK-NEXT:99 2
*)
