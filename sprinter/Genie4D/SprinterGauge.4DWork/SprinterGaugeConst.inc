// File generated 12/10/2022 5:01:20 PM
// Warning! This is a generated file, any manual changes will be
// lost during the next generation.

#constant  IFONT_OFFSET     0
// object indexes into WIDGET pointer
#CONST
    iIAngularMeter0
    iIAngularMeter1
    iILedDigits0
    iIAngularMeter3
    iILedDigits1
    iILedDigits2
    iILedDigits3
#END

// object indexes into ImageControl
#CONST
    iImage0         // offset 0x0
    iWinbutton0     // offset 0x400
    iGauge0         // offset 0x1A00
    iImage1         // offset 0x45C00
    iImage2         // offset 0x46000
    iWinbutton1     // offset 0x46400
#END



#constant  ffsEndUnits4k    0x0048  // First unused 4k Sector on Flash
#constant  ffsEnd4kH        0x0004  // Address of first unused 4k Sector on Flash, High
#constant  ffsEnd4kL        0x8000  // Address of first unused 4k Sector on Flash, Low
#constant  ffsEndUnits32k   0x0009  // First unused 32k Block on Flash
#constant  ffsEnd32kH       0x0004  // Address of first unused 32k Block on Flash, High
#constant  ffsEnd32kL       0x8000  // Address of first unused 32k Block on Flash, Low
#constant  ffsEndUnits64k   0x0005  // First unused 64k Block on Flash
#constant  ffsEnd64kH       0x0005  // Address of first unused 64k Block on Flash, High
#constant  ffsEnd64kL       0x0000  // Address of first unused 64k Block on Flash, Low

#IFNOT EXISTS NOGLOBALS
var hndl ;
#ENDIF
