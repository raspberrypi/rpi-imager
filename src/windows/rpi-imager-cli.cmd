@echo off

rem -----------------------------------------------
rem Modified Batch Script for rpi-imager.exe
rem Purpose: Attempt to run the tool without admin
rem -----------------------------------------------

rem Log the start of the process
echo Starting Raspberry Pi Imager without elevation...
echo.

rem Attempt to run rpi-imager.exe in CLI mode without admin
start /WAIT rpi-imager.exe --cli --no-elevation %*

rem Check for errors and log them
if %errorlevel% neq 0 (
    echo.
    echo An error occurred during execution. Check error.log for details.
    echo %errorlevel% > error.log
) else (
    echo.
    echo Execution completed successfully.
)

rem End of script
pause
