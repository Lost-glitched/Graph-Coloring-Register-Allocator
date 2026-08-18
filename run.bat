@echo off
setlocal
cd /d "%~dp0"

if not exist build mkdir build

where cmake >nul 2>&1
if errorlevel 1 goto :gcc

where nmake >nul 2>&1
if errorlevel 1 goto :gcc

echo Configuring project with CMake...
cmake -S . -B build -G "NMake Makefiles"
if errorlevel 1 goto :failed

echo Building project...
cmake --build build
if errorlevel 1 goto :failed

echo Running register allocator...
if exist "build\register_allocator.exe" (
    "build\register_allocator.exe"
) else if exist "build\Release\register_allocator.exe" (
    "build\Release\register_allocator.exe"
) else if exist "build\Debug\register_allocator.exe" (
    "build\Debug\register_allocator.exe"
) else (
    echo Could not find the built demo executable.
    goto :failed
)
goto :done

:gcc

if exist "C:\msys64\ucrt64\bin\g++.exe" (
    set "CXX=C:\msys64\ucrt64\bin\g++.exe"
) else (
    where g++ >nul 2>&1
    if errorlevel 1 (
        echo CMake and g++ were not found.
        echo Install CMake or GCC, then run this file again.
        goto :failed
    )
    set "CXX=g++"
)

echo No compatible CMake build tool was found. Compiling with GCC...
"%CXX%" -std=c++20 -Wall -Wextra -Wpedantic -Iinclude ^
src/Instruction.cpp src/IRProgram.cpp src/Parser.cpp ^
src/ControlFlowGraph.cpp src/LivenessAnalyzer.cpp ^
src/InterferenceGraph.cpp src/MoveGraph.cpp src/Coalescer.cpp ^
src/SpillCostAnalyzer.cpp src/RegisterAllocator.cpp ^
src/SpillRewriter.cpp src/AllocationResult.cpp src/main.cpp ^
-o build\register_allocator.exe
if errorlevel 1 goto :failed

 echo Running register allocator...
build\register_allocator.exe
goto :done

:failed
echo.
echo The project could not be built or run.

:done
echo.
pause
endlocal
