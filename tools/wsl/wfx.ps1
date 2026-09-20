<#
.SYNOPSIS
    Runs this repository's Linux-only tooling inside WSL2 from one PowerShell command.

.DESCRIPTION
    Windows Defender quarantined MinGW's collect2.exe on this machine, so the host
    simulator no longer links natively. It links and runs perfectly in WSL, which is
    also where the ESPHome host platform works, so both live behind this wrapper.

    The repository is not copied anywhere. WSL sees it at /mnt/c/... and only the
    build directories and the virtualenv live on the Linux filesystem, under /root/wfx,
    because a build directory on /mnt/c is several times slower.

.PARAMETER Command
    bootstrap  install the Linux toolchain and the ESPHome dev virtualenv (safe to repeat)
    sweep      the whole simulator sweep with sanitizers, which is what CI runs
    build      configure and build the simulator only
    run        one simulator run; everything after it is passed to wled_fx_sim
    audio      the audio pipeline test
    effect     the effect and control behaviour test
    snapshot   the ESPHome snapshot harness; everything after it is passed to capture_port.py
    shell      an interactive bash in the distribution, already in the repository
    clean      remove the simulator build directories

.EXAMPLE
    powershell -File tools\wsl\wfx.ps1 bootstrap

.EXAMPLE
    powershell -File tools\wsl\wfx.ps1 sweep

.EXAMPLE
    powershell -File tools\wsl\wfx.ps1 run --effect "Fire 2012" --size 64x64
#>

[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('bootstrap', 'sweep', 'build', 'run', 'audio', 'effect', 'snapshot', 'shell', 'clean')]
    [string]$Command = 'sweep',

    [Parameter(Position = 1, ValueFromRemainingArguments = $true)]
    [string[]]$Rest
)

$ErrorActionPreference = 'Stop'

$Distro = if ($env:WFX_WSL_DISTRO) { $env:WFX_WSL_DISTRO } else { 'Ubuntu-24.04' }

# wsl.exe writes UTF-16 by default, which arrives here as text with a NUL between
# every character. WSL_UTF8 makes its own messages UTF-8; the console encoding
# covers what the Linux side prints.
$env:WSL_UTF8 = '1'
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch { }

# The repository as WSL sees it, derived from where this script actually is, so a
# copy of the tree somewhere else still works.
$RepoWin = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$drive = $RepoWin.Substring(0, 1).ToLower()
$RepoWsl = '/mnt/' + $drive + ($RepoWin.Substring(2) -replace '\\', '/')

function Invoke-Wsl([string]$BashScript) {
    # -u root: the distribution has no user account. A single-quoted here-string
    # keeps PowerShell out of the way of the shell's own quoting.
    & wsl.exe -d $Distro -u root --cd / -- bash -lc $BashScript
    return $LASTEXITCODE
}

# Quote each pass-through argument for the shell, so an effect name with a space
# arrives as one argument.
function Quote-ForShell([string[]]$Items) {
    if (-not $Items) { return '' }
    ($Items | ForEach-Object { "'" + ($_ -replace "'", "'\''") + "'" }) -join ' '
}

$argstr = Quote-ForShell $Rest
$prelude = "export WFX_REPO='$RepoWsl'; "

switch ($Command) {
    'bootstrap' { $script = $prelude + "bash '$RepoWsl/tools/wsl/bootstrap.sh' $argstr" }
    'shell'     { $script = $prelude + "cd '$RepoWsl' && exec bash -i" }
    'snapshot'  {
        $script = $prelude +
            "/root/wfx/esphome-venv/bin/python '$RepoWsl/tools/snapshot/capture_port.py' $argstr"
    }
    default     { $script = $prelude + "bash '$RepoWsl/tools/wsl/sim.sh' $Command $argstr" }
}

$code = Invoke-Wsl $script
exit $code
