@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cmake --build "build"
echo EXITCODE=%ERRORLEVEL%
