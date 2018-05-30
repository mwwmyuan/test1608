@echo off

setlocal EnableDelayedExpansion

set TARGET_LIST=best1101_e_ep best1101_e_ep_msbc best1101_e_mono best1101_e_mono_msbc best1101_e_ep_aac_msbc factory_suite

for %%t in (%TARGET_LIST%) do (
	echo.
	echo ------ Build %%t ------
	echo.
	make T=%%t all lst
	set err=!ERRORLEVEL!
	if !err! neq 0 (
		exit /b !err!
	)
)

@echo on
