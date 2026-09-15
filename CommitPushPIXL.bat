@echo off
setlocal EnableExtensions

rem Review, stage tracked changes, commit and push PIXL to origin/main.
rem Usage: CommitPushPIXL.bat "Your commit message"
if "%~1" == "" set "MESSAGE=PIXL Renderer update"
if not "%~1" == "" set "MESSAGE=%~1"

set "ROOT=%~dp0"
cd /d "%ROOT%"

echo Current working-tree changes:
git status --short
echo.
git diff --check
if errorlevel 1 (
    echo ERROR: whitespace errors were found. Fix them before committing.
    exit /b 1
)

echo This will stage all tracked modifications, commit them, and push HEAD to origin/main.
echo Review the list above carefully. Untracked files are not staged automatically.
set /p "CONFIRM=Continue? Type COMMIT to continue: "
if /I not "%CONFIRM%" == "COMMIT" (
    echo Cancelled.
    exit /b 0
)

git add -u
if errorlevel 1 exit /b 1
echo.
echo Staged files:
git diff --cached --name-status
set /p "FINAL=Commit and push these files? Type PUSH to continue: "
if /I not "%FINAL%" == "PUSH" (
    echo Cancelled. Staged changes remain available for review.
    exit /b 0
)

git commit -m "%MESSAGE%"
if errorlevel 1 exit /b 1
git push origin HEAD:main
if errorlevel 1 exit /b 1
echo.
echo Commit pushed successfully.
endlocal & exit /b 0
