<#
  ps5-native-app-boilerplate - Static ELF/FSELF validator.
  Copyright (C) 2026 BlackBearReloaded
  SPDX-License-Identifier: GPL-3.0-or-later

  Checks the loader-visible structure of a generated native application.
#>

#requires -Version 5.1
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$File
)

$ErrorActionPreference = "Stop"
$path = (Resolve-Path -LiteralPath $File).Path
$data = [IO.File]::ReadAllBytes($path)

function U16([int]$offset) { [BitConverter]::ToUInt16($data, $offset) }
function U32([int]$offset) { [BitConverter]::ToUInt32($data, $offset) }
function U64([int]$offset) { [BitConverter]::ToUInt64($data, $offset) }
function Hex([uint64]$value) { "0x{0:x}" -f $value }
function Align16([uint64]$value) { ($value + 15) -band ([uint64]::MaxValue - 15) }

$container = $false
$elf = 0
$fselfCompatible = $null
$authInfoEmbedded = $null

if ($data.Length -ge 0x20 -and (U32 0) -in @([uint32]0x1D3D154F, [uint32]4009038932)) {
    $container = $true
    $entries = U16 0x18
    $headerSize = U16 0x0c
    $metaSize = U16 0x0e
    $elf = 0x20 + 0x20 * $entries
    if ($elf + 0x40 -gt $data.Length -or (U32 $elf) -ne 0x464c457f) {
        throw "FSELF does not contain an ELF header at the segment-table boundary."
    }

    $phoff = U64 ($elf + 0x20)
    $phnum = U16 ($elf + 0x38)
    $ext = Align16 ($elf + $phoff + 0x38 * $phnum)
    $extMarker = if ($ext + 16 -le $data.Length) { U64 ([int]$ext + 8) } else { 0 }
    $fselfCompatible = $extMarker -eq 1

    # This is where current kstuff-lite looks for an optional 0x88-byte auth record.
    $authOffset = $ext + 0x40 + 0x30 + 0x50 * $entries + 0x50
    $authInfoEmbedded = $authOffset + 8 -le $data.Length -and (U64 ([int]$authOffset)) -eq 0x88

    [pscustomobject]@{
        Layer = "FSELF"
        SegmentEntries = $entries
        HeaderSize = Hex $headerSize
        MetaSize = Hex $metaSize
        ElfOffset = Hex $elf
        ExtendedInfoOffset = Hex $ext
        ExtendedInfoMarker = Hex $extMarker
        KstuffRecognized = $fselfCompatible
        EmbeddedAuthInfo = $authInfoEmbedded
    } | Format-List
}

if ($elf + 0x40 -gt $data.Length -or (U32 $elf) -ne 0x464c457f) {
    throw "Input is neither a raw ELF nor a supported FSELF."
}

$osAbi = $data[$elf + 7]
$abiVersion = $data[$elf + 8]
$type = U16 ($elf + 0x10)
$entry = U64 ($elf + 0x18)
$phoff = U64 ($elf + 0x20)
$phentsize = U16 ($elf + 0x36)
$phnum = U16 ($elf + 0x38)

$types = @()
$mappedLoads = @()
$procParam = $null
$linkingLoad = $null
$programHeaders = @()
for ($i = 0; $i -lt $phnum; $i++) {
    $p = $elf + [int]$phoff + $i * $phentsize
    $pt = U32 $p
    $flags = U32 ($p + 4)
    $offset = U64 ($p + 8)
    $vaddr = U64 ($p + 16)
    $filesz = U64 ($p + 32)
    $memsz = U64 ($p + 40)
    $align = U64 ($p + 48)
    $programHeaders += [pscustomobject]@{
        Index=$i; Type=$pt; Flags=$flags; Offset=$offset; Address=$vaddr
        FileSize=$filesz; MemorySize=$memsz; Align=$align
    }
    $types += $pt
    if ($pt -eq 1) {
        if ($flags -eq 0) { $linkingLoad = $i }
        else { $mappedLoads += [pscustomobject]@{ Index=$i; Flags=$flags; Offset=$offset; Address=$vaddr; FileSize=$filesz; MemorySize=$memsz; Align=$align } }
    }
    if ($pt -eq 0x61000001) { $procParam = $offset }
}

$errors = [Collections.Generic.List[string]]::new()
if ($osAbi -ne 9) { $errors.Add("OS/ABI is not FreeBSD (9).") }
if ($abiVersion -ne 2) { $errors.Add("ABI version is not 2.") }
if ($type -ne 0xfe10) { $errors.Add("ELF type is not ET_SCE_EXEC_ASLR (0xfe10).") }
if ($phentsize -ne 0x38) { $errors.Add("Program-header size is not 0x38.") }
if ($null -eq $procParam) { $errors.Add("PT_SCE_PROCPARAM is missing.") }
if ($null -eq $linkingLoad) { $errors.Add("The flags-zero linking LOAD is missing.") }
foreach ($load in $mappedLoads) {
    if ($load.Align -ne 0x4000) { $errors.Add("Mapped LOAD $($load.Index) is not 0x4000 aligned.") }
    if ($load.MemorySize -lt $load.FileSize) { $errors.Add("LOAD $($load.Index) has memsz smaller than filesz.") }
    if ($load.Flags -notin @(1, 4, 6)) { $errors.Add("LOAD $($load.Index) has unsupported flags $($load.Flags).") }
}
foreach ($header in $programHeaders) {
    if ($header.Type -eq 0x6fffff00) {
        if ($header.MemorySize -ne 0) {
            $errors.Add("Comment header $($header.Index) must have zero memsz.")
        }
        if ($header.Align -ne 0x10) {
            $errors.Add("Comment header $($header.Index) must use 0x10 alignment.")
        }
    }
    if ($header.Type -eq 4 -and $header.Address -eq 0 -and
        $header.MemorySize -ne 0) {
        $errors.Add("Unmapped NOTE header $($header.Index) must have zero memsz.")
    }
}

$companion = $null
$sdk = $null
$procMagic = $null
if (-not $container -and $null -ne $procParam -and $elf + $procParam + 0x60 -le $data.Length) {
    $procMagic = [Text.Encoding]::ASCII.GetString($data, $elf + [int]$procParam + 8, 4)
    $companion = U32 ($elf + [int]$procParam + 0x10)
    $sdk = U32 ($elf + [int]$procParam + 0x14)
    if ($procMagic -ne "ORBI") { $errors.Add("Process-parameter magic is not ORBI.") }
}

[pscustomobject]@{
    Layer = "ELF"
    Container = $container
    OsAbi = $osAbi
    AbiVersion = $abiVersion
    Type = Hex $type
    Entry = Hex $entry
    ProgramHeaders = $phnum
    MappedLoads = $mappedLoads.Count
    LinkingLoadIndex = $linkingLoad
    ProcessParamMagic = $procMagic
    CompanionVersion = if ($null -ne $companion) { Hex $companion } else { $null }
    SdkVersion = if ($null -ne $sdk) { Hex $sdk } else { $null }
    StaticErrors = $errors.Count
} | Format-List

if ($errors.Count) {
    $errors | ForEach-Object { Write-Error $_ }
    exit 1
}
