@ECHO OFF
SET WIXPATH="C:\Program Files (x86)\WiX Toolset v3.7\bin"
IF NOT EXIST %WIXPATH% (
    SET WIXPATH="C:\Program Files\WiX Toolset v3.7\bin"
)
DEL tony.msi
%WIXPATH%\candle.exe -v tony.wxs
%WIXPATH%\light.exe -b ..\.. -ext WixUIExtension -v tony.wixobj
PAUSE
DEL tony.wixobj
DEL tony.wixpdb