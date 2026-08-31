(*
Turbo Tier 4, Cluster C item 6: Dos.GetDate/Dos.GetTime read the real wall
clock (runtime/plang_dos.cpp's own plang_dos_getdate/plang_dos_gettime,
reusing plang_time.cpp's std::time/std::localtime foundation, per this
item's own report).  Nothing about "the current time" is a fixed value a
CHECK line can pin down, so this test instead checks PLAUSIBILITY: Year is
in a sane range, Month/Day/DayOfWeek/Hour/Minute/Second are all within
their own real bounds, and Sec100 (read via clock_gettime for real
hundredths-of-a-second resolution, this item's own documented choice
instead of always reporting 0) is a real 0..99 value.

RUN: %plang -std=turbo -I%plang_unit_dir %s -o %t
RUN: %run %t | FileCheck %s
*)

program DosGetDateGetTime;
uses Dos;
var
  Year, Month, Day, DayOfWeek: Word;
  Hour, Minute, Second, Sec100: Word;
begin
  GetDate(Year, Month, Day, DayOfWeek);
  GetTime(Hour, Minute, Second, Sec100);
  Writeln('year-ok: ', (Year >= 2020) and (Year <= 2200));
  Writeln('month-ok: ', (Month >= 1) and (Month <= 12));
  Writeln('day-ok: ', (Day >= 1) and (Day <= 31));
  Writeln('dow-ok: ', DayOfWeek <= 6);
  Writeln('hour-ok: ', Hour <= 23);
  Writeln('minute-ok: ', Minute <= 59);
  Writeln('second-ok: ', Second <= 59);
  Writeln('sec100-ok: ', Sec100 <= 99);
end.

(*
CHECK:year-ok: TRUE
CHECK-NEXT:month-ok: TRUE
CHECK-NEXT:day-ok: TRUE
CHECK-NEXT:dow-ok: TRUE
CHECK-NEXT:hour-ok: TRUE
CHECK-NEXT:minute-ok: TRUE
CHECK-NEXT:second-ok: TRUE
CHECK-NEXT:sec100-ok: TRUE
*)
