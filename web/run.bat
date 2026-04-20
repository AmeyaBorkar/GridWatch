@echo off
REM Build libdispatch.dll if missing, then launch the Flask web UI.
setlocal
set HERE=%~dp0
pushd "%HERE%\.."

if not exist build\libdispatch.dll (
  echo [run.bat] building libdispatch ^(make shared^)...
  make shared || goto :err
)

echo [run.bat] starting Flask server on http://127.0.0.1:5000/
python web\server.py %*
set RC=%ERRORLEVEL%
popd
exit /b %RC%

:err
echo [run.bat] build failed.
popd
exit /b 1
