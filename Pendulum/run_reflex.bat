@echo off
setlocal

set REFLEX_USE_NPM=1
set PATH=C:\Program Files\nodejs;%PATH%

echo Starting Reflex with npm...
echo REFLEX_USE_NPM=%REFLEX_USE_NPM%

call .\.venv\Scripts\reflex run
