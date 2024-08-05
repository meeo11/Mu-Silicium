#include <Library/DebugLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/BootGraphicsLib.h>
#include <Library/BootGraphics.h>

#include <Protocol/EFIUsbMsd.h>
#include <Protocol/EFIChargerEx.h>

#include "MassStorage.h"

// Global Protocols
STATIC EFI_USB_MSD_PROTOCOL         *mUsbMsdProtocol;
STATIC EFI_CHARGER_EX_PROTOCOL      *mChargerExProtocol;
STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL *mConsoleOutHandle;

EFI_STATUS
StartMassStorage (IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS    Status          = EFI_SUCCESS;
  BOOLEAN       DisplayedNotice = FALSE;
  BOOLEAN       Connected       = FALSE;
  UINTN         CurrentSplash   = 0;

  // Black & White Color
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Black;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL White;

  // Set Color of the Message
  Black.Blue = Black.Green = Black.Red = Black.Reserved = 0x00;
  White.Blue = White.Green = White.Red = White.Reserved = 0xFF;

  // Start Mass Storage
  Status = mUsbMsdProtocol->StartDevice (mUsbMsdProtocol);
  if (EFI_ERROR (Status)) {
    goto exit;
  }

  do {
    EFI_INPUT_KEY Key;

    // Execute Event Handler
    mUsbMsdProtocol->EventHandler (mUsbMsdProtocol);

    // Get Charger Presence
    mChargerExProtocol->GetChargerPresence (&Connected);

    // Display Splash depending on Connect State
    if (Connected && CurrentSplash != BG_MSD_CONNECTED) {
      // Display Connected Splash
      DisplayBootGraphic (BG_MSD_CONNECTED);

      // Set Current Splash Value
      CurrentSplash = BG_MSD_CONNECTED;

      // Reset Notice Message
      DisplayedNotice = FALSE;
    } else if (!Connected && CurrentSplash != BG_MSD_DISCONNECTED) {
      // Display Disconnected Splash
      DisplayBootGraphic (BG_MSD_DISCONNECTED);

      // Set Current Splash Value
      CurrentSplash = BG_MSD_DISCONNECTED;

      // New Message
      CHAR16 *ExitMessage = L"Press Volume Up Button to Exit Mass Storage.";

      // Set Position of Message
      UINTN XPos = (mConsoleOutHandle->Mode->Info->HorizontalResolution - StrLen(ExitMessage) * EFI_GLYPH_WIDTH) / 2;
      UINTN YPos = (mConsoleOutHandle->Mode->Info->VerticalResolution - EFI_GLYPH_HEIGHT) * 48 / 50;

      // Print New Message 
      PrintXY (XPos, YPos, &White, &Black, ExitMessage);
    }

    // Get current Key
    gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);

    // Display Confirm Message
    if (!Connected) {
      if (Key.ScanCode == SCAN_UP && DisplayedNotice == FALSE) {
        CHAR16 *ConfirmMessage = L"Press Power Button to Confirm Exiting Mass Storage.";

        // Set Position of Message
        UINTN XPos = (mConsoleOutHandle->Mode->Info->HorizontalResolution - StrLen(ConfirmMessage) * EFI_GLYPH_WIDTH) / 2;
        UINTN YPos = (mConsoleOutHandle->Mode->Info->VerticalResolution - EFI_GLYPH_HEIGHT) * 48 / 50;

        // Print New Message 
        PrintXY (XPos, YPos, &White, &Black, ConfirmMessage);

        DisplayedNotice = TRUE;
      }

      // Leave Loop
      if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN && DisplayedNotice == TRUE) {
        // Stop Mass Storage
        mUsbMsdProtocol->StopDevice (mUsbMsdProtocol);

        // Remove Assigned BLK IO Protocol
        mUsbMsdProtocol->AssingBlkIoHandle (mUsbMsdProtocol, NULL, 0);

        // Exit Application
        break;
      }
    }
  } while (TRUE);

exit:
  return Status;
}

EFI_STATUS
PrepareMassStorage ()
{
  EFI_STATUS                Status            = EFI_SUCCESS;
  EFI_DEVICE_PATH_PROTOCOL *UFSDevicePath     = (EFI_DEVICE_PATH_PROTOCOL *)&UFSLun0DevicePath;
  EFI_DEVICE_PATH_PROTOCOL *eMMCDevicePath    = (EFI_DEVICE_PATH_PROTOCOL *)&eMMCUserPartitionDevicePath;
  EFI_BLOCK_IO_PROTOCOL    *eMMCBlkIoProtocol = NULL;
  EFI_BLOCK_IO_PROTOCOL    *UFSBlkIoProtocol  = NULL;
  EFI_HANDLE                UFSHandle         = NULL;
  EFI_HANDLE                eMMCHandle        = NULL;

  // Locate USB Mass Storage Protocol
  Status = gBS->LocateProtocol (&gEfiUsbMsdProtocolGuid, NULL, (VOID *)&mUsbMsdProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Locate USB Mass Storage Protocol! Status = %r\n", Status));
    goto exit;
  }

  // Locate Charger Protocol
  Status = gBS->LocateProtocol (&gChargerExProtocolGuid, NULL, (VOID *)&mChargerExProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Locate Charger Ex Protocol! Status = %r\n"));
    goto exit;
  }

  // Locate Console Out Protocol
  Status = gBS->HandleProtocol (gST->ConsoleOutHandle, &gEfiGraphicsOutputProtocolGuid, (VOID *)&mConsoleOutHandle);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Locate Console Out Protocol! Status = %r\n"));
    goto exit;
  }

  // Locate UFS & eMMC Device Path
  Status  = gBS->LocateDevicePath (&gEfiBlockIoProtocolGuid, &UFSDevicePath,  &UFSHandle);
  Status |= gBS->LocateDevicePath (&gEfiBlockIoProtocolGuid, &eMMCDevicePath, &eMMCHandle);
  if (EFI_ERROR (Status) && Status != EFI_NOT_FOUND) {
    DEBUG ((EFI_D_ERROR, "Failed to Get Device Path of UFS and eMMC! Status = %r\n", Status));
    goto exit;
  }

  // Open & Assign UFS BLK IO Protocol
  if (UFSHandle != NULL) {
    // Get Protocol
    Status = gBS->OpenProtocol (UFSHandle, &gEfiBlockIoProtocolGuid, (VOID *)&UFSBlkIoProtocol, NULL, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "Failed to Open BLK IO Protocol of UFS! Status = %r\n", Status));
      goto exit;
    }

    // Assign Protocol
    Status = mUsbMsdProtocol->AssingBlkIoHandle (mUsbMsdProtocol, UFSBlkIoProtocol, 0);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "Failed to Assing UFS BLK IO Protocol! Status = %r\n", Status));
      goto exit;
    }
  }
  
  // Open & Assign eMMC BLK IO Protocol
  if (eMMCHandle != NULL) {
    // Get Protocol
    Status = gBS->OpenProtocol (eMMCHandle, &gEfiBlockIoProtocolGuid, (VOID *)&eMMCBlkIoProtocol, NULL, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "Failed to Open BLK IO Protocol of eMMC! Status = %r\n", Status));
      goto exit;
    }

    // Assign Protocol
    Status = mUsbMsdProtocol->AssingBlkIoHandle (mUsbMsdProtocol, eMMCBlkIoProtocol, 0);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "Failed to Assing eMMC BLK IO Protocol! Status = %r\n", Status));
      goto exit;
    }
  }

exit:
  return Status;
}

EFI_STATUS
EFIAPI
InitMassStorage (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;

  // Check for Sony & Google
  if (FixedPcdGetPtr (PcdSmbiosSystemVendor) == "Sony Group Corporation" || FixedPcdGetPtr (PcdSmbiosSystemVendor) == "Google LLC") {
    return EFI_UNSUPPORTED;
  }

  // Reset Input Protocol
  gST->ConIn->Reset (gST->ConIn, TRUE);

  // Display Warning Message
  DisplayBootGraphic (BG_MSD_WARNING);

  // Check For Key Press
  do {
    EFI_INPUT_KEY Key;

    // Get Current Key Press
    gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);

    // Leave Loop
    if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
      break;
    }

    // Exit Mass Storage
    if (Key.ScanCode == SCAN_UP) {
      return EFI_ABORTED;
    }
  } while (TRUE);

  // Disable WatchDog Timer
  gBS->SetWatchdogTimer (0, 0, 0, (CHAR16 *)NULL);

  // Prepare Mass Storage
  Status = PrepareMassStorage ();
  if (EFI_ERROR (Status)) {
    goto error;
  }

  // Start Mass Storage
  Status = StartMassStorage (SystemTable);
  if (!EFI_ERROR (Status)) {
    goto exit;
  }

error:
  // Display Failed Splash
  DisplayBootGraphic (BG_MSD_ERROR);

  // Check for Any Button
  gBS->WaitForEvent (1, gST->ConIn->WaitForKey, 0);

exit:
  return EFI_SUCCESS;
}
