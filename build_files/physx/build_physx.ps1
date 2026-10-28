# BGE-Remix (P6): builds the PhysX 5 SDK for Blender 2.79 on Windows.
#   build_physx.ps1 -PhysX <PhysX checkout>/physx -Cuda <CUDA 12.8 root> -Out <SDK folder>
# The result: <Out>/include and <Out>/bin/win.x86_64.vc143.mt/release with the
# static libraries and PhysXGpu_64.dll. Packman (NVIDIA's package downloader)
# is not used: CMake, Python and CUDA come from the machine.
param(
	[Parameter(Mandatory = $true)][string]$PhysX,
	[Parameter(Mandatory = $true)][string]$Cuda,
	[Parameter(Mandatory = $true)][string]$Out,
	# GPU code: machine code for these architectures, PTX (compiled by the
	# driver for newer GPUs) for the last. 75 Turing (RTX 20), 86 Ampere
	# (RTX 30), 89 Ada (RTX 40).
	[string]$Sass = "75,86,89",
	[string]$Ptx = "89"
)
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$PhysX = (Resolve-Path $PhysX).Path
$preset = "vc17win64-bgeremix"

Copy-Item "$here/$preset.xml" "$PhysX/buildtools/presets/public/$preset.xml" -Force

# the GPU architectures
$gpuCmake = "$PhysX/source/compiler/cmakegpu/CMakeLists.txt"
$text = Get-Content $gpuCmake -Raw
$pattern = 'IF\(PX_GENERATE_GPU_REDUCED_ARCHITECTURES\)\s*GENERATE_ARCH_CODE_LIST\(SASS "[0-9,]+" PTX "[0-9]+"\)'
if ($text -notmatch $pattern) { throw "the GPU architecture list was not found in $gpuCmake" }
$text = [regex]::Replace($text, $pattern, "IF(PX_GENERATE_GPU_REDUCED_ARCHITECTURES)`n`tGENERATE_ARCH_CODE_LIST(SASS `"$Sass`" PTX `"$Ptx`")")
Set-Content $gpuCmake $text -NoNewline
Write-Host "GPU architectures: SASS $Sass, PTX $Ptx"

# generate (no packman: an empty PM_PATHS, CUDA from the machine). PhysX's
# generator does not quote paths: CUDA through a link without spaces.
if ($Cuda.Contains(" ")) {
	$link = "$env:SystemDrive\bgeremix_cuda"
	if (-not (Test-Path $link)) { New-Item -ItemType Junction -Path $link -Target $Cuda | Out-Null }
	$Cuda = $link
}
$env:PM_PATHS = " "
$env:PM_CUDA_PATH = $Cuda.Replace("\", "/")
Push-Location $PhysX
python buildtools/cmake_generate_projects.py $preset
Pop-Location
if (-not (Test-Path "$PhysX/compiler/$preset/PhysXSDK.sln")) { throw "PhysX projects were not generated" }

# build and install (release)
cmake --build "$PhysX/compiler/$preset" --config release --target INSTALL -- /m /v:minimal
if ($LASTEXITCODE -ne 0) { throw "PhysX build failed" }

$install = "$PhysX/install/$preset/PhysX"
New-Item -ItemType Directory -Force $Out | Out-Null
Copy-Item "$install/include" "$Out/include" -Recurse -Force
Copy-Item "$install/bin" "$Out/bin" -Recurse -Force
Copy-Item "$PhysX/LICENSE.md" "$Out/LICENSE.md" -Force
Copy-Item "$PhysX/version.txt" "$Out/version.txt" -Force

$lib = Get-ChildItem "$Out/bin" -Directory | ForEach-Object { "$($_.FullName)/release" } | Select-Object -First 1
$need = "PhysX_static_64.lib", "PhysXCommon_static_64.lib", "PhysXFoundation_static_64.lib", "PhysXCooking_static_64.lib",
        "PhysXExtensions_static_64.lib", "PhysXCharacterKinematic_static_64.lib", "PhysXPvdSDK_static_64.lib", "PhysXGpu_64.dll"
foreach ($f in $need) {
	if (-not (Test-Path "$lib/$f")) { throw "missing $f in $lib" }
}
Get-ChildItem $lib | Select-Object Name, Length | Format-Table -AutoSize
