#include <Uefi.h>
#include <PiDxe.h>

// #include <Guid/FileInfo.h>
// #include <Guid/FileSystemVolumeLabelInfo.h>

#include <Library/PcdLib.h>
#include <Library/BaseLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/DxeServicesLib.h>

#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/GraphicsOutput.h>

#define MULTIBOOT2_MAGIC  0xE85250D6

#define MULTIBOOT_TAG_TYPE_FRAMEBUFFER 8
#define MULTIBOOT_TAG_TYPE_EFI64 12

#pragma pack(1)
typedef struct {
  UINT32    Magic;
  UINT32    Architecture; 
  UINT32    HeaderLength;
  UINT32    Checksum;
} MULTIBOOT2_HEADER;

typedef struct {
  UINT16    Type;
  UINT16    Flags;
  UINT32    Size;
  UINT32    EntryAddress;
} MULTIBOOT2_HEADER_TAG_EFI64_ENTRY;

typedef struct {
  UINT32 Type;
  UINT32 Size;
  UINT64 Addr;
  UINT32 Pitch;
  UINT32 Width;
  UINT32 Height;
  UINT8  Bpp;
  UINT8  FramebufferType;
  UINT8  Reserved;
} MULTIBOOT2_TAG_FRAMEBUFFER;

typedef struct {
    UINT32 type;
    UINT32 size;
    UINT64 pointer;
} MULTIBOOT2_TAG_EFI64;

typedef struct {
  UINT32 TotalSize ;
  UINT32 Reserved;
  MULTIBOOT2_TAG_FRAMEBUFFER Fb;
  UINT8  FbPadding[1];
  MULTIBOOT2_TAG_EFI64 Efi64;
} MULTIBOOT2_BOOT_INFO;
#pragma pack()

// From JumpToKernel.nasm
VOID
EFIAPI
JumpToUefiKernel (
  VOID *KernelStart,
  VOID *KernelBootParams
  );

EFI_STATUS
EFIAPI
FindKernelHeader (
  IN  VOID    *Buffer,
  IN  UINTN   BufferSize,
  IN  UINTN   *KernelOffset
  )
{
  MULTIBOOT2_HEADER *Header;
  UINTN             Offset;

  if (Buffer == NULL || KernelOffset == NULL || BufferSize < sizeof(MULTIBOOT2_HEADER)) {
    return EFI_INVALID_PARAMETER;
  }

  // Ищем заголовок Multiboot2
  for (Offset = 0; Offset <= BufferSize - sizeof(MULTIBOOT2_HEADER); Offset += 4) {
    Header = (MULTIBOOT2_HEADER *)((UINT8 *)Buffer + Offset);
    if (Header->Magic == MULTIBOOT2_MAGIC) {
      break;
    }
  }

  *KernelOffset = Offset;
  
  return EFI_SUCCESS;
}

VOID *
EFIAPI
FindKernelEntryPoint (
  IN  VOID    *Buffer,
  IN  UINTN   BufferSize
  )
{
  MULTIBOOT2_HEADER *Header;
  UINT8             *CurrentPtr;
  UINT8             *BufferEnd;

  if (Buffer == NULL || BufferSize < sizeof(MULTIBOOT2_HEADER)) {
    return NULL;
  }

  BufferEnd = (UINT8 *)Buffer + BufferSize;
  
  Header = (MULTIBOOT2_HEADER *)((UINT8 *)Buffer);
  // Ищем тег типа 9 (EFI entry point)
  CurrentPtr = (UINT8 *)Header + sizeof(MULTIBOOT2_HEADER);
  
  while (CurrentPtr < (UINT8 *)Header + Header->HeaderLength) {
    MULTIBOOT2_HEADER_TAG_EFI64_ENTRY *Tag = (MULTIBOOT2_HEADER_TAG_EFI64_ENTRY *)CurrentPtr;
    
    if (Tag->Type == 0 && Tag->Size == 8) {
      break; // Конец тегов
    }
    if (Tag->Type == 9) {
      return (VOID *)(UINTN)Tag->EntryAddress;
    }

    CurrentPtr += ALIGN_VALUE(Tag->Size, 8);
    if (CurrentPtr >= BufferEnd) {
      break;
    }
  }
  
  return NULL;
}

EFI_STATUS
EFIAPI
MakeFbTag (
  IN  MULTIBOOT2_TAG_FRAMEBUFFER    *Tag
  )
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL          *GraphicsOutput;
  EFI_STATUS                            Status;

  if (Tag == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  Status = gBS->HandleProtocol (
                  gST->ConsoleOutHandle,
                  &gEfiGraphicsOutputProtocolGuid,
                  (VOID **)&GraphicsOutput
                  );
  if (EFI_ERROR (Status)) {
    Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&GraphicsOutput);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }
  Tag->Type = MULTIBOOT_TAG_TYPE_FRAMEBUFFER;
  Tag->Size = sizeof(MULTIBOOT2_TAG_FRAMEBUFFER);
  Tag->Addr = GraphicsOutput->Mode->FrameBufferBase;
  Tag->Height = GraphicsOutput->Mode->Info->VerticalResolution;
  Tag->Width = GraphicsOutput->Mode->Info->HorizontalResolution;

  return EFI_SUCCESS;
}

EFI_STATUS
        EFIAPI
MakeEfi64Tag (
        IN  MULTIBOOT2_TAG_EFI64    *Tag
)
{
    if (Tag == NULL) {
        return EFI_INVALID_PARAMETER;
    }

    Tag->type = MULTIBOOT_TAG_TYPE_EFI64; // MULTIBOOT_TAG_TYPE_EFI64
    Tag->size = sizeof(MULTIBOOT2_TAG_EFI64);
    Tag->pointer = (UINT64)gST; // EFI System Table pointer

    return EFI_SUCCESS;
}

/**
  The user Entry Point for Application. The user code starts with this function
  as the real entry point for the application.

  @param[in] ImageHandle    The firmware allocated handle for the EFI image.
  @param[in] SystemTable    A pointer to the EFI System Table.

  @retval EFI_SUCCESS       The entry point is executed successfully.
  @retval other             Some error occurs when executing this entry point.

**/
EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                        Status;
  EFI_LOADED_IMAGE_PROTOCOL         *LoadedImage;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL   *Volume;
  EFI_FILE_HANDLE                   File;
  EFI_DEVICE_PATH_PROTOCOL          *DevPath;
  VOID                              *FileBuffer;
  UINTN                             FileSize;
  UINT32                            AuthenticationStatus;
  UINTN                             KernelOffset;
  UINTN                             KernelSize;
  EFI_PHYSICAL_ADDRESS              KernelAddress;
  UINTN                             Pages;
  VOID                              *KernelStart;
  MULTIBOOT2_BOOT_INFO              *BootInfo;

  Status = gBS->HandleProtocol(
                  gImageHandle,
                  &gEfiLoadedImageProtocolGuid,
                  (VOID **)&LoadedImage
                  );
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "Failed to get loaded image protocol: %r\n", Status));
    goto Error;
  }

  Status = gBS->HandleProtocol(
                  LoadedImage->DeviceHandle,
                  &gEfiSimpleFileSystemProtocolGuid,
                  (VOID *)&Volume
                  );
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "Failed to get file system protocol: %r\n", Status));
    goto Error;
  }

  if (!EFI_ERROR (Status)) {
    Status = Volume->OpenVolume (
                       Volume,
                       &File
                       );
  }
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "Failed to Open Volume: %r\n", Status));
    goto Error;
  }

  DevPath = DevicePathFromHandle (LoadedImage->DeviceHandle);
  DevPath = FileDevicePath (LoadedImage->DeviceHandle, L"\\kernel.bin");

  FileBuffer = GetFileBufferByFilePath (FALSE, DevPath, &FileSize, &AuthenticationStatus);
  if (FileBuffer == NULL) {
    DEBUG((DEBUG_ERROR, "Failed to GetFileBufferByFilePath\n"));
    goto Error;
  }

  Status = FindKernelHeader(FileBuffer, FileSize, &KernelOffset);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "Failed to FindKernelHeader\n"));
    goto Error;
  }

  KernelSize = FileSize - KernelOffset;
  Pages = KernelSize/(4*1024) + 1;

  KernelAddress = 0x100000;

  Status = gBS->AllocatePages (
                  AllocateAddress,
                  EfiLoaderCode,
                  Pages,
                  &KernelAddress
                  );
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "Failed to AllocatePages: %r\n", Status));
    goto Error;
  }

  gBS->CopyMem((VOID *)KernelAddress, FileBuffer + KernelOffset, KernelSize);

  KernelStart = FindKernelEntryPoint((VOID *)KernelAddress, KernelSize);
  if (KernelStart == NULL) {
    DEBUG((DEBUG_ERROR, "Failed to FindKernelEntryPoint\n"));
    goto Error;
  }

  Status = gBS->AllocatePool (
                  EfiLoaderCode,
                  sizeof(MULTIBOOT2_BOOT_INFO),
                  (VOID**)&BootInfo
                  );
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "Failed to AllocatePool: %r\n", Status));
    goto Error;
  }

  BootInfo->TotalSize = sizeof(MULTIBOOT2_BOOT_INFO);

  Status = MakeFbTag(&(BootInfo->Fb));
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "Failed to MakeFbTag: %r\n", Status));
    goto Error;
  }
  Status = MakeEfi64Tag(&(BootInfo->Efi64));
    if (EFI_ERROR(Status)) {
        DEBUG((DEBUG_ERROR, "Failed to MakeEfi64Tag: %r\n", Status));
        goto Error;
    }
  DEBUG((DEBUG_ERROR, "KernelStart: 0x%X, BootInfoPtr: 0x%X\n",KernelStart, (VOID *)BootInfo));
  JumpToUefiKernel(KernelStart, (VOID *)BootInfo);

Error:
  if (KernelAddress != 0) {
    gBS->FreePages(KernelAddress, Pages);
  }

  if (BootInfo != NULL) {
    gBS->FreePool(BootInfo);
  }
  return Status;
}
