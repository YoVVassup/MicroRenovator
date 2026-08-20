# MicroRenovator

![MicroRenovator](https://github.com/syncsrc/syncsrc.github.io/blob/master/public/microrenovator.png?raw=true)

# WARNING

_This application is designed to modify the EFI partition and bootloader of
your system. Users acknowledge that running this program can result
in corruption of the operating system and loss of data._


## Background

The mitigations for [Spectre](https://spectreattack.com/) require updates to
both operating system kernels and
[processor microcode](https://www.intel.com/content/dam/www/public/us/en/documents/sa00115-microcode-update-guidance.pdf).
While microcode updates can be deployed at runtime, not all operating system
vendors are redistributing processor microcode updates, instead relying on
hardware manufactures to supply it through updated platform firmware. However,
millions of system are no longer receiving manufacturer support, and so have
no means to apply the processor microcode patches, leaving them unable to
mitigate the Spectre vulnerabilities.

MicroRenovator provides a mechanism for deploying processor microcode that is
independent of both manufacturer and operating-system supplied updates, by
adding a custom EFI boot script which loads microcode prior to the operating
system being run. This enables the operating system kernel to detect the updated
microcode and enable Spectre mitigations that depend on it.


## Important Limitations

**This tool does NOT downgrade microcode below the version already loaded by BIOS.**

Intel CPUs have a hardware version lock: once BIOS loads a microcode update (e.g. v27),
the CPU refuses to load any microcode with a lower revision number. WRMSR to
MSR 0x8B returns the current version, and the CPU silently rejects updates
with a version less than or equal to the already-loaded one.

What this means in practice:

- **If your BIOS ships with microcode v27+** (most systems from 2019 onward):
  this tool can only reset to v27 or higher. It **cannot** load v22 to restore
  undervolt capability — the CPU will reject it.
- **If your BIOS ships with older microcode** (pre-2019 systems):
  this tool can load a newer version with Spectre mitigations.
- **To actually downgrade microcode**, you must modify the BIOS firmware itself
  (replace microcode in FIT/BIOS region), which requires:
  - An external SPI programmer (CH341A) or BIOS flash tool
  - Disabling RSA signature verification (if the BIOS uses it)
  - Matching the microcode size to the original slot in the BIOS image
  - Accepting the risk of a bricked system

**In short: this tool is useful for systems that never received microcode updates.
On systems where the BIOS already includes recent microcode, it has no practical effect.**


## Usage

Boot the target system using a linux LiveCD or USB, such as
[Fedora](https://getfedora.org/) or [Ubuntu](https://www.ubuntu.com/download)

Clone this repository
```
git clone https://github.com/syncsrc/MicroRenovator.git
```
Then run uRenovate.sh to install the microcode updater
```
./uRenovate.sh
```
The installer will perform the following actions:
1. find appropriate microcode for the current system
2. attempt to locate an EFI partition
3. find the bootloader on that partition
4. copy the included microcode updater to the EFI partition
5. add a startup script to run the microcode updater prior to the OS bootloader

To uninstall, run
```
./uRenovate.sh -u
```


## OpenCore Usage

Uload.efi is a UEFI application that OpenCore loads from the `UEFI → Drivers` list.
It runs before the OS bootloader, applying microcode update via WRMSR.

### Installation

1. Copy `Uload.efi` to `EFI/OC/Drivers/` on your EFI partition
2. Copy your microcode `.bin` file to the same directory (e.g. `EFI/OC/Drivers/haswell_0x22.bin`)
3. Add entry to `UEFI → Drivers` in your `config.plist`:

```xml
<dict>
    <key>Arguments</key>
    <string>/EFI/OC/Drivers/haswell_0x22.bin</string>
    <key>Comment</key>
    <string></string>
    <key>Enabled</key>
    <true/>
    <key>LoadEarly</key>
    <true/>
    <key>Path</key>
    <string>Uload.efi</string>
</dict>
```

`LoadEarly = true` ensures Uload.efi runs before OpenCore bootloader.
`Arguments` contains the POSIX-style path to the microcode file.

### Filename resolution

Uload.efi resolves the microcode filename in this order:

1. **Shell command-line argument** — when launched from UEFI Shell
2. **`LoadedImage->LoadOptions`** — OpenCore passes `Arguments` field here
3. **Default: `ucode.pdb`** — in the root of the boot volume

OpenCore uses forward slashes (`/`). Uload.efi converts `/` to `\` internally.

### ScanPolicy

For Uload.efi to read microcode from any volume, set `Misc → Security → ScanPolicy` to `0`.

### Directory structure

```
EFI/
  OC/
    Drivers/
      Uload.efi                ← Microcode loader
      haswell_0x22.bin         ← Microcode file
    config.plist
```


## Offline Usage

The kickstart file can be used to build a custom LiveCD image based on Fedora
27 that includes all the necessary files and packages to build and install the
microcode loader application.

Install the LiveCD Creator utility and the sample kickstart files, and make a
local copy of the kickstart files to work with.
```
dnf -y install livecd-tools spin-kickstarts
cp /usr/share/spin-kickstarts/*.ks .
```
The LiveCD utility will need to be modified to launch the correct OS on boot.
```
sed -i 's/set default="1"/set default="0"/' /usr/lib/python3.6/site-packages/imgcreate/live.py
```
Finally, run LiveCD-Creator to build the ISO
```
livecd-creator --verbose --config=reno-live.ks --fslabel=URENO
```
The resulting URENO.iso file is a bootable image that can be burned to a DVD or
USB drive like any other live image. Once booted into this live image, simply
run the uRenovate.sh installer script.


## Building EFI Utilities

Building the EFI applications requires
[EDK2](https://github.com/tianocore/edk2).

### Linux (GCC5)

```
git clone https://github.com/tianocore/edk2.git
cd edk2
git submodule update --init --depth 1
```

Copy the `Uload/` directory into the `edk2/` folder, then run:
```
./build_efi.sh
```
Or manually:
```
sed -i 's/NoInterrupt  = FALSE/NoInterrupt  = TRUE/' ShellPkg/Application/Shell/Shell.c
make -C BaseTools
. edksetup.sh
build -a X64 -p ShellPkg/ShellPkg.dsc -b RELEASE -t GCC5
build -a X64 -p Uload/Uload.dsc -b RELEASE -t GCC5
```

### Windows (Visual Studio 2022)

Requirements:
- [Visual Studio 2022](https://visualstudio.microsoft.com/) with **Desktop development with C++** workload
- [NASM](https://www.nasm.us/) (2.16.01 or later) — add to PATH
- [Python 3](https://www.python.org/) — add to PATH

From **Developer Command Prompt for VS 2022**:
```
build_efi_win.bat
```
Or manually:
```
cd edk2
nmake
build -a X64 -p ShellPkg\ShellPkg.dsc -b RELEASE -t VS2022
build -a X64 -p Uload\Uload.dsc -b RELEASE -t VS2022
```

Output files:
- `edk2/Build/Shell/RELEASE_VS2022/X64/.../Shell.efi`
- `edk2/Build/Uload/RELEASE_VS2022/X64/Uload.efi`

To use the resulting files instead of the provided .efi binaries, change the
`edk2_dir` in `uRenovate.sh` to point at the desired `edk2/` directory.

If using a LiveCD created by the MicroRenovator kickstart file, running the
included `build_efi.sh` script will generate the necessary files.


## Testing in QEMU

You can test Uload.efi in a QEMU virtual machine before deploying on real hardware. QEMU with TCG emulation allows microcode MSR writes, so the microcode version will actually update (unlike VMware which blocks MSR access).

### Prerequisites

- [QEMU](https://qemu.weilnetz.de/w64/) (11.0 or later) — install to default location
- OVMF firmware files (downloaded automatically by `run_qemu.bat` on first run, or manually from [ovmf-prebuilt](https://github.com/rust-osdev/ovmf-prebuilt/releases/latest))
- The built `Shell.efi` and `Uload.efi`
- A microcode `.bin` file (see [Microcode Files](#microcode-files))

### Quick start

1. **Download OVMF** (one-time setup):
```powershell
cd C:\Temp\opencode\uefi-test
# Download OVMF firmware from rust-osdev/ovmf-prebuilt
Invoke-WebRequest -Uri "https://github.com/rust-osdev/ovmf-prebuilt/releases/download/edk2-stable202602-r1/edk2-stable202602-r1-bin.tar.xz" -OutFile ovmf.tar.xz
# Extract x64/code.fd and x64/vars.fd to current directory
```

2. **Create boot directory**:
```
boot\
  EFI\
    BOOT\
      BOOTX64.EFI      ← Shell.efi
      startup.nsh       ← auto-run script
    MICRO\
      ULOAD.EFI         ← Uload.efi
  haswell_0x22.bin      ← microcode file
```

3. **Create `boot\EFI\BOOT\startup.nsh`**:
```
@echo off
map -r
\EFI\MICRO\ULOAD.EFI haswell_0x22.bin
```

4. **Run QEMU**:
```powershell
# Copy OVMF_VARS template (fresh each run)
copy vars.fd vars_test.fd

# Launch QEMU with OVMF
qemu-system-x86_64.exe `
  -machine q35 `
  -cpu Haswell `
  -m 2048 `
  -smp 4 `
  -drive if=pflash,format=raw,unit=0,file=code.fd,readonly=on `
  -drive if=pflash,format=raw,unit=1,file=vars_test.fd `
  -drive file="fat:rw:boot",format=raw `
  -net none
```

Or use the included `run_qemu.bat` script.

### Expected output

```
Shell> \EFI\MICRO\ULOAD.EFI haswell_0x22.bin
Loading microcode from: haswell_0x22.bin
Patch header version = 1
Patch update revision = 0x22
Patch date = 1272017
Patch processor signature = 0x306C3
...
4 Processors detected, 4 enabled
Attempting to load ucode on processor 0
CPU 0 is on microcode version 1
Attempting to load ucode on processor 1
CPU 1 is on microcode version 1
Attempting to load ucode on processor 2
CPU 2 is on microcode version 1
Attempting to load ucode on processor 3
CPU 3 is on microcode version 1
```

The microcode version changes from `0` to `1`, confirming that QEMU's TCG emulation allows the MSR write to go through.


## Microcode Files

Uload.efi loads a raw Intel microcode binary file. By default it looks for
`ucode.pdb` in the root of the EFI partition, but you can specify any filename
as a command-line argument:

### Obtaining microcode binaries

Intel provides microcode updates through the
[Microcode Guidance](https://www.intel.com/content/www/us/en/security-center/advisory/intel-sa00115.html)
document and the [Linux firmware repository](https://github.com/intel/Intel-Linux-Process-Microcode-File).

**From Intel Microcode Guidance (recommended):**

1. Download the Microcode Revision Guidance PDF from
   [Intel SA00115](https://www.intel.com/content/www/us/en/security-center/advisory/intel-sa00115.html)
2. Find your CPU model (check `CPUID` with CPU-Z or similar tool)
3. Download the corresponding `.bin` file from the guidance tables
4. Use with Uload.efi: `Uload.efi haswell_0x22.bin`

**From Linux firmware repository:**
```
git clone https://github.com/intel/Intel-Linux-Process-Microcode-File.git
```
The `.bin` files are in the `intel-ucode/` directory. Use `iucode_tool` to
find the correct one for your processor:
```
iucode_tool -l intel-ucode/
```

Example microcode files are provided in `third-party/`:
- `haswell_0x22.bin` — Microcode revision 0x22 for Haswell processors
  (CPUID 0x306C3, date 2017-12-07)


## Known Issues
* Not compatible with Sleep (S3). Hibernate is not impacted
* Not currently compatible with UEFI secure boot
* Windows sometimes fails to boot after running Uload.efi, rebooting usually resolves the problem
* When loaded by OpenCore, microcode file must be on a readable volume (set ScanPolicy to 0)


## ToDo
* error handling in EFI application and script
