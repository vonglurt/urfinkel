9700 rem ---- renderer conformance: draw the opening board and park ----
9701 rem appended to a copy of urroyal.bas whose line 300 becomes
9702 rem "goto 9700". the same names, hues and shades the compiled demo
9703 rem uses, so the two screenshots must match cell for cell.
9706 n$(0)="alpha": n$(1)="beta": pc(0)=9: pl(0)=6: pc(1)=7: pl(1)=5
9708 m=2: nd=0
9710 gosub 1000
9712 do: loop
