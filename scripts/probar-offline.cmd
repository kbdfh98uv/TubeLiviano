@echo off
setlocal EnableExtensions
rem Compatible con cmd.exe de XP. No requiere Python, PowerShell ni Internet.
if exist "%~dp0TubeLiviano.exe" (
  pushd "%~dp0"
) else if exist "%~dp0..\..\TubeLiviano.exe" (
  pushd "%~dp0..\.."
) else if exist "%~dp0..\build\portable\TubeLiviano.exe" (
  pushd "%~dp0..\build\portable"
) else (
  echo ERROR: no se encuentra TubeLiviano.exe. Extraiga todo el ZIP.
  exit /b 2
)
set "TL_ROOT=%CD%"
set "TL_REPORTS=%TEMP%\TubeLiviano-offline-%RANDOM%-%RANDOM%"
mkdir "%TL_REPORTS%" >nul 2>&1
if not exist "%TL_REPORTS%\." (
  echo ERROR: no se pudo crear la carpeta de informes en TEMP.
  popd
  exit /b 2
)
ver >"%TL_REPORTS%\resultado.txt"
echo Esta prueba decodifica imagen y audio con salida de audio dummy.
echo Para escuchar el tono, use el boton Probar imagen y audio.
set TL_FAILURES=0
for %%P in (144 240 360) do (
  call :profile %%P
  if errorlevel 1 set /a TL_FAILURES+=1
)
>>"%TL_REPORTS%\resultado.txt" echo Fallos: %TL_FAILURES%
echo Informes: %TL_REPORTS%
type "%TL_REPORTS%\resultado.txt"
popd
if not "%TL_FAILURES%"=="0" exit /b 1
exit /b 0

:profile
if not exist "%TL_ROOT%\media\prueba-%1.mp4" (
  echo %1p: FALLO - falta el clip>>"%TL_REPORTS%\resultado.txt"
  exit /b 1
)
rem Permite arranque lento desde USB/Atom; el clip termina a los ocho segundos.
start "" /wait "%TL_ROOT%\TubeLiviano.exe" --self-test-media "%TL_ROOT%\media\prueba-%1.mp4" --report "%TL_REPORTS%\%1p.json" --test-ms 30000
if not "%ERRORLEVEL%"=="0" (
  echo %1p: FALLO - proceso no exitoso>>"%TL_REPORTS%\resultado.txt"
  exit /b 1
)
if not exist "%TL_REPORTS%\%1p.json" (
  echo %1p: FALLO - no se genero informe>>"%TL_REPORTS%\resultado.txt"
  exit /b 1
)
findstr /R /C:"passed.:true" "%TL_REPORTS%\%1p.json" >nul
if errorlevel 1 (
  echo %1p: FALLO - informe no aprobado>>"%TL_REPORTS%\resultado.txt"
  exit /b 1
)
echo %1p: APROBADO>>"%TL_REPORTS%\resultado.txt"
exit /b 0
