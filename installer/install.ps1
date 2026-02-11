Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Test-IsAdmin {
  $id  = [Security.Principal.WindowsIdentity]::GetCurrent()
  $pri = New-Object Security.Principal.WindowsPrincipal($id)
  return $pri.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Ensure-Admin {
  if (Test-IsAdmin) { return }

  Write-Host "[INFO] Elevating to Administrator..." -ForegroundColor Yellow
  $args = @(
    '-NoProfile',
    '-ExecutionPolicy', 'Bypass',
    '-File', "`"$PSCommandPath`""
  )

  $p = Start-Process -FilePath 'powershell.exe' -Verb RunAs -ArgumentList $args -PassThru
  $p.WaitForExit()
  exit $p.ExitCode
}

function Ensure-NotRunningAviUtl2 {
  $procs = @(Get-Process -ErrorAction SilentlyContinue | Where-Object {
    $_.ProcessName -like 'aviutl2*'
  })
  if ($procs.Count -gt 0) {
    $names = ($procs | Select-Object -ExpandProperty ProcessName -Unique) -join ', '
    throw "AviUtl2 seems running ($names). Please close AviUtl2 and retry."
  }
}

function Copy-MergeOverwrite {
  param(
    [Parameter(Mandatory=$true)][string]$Src,
    [Parameter(Mandatory=$true)][string]$Dst
  )

  if (!(Test-Path -LiteralPath $Src)) {
    throw "Source not found: $Src"
  }

  $srcPath = (Resolve-Path -LiteralPath $Src).Path.TrimEnd('\')
  if (!(Test-Path -LiteralPath $Dst)) {
    New-Item -ItemType Directory -Force -Path $Dst | Out-Null
  }
  $dstPath = (Resolve-Path -LiteralPath $Dst).Path.TrimEnd('\')

  Write-Host "[INFO] Copy (merge/overwrite) $srcPath -> $dstPath"

  # ディレクトリも含めて走査し、ファイルは常に上書きコピーする（タイムスタンプ比較でスキップしない）
  Get-ChildItem -LiteralPath $srcPath -Recurse -Force | ForEach-Object {
    $rel = $_.FullName.Substring($srcPath.Length).TrimStart('\')
    $target = Join-Path $dstPath $rel

    if ($_.PSIsContainer) {
      if (!(Test-Path -LiteralPath $target)) {
        New-Item -ItemType Directory -Force -Path $target | Out-Null
      }
    } else {
      $parent = Split-Path -Parent $target
      if (!(Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
      }
      Copy-Item -LiteralPath $_.FullName -Destination $target -Force
    }
  }
}

function Invoke-UvSyncLocked {
  param(
    [Parameter(Mandatory=$true)][string]$UvExePath
  )

  if (!(Test-Path -LiteralPath $UvExePath)) {
    throw "uv.exe not found: $UvExePath"
  }

  $pythonDir = Split-Path -Parent $UvExePath
  Write-Host "[INFO] Running: uv.exe sync --locked (in $pythonDir)"

  Push-Location $pythonDir
  try {
    & $UvExePath sync --locked
    $code = $LASTEXITCODE
    if ($code -ne 0) {
      throw "uv sync --locked failed with exit code $code"
    }
  } finally {
    Pop-Location
  }
}

Ensure-Admin

$logPath = Join-Path $PSScriptRoot "SAM3_install.log"
$transcriptStarted = $false

try {
  Start-Transcript -Path $logPath -Append | Out-Null
  $transcriptStarted = $true

  Write-Host "[INFO] SAM3 installer started."
  Write-Host "[INFO] Log: $logPath"

  Ensure-NotRunningAviUtl2

  $dstRoot = "C:\ProgramData\aviutl2"
  $dstPluginRoot = Join-Path $dstRoot "Plugin"
  $dstScriptRoot = Join-Path $dstRoot "Script"

  # Plugin / Script が無ければ最初にエラー
  if (!(Test-Path -LiteralPath $dstPluginRoot)) {
    throw "Not found: $dstPluginRoot  (AviUtl2 may not be installed in C:\ProgramData\aviutl2)"
  }
  if (!(Test-Path -LiteralPath $dstScriptRoot)) {
    throw "Not found: $dstScriptRoot  (AviUtl2 may not be installed in C:\ProgramData\aviutl2)"
  }

  # ZIP展開済みフォルダ（install.ps1 と同階層）内の aviutl2/ をソースとして扱う
  $srcRoot = Join-Path $PSScriptRoot "aviutl2"
  $srcPluginSAM3 = Join-Path $srcRoot "Plugin\SAM3"
  $srcScriptSAM3 = Join-Path $srcRoot "Script\SAM3"

  if (!(Test-Path -LiteralPath $srcPluginSAM3)) { throw "Package source not found: $srcPluginSAM3  (Did you extract the ZIP?)" }
  if (!(Test-Path -LiteralPath $srcScriptSAM3)) { throw "Package source not found: $srcScriptSAM3  (Did you extract the ZIP?)" }

  $dstPluginSAM3 = Join-Path $dstPluginRoot "SAM3"
  $dstScriptSAM3 = Join-Path $dstScriptRoot "SAM3"

  # マージ＋上書きコピー（既存の Jobs 等は消さない）
  Copy-MergeOverwrite -Src $srcPluginSAM3 -Dst $dstPluginSAM3
  Copy-MergeOverwrite -Src $srcScriptSAM3 -Dst $dstScriptSAM3

  # C:\ProgramData\aviutl2\Plugin\SAM3\python\uv.exe を使って sync --locked
  $uvExe = Join-Path $dstPluginSAM3 "python\uv.exe"
  Invoke-UvSyncLocked -UvExePath $uvExe

  Write-Host "[OK] Installation finished successfully."
  exit 0
}
catch {
  Write-Host ""
  Write-Host "[ERROR] Installation failed." -ForegroundColor Red
  Write-Host ("[ERROR] " + $_.Exception.Message) -ForegroundColor Red
  Write-Host "[INFO] See log: $logPath"
  exit 1
}
finally {
  if ($transcriptStarted) {
    try { Stop-Transcript | Out-Null } catch {}
  }
}