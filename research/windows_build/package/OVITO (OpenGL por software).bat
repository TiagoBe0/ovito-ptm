@echo off
rem Starts OVITO with Mesa's software OpenGL (opengl32sw.dll in this folder), for machines without a working GPU driver.
set QT_OPENGL=software
set GALLIUM_DRIVER=llvmpipe
start "" "%~dp0ovito.exe" %*
