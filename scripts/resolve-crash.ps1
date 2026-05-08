<#
.SYNOPSIS
  Resolve "module+RVA" addresses (e.g. those printed by our Windows crash
  filter, or copied from a WinDbg STACK_TEXT) to Function + Source:Line,
  using only the .pdb next to the binary and the OS-provided dbghelp.dll.

  No WinDbg, no Visual Studio install required — works even on a stripped-
  down Build Tools machine.

.PARAMETER ExePath
  Path to aurora-mail.exe. The matching .pdb must live next to it.

.PARAMETER Offset
  One or more RVAs from the module base. Hex with or without 0x.
  Examples:
    -Offset 0x518B
    -Offset 0x518B,0xB7C8F,0x45326

.EXAMPLE
  pwsh .\scripts\resolve-crash.ps1 `
       -ExePath C:\src\aurora-mail\build\windows-release\desktop\Release\aurora-mail.exe `
       -Offset  0x518B,0xB7C8F,0x45326,0xDC03D,0xD2AE9,0x1A68E,0x1BD4A
#>
param(
  [Parameter(Mandatory)] [string]   $ExePath,
  [Parameter(Mandatory)] [string[]] $Offset
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $ExePath)) {
  throw "ExePath not found: $ExePath"
}

$pdbPath = [System.IO.Path]::ChangeExtension($ExePath, '.pdb')
if (-not (Test-Path -LiteralPath $pdbPath)) {
  Write-Warning "No PDB next to the binary; resolution will be name-only or fail. Looked at: $pdbPath"
}

$dbghelp = @"
using System;
using System.Runtime.InteropServices;

public static class DbgHelp {
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    public struct IMAGEHLP_LINE64 {
        public uint   SizeOfStruct;
        public IntPtr Key;
        public uint   LineNumber;
        public IntPtr FileName;
        public ulong  Address;
    }

    [DllImport("dbghelp.dll", SetLastError = true)]
    public static extern bool SymInitialize(IntPtr hProcess,
        [MarshalAs(UnmanagedType.LPStr)] string searchPath, bool fInvadeProcess);

    [DllImport("dbghelp.dll", SetLastError = true)]
    public static extern bool SymCleanup(IntPtr hProcess);

    [DllImport("dbghelp.dll", SetLastError = true)]
    public static extern uint SymSetOptions(uint options);

    [DllImport("dbghelp.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern ulong SymLoadModuleExW(IntPtr hProcess, IntPtr hFile,
        string ImageName, string ModuleName,
        ulong BaseOfDll, uint DllSize, IntPtr Data, uint Flags);

    [DllImport("dbghelp.dll", SetLastError = true)]
    public static extern bool SymFromAddr(IntPtr hProcess, ulong Address,
        out ulong Displacement, IntPtr Symbol);

    [DllImport("dbghelp.dll", SetLastError = true)]
    public static extern bool SymGetLineFromAddr64(IntPtr hProcess, ulong dwAddr,
        out uint pdwDisplacement, IntPtr Line);
}

public static class K32 {
    [DllImport("kernel32.dll")] public static extern IntPtr GetCurrentProcess();
}
"@
Add-Type -TypeDefinition $dbghelp

# SYMOPT_LOAD_LINES (0x10) | SYMOPT_UNDNAME (0x02) | SYMOPT_DEFERRED_LOADS (0x04)
[void][DbgHelp]::SymSetOptions(0x10 -bor 0x02 -bor 0x04)

$hProcess = [K32]::GetCurrentProcess()
$pdbDir   = Split-Path -Parent $ExePath
if (-not [DbgHelp]::SymInitialize($hProcess, $pdbDir, $false)) {
  throw "SymInitialize failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
}

# We pick a synthetic load base; PDB resolution is RVA-keyed so the absolute
# value doesn't matter — only the offset does.
$fakeBase = [uint64]0x10000000

try {
  $modBase = [DbgHelp]::SymLoadModuleExW($hProcess, [IntPtr]::Zero,
              $ExePath, $null, $fakeBase, 0, [IntPtr]::Zero, 0)
  if ($modBase -eq 0) {
    throw "SymLoadModuleExW failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
  }

  # SYMBOL_INFO: 88-byte fixed header + name buffer. We allocate enough
  # for a 1024-char name and treat the trailing bytes as the name string.
  $nameMax       = 1024
  $headerSize    = 88
  $totalSymBytes = $headerSize + $nameMax
  $symPtr        = [Runtime.InteropServices.Marshal]::AllocHGlobal($totalSymBytes)

  try {
    foreach ($raw in $Offset) {
      $text = $raw.Trim()
      if ($text.StartsWith('0x', 'OrdinalIgnoreCase')) { $text = $text.Substring(2) }
      $rva  = [Convert]::ToUInt64($text, 16)
      $abs  = [uint64]$fakeBase + $rva

      # Re-init SYMBOL_INFO header in-place every iteration (dbghelp writes back).
      [Runtime.InteropServices.Marshal]::WriteInt32($symPtr, 0, $headerSize)        # SizeOfStruct
      [Runtime.InteropServices.Marshal]::WriteInt32($symPtr, 84, $nameMax)          # MaxNameLen
      # zero out NameLen + name buffer
      for ($i = $headerSize; $i -lt $totalSymBytes; $i++) {
        [Runtime.InteropServices.Marshal]::WriteByte($symPtr, $i, 0)
      }

      $disp = [uint64]0
      $line = ""

      if ([DbgHelp]::SymFromAddr($hProcess, $abs, [ref]$disp, $symPtr)) {
        $namePtr = [IntPtr]::Add($symPtr, $headerSize)
        $name    = [Runtime.InteropServices.Marshal]::PtrToStringAnsi($namePtr)
        if ([string]::IsNullOrEmpty($name)) { $name = "<no symbol>" }
        $line = ("0x{0,-8:X}  {1}  (+0x{2:X})" -f $rva, $name, $disp)
      } else {
        $err  = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        $line = ("0x{0,-8:X}  <SymFromAddr failed: GLE={1}>" -f $rva, $err)
      }

      $linePtr  = [Runtime.InteropServices.Marshal]::AllocHGlobal(
                    [Runtime.InteropServices.Marshal]::SizeOf([type][DbgHelp+IMAGEHLP_LINE64]))
      try {
        $hdr = New-Object DbgHelp+IMAGEHLP_LINE64
        $hdr.SizeOfStruct = [Runtime.InteropServices.Marshal]::SizeOf([type][DbgHelp+IMAGEHLP_LINE64])
        [Runtime.InteropServices.Marshal]::StructureToPtr($hdr, $linePtr, $false)

        $lineDisp = [uint32]0
        if ([DbgHelp]::SymGetLineFromAddr64($hProcess, $abs, [ref]$lineDisp, $linePtr)) {
          $hdr      = [Runtime.InteropServices.Marshal]::PtrToStructure($linePtr, [type][DbgHelp+IMAGEHLP_LINE64])
          $fileName = [Runtime.InteropServices.Marshal]::PtrToStringAnsi($hdr.FileName)
          $line    += ("`n              {0}:{1} (+{2} bytes)" -f $fileName, $hdr.LineNumber, $lineDisp)
        }
      }
      finally { [Runtime.InteropServices.Marshal]::FreeHGlobal($linePtr) }

      Write-Output $line
    }
  }
  finally { [Runtime.InteropServices.Marshal]::FreeHGlobal($symPtr) }
}
finally {
  [void][DbgHelp]::SymCleanup($hProcess)
}
