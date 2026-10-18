<#
.SYNOPSIS
  Build, package and sign the XPort Xbox (UWP, x64) app.

.DESCRIPTION
  Usage (from any directory, Windows PowerShell 5+ or PowerShell 7):

    .\xbox\build.ps1                  # = package: configure + build + signed sideload package
    .\xbox\build.ps1 configure        # (re)generate the Visual Studio solution only
    .\xbox\build.ps1 build            # compile only (no package); fastest edit/compile loop
    .\xbox\build.ps1 package          # build + create the signed .msix for sideloading
    .\xbox\build.ps1 clean            # delete the build directory

  Options:
    -Config Release|Debug             # default Release
    -BuildDir <path>                  # default <repo>\build-xbox

  Requirements:
    - Visual Studio 2022 with the "Universal Windows Platform development" workload,
      "C++ (v143) Universal Windows Platform tools", "C++ Clang tools for Windows" and
      "C++ CMake tools for Windows" (the Remote Play library is built with clang-cl + Ninja)
    - Windows 10/11 SDK 10.0.19041 or newer
    - CMake 3.20+ and Git on PATH
    - Python 3 on PATH with protobuf tooling (generates the protocol code):
        python -m pip install "protobuf<4" "grpcio-tools<1.49"
    Required git submodules are initialised automatically, and the Visual Studio developer
    environment is loaded automatically when not already in a Developer PowerShell.

  How it builds:
    1. xbox\core (Ninja + clang-cl): lib\ and its dependencies for UWP, into
       <BuildDir>\core-<Config>, which also writes xport-core.cmake (libs, includes, defines)
    2. xbox (Visual Studio, WindowsStore): the app, importing xport-core.cmake

  Output:
    <BuildDir>\AppPackages\XPort_<version>_x64_Test\
      XPort_<version>_x64.msix           the app
      XPort_<version>_x64.cer            the signing certificate (PC installs only)
      Dependencies\x64\*.appx            VCLibs framework package(s)

  Deploying to an Xbox in Dev Mode (Device Portal, https://<xbox-ip>:11443):
    Home > "Add" > choose the .msix > "Next" > add every file in Dependencies\x64 >
    "Start". Then in Dev Home select XPort > View details (Menu button) > set
    "App type" to "Game" (full CPU/GPU/memory; "App" is heavily restricted).

  Logs:
    The app writes LocalState\xport.log. In Device Portal: File explorer >
    LocalAppData > XPort_<id> > LocalState > xport.log. A debugger attached from
    Visual Studio also sees every line (OutputDebugString).

  Signing:
    A self-signed "CN=XPort Dev" code-signing certificate is created on first use in
    xbox\.cert\ (git-ignored) and reused afterwards. The publisher in
    Package.appxmanifest.in must match its subject (CMake cache: XPORT_PUBLISHER).
#>
[CmdletBinding()]
param(
	[Parameter(Position = 0)]
	[ValidateSet('package', 'configure', 'build', 'clean')]
	[string]$Command = 'package',
	[ValidateSet('Release', 'Debug')]
	[string]$Config = 'Release',
	[string]$BuildDir
)

$ErrorActionPreference = 'Stop'
$XboxDir = $PSScriptRoot
$RepoRoot = Split-Path -Parent $XboxDir
if (-not $BuildDir) { $BuildDir = Join-Path $RepoRoot 'build-xbox' }
$CertDir = Join-Path $XboxDir '.cert'
# Per-config core build: Debug and Release need matching C runtimes (/MDd vs /MD).
$CoreDir = Join-Path $BuildDir "core-$Config"
$Publisher = 'CN=XPort Dev'

function Invoke-Checked([string]$exe, [string[]]$arguments) {
	Write-Host ">> $exe $($arguments -join ' ')" -ForegroundColor Cyan
	& $exe @arguments
	if ($LASTEXITCODE -ne 0) { throw "$exe failed with exit code $LASTEXITCODE" }
}

function Initialize-Prerequisites {
	# Submodules the Remote Play library needs (cheap no-op once checked out).
	Invoke-Checked 'git' @('-C', $RepoRoot, 'submodule', 'update', '--init', '--depth', '1',
		'third-party/nanopb', 'third-party/jerasure', 'third-party/gf-complete', 'third-party/curl')
	& python -c "import google.protobuf, grpc_tools" 2>$null
	if ($LASTEXITCODE -ne 0) {
		throw 'Python protobuf tooling missing. Run: python -m pip install "protobuf<4" "grpcio-tools<1.49"'
	}
}

function Import-VsDevEnv {
	if (-not $env:VSCMD_VER) {
		$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
		if (-not (Test-Path $vswhere)) { throw 'vswhere.exe not found: install Visual Studio 2022' }
		$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
		if (-not $vs) { throw 'No Visual Studio installation with the C++ tools found' }
		$devCmd = Join-Path $vs 'Common7\Tools\VsDevCmd.bat'
		Write-Host "Loading Visual Studio developer environment from $vs" -ForegroundColor Yellow
		$vars = & cmd.exe /c "`"$devCmd`" -arch=amd64 -host_arch=amd64 -no_logo && set"
		if ($LASTEXITCODE -ne 0) { throw "VsDevCmd.bat failed with exit code $LASTEXITCODE" }
		foreach ($line in $vars) {
			if ($line -match '^([^=]+)=(.*)$') {
				[Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
			}
		}
	}
	foreach ($tool in 'clang-cl', 'ninja') {
		if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
			throw "$tool not found: install the Visual Studio components listed in this script's header"
		}
	}
}

function Build-Core {
	Initialize-Prerequisites
	Import-VsDevEnv
	if (-not (Test-Path (Join-Path $CoreDir 'build.ninja'))) {
		Invoke-Checked 'cmake' @('-S', (Join-Path $XboxDir 'core'), '-B', $CoreDir, '-G', 'Ninja',
			"-DCMAKE_BUILD_TYPE=$Config",
			'-DCMAKE_C_COMPILER=clang-cl', '-DCMAKE_CXX_COMPILER=clang-cl',
			# fetched dependencies (mbedTLS 2.28) declare very old cmake_minimum_required versions
			'-DCMAKE_POLICY_VERSION_MINIMUM=3.5')
	}
	Invoke-Checked 'cmake' @('--build', $CoreDir)
}

function Invoke-Configure {
	# Re-run every time: cheap when cached, and points the app at the core for this -Config.
	Invoke-Checked 'cmake' @('-S', $XboxDir, '-B', $BuildDir,
		'-G', 'Visual Studio 17 2022', '-A', 'x64',
		'-DCMAKE_SYSTEM_NAME=WindowsStore', '-DCMAKE_SYSTEM_VERSION=10.0',
		"-DXPORT_CORE_DIR=$CoreDir",
		"-DXPORT_PUBLISHER=$Publisher")
}

function Get-SigningCert {
	$pfx = Join-Path $CertDir 'XPort_Dev.pfx'
	$pwFile = Join-Path $CertDir 'pfx-password.txt'
	if ((Test-Path $pfx) -and (Test-Path $pwFile)) {
		return @{ Pfx = $pfx; Password = (Get-Content $pwFile -Raw).Trim() }
	}
	Write-Host "Creating self-signed signing certificate '$Publisher' in $CertDir" -ForegroundColor Yellow
	New-Item -ItemType Directory -Force -Path $CertDir | Out-Null
	$cert = New-SelfSignedCertificate -Type Custom -Subject $Publisher `
		-KeyUsage DigitalSignature -FriendlyName 'XPort sideload signing' `
		-CertStoreLocation 'Cert:\CurrentUser\My' `
		-TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3', '2.5.29.19={text}')
	$password = [guid]::NewGuid().ToString('N')
	$secure = ConvertTo-SecureString -String $password -Force -AsPlainText
	Export-PfxCertificate -Cert "Cert:\CurrentUser\My\$($cert.Thumbprint)" -FilePath $pfx -Password $secure | Out-Null
	Set-Content -Path $pwFile -Value $password -NoNewline
	return @{ Pfx = $pfx; Password = $password }
}

switch ($Command) {
	'clean' {
		if (Test-Path $BuildDir) { Remove-Item -Recurse -Force $BuildDir }
		Write-Host "Removed $BuildDir"
	}
	'configure' {
		Build-Core
		Invoke-Configure
	}
	'build' {
		Build-Core
		Invoke-Configure
		Invoke-Checked 'cmake' @('--build', $BuildDir, '--config', $Config, '--',
			'/p:AppxPackageSigningEnabled=false', '/m')
	}
	'package' {
		Build-Core
		Invoke-Configure
		$cert = Get-SigningCert
		$packageDir = (Join-Path $BuildDir 'AppPackages') + '\'
		Invoke-Checked 'cmake' @('--build', $BuildDir, '--config', $Config, '--',
			'/m',
			'/p:GenerateAppxPackageOnBuild=true',
			'/p:AppxBundle=Never',
			'/p:UapAppxPackageBuildMode=SideloadOnly',
			'/p:AppxPackageSigningEnabled=true',
			"/p:PackageCertificateKeyFile=$($cert.Pfx)",
			"/p:PackageCertificatePassword=$($cert.Password)",
			"/p:AppxPackageDir=$packageDir")
		Write-Host ''
		Write-Host 'Packages:' -ForegroundColor Green
		Get-ChildItem -Recurse -Path $packageDir -Include *.msix, *.appx, *.cer |
			ForEach-Object { Write-Host "  $($_.FullName)" }
	}
}
