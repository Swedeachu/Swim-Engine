<#
.SYNOPSIS
    Counts Swim Engine's lines of code.

.DESCRIPTION
    Scans the repository (the parent of this scripts folder) and reports files,
    code, comment and blank lines:
      - by area (engine runtime, game/examples, tests, tools, shaders, build
        scripts, docs);
      - by engine module (Source/Engine/<module>, Systems/<system>, Renderer/<layer>);
      - by file type;
      - and the largest files.
    Build outputs, dependency caches (.cache, _deps), generated files and the
    retired Deprecated/ tree are skipped unless -IncludeDeprecated is given.
    Documentation is reported separately and never counted as code.

    Line classification is lexical: blank lines, whole-line comments (//, /* */,
    #, REM/::) and everything else as code. Lines that mix code and a trailing
    comment count as code.

.PARAMETER IncludeDeprecated
    Also count the retired Deprecated/ tree (reported as its own area).

.PARAMETER Top
    How many of the largest files to list (default 15; 0 hides the list).

.EXAMPLE
    scripts\count-lines-of-code.bat
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts\count-lines-of-code.ps1 -Top 25 -IncludeDeprecated
#>
[CmdletBinding()]
param(
    [switch]$IncludeDeprecated,
    [int]$Top = 15
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$rootPrefixLength = $root.TrimEnd('\', '/').Length + 1

# File types counted, and the comment style each uses.
$languages = @{
    '.cpp'   = @{ Name = 'C++ source'; Style = 'C' }
    '.cc'    = @{ Name = 'C++ source'; Style = 'C' }
    '.cxx'   = @{ Name = 'C++ source'; Style = 'C' }
    '.c'     = @{ Name = 'C source'; Style = 'C' }
    '.h'     = @{ Name = 'C/C++ header'; Style = 'C' }
    '.hpp'   = @{ Name = 'C/C++ header'; Style = 'C' }
    '.inl'   = @{ Name = 'C/C++ header'; Style = 'C' }
    '.slang' = @{ Name = 'Slang shader'; Style = 'C' }
    '.hlsl'  = @{ Name = 'HLSL shader'; Style = 'C' }
    '.glsl'  = @{ Name = 'GLSL shader'; Style = 'C' }
    '.vert'  = @{ Name = 'GLSL shader'; Style = 'C' }
    '.frag'  = @{ Name = 'GLSL shader'; Style = 'C' }
    '.comp'  = @{ Name = 'GLSL shader'; Style = 'C' }
    '.cmake' = @{ Name = 'CMake'; Style = 'Hash' }
    '.txt'   = @{ Name = 'CMake'; Style = 'Hash' } # Only CMakeLists.txt passes the filter below.
    '.ps1'   = @{ Name = 'PowerShell'; Style = 'Hash' }
    '.psm1'  = @{ Name = 'PowerShell'; Style = 'Hash' }
    '.py'    = @{ Name = 'Python'; Style = 'Hash' }
    '.sh'    = @{ Name = 'Shell'; Style = 'Hash' }
    '.bat'   = @{ Name = 'Batch'; Style = 'Batch' }
    '.cmd'   = @{ Name = 'Batch'; Style = 'Batch' }
    '.md'    = @{ Name = 'Markdown'; Style = 'Doc' }
}

# Directory names never scanned (build trees, caches, tooling state).
$skippedDirectories = @('.git', '.vs', '.vscode', '.idea', '.cache', 'build', 'out', '_deps', '__pycache__', 'node_modules', 'Generated')
if (-not $IncludeDeprecated) {
    $skippedDirectories += 'Deprecated'
}

function Get-RelativePath([string]$path) {
    return $path.Substring($rootPrefixLength).Replace('\', '/')
}

function Get-SourceFiles([string]$directory) {
    foreach ($entry in Get-ChildItem -LiteralPath $directory -Force) {
        if ($entry.PSIsContainer) {
            if ($skippedDirectories -notcontains $entry.Name -and -not $entry.Name.StartsWith('build-')) {
                Get-SourceFiles $entry.FullName
            }
            continue
        }
        $extension = $entry.Extension.ToLowerInvariant()
        if (-not $languages.ContainsKey($extension)) {
            continue
        }
        if ($extension -eq '.txt' -and $entry.Name -ne 'CMakeLists.txt') {
            continue
        }
        $entry
    }
}

# The reporting area of a repository-relative path.
function Get-Area([string]$relative, [string]$style) {
    if ($style -eq 'Doc') { return 'Documentation' }
    if ($relative.StartsWith('Deprecated/')) { return 'Deprecated (retired)' }
    if ($relative.StartsWith('Source/Engine/')) { return 'Engine runtime' }
    if ($relative.StartsWith('Source/Tests/')) { return 'Tests' }
    if ($relative.StartsWith('Source/Tools/')) { return 'Tools' }
    if ($relative.StartsWith('Source/Shaders/')) { return 'Shaders' }
    if ($relative.StartsWith('Source/Game/') -or $relative.StartsWith('Source/Examples/')) { return 'Game and examples' }
    if ($relative.StartsWith('Source/')) { return 'Other source' }
    return 'Build and scripts'
}

# Engine module: Source/Engine/<module>, Systems/<system>, Systems/Renderer/<layer>.
function Get-Module([string]$relative) {
    $parts = $relative.Split('/')
    if ($parts.Length -lt 4) { return 'Engine (root files)' }
    if ($parts[2] -ne 'Systems') { return $parts[2] }
    if ($parts.Length -lt 5) { return 'Systems (root files)' }
    if ($parts[3] -eq 'Renderer') {
        if ($parts.Length -lt 6) { return 'Renderer (root files)' }
        return 'Renderer/' + $parts[4]
    }
    return 'Systems/' + $parts[3]
}

function Measure-Lines([string]$path, [string]$style) {
    $code = 0
    $comment = 0
    $blank = 0
    $inBlock = $false
    foreach ($raw in [System.IO.File]::ReadLines($path)) {
        $line = $raw.Trim()
        if ($line.Length -eq 0) {
            $blank++
            continue
        }
        switch ($style) {
            'C' {
                if ($inBlock) {
                    $comment++
                    if ($line.Contains('*/')) { $inBlock = $false }
                }
                elseif ($line.StartsWith('//')) {
                    $comment++
                }
                elseif ($line.StartsWith('/*')) {
                    $comment++
                    $inBlock = -not $line.Contains('*/')
                }
                else {
                    $code++
                }
            }
            'Hash' {
                if ($inBlock) {
                    $comment++
                    if ($line.Contains('#>') -or $line.StartsWith(']]')) { $inBlock = $false }
                }
                elseif ($line.StartsWith('<#')) {
                    $comment++ # PowerShell block comment.
                    $inBlock = -not $line.Contains('#>')
                }
                elseif ($line.StartsWith('#[[')) {
                    $comment++ # CMake bracket comment.
                    $inBlock = -not $line.Contains(']]')
                }
                elseif ($line.StartsWith('#') -and -not $line.StartsWith('#!')) {
                    $comment++
                }
                else {
                    $code++
                }
            }
            'Batch' {
                if ($line.StartsWith('::') -or $line -match '^(?i)@?rem(\s|$)') { $comment++ } else { $code++ }
            }
            default {
                $code++ # Documentation: every non-blank line is text.
            }
        }
    }
    return @($code, $comment, $blank)
}

function New-Tally([string]$name) {
    return [pscustomobject]@{ Name = $name; Files = 0; Code = 0; Comments = 0; Blank = 0; Total = 0 }
}

function Add-ToTally($tallies, [string]$key, $measure) {
    if (-not $tallies.ContainsKey($key)) { $tallies[$key] = New-Tally $key }
    $tally = $tallies[$key]
    $tally.Files++
    $tally.Code += $measure[0]
    $tally.Comments += $measure[1]
    $tally.Blank += $measure[2]
    $tally.Total += $measure[0] + $measure[1] + $measure[2]
}

function Write-Table([string]$title, $rows, [string]$firstColumn) {
    Write-Host ''
    Write-Host $title -ForegroundColor Cyan
    $rows | Format-Table -AutoSize @(
        @{ Label = $firstColumn; Expression = { $_.Name } },
        @{ Label = 'Files'; Expression = { '{0:N0}' -f $_.Files }; Align = 'Right' },
        @{ Label = 'Code'; Expression = { '{0:N0}' -f $_.Code }; Align = 'Right' },
        @{ Label = 'Comments'; Expression = { '{0:N0}' -f $_.Comments }; Align = 'Right' },
        @{ Label = 'Blank'; Expression = { '{0:N0}' -f $_.Blank }; Align = 'Right' },
        @{ Label = 'Total'; Expression = { '{0:N0}' -f $_.Total }; Align = 'Right' }
    ) | Out-String -Width 200 | ForEach-Object { Write-Host $_.TrimEnd() }
}

$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
Write-Host "[Swim] Counting lines of code under $root"

$areas = @{}
$modules = @{}
$types = @{}
$files = New-Object System.Collections.Generic.List[object]
$codeTotal = New-Tally 'All code (excluding documentation)'

foreach ($file in Get-SourceFiles $root) {
    $language = $languages[$file.Extension.ToLowerInvariant()]
    $relative = Get-RelativePath $file.FullName
    $measure = Measure-Lines $file.FullName $language.Style
    $area = Get-Area $relative $language.Style
    Add-ToTally $areas $area $measure
    Add-ToTally $types $language.Name $measure
    if ($area -eq 'Engine runtime') {
        Add-ToTally $modules (Get-Module $relative) $measure
    }
    if ($language.Style -ne 'Doc' -and $area -ne 'Deprecated (retired)') {
        $codeTotal.Files++
        $codeTotal.Code += $measure[0]
        $codeTotal.Comments += $measure[1]
        $codeTotal.Blank += $measure[2]
        $codeTotal.Total += $measure[0] + $measure[1] + $measure[2]
        $files.Add([pscustomobject]@{ Path = $relative; Code = $measure[0]; Total = $measure[0] + $measure[1] + $measure[2] })
    }
}

$areaOrder = @('Engine runtime', 'Game and examples', 'Tests', 'Tools', 'Shaders', 'Other source', 'Build and scripts', 'Documentation', 'Deprecated (retired)')
$areaRows = foreach ($name in $areaOrder) { if ($areas.ContainsKey($name)) { $areas[$name] } }
Write-Table 'By area' $areaRows 'Area'
Write-Table 'Engine runtime by module' ($modules.Values | Sort-Object Code -Descending) 'Module'
Write-Table 'By file type' ($types.Values | Sort-Object Code -Descending) 'Type'

if ($Top -gt 0) {
    Write-Host ''
    Write-Host "Largest $Top files (code lines)" -ForegroundColor Cyan
    $files | Sort-Object Code -Descending | Select-Object -First $Top |
        Format-Table -AutoSize @(
            @{ Label = 'Code'; Expression = { '{0:N0}' -f $_.Code }; Align = 'Right' },
            @{ Label = 'Total'; Expression = { '{0:N0}' -f $_.Total }; Align = 'Right' },
            @{ Label = 'File'; Expression = { $_.Path } }
        ) | Out-String -Width 200 | ForEach-Object { Write-Host $_.TrimEnd() }
}

Write-Host ''
Write-Host ('[Swim] {0}: {1:N0} files, {2:N0} code lines, {3:N0} comment lines, {4:N0} blank lines, {5:N0} total.' -f `
        $codeTotal.Name, $codeTotal.Files, $codeTotal.Code, $codeTotal.Comments, $codeTotal.Blank, $codeTotal.Total) -ForegroundColor Green
if ($areas.ContainsKey('Documentation')) {
    Write-Host ('[Swim] Documentation (not counted as code): {0:N0} files, {1:N0} lines of text.' -f `
            $areas['Documentation'].Files, $areas['Documentation'].Code)
}
if (-not $IncludeDeprecated) {
    Write-Host '[Swim] Deprecated/ was skipped; pass -IncludeDeprecated to count it.'
}
Write-Host ('[Swim] Done in {0:N1} s.' -f $stopwatch.Elapsed.TotalSeconds)
