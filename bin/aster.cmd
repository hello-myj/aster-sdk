@echo off
setlocal
set "ASTER_ROOT=%~dp0.."
if exist "%ASTER_ROOT%\dist\windows\aster.exe" (
  "%ASTER_ROOT%\dist\windows\aster.exe" %*
  exit /b
)
if exist "%ASTER_ROOT%\.venv-windows\Scripts\python.exe" (
  "%ASTER_ROOT%\.venv-windows\Scripts\python.exe" "%ASTER_ROOT%\tools\aster.py" %*
  exit /b
)
if defined ASTER_PYTHON (
  "%ASTER_PYTHON%" "%ASTER_ROOT%\tools\aster.py" %*
  exit /b
)
python "%ASTER_ROOT%\tools\aster.py" %*
