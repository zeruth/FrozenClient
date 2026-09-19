@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d C:\Users\tyler\runicworld-client
"C:\Users\tyler\AppData\Local\Programs\CLion\bin\cmake\win\x64\bin\cmake.exe" -S . -B cmake-build-release -G Ninja -DCMAKE_MAKE_PROGRAM="C:\Users\tyler\AppData\Local\Programs\CLion\bin\ninja\win\x64\ninja.exe" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
