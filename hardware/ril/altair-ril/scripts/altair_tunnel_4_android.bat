@echo off
::/*  ---------------------------------------------------------------------------
::
::    Copyright (C) 2014 Altair Semiconductor Ltd.
::
::    This program is free software: you can redistribute it and/or modify
::    it under the terms of the GNU General Public License version 2 as published by
::    the Free Software Foundation, see <http://www.gnu.org/licenses/>.
::
::    This program is distributed in the hope that it will be useful,
::    but WITHOUT ANY WARRANTY; without even the implied warranty of
::    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
::    GNU General Public License for more details.
::
::  --------------------------------------------------------------------------- */


::--------------------------------------------------------
::-- script configuration
::--------------------------------------------------------
::(1) socat path
set android_socat_location=socat\android_x86\socat
::set android_socat_location=socat/arm/socat


::--------------------------------------------------------
::-- get android device connectivity definition
::--------------------------------------------------------
if exist defines.bat (
    call defines.bat
	echo. loading defineds.bat , androidDev = %androidDev%
) else (
    set androidDev=-d
)
:: confirm the android device is available ( except for -s and -l command)
if "%1"=="-s" goto dontCheckDevicePresence
if "%1"=="-l" goto dontCheckDevicePresence
if "%1"=="" goto dontCheckDevicePresence

call:mADB "shell whoami"
if %ERRORLEVEL%==-1 (
	echo. ERROR: can not access Android using  "adb %androidDev%:5555" 
	echo. the root cause may be one of the following
	echo.   1. the device is not present at "adb devices"
	echo.         - for USB device check connections and try "adb kill-servers"
	echo.         - for VM device , use "adb kill-servers" and then "adb connect tablet_IP"
	echo.   2. the script parameters are not configured, ( you need to execute one of the following
    echo.           once , since the configuration is persistance in the defines.bat file.	
	echo.         - for USB device please execute -l option 
	echo.         - for VM device please execute -s tablet_ip 
	goto:eof
)
:dontCheckDevicePresence



::--------------------------------------------------------
::-- get parameters
::--------------------------------------------------------

if not "%3"=="" (
    echo. parameter ERROR, more the 2 parameters are provided
    call:usage
    goto:eof
)

if "%1"=="" (
echo. parameter ERROR, no parameters are provided
    call:usage
    goto:eof
)

if %1==-d (
    call:disable_modem
	goto:eof
)
if %1==-e (
    call:enable_modem 
	goto:eof
)
if %1==-a (
    call:add_tunnels 
	goto:eof
)

if %1==-ae (
:: extended capabilities
    call:add_tunnels 1
	goto:eof
)

if %1==-r (
    call:remove_tunnels
	goto:eof
)

if %1==-s (
    if "%2"=="" (
       echo. parameter ERROR, only one parameters is provided	
	   call:usage
       goto:eof
	)
    call:set_Android_Ip %2
	goto:eof
)

if %1==-l (
    call:set_local_IP
	goto:eof
)

echo. parameter ERROR, parameter [%1]not supported	
	   call:usage
goto:eof



::--------------------------------------------------------
::-- Function section starts below here
::--------------------------------------------------------

:usage
echo. This script configures the modem to send logs to host
echo. Usage: %0 [-d] [-e] [-a] [-ae] [-r] [-s tablet_ip] [-l]
echo.     ---------- UE configuration command -------------
echo.        -d disable log to host in the modem and remove all debug socat 
echo.        -e enable  log to host in the modem and creates socat for debug 
echo.             NOTE: Both [-d] and [-e] will reset the modem. 
echo.                   these configuration in the modem are persistent
echo.                   and will take affect only after reset the modem.
echo.     ---------- tunnel configuration command -------------
echo.        the following command will create tunnels to allow connectivity
echo.        from the tools running on the PC to the UE behind the Android
echo.        -a add socat on the android for debug tunneling
echo.        -ae same as -a with extended capabilities like dbgview and msgview
echo.        -r remove socat on android for debug tunneling
echo.     ---------- script configuration ---------------------
echo.        the following parameter will define how the tablet is connected.
echo.        note that these command are persistant.
echo.        -l the tablet is connected localy to USB
echo         -s the tablet is connected with eth cable with the "tablet_ip"

echo. example: 
echo.       configure modem to enable logs to host and add socat to remote PC  
echo.                 %0 -e 
echo.       configure modem to disable logs to remote PC and remove socats  
echo.                 %0 -d
echo.       add logs for debug tunneling (assume the modem is already in log_to host)
echo.                 %0 -a 
echo.       remove socat used for debuging (no changes in the modem) 
echo.                 %0 -r
goto:eof


:disable_modem
echo "Disabling Logs to host in the UE"
call:mEnableModemIfc
:: use adb shell to  telnet to the modem and execute AT command to change debug mode
set mycmd="{ echo '/etc/ue_lte/at.sh AT%%SETACFG=lte-gw.loging_param.logs_to_host,disable'; sleep 2;} | telnet 10.0.0.1"
echo. mycmd=%mycmd%
adb %androidDev% shell "{ echo '/etc/ue_lte/at.sh AT%%SETACFG=lte-gw.loging_param.logs_to_host,disable'; sleep 2;} | telnet 10.0.0.1"
:: reboot the modem , so previous configuration will take effect
adb %androidDev% shell "{ echo "reboot"; sleep 2;} | telnet 10.0.0.1"
:: let the modem power up
echo please wait while the modem is powering up
goto:eof

:enable_modem
echo "Enableing Logs to host"
call:mEnableModemIfc
:: use adb shell to  telnet to the modem and execute AT command to change debug mode 
adb %androidDev% shell "{ echo '/etc/ue_lte/at.sh AT%%SETACFG=lte-gw.loging_param.logs_to_host,enable'; sleep 2;} | telnet 10.0.0.1"
:: reboot the modem , so previous configuration will take effect
adb %androidDev% shell "{ echo "reboot"; sleep 2;} | telnet 10.0.0.1"
:: let the modem power up
echo please wait 40 sec while the modem is powering up
call:mSleep 40
:: configure modem interface and IP address
call:mEnableModemIfc 
goto:eof

:add_tunnels
set extended=1
if "%1"==""  set extended=0
echo.  adding tunnels
:: (1) terminate any old tunnel if exists
:: (1.1) AT command ( originally on port 5555 will be present at port 5556
adb %androidDev% shell "busybox pkill -9 -f 'socat tcp4-listen:5556,reuseaddr,fork tcp4:10.0.0.1:5555'"
:: (1.2) telnet 
adb %androidDev% shell "busybox pkill -9 -f 'socat tcp4-listen:23,reuseaddr,fork tcp4:10.0.0.1:23'"
:: (1.3) http
adb %androidDev% shell "busybox pkill -9 -f 'socat tcp4-listen:5557,reuseaddr,fork tcp4:10.0.0.1:80'"
if "%extended%"=="1" (
	:: (1.10) msgview and dbgview , originally at UDP4566, redirect true tunnel tcp6000
	adb %androidDev% shell "busybox pkill -9 -f 'udp4-listen:4566'"
	adb %androidDev% shell "busybox pkill -9 -f 'tcp4-listen:6000'"
)

:: (2) terminate any old application if exists

:: the following is for the log collection.
if "%extended%"=="1" (
	taskkill /F /IM socat.exe 2> nul
)

:: (3) configure android to forward tcp tunnels
call:mEnableModemIfc
:: (3.1) AT command 
call:mADB "forward tcp:5556 tcp:5556"
:: (3.2) telnet 
call:mADB "forward tcp:23 tcp:23"
:: (3.3) http
call:mADB "forward tcp:80 tcp:5557"
if "%extended%"=="1" (
	:: (3.10) msgview and dbgview 
	call:mADB "forward tcp:6000 tcp:6000"
)

:: (4) configure tunnels on android device
:: (4.1) AT command 
adb %androidDev% shell "busybox nohup socat tcp4-listen:5556,reuseaddr,fork tcp4:10.0.0.1:5555 &"
:: (4.2) telnet 
adb %androidDev% shell "busybox nohup socat tcp4-listen:23,reuseaddr,fork tcp4:10.0.0.1:23  &"
:: (4.3)  http
adb %androidDev% shell "busybox nohup socat tcp4-listen:5557,reuseaddr,fork tcp4:10.0.0.1:80  &"
if "%extended%"=="1" (
	:: (4.10) msgview and dbgview
	start adb %androidDev% shell "socat -u udp4-listen:4566,reuseaddr,fork - | socat -u - tcp4-listen:6000,reuseaddr,fork"
)

:: (5) configure local tunnels on hosting window
if "%extended%"=="1" (
    :: ( 5.3) msgview and dbgview
	set str=socat\windows\socat tcp4-connect:127.0.0.1:6000 udp4:127.0.0.1:4566
	start "%str%" %str%
)
goto:eof


:remove_tunnels
echo.  remove_tunnels
adb %androidDev% shell "busybox pkill -9 -f 'socat'"

goto:eof

:set_Android_Ip
echo.  seting Android IP to %~1
del defines.bat
echo. set androidDev=-s %~1%:5555 > defines.bat
:: we also added a shortcut to allow easy execution of adb commnad.
:: for example "adb %ad% shell"
echo. set ad=adb %androidDev% >> defines.bat
call defines.bat
:: connect to the device
adb kill-server
adb connect %~1
call:mADB "push %android_socat_location% /system/bin/socat"
goto:eof

:set_local_IP
echo.  seting Local USB Android 
del defines.bat
echo set androidDev=-d > defines.bat
call defines.bat
call:mADB "push %android_socat_location% /system/bin/socat"
goto:eof

::--------------------------------------------------------
::-- internal Function section starts below here
::--------------------------------------------------------

:mADB 
echo. execute "adb %androidDev% %~1"
adb %androidDev% %~1 
goto:eof

:mAdbShell
echo. execute adb %androidDev% shell %1
adb %androidDev% shell %1 
goto:eof

:mEnableModemIfc
call:mADB root
::configure IP connectivity to the modem just in case it is not available 
call:mADB "shell ifconfig eth1:1 10.0.0.10 up"
goto:eof

:mSleep
:: since xp doesnt support timeout correctly we use workaround for delay
@ping 127.0.0.1 -n %1 -w 1000 > nul
