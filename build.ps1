param([switch]$Test, [switch]$Gui)
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Force build | Out-Null
    $coreSources = @('src/hash_map.cpp','src/avl.cpp','src/friend_graph.cpp','src/inverted_index.cpp',
                     'src/system.cpp','src/features.cpp','src/data_io.cpp','src/persistence.cpp')
    $compileFlags = @('-std=c++17','-O2','-Wall','-Wextra','-Wpedantic','-Iinclude')
    & g++ @compileFlags @coreSources src/auth.cpp src/main.cpp src/web_server.cpp -o build/club.exe -lws2_32 -lshell32 -lbcrypt -pthread
    if ($LASTEXITCODE -ne 0) { throw 'Application compilation failed' }
    if ($Test) {
        & g++ @compileFlags @coreSources tests/test_main.cpp -o build/club_tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Test compilation failed' }
        Push-Location build
        try { & .\club_tests.exe; if ($LASTEXITCODE -ne 0) { throw 'Unit tests failed' } }
        finally { Pop-Location }
        & python tests/test_http.py "$PSScriptRoot/build/club.exe"
        if ($LASTEXITCODE -ne 0) { throw 'HTTP tests failed' }
        & python tests/test_auth.py "$PSScriptRoot/build/club.exe"
        if ($LASTEXITCODE -ne 0) { throw 'Authorization tests failed' }
    }
    if ($Gui) { & .\build\club.exe gui }
} finally { Pop-Location }
