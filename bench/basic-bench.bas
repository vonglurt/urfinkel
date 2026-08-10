9700 rem ---- migration benchmark: the basic renderer, timed ----
9701 rem appended to a copy of urroyal.bas whose line 300 becomes
9702 rem "goto 9700", so the boot tables are built and nothing else runs.
9703 rem k2 is the loop counter on purpose: it is declared at line 109 and
9704 rem so sits near the front of the variable table, and no timed
9705 rem subroutine uses it. a fresh name would be scanned for last.
9706 n$(0)="alpha": n$(1)="beta": pc(0)=9: pl(0)=6: pc(1)=7: pl(1)=5
9708 m=2: nd=0: cp=0: r=2: d1=1: d2=1
9710 rem the full board draw - gosub 1000
9712 tt=ti: gosub 1000: b1=ti-tt
9714 rem one 4x4 shaded button - gosub 7200
9716 tt=ti: for k2=1 to 20: tb=87: tw=0: tf=0: x=0: y=0: gosub 7200: next
9718 b2=ti-tt
9720 rem both token rows, the per-move update - gosub 7650
9722 tt=ti: for k2=1 to 10: gosub 7650: next: b3=ti-tt
9724 rem one line spoken into the chronicle - gosub 8000
9726 t$="the quick brown fox jumps over it"
9728 tt=ti: for k2=1 to 10: gosub 8000: next: b4=ti-tt
9730 rem the legal-move generator - gosub 3500
9732 tt=ti: for k2=1 to 20: gosub 3500: next: b5=ti-tt
9740 color 0,1,0: color 4,9,5: color 1,2,7: scnclr
9742 print "ur royal - basic 3.5 renderer": print
9744 print "op          total      per call"
9746 print "--------------------------------"
9748 print "board  x1  :";b1;"jif";int(b1*20);"ms"
9750 print "button x20 :";b2;"jif";int(b2*20/20);"ms"
9752 print "tokens x10 :";b3;"jif";int(b3*20/10);"ms"
9754 print "voice  x10 :";b4;"jif";int(b4*20/10);"ms"
9756 print "moves  x20 :";b5;"jif";int(b5*20/20);"ms"
9758 print: print "50 jiffies = 1 second (pal)"
9760 do: loop
