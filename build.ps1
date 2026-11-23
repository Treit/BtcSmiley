param([switch]$Test)

$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    if (Get-Command cl -ErrorAction SilentlyContinue) {
        & cl /nologo /W4 /WX /O1 /std:c11 BtcSmiley.c /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib shell32.lib winhttp.lib
    } else {
        & gcc -std=c11 -Os -Wall -Wextra -Werror -mwindows -o BtcSmiley.exe BtcSmiley.c -lwinhttp -lshell32 -lgdi32 -luser32
    }

    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }

    if ($Test) {
        if (Get-Command cl -ErrorAction SilentlyContinue) {
            & cl /nologo /W4 /WX /O1 /std:c11 tests.c /link /SUBSYSTEM:CONSOLE user32.lib gdi32.lib shell32.lib winhttp.lib
        } else {
            & gcc -std=c11 -Os -Wall -Wextra -Werror -o tests.exe tests.c -lwinhttp -lshell32 -lgdi32 -luser32
        }

        if ($LASTEXITCODE -ne 0) { throw 'Test build failed.' }

        & .\tests.exe

        if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
    }
} finally {
    Pop-Location
}
