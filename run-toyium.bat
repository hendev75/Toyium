@echo off
rem Toyium OS launcher - works from any directory
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\run-toyium.ps1" %*
