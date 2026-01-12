@echo off
REM RoboMaster自动攻击系统 - Windows构建脚本

echo === RoboMaster自动攻击系统 - 构建脚本 ===

REM 检查CMake
where cmake >nul 2>&1
if %errorlevel% neq 0 (
    echo 错误: 未找到CMake，请先安装CMake
    pause
    exit /b 1
)

REM 创建构建目录
if exist build (
    echo 清理旧的构建目录...
    rmdir /s /q build
)

mkdir build
cd build

echo 配置CMake...
cmake .. -G "Visual Studio 16 2019" -A x64

if %errorlevel% neq 0 (
    echo CMake配置失败，尝试使用默认生成器...
    cmake ..
)

echo 开始编译...
cmake --build . --config Release

if %errorlevel% neq 0 (
    echo 编译失败！
    pause
    exit /b 1
)

echo 构建完成！
echo 可执行文件位置: %CD%\bin\Release\RM_Auto_Attack.exe
echo.
echo 运行程序:
echo   cd bin\Release
echo   RM_Auto_Attack.exe

pause
