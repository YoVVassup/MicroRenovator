//
//  Copyright (c) 2018  syncsrc.org
//
//  This program and the accompanying materials
//  are licensed and made available under the terms and conditions of the BSD License
//  which accompanies this distribution. The full text of the license may be found at
//  http://opensource.org/licenses/bsd-license.php
//
//  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
//  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
//  


#include <Uefi.h>

#include <Library/UefiLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/PrintLib.h>
#include <Library/DevicePathLib.h>

#include <Protocol/SimpleFileSystem.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/ShellParameters.h>
#include <Guid/FileInfo.h>

#include <Library/MpInitLib/MpLib.h>


// Dual-mode conventions: allow OpenCore to load this as a "driver"
CONST UINT32 _gUefiDriverRevision = 0;
CHAR8* gEfiCallerBaseName = "Uload";

EFI_STATUS
EFIAPI
UefiUnload(IN EFI_HANDLE ImageHandle) {
  return EFI_SUCCESS;
}


#define UTILITY_VERSION L"0.8"

typedef struct {
  UINT64  Address;
  UINT32  Revision;
} MICROCODE_LOAD_BUFFER;


// Print some info about a microcode patch. Added for debugging.
// CPU_MICROCODE_HEADER is defined in UefiCpuPkg/Include/Register/Microcode.h
EFI_STATUS
DisplayMicrocodeInfo(IN CPU_MICROCODE_HEADER *MicrocodePatch)
{
  Print(L"Patch header version = %u\n", MicrocodePatch->HeaderVersion);
  Print(L"Patch update revision = 0x%x\n", MicrocodePatch->UpdateRevision);
  Print(L"Patch date = %x\n", MicrocodePatch->Date);
  Print(L"Patch processor signature = 0x%x\n", MicrocodePatch->ProcessorSignature);
  Print(L"Patch checksum = 0x%x\n", MicrocodePatch->Checksum);
  Print(L"Patch loader revision = 0x%x\n", MicrocodePatch->LoaderRevision);
  Print(L"Patch processor flags = 0x%x\n", MicrocodePatch->ProcessorFlags);
  Print(L"Patch data size = 0x%x\n", MicrocodePatch->DataSize);
  Print(L"Patch total size = 0x%x\n", MicrocodePatch->TotalSize);
  return EFI_SUCCESS;
}


// shamelessly copied from MicrocodeUpdateDxe/MicrocodeUpdate.c
UINT32
GetCurrentMicrocodeSignature ( VOID )
{
  UINT64 Signature;

  AsmWriteMsr64(MSR_IA32_BIOS_SIGN_ID, 0);
  AsmCpuid(CPUID_VERSION_INFO, NULL, NULL, NULL, NULL);
  Signature = AsmReadMsr64(MSR_IA32_BIOS_SIGN_ID);
  return (UINT32)RShiftU64(Signature, 32);
}


// shamelessly copied from MicrocodeUpdateDxe/MicrocodeUpdate.c
UINT32
TriggerMicrocodeUpdate ( IN UINT64  Address )
{
  AsmWriteMsr64(MSR_IA32_BIOS_UPDT_TRIG, Address);
  return GetCurrentMicrocodeSignature();
}


// shamelessly copied from MicrocodeUpdateDxe/MicrocodeUpdate.c
VOID
EFIAPI
MicrocodeLoadAp ( IN OUT VOID  *Buffer )
{
  MICROCODE_LOAD_BUFFER                *MicrocodeLoadBuffer;

  MicrocodeLoadBuffer = Buffer;
  MicrocodeLoadBuffer->Revision = TriggerMicrocodeUpdate (MicrocodeLoadBuffer->Address);
}


// Copied from MicrocodeUpdateDxe/MicrocodeUpdate.c with minimal modification
UINT32
TriggerMicrocodeUpdateOnThis (IN  UINTN                       Bsp,
		     IN  EFI_MP_SERVICES_PROTOCOL    *MpService,
		     IN  UINTN                       CpuIndex,
		     IN  UINT64                      Address )
{
  EFI_STATUS                           Status;
  MICROCODE_LOAD_BUFFER                MicrocodeLoadBuffer;

  if (CpuIndex == Bsp) {
    return TriggerMicrocodeUpdate (Address);
  } else {
    MicrocodeLoadBuffer.Address = Address;
    MicrocodeLoadBuffer.Revision = 0;
    Status = MpService->StartupThisAP (
				       MpService,
				       MicrocodeLoadAp,
				       CpuIndex,
				       NULL,
				       0,
				       &MicrocodeLoadBuffer,
				       NULL
				       );
    ASSERT_EFI_ERROR(Status);
    return MicrocodeLoadBuffer.Revision;
  }
}


// Normalize path separators: convert '/' to '\' for UEFI compatibility
VOID
NormalizePath(IN CHAR16 *Path)
{
  UINTN i;
  for (i = 0; Path[i] != L'\0'; i++) {
    if (Path[i] == L'/') {
      Path[i] = L'\\';
    }
  }
}


// Try to open a file from a specific SimpleFileSystem handle
EFI_STATUS
TryOpenFile(
  IN  EFI_HANDLE                DeviceHandle,
  IN  CHAR16                    *FilePath,
  OUT EFI_FILE_PROTOCOL         **File
)
{
  EFI_STATUS                    Status;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
  EFI_FILE_PROTOCOL             *Root;

  Status = gBS->HandleProtocol(
    DeviceHandle,
    &gEfiSimpleFileSystemProtocolGuid,
    (VOID**)&FileSystem
  );
  if (EFI_ERROR(Status)) {
    return Status;
  }

  Status = FileSystem->OpenVolume(FileSystem, &Root);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  Status = Root->Open(Root, File, FilePath, EFI_FILE_MODE_READ, 0);
  Root->Close(Root);

  return Status;
}


// Open microcode file using SimpleFileSystem protocol
// Tries LoadedImage->DeviceHandle first, then scans all SimpleFileSystem handles
EFI_STATUS
OpenMicrocodeFile(
  IN  EFI_HANDLE                ImageHandle,
  IN  CHAR16                    *FilePath,
  OUT EFI_FILE_PROTOCOL         **File,
  OUT EFI_HANDLE                *FoundDeviceHandle
)
{
  EFI_STATUS                    Status;
  EFI_LOADED_IMAGE_PROTOCOL     *LoadedImage;
  UINTN                         HandleCount;
  EFI_HANDLE                    *HandleBuffer;
  UINTN                         i;

  // Normalize path separators (/ → \)
  NormalizePath(FilePath);

  // Get the device this application was loaded from
  Status = gBS->HandleProtocol(
    ImageHandle,
    &gEfiLoadedImageProtocolGuid,
    (VOID**)&LoadedImage
  );
  if (EFI_ERROR(Status)) {
    Print(L"ERROR: Failed to get LoadedImage protocol: %r\n", Status);
    return Status;
  }

  // Try 1: Open from the device we were loaded from
  Status = TryOpenFile(LoadedImage->DeviceHandle, FilePath, File);
  if (!EFI_ERROR(Status)) {
    *FoundDeviceHandle = LoadedImage->DeviceHandle;
    return EFI_SUCCESS;
  }
  Print(L"Note: File not found on LoadedImage device, scanning all volumes...\n");

  // Try 2: Scan ALL SimpleFileSystem handles (OpenCore compatibility)
  // OpenCore may remap DeviceHandle to a controller without SimpleFileSystem
  HandleBuffer = NULL;
  HandleCount = 0;
  Status = gBS->LocateHandleBuffer(
    ByProtocol,
    &gEfiSimpleFileSystemProtocolGuid,
    NULL,
    &HandleCount,
    &HandleBuffer
  );
  if (EFI_ERROR(Status) || HandleCount == 0) {
    Print(L"ERROR: No SimpleFileSystem volumes found: %r\n", Status);
    return EFI_NOT_FOUND;
  }

  for (i = 0; i < HandleCount; i++) {
    // Skip the one we already tried
    if (HandleBuffer[i] == LoadedImage->DeviceHandle) {
      continue;
    }
    Status = TryOpenFile(HandleBuffer[i], FilePath, File);
    if (!EFI_ERROR(Status)) {
      *FoundDeviceHandle = HandleBuffer[i];
      FreePool(HandleBuffer);
      return EFI_SUCCESS;
    }
  }

  FreePool(HandleBuffer);
  Print(L"ERROR: Could not find %s on any volume\n", FilePath);
  return EFI_NOT_FOUND;
}


// Main application entry point.
// Usage: Uload.efi [filename]
// If no filename is specified, defaults to "ucode.pdb".
// Compatible with both UEFI Shell and OpenCore loading.
EFI_STATUS
EFIAPI
UefiMain(
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                 Status = EFI_SUCCESS;
  EFI_FILE_PROTOCOL          *File = NULL;
  CHAR16                     *DefaultName = L"ucode.pdb";
  CHAR16                     *FileName;
  CHAR16                     *FullFileName;
  UINT8                      *Buffer;
  UINTN                      FileInfoSize;
  EFI_FILE_INFO              *FileInfo;
  UINTN                      Size;
  UINTN                      ReadSize;
  VOID                       *MpProto;
  EFI_MP_SERVICES_PROTOCOL   *Mp;
  UINTN                      NumProc;
  UINTN                      NumEnabled;
  UINTN                      cpu;
  UINTN                      CurrentCpu;
  UINT32                     TestCpu;
  CPU_MICROCODE_HEADER       *MicrocodePatch;
  EFI_HANDLE                 FoundDeviceHandle;

  Print(L"MicroRenovator v%s\n", UTILITY_VERSION);

  // Resolve microcode filename in priority order:
  // 1. Shell command-line argument (Shell environment)
  // 2. LoadedImage->LoadOptions (OpenCore Arguments field)
  // 3. Default: "ucode.pdb"
  FileName = DefaultName;
  {
    EFI_SHELL_PARAMETERS_PROTOCOL *ShellParams = NULL;
    Status = gBS->HandleProtocol(
                    ImageHandle,
                    &gEfiShellParametersProtocolGuid,
                    (VOID **)&ShellParams
                    );
    if (!EFI_ERROR(Status) && ShellParams != NULL && ShellParams->Argc > 1) {
      FileName = ShellParams->Argv[1];
      Print(L"Using filename from shell args\n");
    } else {
      // Try LoadedImage->LoadOptions (OpenCore passes Arguments here)
      EFI_LOADED_IMAGE_PROTOCOL *LoadedImageOpt = NULL;
      Status = gBS->HandleProtocol(
                      ImageHandle,
                      &gEfiLoadedImageProtocolGuid,
                      (VOID **)&LoadedImageOpt
                      );
      if (!EFI_ERROR(Status) && LoadedImageOpt != NULL &&
          LoadedImageOpt->LoadOptions != NULL && LoadedImageOpt->LoadOptionsSize > 2) {
        // LoadOptions is a CHAR16 string
        CHAR16 *OptStr = (CHAR16 *)LoadedImageOpt->LoadOptions;
        UINTN OptChars = (LoadedImageOpt->LoadOptionsSize / sizeof(CHAR16)) - 1;
        if (OptChars > 0 && OptStr[0] != L'\0') {
          // Make a null-terminated copy
          CHAR16 *OptCopy = AllocatePool((OptChars + 1) * sizeof(CHAR16));
          if (OptCopy != NULL) {
            CopyMem(OptCopy, OptStr, OptChars * sizeof(CHAR16));
            OptCopy[OptChars] = L'\0';
            FileName = OptCopy;
            Print(L"Using filename from LoadOptions\n");
          }
        }
      }
    }
  }
  Print(L"Loading microcode from: %s\n", FileName);

  // Make a mutable copy of FileName for path normalization
  FullFileName = AllocateZeroPool(StrSize(FileName));
  if (FullFileName == NULL) {
    Print(L"ERROR: Could not allocate memory\n");
    Status = EFI_OUT_OF_RESOURCES;
    goto Error;
  }
  CopyMem(FullFileName, FileName, StrSize(FileName));

  // Open the microcode file using SimpleFileSystem protocol
  Status = OpenMicrocodeFile(gImageHandle, FullFileName, &File, &FoundDeviceHandle);
  if (EFI_ERROR(Status)) {
    Print(L"ERROR: Could not open file: %r\n", Status);
    goto Error;
  }

  // Get file size
  FileInfoSize = 0;
  Status = File->GetInfo(File, &gEfiFileInfoGuid, &FileInfoSize, NULL);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    Print(L"ERROR: Could not get file info size: %r\n", Status);
    goto Error;
  }
  FileInfo = AllocatePool(FileInfoSize);
  if (FileInfo == NULL) {
    Print(L"ERROR: Could not allocate memory for FileInfo\n");
    Status = EFI_OUT_OF_RESOURCES;
    goto Error;
  }
  Status = File->GetInfo(File, &gEfiFileInfoGuid, &FileInfoSize, FileInfo);
  if (EFI_ERROR(Status)) {
    Print(L"ERROR: Could not get file info: %r\n", Status);
    FreePool(FileInfo);
    goto Error;
  }
  Size = FileInfo->FileSize;
  FreePool(FileInfo);

  // Allocate a buffer to read ucode into
  Buffer = AllocateZeroPool(Size);
  if (Buffer == NULL) {
    Print(L"ERROR: Could not allocate %u bytes of memory\n", Size);
    Status = EFI_OUT_OF_RESOURCES;
    goto Error;
  }

  // Read file into Buffer
  ReadSize = Size;
  Status = File->Read(File, &ReadSize, Buffer);
  if (EFI_ERROR(Status)) {
    Print(L"ERROR: Could not read file: %r\n", Status);
    FreePool(Buffer);
    goto Error;
  }

  // Display microcode patch info
  MicrocodePatch = (CPU_MICROCODE_HEADER*) Buffer;
  Status = DisplayMicrocodeInfo(MicrocodePatch);
  if (EFI_ERROR(Status)) {
    Print(L"Error parsing microcode patch: %r\n", Status);
    FreePool(Buffer);
    goto Error;
  }

  // Find the MP Services Protocol
  Status = gBS->LocateProtocol( &gEfiMpServiceProtocolGuid, NULL, &MpProto);
  if (EFI_ERROR(Status)) {
    Print(L"Unable to locate the MpService procotol: %r\n", Status);
    FreePool(Buffer);
    goto Error;
  }
  Mp = (EFI_MP_SERVICES_PROTOCOL*) MpProto;

  // Get Number of Processors and Number of Enabled Processors
  Status = Mp->GetNumberOfProcessors( Mp, &NumProc, &NumEnabled);
  if (EFI_ERROR(Status)) {
    Print(L"Unable to get the number of processors: %r\n", Status);
    FreePool(Buffer);
    goto Error;
  } else {
    Print(L"%u Processors detected, %u enabled\n", NumProc, NumEnabled);
  }

  // This is probably not the best way to determine the BSP
  Status = Mp->WhoAmI(Mp, &CurrentCpu);
  if (EFI_ERROR(Status)) {
    Print(L"Unable to determin BSP: %r\n", Status);
    FreePool(Buffer);
    goto Error;
  } else {
    Print(L"Processor %u appears to be the BSP\n", CurrentCpu);
  }

  // Apply microcode patch
  for ( cpu=0; cpu<NumEnabled; cpu++ ) {
    TestCpu = (UINT32) cpu;

    // Starting point of uCode patch data = (UINTN) Buffer + sizeof(CPU_MICROCODE_HEADER)
    Print(L"Attempting to load ucode on processor %u\n", cpu);
    TestCpu = TriggerMicrocodeUpdateOnThis(CurrentCpu, Mp, cpu, (UINTN) Buffer + sizeof(CPU_MICROCODE_HEADER));
    Print(L"CPU %u is on microcode version %x\n", cpu, TestCpu);
  }

  FreePool(Buffer);

 Error:
  if (File != NULL) {
    File->Close(File);
  }
  if (FullFileName != NULL) {
    FreePool(FullFileName);
  }

  return Status;
}
