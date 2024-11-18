xcopy ..\Common-Driver-Files\src\*.lua src\*

smartthings edge:drivers:package -p personal -I

REM  --hub 0bc6362a-461e-4c46-af86-cbc0b5c97b58 --channel 45f7c322-dca4-4764-af7a-855e2af4e728

del src\_*.lua