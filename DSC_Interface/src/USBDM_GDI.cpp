/*! \file
   \brief Entry points for USBDM library

   USBDM_GDI.cpp

   \verbatim
   USBDM GDI interface DLL
   Copyright (C) 2008  Peter O'Donoghue

    This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
\endverbatim
\verbatim
Change History
-============================================================================================
|  Dec 21 2012 | Fixed Reset PC Hack in HCS08 & CFV1                               - pgo V4.10.4
|  Dec 21 2012 | Removed Mass Erase as separate operation                          - pgo V4.10.4
|  Nov 30 2012 | Changed logging                                                   - pgo V4.10.4
|  Oct  8 2012 | Modified ARM accesses to use maximum aligned sizes                - pgo V4.10.2
|  May  7 2012 | Changed to using RESET_DEFAULT for ARM in DiExecResetChild()      - pgo V4.9.5
|  Mar 30 2012 | Fixed block address error in DIMemoryWrite() when flashing        - pgo V4.9.4
|  Mar 30 2012 | Changed to USBDM_SetExtendedOptions() etc.                        - pgo V4.9.4
|  Mar 24 2012 | Added actions to PROCESS_DETACH (for DSC programmer)              - pgo V4.9.4
|  Jan 14 2012 | Ported to DSC                                                     - pgo V4.9
|  Jul 16 2011 | Extended TargetConnect strategies & Messages HCSxx                - pgo V4.7
|  Jul  3 2011 | Changes to DSC code in DLL                                        - pgo V4.7
|  Jul  3 2011 | Changes related to CFVx recovery from sleeping                    - pgo V4.7
|  Jul  2 2011 | Added halt/connect in DiExecResetChild() - HCS08                  - pgo V4.7
|  Jun 30 2011 | Added checks for WAIT/WFE states                                  - pgo V4.7
|  Jun 30 2011 | Re-arranged connection code to avoid spurious secured error       - pgo V4.7
|  Jun    2011 | Added autoconnect on DiGdiGetStatus                               - pgo
+============================================================================================
\endverbatim
*/

#ifndef LEGACY
//#define USE_MEE  // Experiment - only applicable to Eclipse
#endif

#include <string>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#ifdef __unix__
#include <dlfcn.h>
#endif
#include "Common.h"
#include "Log.h"
#include "Version.h"
#include "USBDM_AUX.h"
#include "USBDM_API.h"
#include "GDI.h"
#include "GDI_Aux.h"
#include "Metrowerks.h"
#include "Conversion.h"
#include "TargetDefines.h"
#include "FlashImage.h"
#include "DeviceData.h"
#include "Names.h"
#include "wxPlugin.h"
#include "FindWindow.h"
#ifdef LEGACY
#include "USBDMDialogue.h"
#include "AppSettings.h"
#endif

#if (TARGET != CFVx) && !defined(LEGACY)
// Flash programming is not supported on above targets
#define FLASH_PROGRAMMING 1
#endif

#ifdef FLASH_PROGRAMMING
#include "FlashProgramming.h"
#endif
#if TARGET == CFVx
#include "osbdm_cfv234.h"
#elif TARGET == CFV1
#include "osbdm_cfv1.h"
#elif TARGET == MC56F80xx
#include "USBDM_DSC_API.h"
#include "DSC_Utilities.h"
#elif (TARGET == ARM)
#include "ArmDebug.h"
#include "ARM_Definitions.h"
#endif

#include "MetrowerksInterface.h"

// Nasty hack - records the first pc write to use as reset PC on future resets
bool     pcWritten            = false;
uint32_t pcResetValue         = 0x000000;
bool     programmingSupported = false;
bool     forceMassErase       = false;

bool usbdm_gdi_dll_open(void);
bool usbdm_gdi_dll_close(void);

static USBDMStatus_t USBDMStatus;
#if TARGET == MC56F80xx
static dscInfo_t     dscInformation;
#endif
static const TargetType_t targetType = T_MC56F80xx;

#ifdef FLASH_PROGRAMMING
static FlashProgrammer *flashProgrammer = NULL;
#endif

const static DiFeaturesT diFeatures = {
  /* .szIdentification                 = */ (DiStringT)"USBDM DSC",
  /* .szVersion                        = */ (DiStringT)"1.2.6",
  /* .dnVersion                        = */ (1<<16)+(2<<8)+6,
  /* .pszConfig                        = */ NULL,
  /* .dnConfigArrayItems               = */ 0,
  /* .dccIOChannel                     = */ DI_COMM_NONE,
  /* .fMemorySetMapAvailable           = */ FALSE,
  /* .fMemorySetCpuMapAvailable        = */ FALSE,
  /* .pszMemoryType                    = */ NULL,
  /* .dnMemTypeArrayItems              = */ 0,
  /* .fEnableReadaheadCache            = */ FALSE,
  /* .fTimerInCycles                   = */ FALSE,
  /* .dnTimerResolutionMantissa        = */ 128,
  /* .dnTimerResolutionExponent        = */ 0,
  /* .ddfDownloadFormat = */    {
                  /* .dafAbsFileFormat = */ DI_ABSF_BINARY,
                  /* .dnBufferSize     = */ 1000,
                  /* .daAddress        = */ {0},
                 },
  /* .ddfAuxDownloadFormat = */ {
                  /* .dafAbsFileFormat = */ DI_ABSF_NONE,
                  /* .dnBufferSize     = */ 0,
                  /* .daAddress        = */ {0,{{{0}}}},
                 },
  /* .fAuxiliaryDownloadPathAvailable  = */ FALSE,
  /* .dcCallback                       = */ DI_CB_MTWKS_EXTENSION|DI_CB_DEBUG|DI_CB_LOG,
  /* .fRegisterClassSupport            = */ FALSE,
  /* .fSingleStepSupport               = */ TRUE,
  /* .fContinueUntilSupport            = */ FALSE,
  /* .fContinueBackgroundSupport       = */ FALSE,
  /* .dnNrCodeBpAvailable              = */ 0, // Code breakpoints
  /* .dnNrDataBpAvailable              = */ 0, // Data read/write breakpoints
  /* .fExecFromCodeBp                  = */ FALSE,
  /* .fExecFromDataBp                  = */ FALSE,
  /* .fUnifiedBpLogic                  = */ TRUE,
  /* .fExecCycleCounterAvailable       = */ FALSE,
  /* .fExecTimeAvailable               = */ FALSE,
  /* .fInstrTraceAvailable             = */ FALSE,
  /* .fRawTraceAvailable               = */ FALSE,
  /* .fCoverageAvailable               = */ FALSE,
  /* .fProfilingAvailable              = */ FALSE,
  /* .fStateSaveRestoreAvailable       = */ FALSE,
  /* .dnStateStoreMaxIndex             = */ 0,
  /* .pdbgBackground                   = */ NULL,
  /* .dnBackgroundArrayItems           = */ 0,
  /* .fDirectDiAccessAvailable         = */ FALSE,  // Direct commands not available
  /* .fApplicationIOAvailable          = */ FALSE,
  /* .fKernelAware                     = */ FALSE,
#ifdef USE_MEE
  /* .fMeeAvailable                    = */ TRUE,
#else
  /* .fMeeAvailable                    = */ FALSE,
#endif
  /* .dnNrCpusAvailable                = */ 1,
  /* .deWordEndianness                 = */ DI_BIG_ENDIAN,
  /* .dnNrHardWareCodeBpAvailable      = */ 0, // No hardware code breakpoints!
  /* .fCodeHardWareBpSkids             = */ FALSE,
  /* .pReserved                        = */ NULL,
};

//============================================================================
#ifdef FLASH_PROGRAMMING
//! BDM Options to be used with the target when Flash programming
//!
static USBDM_ExtendedOptions_t bdmProgrammingOptions = {
   sizeof(USBDM_ExtendedOptions_t),
   targetType
};

//! Description of currently selected device
//!
static DeviceData deviceOptions;
//! Flash image for programming
//!
static FlashImage *flashImage = NULL;
#endif

//! BDM Options to be used with the target in general use
//!
static USBDM_ExtendedOptions_t bdmOptions;

// How hard to try to connect initially
static RetryMode initialConnectOptions = (RetryMode)(retryAlways|retryWithInit|retryByReset);
// How hard to try to connect after initial connection
static RetryMode softConnectOptions    = retryNever;

//=====================================================================================
// Error handling
static DiReturnT   currentError       = DI_ERR_STATE; // No functions available until DiGdiInitIO() is called
static const char *currentErrorString = "GDI Not opened";

//! Check if pending error condition and return immediately if so
//!
#define CHECK_ERROR_STATE() \
   if (currentError == DI_ERR_FATAL) {\
      Logging::print("CHECK_ERROR_STATE() - failed, rc=%d\n", currentError); \
      return currentError; \
   }

//!
//!
//! @param errorCode   - Error code identifying error type
//! @param errorString - Error message (NULL => use generic message)
//!
//! @return errorCode passed as 1st parameter
//!
DiReturnT setErrorState(DiReturnT   errorCode,
                        const char *errorString = NULL) {

   currentError       = errorCode;
   currentErrorString = errorString;

   if ((errorString == NULL) || (errorCode == DI_OK)) {
     // Add default message if specific message not already provided
     switch(errorCode) {
        case DI_OK :
           currentErrorString = ("No error");
           break;
        case DI_ERR_STATE :
           currentErrorString = ("GDI function called with incorrect GDI internal state");
           break;
        case DI_ERR_PARAM :
           currentErrorString = ("Illegal parameter to GDI function");
           break;
        case DI_ERR_COMMUNICATION :
           currentErrorString = ("Communication error in GDI function");
           break;
        case DI_ERR_NOTSUPPORTED  :
           currentErrorString = ("GDI Function not supported");
           break;
        case DI_ERR_NONFATAL :
           currentErrorString = ("Non-fatal error in GDI function");
           break;
        case DI_ERR_CANCEL :
           currentErrorString = ("GDI function cancelled");
           break;
        case DI_ERR_FATAL :
           currentErrorString = ("Fatal error in GDI function");
           break;
     }
   }
   return currentError;
}
#ifndef LEGACY
int regMap[] = {
/* 00 */ DSC_RegA2 ,
/* 01 */ DSC_RegB2 ,
/* 02 */ DSC_RegC2 ,
/* 03 */ DSC_RegD2,
/* 04 */ DSC_RegA0 ,
/* 05 */ DSC_RegA1 ,
/* 06 */ DSC_RegB0 ,
/* 07 */ DSC_RegB1 ,
/* 08 */ DSC_RegC0 ,
/* 09 */ DSC_RegC1 ,
/* 0A */ DSC_RegD0 ,
/* 0B */ DSC_RegD1 ,
/* 0C */ DSC_RegY0 ,
/* 0D */ DSC_RegY1 ,
/* 0E */ -1 ,
/* 0F */ DSC_RegN3 ,
/* 10 */ DSC_RegM01 ,
/* 11 */ DSC_RegOMR ,
/* 12 */ DSC_RegSR ,
/* 13 */ DSC_RegLC ,
/* 14 */ DSC_RegLC2 ,
/* 15 */ DSC_RegPC,
/* 16 */ -1 ,
/* 17 */ DSC_RegR0 ,
/* 18 */ DSC_RegR1 ,
/* 19 */ DSC_RegR2 ,
/* 1A */ DSC_RegR3 ,
/* 1B */ DSC_RegR4 ,
/* 1C */ DSC_RegR5 ,
/* 1D */ DSC_RegN ,
/* 1E */ DSC_RegSP   ,
/* 1F */ DSC_RegLA ,
/* 20 */ DSC_RegLA2 ,
/* 21 */ DSC_RegHWS0 ,
/* 22 */ DSC_RegHWS1 ,
/* 23 */ -1 ,
/* 24 */ -1 ,
/* 25 */ -1 ,
/* 26 */ -1 ,
/* 27 */ -1 ,
/* 28 */ -1 ,
/* 29 */ DSC_RegOCR  ,
/* 2A */ -1 ,
/* 2B */ DSC_RegOSR ,
/* 2C */ -1 ,
/* 2D */ -1 ,
/* 2E */ DSC_RegOTBPR   ,
/* 2F */ -1 ,
/* 30 */ DSC_RegOB0CR ,
/* 31 */ DSC_RegOB0AR1 ,
/* 32 */ DSC_RegOB0AR2 ,
/* 33 */ DSC_RegOB0MSK   ,
/* 34 */ DSC_RegOB0CNTR  ,
/* 35 */ -1 ,
/* 36 */ -1 ,
/* 37 */ -1 ,
/* 38 */ -1 ,
/* 39 */ -1 ,
/* 3A */ -1 ,
/* 3B */ -1 ,
/* 3C */ -1 ,
/* 3D */ -1 ,
/* 3E */ -1 ,
/* 3F */ -1 ,
/* 40 */ -1 ,
/* 41 */ -1 ,
/* 42 */ -1 ,
/* 43 */ -1 ,
/* 44 */ -1 ,
/* 45 */ -1 ,
/* 46 */ -1 ,
/* 47 */ -1 ,
/* 48 */ -1 ,
/* 49 */ -1 ,
/* 4A */ -1 ,
/* 4B */ -1 ,
/* 4C */ -1 ,
/* 4D */ -1 ,
};

DSC_Registers_t mapReg(unsigned cwRegNo) {
   int mappedRegNo = -1;
   if (cwRegNo < sizeof(regMap)/sizeof(regMap[0]))
      mappedRegNo = regMap[cwRegNo];
   if (mappedRegNo<0) {
      Logging::print("mapReg(0x%X) - Unknown reg\n", cwRegNo);
      return DSC_UnknownReg;
   }
   return (DSC_Registers_t)mappedRegNo;
}
#else
DSC_Registers_t mapReg(unsigned cwRegNo) {
   return (DSC_Registers_t)cwRegNo;
}
#endif

DiReturnT setErrorState(DiReturnT       errorCode,
                        USBDM_ErrorCode rc) {

   return setErrorState(errorCode, USBDM_GetErrorString(rc));
}

static DiStringT getGDIErrorMessage(void) {

   return (DiStringT)currentErrorString;
}

//!  Close connection to target
//!
//!  @return error code, see \ref USBDM_ErrorCode
//!
static DiReturnT closeBDM(void) {

   USBDM_ErrorCode rc;

   Logging::print("closeBDM()\n");

   rc = USBDM_Close();

   if (rc != BDM_RC_OK) {
      return setErrorState(DI_ERR_FATAL, rc);
   }
   return setErrorState(DI_OK);
}

//===================================================================
//! Does the following:
//!   - Initialises USBDM interface
//!   - Legacy  - Display USBDM Dialogue to obtain settings
//!   - Eclipse - Loads settings from Codewarrior
//!   - Loads device data from database
//!   - Opens BDM
//!   - Sets BDM options
//!   - Sets BDM target type
//!   - Sets up Flash programmer
//!
//!  Displays an error dialogue on failure. \n
//!  May prompt to retry connection for certain errors.
//!
//! @return \n
//!     DI_OK              => OK \n
//!     DI_ERR_FATAL       => Error see \ref currentErrorString
//!
static USBDM_ErrorCode initialiseBDMInterface(void) {
   LOGGING;

   USBDM_ErrorCode rc;

   programmingSupported = false; // Default to no programming available

   rc = USBDM_Init();
   if (rc != BDM_RC_OK) {
      USBDM_Exit();
      Logging::print("initialiseBDMInterface() - failed, reason = %s\n", USBDM_GetErrorString(rc));
      return rc;
   }
   // Close any existing connection
   closeBDM();

#if defined(GDI) && defined(LEGACY)
   // Create empty app settings
   AppSettings appSettings("GDI_", targetType);
   appSettings.loadFromAppDirFile();

   // Display dialogue to obtain BDM settings
   string deviceName;
   string projectName;
   loadNames(deviceName, projectName);
   // Create the USBDM Dialogue
   UsbdmDialogue dialogue(SharedPtr(new Shared(targetType)), appSettings);

   //   SetTopWindow(dialogue);
   dialogue.Create(NULL);
   rc = dialogue.execute(deviceName, projectName);

   if (rc == BDM_RC_OK) {
      appSettings.writeToAppDirFile();
		dialogue.getBdmOptions(bdmOptions);
#if (TARGET != CFVx) && (TARGET != MC56F80xx)
		dialogue.getDeviceOptions(deviceOptions);
#endif
   }
#else
   // Load the settings from the Eclipse environment
   string preferredBdm;
   if (rc == BDM_RC_OK) {
      rc = loadSettings(targetType, bdmOptions, preferredBdm);
   }
   // Cycle Vdd on connect is done by GDI instead of BDM H/W
   if ((bdmOptions.targetVdd != BDM_TARGET_VDD_OFF) && bdmOptions.cycleVddOnConnect) {
      initialConnectOptions = (RetryMode)(retryWithInit|retryAlways|retryByReset|retryByPower);
      softConnectOptions    = (RetryMode)(retryAlways|retryByPower);
   }
   else {
      initialConnectOptions = (RetryMode)(retryWithInit|retryAlways|retryByReset);
      softConnectOptions    = (RetryMode)(retryAlways);
   }
   bdmOptions.cycleVddOnConnect = FALSE;

   if (rc == BDM_RC_OK) {
   	rc = USBDM_OpenBySerialNumberWithRetry(targetType, preferredBdm);
   }
#if TARGET == CFVx
   if (rc == BDM_RC_OK) {
      HardwareCapabilities_t capabilities;
      if ((USBDM_GetCapabilities(&capabilities)==BDM_RC_OK) &&
            (capabilities&BDM_CAP_PST) == 0) {
         // Don't use PST option if not supported by BDM
         bdmOptions.usePSTSignals = false;
      }
   }
#endif
   if (rc == BDM_RC_OK) {
   	rc = USBDM_SetOptionsWithRetry(&bdmOptions);
   }
#endif
   // Set Target
   if (rc == BDM_RC_OK) {
      rc = USBDM_SetTargetTypeWithRetry(targetType);
   }
   if (rc != BDM_RC_OK) {
      USBDM_Exit();
      Logging::print("initialiseBDMInterface() - failed, reason = %s\n", USBDM_GetErrorString(rc));
      return rc;
   }
#ifdef FLASH_PROGRAMMING
   // Set up flash programmer for target
   if (flashProgrammer != NULL) {
      delete flashProgrammer;
   }
   // Load description of device
   rc = getDeviceData(deviceOptions);
   if (rc != BDM_RC_OK) {
      deviceOptions.setTargetName("Unknown");
      return rc;
   }
#if (TARGET == RS08)
   DeviceData::EraseOptions eraseOptions = deviceOptions.getEraseOption();
   if ((eraseOptions == DeviceData::eraseSelective) || (eraseOptions == DeviceData::eraseAll)) {
      // These targets only support mass erase
      deviceOptions.setEraseOption(DeviceData::eraseMass);
   }
#endif
   deviceOptions.setSecurity(SEC_INTELLIGENT);
   
   // Copy required options for Flash programming.
   // Other options are reset to default.
   bdmProgrammingOptions.size                = sizeof(USBDM_ExtendedOptions_t);
   bdmProgrammingOptions.targetType          = targetType;
   USBDM_GetDefaultExtendedOptions(&bdmProgrammingOptions);
   bdmProgrammingOptions.targetVdd           = bdmOptions.targetVdd;
   bdmProgrammingOptions.interfaceFrequency  = bdmOptions.interfaceFrequency;
   flashProgrammer = new FlashProgrammer;
   flashProgrammer->setDeviceData(deviceOptions);
   programmingSupported = true;
#endif

   return rc;
}

//===================================================================
//! Extended process for connecting to the target
//!
//! This procedure will prompt the user to
//! cycle the target power supply if necessary.  The
//! BKGD line will be held low while this is occurring.
//!
//! @param retryMode   How hard to retry \n
//!     The following are silent retries: \n
//!       retryNever      - give up after basic attempt, target state not affected \n
//!      +retryByReset    - quietly retry after hardware reset (if supported by target and allows debug entry). May be combined with others. \n
//!       retryNotPartial - give up if quietly if device may be secured (BDM_RC_SECURED or BDM_RC_BDM_EN_FAILED) \n
//!      + retryWithInit  - do DSC_Initialise() first
//!     The following is interactive:\n
//!       retryAlways     - retry with dialogue prompt to user \n
//! @return \n
//!     BDM_RC_OK          => OK \n
//!     other              => Error see \ref USBDM_ErrorCode
//!
USBDM_ErrorCode targetConnect(RetryMode retryMode) {
USBDM_ErrorCode rc;
   LOGGING;
   Logging::print("- %s\n", getConnectionRetryName(retryMode) );
   do {
      rc = getBDMStatus(&USBDMStatus);
      if (rc != BDM_RC_OK) {
         break;
      }
#if TARGET == ARM
      rc = USBDM_Connect();
      if (rc != BDM_RC_OK) {
         rc = USBDM_Connect(); // retry
      }
#elif TARGET == MC56F80xx
      if (retryMode & retryWithInit) {
         dscInformation.size = sizeof(dscInfo_t);
         rc = DSC_GetInfo(&dscInformation);
         if (rc != BDM_RC_OK) {
            break;
         }
         rc = DSC_Initialise();
         if (rc != BDM_RC_OK) {
            break;
         }
      }
      rc = DSC_Connect();
      if (rc != BDM_RC_OK) {
         if (retryMode&retryByReset) {
            DSC_TargetReset((TargetMode_t)(RESET_SPECIAL|RESET_DEFAULT));
         }
         rc = DSC_Connect(); // retry
      }
      if (rc != BDM_RC_OK) {
         break;
      }
#else
      rc = USBDM_TargetConnectWithRetry(retryMode);
#endif
   } while (0);
   Logging::print("rc = %s\n", USBDM_GetErrorString(rc));
   return rc;
}

//! Open GDI
//!
//! @param dnGdiVersionH
//! @param dnGdiVersionL
//! @param dnArgc
//! @param szArgv
//! @param UdProcessEvents
//!
//!
//! @return \n
//!     DI_OK              => OK \n
//!     DI_ERR_FATAL       => Error see \ref currentErrorString
//!
USBDM_GDI_API
DiReturnT DiGdiOpen ( DiUInt32T      dnGdiVersionH,
                      DiUInt32T      dnGdiVersionL,
                      DiUInt32T      dnArgc,
                      DiConstStringT szArgv[],
                      void (*UdProcessEvents)(void) ) {
   LOGGING;
   unsigned sub;

   usbdm_gdi_dll_open();
   
#if TARGET == MC56F80xx
   DSC_SetLogFile(Logging::getLogFileHandle());
#endif

   Logging::print("DiGdiOpen(\n"
         "   dnGdiVersionH=0x%X, dnGdiVersionL=0x%X, \n"
         "   dnArgc=%d, szArgv=0x%p, \n"
         "   UdProcessEvents=0x%p"
         ")\n",
         dnGdiVersionH, dnGdiVersionL, dnArgc, szArgv, UdProcessEvents);
   for (sub = 0; sub < dnArgc; sub++) {
      Logging::print("argv[%2i]:\'%s\'\n", sub, szArgv[sub]);
   }
#ifdef FLASH_PROGRAMMING
   flashImage = NULL;
#endif

   // No functions available until DiGdiInitIO() is called
   currentError       = DI_ERR_STATE;
   currentErrorString = "GDI Not opened";

   USBDM_ErrorCode rc = USBDM_Init();
   if (rc != BDM_RC_OK) {
      return setErrorState(DI_ERR_FATAL, rc);
   }
   else {
      return setErrorState(DI_OK);
   }
}

//! Close GDI
//!
//! @param fClose
//!
//!
//! @return \n
//!     DI_OK              => OK \n
//!     DI_ERR_FATAL       => Error see \ref currentErrorString
//!
USBDM_GDI_API
DiReturnT DiGdiClose ( DiBoolT fClose ) {
   closeBDM();
   USBDM_Exit();
   Logging::print("DiGdiClose()\n");
   usbdm_gdi_dll_close();
   setErrorState(DI_ERR_STATE, ("GDI Not open"));
#ifdef FLASH_PROGRAMMING
   if (flashImage != NULL) {
      delete flashImage;
      flashImage = NULL;
   }
   if (flashProgrammer != NULL) {
      delete flashProgrammer;
      flashProgrammer = NULL;
   }
#endif
   return DI_OK;
}

//! Get GDI version
//!
//! @param dnGdiVersion
//!
//!
//! @return \n
//!     DI_OK              => OK \n
//!     DI_ERR_FATAL       => Error see \ref currentErrorString
//!
USBDM_GDI_API
DiReturnT DiGdiVersion ( DiUInt32T *dnGdiVersion ) {

   Logging::print("DiGdiVersion()\n");
   *dnGdiVersion = USBDM_GDI_INTERFACE_VERSION;
   return setErrorState(DI_OK);
}

//! Get Features Supported by GDI
//!
//! @param pdfFeatures
//!
//!
//! @return \n
//!     DI_OK              => OK \n
//!     DI_ERR_FATAL       => Error see \ref currentErrorString
//!
USBDM_GDI_API
DiReturnT DiGdiGetFeatures ( pDiFeaturesT pdfFeatures ) {

   Logging::print("DiGdiGetFeatures()\n");
   *pdfFeatures = diFeatures;
   return setErrorState(DI_OK);
}

//! Set GDI Configuration
//!
//! @param szConfig
//!
//! @return \n
//!     DI_OK              => OK \n
//!     DI_ERR_FATAL       => Error see \ref currentErrorString
//!
USBDM_GDI_API
DiReturnT DiGdiSetConfig ( DiConstStringT szConfig ) {

   Logging::print("DiGdiSetConfig() - not implemented\n");
   return setErrorState(DI_OK);
}

//! Does initial target connection with check for secured target.
//! The user will be prompted to mass erase the target if secured.
//!
//! @return \n
//!     DI_OK              => OK \n
//!     DI_ERR_FATAL       => Error see \ref currentErrorString
//!
USBDM_ErrorCode initialConnect(void) {
   LOGGING;
   USBDM_ErrorCode rc;

   Logging::print("initialConnect()\n");

   forceMassErase = false;
   pcWritten      = false;

   // Initial connect using all strategies
   rc = targetConnect(initialConnectOptions);
#if (TARGET == MC56F80xx)
   Logging::print("Doing DSC_TargetHalt()\n");
   rc = DSC_TargetHalt();
   if (rc != BDM_RC_OK) {
      if (initialConnectOptions&retryByReset) {
         DSC_TargetReset((TargetMode_t)(RESET_SPECIAL|RESET_DEFAULT));
      }
      rc = DSC_TargetHalt(); // retry
   }
#endif

#ifdef FLASH_PROGRAMMING

#if (TARGET == HCS08) || (TARGET == CFV1) || (TARGET == RS08)
   if (rc == BDM_RC_OK) {
      if (flashProgrammer->checkTargetUnSecured() == PROGRAMMING_RC_ERROR_SECURED) {
         rc = BDM_RC_SECURED;
      }
   }
#endif

   if ((rc == BDM_RC_SECURED) &&
       (flashProgrammer->getDeviceData()->getEraseOption() != DeviceData::eraseMass)){
         // Prompt user to change erase option to 'mass erase'
         mtwksDisplayLine("targetConnect(): Target is secured\n");
         int getYesNo = displayDialogue(
               "Device appears to be secure and may \n"
               "only be programmed after a mass erase \n"
               "which completely erases the device.\n\n"
               "Temporarily enable Mass erase?",
               "Device is secured",
               wxYES_NO|wxYES_DEFAULT|wxICON_INFORMATION);
         Logging::print("Setting forceMassErase = %s\n", (getYesNo == wxYES)?"True":"False");
         if (getYesNo == wxYES) {
         Logging::print("Setting forceMassErase\n");
            forceMassErase = true; // Force mass erase & ignore ignore any error before done
            flashProgrammer->getDeviceData()->setEraseOption(DeviceData::eraseMass);
            // Ignore secured error
            rc = BDM_RC_OK;
         }
   }
#endif

   return rc;
}

//! Configure I/O System
//!
//! @param pdcCommSetup unused
//!
//! @return \n
//!     DI_OK              => OK \n
//!     DI_ERR_FATAL       => Error see \ref currentErrorString
//!
USBDM_GDI_API
DiReturnT DiGdiInitIO( pDiCommSetupT pdcCommSetup ) {
   LOGGING;
   Logging::setLoggingLevel(100);
   USBDM_ErrorCode bdmRc;

   Logging::print("pdcCommSetup = %p\n", pdcCommSetup);
   if (pdcCommSetup != NULL) {
      Logging::print("\n"
            "pdcCommSetup                   = %p\n"
            "pdcCommSetup->dccType          = %d\n"
            "pdcCommSetup->fCheckConnection = %s\n",
            pdcCommSetup,
            pdcCommSetup->dccType,
            pdcCommSetup->fCheckConnection?"True":"False"
            );
   }
   mtwksDisplayLine("\n"
         "=============================================\n"
         "  USBDM GDI Version %s\n"
         "=============================================\n",
         USBDM_VERSION_STRING);

#if !defined(useWxWidgets)
#ifdef _WIN32
   // Set up handle on Eclipse Window once only
   setDefaultWindowParent(FindEclipseWindowHwnd());
#endif
#endif

#if TARGET == MC56F80xx
   DSC_SetLogFile(Logging::getLogFileHandle());
#endif
   // Open & Configure BDM
   bdmRc = initialiseBDMInterface();
   if ((bdmRc != BDM_RC_OK)&&(bdmRc != BDM_RC_UNKNOWN_DEVICE)) {
      DiReturnT rc = setErrorState(DI_ERR_COMMUNICATION, bdmRc);
      Logging::print("Failed - %s\n", currentErrorString);
      return rc;
   }
#ifndef USE_MEE
   // Initial connect is treated differently
   bdmRc = initialConnect();
   if (bdmRc != BDM_RC_OK) {
      DiReturnT rc = setErrorState(DI_ERR_COMMUNICATION, bdmRc);
      Logging::print("Failed - %s\n", currentErrorString);
      return rc;
   }
#else
   Logging::print("Doing mtwksSetMEE()\n");
   mtwksSetMEE(1);
#endif
   return setErrorState(DI_OK);
}

//! Initialise Register Name/Number Map
//!
//! @param dnEntries
//! @param pdriRegister
//! @param dnProgramCounter
//!
//! @return \n
//!     DI_OK              => OK \n
//!     DI_ERR_FATAL       => Error see \ref currentErrorString
//!
USBDM_GDI_API
DiReturnT DiGdiInitRegisterMap ( DiUInt32T         dnEntries,
                                 DiRegisterInfoT*  pdriRegister,
                                 DiUInt32T         dnProgramCounter ) {
   Logging::print("DiGdiInitRegisterMap() - not implemented\n");
   return setErrorState(DI_ERR_NOTSUPPORTED);
}

//! Initialise Memory Space Name/Number Map
//!
//! @param dnEntries
//! @param pdmiMemSpace
//!
//! @return \n
//!     DI_OK              => OK \n
//!     DI_ERR_FATAL       => Error see \ref currentErrorString
//!
USBDM_GDI_API
DiReturnT DiGdiInitMemorySpaceMap ( DiUInt32T            dnEntries,
                                    DiMemorySpaceInfoT*  pdmiMemSpace ) {
   Logging::print("DiGdiInitMemorySpaceMap() - not implemented\n");
   return setErrorState(DI_ERR_NOTSUPPORTED);
}

//! 2.2.1.9 Add Callback Procedures
//!
//! @param dcCallbackType
//! @param Callback
//!
//! @return \n
//!     DI_OK              => OK \n
//!     DI_ERR_FATAL       => Error see \ref currentErrorString
//!
USBDM_GDI_API
DiReturnT DiGdiAddCallback ( DiCallbackT dcCallbackType,
                             CallbackF   Callback ) {

	Logging::print("DiGdiAddCallback(type=%4.4X, address=%p)\n",
         dcCallbackType, Callback);

   setCallback(dcCallbackType, Callback);

   return setErrorState(DI_OK);
}

//! 2.2.1.10 Cancel GDI Procedure
//!
//!
//! @return \n
//!     DI_ERR_NOTSUPPORTED  => Error see \ref currentErrorString
//!
USBDM_GDI_API
DiReturnT DiGdiCancel ( void ) {

   Logging::print("DiGdiCancel() - not implemented\n");
   return setErrorState(DI_ERR_NOTSUPPORTED);
}

//! 2.2.1.11 Synchronize UD
//!
//! @param pfUpdate
//!
//! @return \n
//!     DI_ERR_NOTSUPPORTED  => Error see \ref currentErrorString
//!
USBDM_GDI_API
DiReturnT DiGdiSynchronize ( DiBoolT *pfUpdate ) {

   Logging::print("DiGdiSynchronize() - not implemented\n");
   return setErrorState(DI_ERR_NOTSUPPORTED);
}

static DiMenuItemT menuItems[] =
{
/* szMenuTitle  szDiCommand    */
 { "Command 1", "cmd1", },
};

//! 2.2.2.1 Add DI Specific Commands to UD Menu
//!
//! @param pdnNrEntries
//! @param pdmiMenuItem
//!
USBDM_GDI_API
DiReturnT DiDirectAddMenuItem ( DiUInt32T    *pdnNrEntries,
                                 pDiMenuItemT *pdmiMenuItem ) {

   Logging::print("DiDirectAddMenuItem()\n");
   *pdnNrEntries = sizeof(menuItems)/sizeof(menuItems[0]);
   *pdmiMenuItem = menuItems;
   return setErrorState(DI_OK);
}

#define MAX_DIRECT_RETURN_STRING_LENGTH (2000)
static char directCommandResult[MAX_DIRECT_RETURN_STRING_LENGTH+1] = "";
static char *directCommandResultPtr  = NULL;

/*! \brief Provides a print function which prints data to DI_DIRECT message buffer
 *
 *  @param format Format and parameters as for printf()
 */
void setDirectCommandString(const char *format, ...) {
   va_list list;

   if (format == NULL) {
      directCommandResultPtr = NULL;
      return;
   }
   va_start(list, format);
   vsnprintf(directCommandResult, MAX_DIRECT_RETURN_STRING_LENGTH, format, list);
   directCommandResultPtr = directCommandResult;
   va_end(list);
}

#if TARGET != RS08
//! 2.2.2.2 Send Native Command to DI
//!
//! @param pszCommand
//! @param pduiUserInfo
//!
//! @return \n
//!     DI_ERR_NOTSUPPORTED  => Error see \ref currentErrorString
//!
USBDM_GDI_API
DiReturnT DiDirectCommand ( DiConstStringT  pszCommand,
                            DiUserInfoT     *pduiUserInfo ) {

   Logging::print("DiDirectCommand(%s, %p)\n", pszCommand, pduiUserInfo);
   return setErrorState(DI_ERR_NOTSUPPORTED);
}
#endif

//! 2.2.2.3 Read String from DI
//!
//! @param dnNrToRead
//! @param pcBuffer
//! @param dnNrRead
//!
//! @return \n
//!     DI_ERR_NOTSUPPORTED  => Error see \ref currentErrorString
//!
USBDM_GDI_API
DiReturnT DiDirectReadNoWait ( DiUInt32T  dnNrToRead,
                               char        *pcBuffer,
                               DiUInt32T   *dnNrRead ) {

   Logging::print("() - not implemented\n");
   *pcBuffer = '\0';
   *dnNrRead = 0;
   return setErrorState(DI_ERR_NOTSUPPORTED);
}

//! 2.2.3.1 Retrieve Error Message
//!
//! @param pszErrorMsg
//!
USBDM_GDI_API
void DiErrorGetMessage ( DiConstStringT *pszErrorMsg ) {

   *pszErrorMsg = getGDIErrorMessage();

   if (pszErrorMsg == NULL) {
      Logging::print("DiErrorGetMessage() => not set\n");
   }
   else {
      Logging::print("DiErrorGetMessage() => %s\n", *pszErrorMsg);
   }
   mtwksDisplayLine("DiErrorGetMessage() => %s\n", getGDIErrorMessage());

   // Clear all errors apart from fatal
   if (currentError != DI_ERR_FATAL) {
      setErrorState(DI_OK);
   }
}

//! 2.2.4.1 Configure Target Memory
//!
//! @param dmmMap
//!
//! @return \n
//!     DI_ERR_NOTSUPPORTED  => Error see \ref currentErrorString
//!
USBDM_GDI_API
DiReturnT DiMemorySetMap ( DiMemoryMapT dmmMap ) {

   Logging::print("DiMemorySetMap() - not implemented\n");
   return setErrorState(DI_ERR_NOTSUPPORTED);
}

//! 2.2.4.2 Retrieve Target Memory Configuration
//!
//! @param pdmmMap
//!
//! @return \n
//!     DI_ERR_NOTSUPPORTED  => Error see \ref currentErrorString
//!
USBDM_GDI_API
DiReturnT DiMemoryGetMap ( DiMemoryMapT *pdmmMap ) {

   Logging::print("DiMemoryGetMap() - not implemented\n");
   return setErrorState(DI_ERR_NOTSUPPORTED);
}

//! 2.2.4.3 Configure CPU to Memory Interface
//!
//! @param dmtcMap
//!
//! @return \n
//!     DI_ERR_NOTSUPPORTED  => Error see \ref currentErrorString
//!
USBDM_GDI_API
DiReturnT DiMemorySetCpuMap ( DiMemoryToCpuT dmtcMap ) {

   Logging::print("DiMemorySetCpuMap() - not implemented\n");
   return setErrorState(DI_ERR_NOTSUPPORTED);
}

//! 2.2.4.4 Retrieve CPU to Memory Interface
//!
//! @param pdmtcMap
//!
//! @return \n
//!     DI_ERR_NOTSUPPORTED  => Error see \ref currentErrorString
//!
USBDM_GDI_API
DiReturnT DiMemoryGetCpuMap ( DiMemoryToCpuT *pdmtcMap ) {

   Logging::print("DiMemoryGetCpuMap() - not implemented\n");
   return setErrorState(DI_ERR_NOTSUPPORTED);
}

#ifdef FLASH_PROGRAMMING

DiReturnT programTargetFlash(void) {
   LOGGING;
   USBDM_ErrorCode rc;

   // Set BDM options for programming
   USBDM_SetExtendedOptions(&bdmProgrammingOptions);

#if TARGET == MC56F80xx
   DSC_Initialise();
   rc = DSC_TargetReset((TargetMode_t)(RESET_DEFAULT|RESET_SPECIAL));
#endif

   if (flashProgrammer == NULL) {
      mtwksDisplayLine("programTargetFlash() - flashProgrammer NULL\n");
      return setErrorState(DI_ERR_FATAL, "flashProgrammer NULL!");
   }
//   USBDM_ErrorCode flashRC = flashProgrammer->confirmSDID();
//
//   // ToDo - check if should just fail on RS08/HCS08 as SDID should always be readable
//   if (flashRC == PROGRAMMING_RC_ERROR_WRONG_SDID) {
//      // Target is wrong device
//      displayDialogue("Selected device doesn't agree with connected device.",
//                      "Wrong Device",
//                      wxOK|wxICON_ERROR);
//      return setErrorState(DI_ERR_FATAL, flashRC);
//   }
//   if (flashRC != PROGRAMMING_RC_OK) {
//      // Failed to read target type - may  be secured
//      int getYesNo = displayDialogue("Cannot confirm target device type.\n"
//                                     "Device may be secured.\n\n"
//                                     "Continue?",
//                                     "Wrong Device",
//                                  wxYES_NO|wxYES_DEFAULT|wxICON_WARNING);
//      if (getYesNo != wxYES)
//         return setErrorState(DI_ERR_FATAL, flashRC);
//   }
   // Program target
   rc = flashProgrammer->programFlash(flashImage, NULL, true);
   if (rc != PROGRAMMING_RC_OK) {
      return setErrorState(DI_ERR_FATAL, rc);
   }
   return DI_OK;
}
#endif

//! 2.2.5.1 Download Application
//!
//! @param fUseAuxiliaryPath
//! @param ddcDownloadCommand
//! @param ddfDownloadFormat
//! @param pchBuffer
//!
//! @note DSC/CFVx GDI doesn't support flash programming
//!
#ifndef FLASH_PROGRAMMING
USBDM_GDI_API
DiReturnT DiMemoryDownload ( DiBoolT            fUseAuxiliaryPath,
                             DiDownloadCommandT ddcDownloadCommand,
                             DiDownloadFormatT  ddfDownloadFormat,
                             char               *pchBuffer ) {
   return setErrorState(DI_ERR_NOTSUPPORTED);
}
#else
USBDM_GDI_API
DiReturnT DiMemoryDownload ( DiBoolT            fUseAuxiliaryPath,
                             DiDownloadCommandT ddcDownloadCommand,
                             DiDownloadFormatT  ddfDownloadFormat,
                             char               *pchBuffer ) {
   LOGGING;
   const char *command[] = {"DI_DNLD_INIT","DI_DNLD_WRITE","DI_DNLD_TERMINATE","DI_DNLD_ABORT"};
   U32c address(ddfDownloadFormat.daAddress);
   DiReturnT rc;
   Logging::print("DiMemoryDownload() - aux=%s, comm=%s, format=0x%X, addr=0x%4.4X, size 0x%X\n",
         fUseAuxiliaryPath?"T":"F",
         command[ddcDownloadCommand&0x03],
         ddfDownloadFormat.dafAbsFileFormat,
         (uint32_t)address,
         ddfDownloadFormat.dnBufferSize);

   CHECK_ERROR_STATE();

   if (!programmingSupported) {
      return setErrorState(DI_ERR_NOTSUPPORTED);
   }
   if ((ddfDownloadFormat.dafAbsFileFormat & (DI_ABSF_FILENAME|DI_ABSF_BINARY)) == 0) {
      Logging::print("DiMemoryDownload() - unsupported format %X\n",
            ddfDownloadFormat.dafAbsFileFormat);
      return setErrorState(DI_ERR_NOTSUPPORTED);
   }
   switch (ddcDownloadCommand) {
      case DI_DNLD_INIT:
         mtwksDisplayLine("DiMemoryDownload() - DI_DNLD_INIT - New memory image opened\n");
         Logging::print("DiMemoryDownload() - DI_DNLD_INIT - New memory image opened\n");
         // Create flash image to contain any changes to target memory
         flashImage = new FlashImage();
         return setErrorState(DI_OK);

      case DI_DNLD_TERMINATE:
         rc = DI_OK;
         if ((flashImage != NULL) && (!flashImage->isEmpty() || forceMassErase)) {
            mtwksDisplayLine("DiMemoryDownload() - DI_DNLD_TERMINATE - Programming memory image...\n");
            Logging::print("DiMemoryDownload() - DI_DNLD_TERMINATE - Programming memory image\n");
            rc = programTargetFlash();
            mtwksDisplayLine("DiMemoryDownload() - DI_DNLD_TERMINATE - Programming complete, rc = %d\n", rc);
         }
         else {
            mtwksDisplayLine("DiMemoryDownload() - DI_DNLD_TERMINATE - Memory image is empty - Flash programming skipped\n");
            Logging::print("DiMemoryDownload() - DI_DNLD_TERMINATE - Memory image is empty - Flash programming skipped\n");
         }
         delete flashImage;
         flashImage = NULL;
         if (rc != DI_OK) {
            return rc;
         }
         // Restore original options
         USBDM_SetExtendedOptions(&bdmOptions);
         mtwksDisplayLine("DiMemoryDownload() - DI_DNLD_TERMINATE - Resetting target\n");
         Logging::print("DiMemoryDownload() -  Resetting target\n");
#if TARGET == MC56F80xx
         DSC_TargetReset((TargetMode_t)(RESET_DEFAULT|RESET_SPECIAL));
         DSC_Connect();
#else
         USBDM_TargetReset((TargetMode_t)(RESET_DEFAULT|RESET_SPECIAL));
         USBDM_Connect();
#endif
         forceMassErase = false;
         return setErrorState(DI_OK);
      case DI_DNLD_WRITE:
         Logging::print("DI_DNLD_WRITE\n");
         return setErrorState(DI_ERR_NOTSUPPORTED);
      case DI_DNLD_ABORT:
         Logging::print("DI_DNLD_ABORT\n");
         delete flashImage;
         flashImage = NULL;
         break;
   }
   return setErrorState(DI_OK);
}
#endif

// This is largest size memory read/write block passed to USBDM layer
// Larger sizes are subdivided - but this is unlikely to occur.
#define MAX_BLOCK_SIZE (0x1000)
#if (MAX_BLOCK_SIZE % 4) != 0
#error "MAX_BLOCK_SIZE must be a multiple of 4"
#endif

static uint8_t memoryReadWriteBuffer[MAX_BLOCK_SIZE];

#if TARGET == ARM
//! Write data to target memory
//! ARM Memory writes are decomposed into a minimal sequence of the largest aligned object writes
//!
//! @param memorySpace = Size of data elements (1/2/4 bytes)
//! @param byteCount   = Number of _bytes_ to transfer
//! @param address     = Memory address
//! @param buffer      = Ptr to block of data to write
//!
//! @return error code \n
//!     BDM_RC_OK    => OK \n
//!     other        => Error code - see \ref USBDM_ErrorCode
//!
static USBDM_ErrorCode ARM_WriteMemory(MemorySpace_t memorySpace, int byteCount, int address, uint8_t *buffer) {
   USBDM_ErrorCode rc = BDM_RC_OK;
   int index = 0;
   if ((address&0x01) != 0) {
      // Odd leading byte
      rc = USBDM_WriteMemory(MS_Byte, 1, address, buffer+index);
      address++;
      index++;
      byteCount--;
   }
   if ((rc == BDM_RC_OK) && ((address&0x02) != 0) && (byteCount >= 2)) {
      // Odd leading word
      rc = USBDM_WriteMemory(MS_Word, 2, address, buffer+index);
      address      += 2;
      index        += 2;
      byteCount    -= 2;
   }
   if ((rc == BDM_RC_OK) && (byteCount >= 4)) {
      // Bulk longwords
      int longWordCount = byteCount&~0x3;
      rc = USBDM_WriteMemory(MS_Long, longWordCount, address, buffer+index);
      address += longWordCount;
      index        += longWordCount;
      byteCount    -= longWordCount;
   }
   if ((rc == BDM_RC_OK) && (byteCount >= 2)) {
      // Odd trailing word
      rc = USBDM_WriteMemory(MS_Word, 2, address, buffer+index);
      address      += 2;
      index        += 2;
      byteCount    -= 2;
   }
   if ((rc == BDM_RC_OK) && (byteCount >= 1)) {
      // Odd trailing byte
      rc = USBDM_WriteMemory(MS_Byte, 1, address, buffer+index);
      address++;
      index++;
      byteCount--;
   }
   if ((rc == BDM_RC_OK) && (byteCount > 0)) {
      rc = PROGRAMMING_RC_ERROR_INTERNAL_CHECK_FAILED;
   }
   return rc;
}
#endif

//! 2.2.5.2 Write Data to Target Memory
//!
//! @param daTarget        Target memory address
//! @param pdmvBuffer      Data to write (LittleEndian on DSC)
//! @param dnBufferItems   Number of units to write
//!
USBDM_GDI_API
DiReturnT DiMemoryWrite ( DiAddrT       daTarget,
                          DiMemValueT  *pdmvBuffer,
                          DiUInt32T     dnBufferItems ) {
uint32_t        address      = (U32c)daTarget;   // Load address
MemorySpace_t   memorySpace;                     // Memory space & size
int             organization;                    // For display
uint32_t        endAddress;                      // End address

   CHECK_ERROR_STATE();

   switch(daTarget.dmsMemSpace) {
      case 0x13 : // (word address, elements=2bytes)
         memorySpace  = MS_XByte;
         organization = BYTE_ADDRESS|BYTE_DISPLAY;
         address     *= 2; // Change to byte address
         endAddress   = address + 2*dnBufferItems - 1;
         break;
      case 0x10 : // (cw)
      case 0x14 : // (cw-e, word address, elements=2bytes)
         memorySpace  = MS_XWord;
         organization = WORD_ADDRESS|WORD_DISPLAY;
         endAddress   = address + dnBufferItems - 1;
         break;
      case 0x15 :
         memorySpace  = MS_XLong;
         organization = WORD_ADDRESS|LONG_DISPLAY;
         endAddress   = address + 2*dnBufferItems - 1;
         break;
      case 0x11 : // (cw-e, word address, elements=2bytes)
      case 0x17 :
         memorySpace  = MS_PWord;
         organization = WORD_ADDRESS|WORD_DISPLAY;
         endAddress   = address + dnBufferItems - 1;
         break;
      default :
         Logging::print("DiMemoryWrite(daTarget.dmsMemSpace=0x%X, address=0x%4.4X, dnBufferItems=%d)\n"
               "Error - Unexpected daTarget.dmsMemSpace value\n",
               daTarget.dmsMemSpace,
               (uint32_t)address,
			      dnBufferItems);
         return setErrorState(DI_ERR_NOTSUPPORTED, "Unknown memory space");
   }
   Logging::print("DiMemoryWrite(daTarget.dmsMemSpace=%2X, dnBufferItems=%3d, [0x%06X...0x%06X])\n",
         daTarget.dmsMemSpace,
         dnBufferItems,
         address,
         endAddress);

#if defined(LOG) && 0
   Logging::print("DiMemoryWrite                                              0011223344556677\n"
         "daTarget.dmsMemSpace = %X, daTarget.dlaLinAddress.v.val = 0x%02X%02X%02X%02X%02X%02X%02X%02X,"
         " dnBufferItems = %d\n",
         daTarget.dmsMemSpace,
         daTarget.dlaLinAddress.v.val[0],daTarget.dlaLinAddress.v.val[1],
         daTarget.dlaLinAddress.v.val[2],daTarget.dlaLinAddress.v.val[3],
         daTarget.dlaLinAddress.v.val[4],daTarget.dlaLinAddress.v.val[5],
         daTarget.dlaLinAddress.v.val[6],daTarget.dlaLinAddress.v.val[7],
         dnBufferItems);

   for (unsigned t=0; t<dnBufferItems; t++) {
   Logging::print("DiMemoryWrite                  0011223344556677\n"
         "pdmvBuffer[%2d].dmValue.val = 0x%02X%02X%02X%02X%02X%02X%02X%02X\n",
         t,
         pdmvBuffer[t].dmValue.val[0],pdmvBuffer[t].dmValue.val[1],
         pdmvBuffer[t].dmValue.val[2],pdmvBuffer[t].dmValue.val[3],
         pdmvBuffer[t].dmValue.val[4],pdmvBuffer[t].dmValue.val[5],
         pdmvBuffer[t].dmValue.val[6],pdmvBuffer[t].dmValue.val[7]
         );
   if (t >= 4)
      break;
   }
#endif
   uint32_t offset = 0;
   while (dnBufferItems>0) {
      // Copy a block of data to memoryReadWriteBuffer
      uint32_t writeAddress = address;

      // Copy until buffer full or complete
      unsigned sub = 0;
      while ((sub<sizeof(memoryReadWriteBuffer)) && (dnBufferItems>0)) {
         U32c value(pdmvBuffer[offset++]);
         dnBufferItems--;
         // Unpack items
         switch (memorySpace&MS_SIZE) {
            case MS_Byte :
               memoryReadWriteBuffer[sub++] = value[2];
               memoryReadWriteBuffer[sub++] = value[3];
               address += 2; // Advance byte address
               break;
            case MS_Word :
               memoryReadWriteBuffer[sub++] = value[2];
               memoryReadWriteBuffer[sub++] = value[3];
               address += 1; // Advance word address
               break;
            case MS_Long :
               memoryReadWriteBuffer[sub++] = value[0];
               memoryReadWriteBuffer[sub++] = value[1];
               memoryReadWriteBuffer[sub++] = value[2];
               memoryReadWriteBuffer[sub++] = value[3];
               address += 2; // Advance word address
               break;
         }
      }
      Logging::printDump(memoryReadWriteBuffer, sub, address, organization);

      bool writeDone = false;
#ifdef FLASH_PROGRAMMING
      int marker = 0;
      if (flashImage != NULL) {
         // Write data to programming buffer
         Logging::print("DiMemoryWrite() - loadDataBytes(address=0x%06X, size=0x%04X)\n", writeAddress, sub);
//         printDump(memoryReadWriteBuffer,sub,writeAddress,memorySpace&MS_SIZE);
         if ((memorySpace&MS_SPACE) == MS_Data) {
            marker = FlashImage::DataOffset; // Uses offset to indicate data memory space
         }
         USBDM_ErrorCode rc = flashImage->loadDataBytes(sub, writeAddress|marker, memoryReadWriteBuffer, true);
         writeDone = true;
         if (rc != BDM_RC_OK) {
            return setErrorState(DI_ERR_FATAL, rc);
         }
      }
#endif
      if (!writeDone) {
         // Write data directly to memory
         USBDM_ErrorCode rc = BDM_RC_OK;
#if TARGET == ARM
         // ARM is special case - ignore write size and use longs!
         rc = ARM_WriteMemory(memorySpace, sub, writeAddress, memoryReadWriteBuffer);
#elif TARGET == MC56F80xx
         rc = DSC_WriteMemory(memorySpace, sub, writeAddress, memoryReadWriteBuffer);
#else
         rc = USBDM_WriteMemory(memorySpace, sub, writeAddress, memoryReadWriteBuffer);
#endif
         if (rc != BDM_RC_OK) {
            return setErrorState(DI_ERR_NONFATAL, rc);
         }
      }
   }
   return setErrorState(DI_OK);
}

#if TARGET == ARM
//! Read data from target memory
//! ARM Memory reads are decomposed into a minimal sequence of the largest aligned object reads
//!
//! @param memorySpace = Size of data (1/2/4 bytes)
//! @param byteCount   = Number of bytes to transfer
//! @param address     = Memory address
//! @param buffer      = Where to place data
//!
//! @return error code \n
//!     BDM_RC_OK    => OK \n
//!     other        => Error code - see \ref USBDM_ErrorCode
//!
static USBDM_ErrorCode ARM_ReadMemory(MemorySpace_t memorySpace, int byteCount, int address, uint8_t *buffer) {
   USBDM_ErrorCode rc = BDM_RC_OK;
   int index = 0;
   if ((address&0x01) != 0) {
      // Odd leading byte
      rc = USBDM_ReadMemory(MS_Byte, 1, address, buffer+index);
      address++;
      index++;
      byteCount--;
   }
   if ((rc == BDM_RC_OK) && ((address&0x02) != 0) && (byteCount >= 2)) {
      // Odd leading word
      rc = USBDM_ReadMemory(MS_Word, 2, address, buffer+index);
      address   += 2;
      index     += 2;
      byteCount -= 2;
   }
   if ((rc == BDM_RC_OK) && (byteCount >= 4)) {
      // Bulk longwords
      int longWordCount = byteCount&~0x3;
      rc = USBDM_ReadMemory(MS_Long, longWordCount, address, buffer+index);
      address   += longWordCount;
      index     += longWordCount;
      byteCount -= longWordCount;
   }
   if ((rc == BDM_RC_OK) && (byteCount >= 2)) {
      // Odd trailing word
      rc = USBDM_ReadMemory(MS_Word, 2, address, buffer+index);
      address   += 2;
      index     += 2;
      byteCount -= 2;
   }
   if ((rc == BDM_RC_OK) && (byteCount >= 1)) {
      // Odd trailing byte
      rc = USBDM_ReadMemory(MS_Byte, 1, address, buffer+index);
      address++;
      index++;
      byteCount--;
   }
   if ((rc == BDM_RC_OK) && (byteCount > 0)) {
      rc = PROGRAMMING_RC_ERROR_INTERNAL_CHECK_FAILED;
   }
   return rc;
}
#endif

//! 2.2.5.3 Read Data from Target Memory
//!
//! @param daTarget
//! @param pdmvBuffer
//! @param dnBufferItems
//!
//! @note On DSC the daTarget is a word address
//!
USBDM_GDI_API
DiReturnT DiMemoryRead ( DiAddrT       daTarget,
                         DiMemValueT   *pdmvBuffer,
                         DiUInt32T     dnBufferItems ) {
uint32_t        address      = (U32c)daTarget;   // Load address
MemorySpace_t   memorySpace;                     // Memory space & size
uint32_t        endAddress;                      // End address
int             organization;
uint32_t        numBytes;                        // Number of bytes to transfer

   CHECK_ERROR_STATE();

   switch(daTarget.dmsMemSpace) {
      case 0x13 :  // (cw-e, word address, elements=2bytes)
         memorySpace  = MS_XByte;
         organization = BYTE_ADDRESS|BYTE_DISPLAY;
         address     *= 2; // Change to byte address
         endAddress   = address + 2*dnBufferItems - 1;
         numBytes     = 2*dnBufferItems;
         break;
      case 0x10 :  // (cw)
      case 0x14 :  // (cw-e, word address, elements=2bytes)
         memorySpace  = MS_XWord;
         organization = WORD_ADDRESS|WORD_DISPLAY;
         endAddress   = address + dnBufferItems - 1;
         numBytes     = 2*dnBufferItems;
         break;
      case 0x15 :
         memorySpace  = MS_XLong;
         organization = WORD_ADDRESS|LONG_DISPLAY;
         endAddress   = address + 2*dnBufferItems - 1;
         numBytes     = 4*dnBufferItems;
         break;
      case 0x11 :  // (cw-e, word address, elements=2bytes)
      case 0x17 :
         memorySpace  = MS_PWord;
         organization = WORD_ADDRESS|WORD_DISPLAY;
         endAddress   = address + dnBufferItems - 1;
         numBytes     = 2*dnBufferItems;
         break;
      default :
         Logging::print("DiMemoryRead(daTarget.dmsMemSpace=0x%X, address=0x%4.4X, dnBufferItems=%d)\n"
               "Error - Unexpected daTarget.dmsMemSpace value\n",
               daTarget.dmsMemSpace,
               (uint32_t)address,
               dnBufferItems);
         return setErrorState(DI_ERR_NOTSUPPORTED, "Unknown memory space");
   }
   Logging::print("DiMemoryRead(daTarget.dmsMemSpace=%X, dnBufferItems=%d, [0x%06X...0x%06X])\n",
         daTarget.dmsMemSpace,
         dnBufferItems,
         address,
         endAddress);

   CHECK_ERROR_STATE();
   (void)organization;
   unsigned offset = 0;
   while (dnBufferItems>0) {
      uint32_t blockSize = numBytes;
      if (blockSize > sizeof(memoryReadWriteBuffer)) {
         blockSize = sizeof(memoryReadWriteBuffer);
      }
USBDM_ErrorCode rc = BDM_RC_OK;
#if TARGET == ARM
      rc = ARM_ReadMemory(memorySpace, blockSize, address, memoryReadWriteBuffer);
#elif TARGET == MC56F80xx
      rc = DSC_ReadMemory(memorySpace, blockSize, address, memoryReadWriteBuffer);
#else
      rc = USBDM_ReadMemory(memorySpace, blockSize, address, memoryReadWriteBuffer);
#endif		 
      if (rc != BDM_RC_OK) {
         Logging::print("DiMemoryRead(...) - Failed, rc= %s\n", USBDM_GetErrorString(rc));
         return setErrorState(DI_ERR_NONFATAL, rc);
      }
//      Logging::print("DiMemoryRead(...) - Result\n");
//      Logging::printDump(memoryBuffer, blockSize, address);

      // Copy until buffer full or complete
      uint32_t sub = 0;
      while (sub<blockSize) {
         uint8_t v1=0,v2=0,v3=0,v4=0;
         switch (memorySpace&MS_SIZE) {
         case MS_Byte :
            v3 = memoryReadWriteBuffer[sub++];
            v4 = memoryReadWriteBuffer[sub++];
            address += 2; // Increment byte address
            break;
         case MS_Word :
            v3 = memoryReadWriteBuffer[sub++];
            v4 = memoryReadWriteBuffer[sub++];
            address += 1; // Increment word address
            break;
         case MS_Long :
            v1 = memoryReadWriteBuffer[sub++];
            v2 = memoryReadWriteBuffer[sub++];
            v3 = memoryReadWriteBuffer[sub++];
            v4 = memoryReadWriteBuffer[sub++];
            address += 2; // Increment word address
            break;
         }
         // Pack item
         pdmvBuffer[offset++] = (U32c)((v1<<24)+(v2<<16)+(v3<<8)+v4);
         dnBufferItems--;
      }
      numBytes -= blockSize;
   }
   assert(numBytes == 0);
   return setErrorState(DI_OK);
}

#if TARGET == ARM
// ARM Register numbers used by Codewarrior
//
#define arm_RegPRIMASK 19
#define arm_RegFAULTMASK  20
#define arm_RegBASEPRI 21
#define arm_RegCONTROL 22
#endif

//! 2.2.6.1 Write Value to Register
//!
//! @param dnRegNumber
//! @param drvValue
//!
USBDM_GDI_API
DiReturnT DiRegisterWrite ( DiUInt32T        dnRegNumber,
                            DiRegisterValueT drvValue ) {
   U32c            regValue(drvValue);
   DSC_Registers_t regNum = mapReg(dnRegNumber);
   USBDM_ErrorCode rc = BDM_RC_OK;

   Logging::print("DiRegisterWrite(0x%X,%s) => %lX\n", dnRegNumber, DSC_GetRegisterName(regNum), (uint32_t)regValue);

   CHECK_ERROR_STATE();

   if ((regNum == DSC_RegPC) && !pcWritten) {
      pcWritten    = true;
      pcResetValue = regValue;
   }
   if (regNum == DSC_UnknownReg) {
//      rc = BDM_RC_OK;
      rc = BDM_RC_ILLEGAL_PARAMS;
   }
   else {
      rc = DSC_WriteRegister(regNum, (uint32_t)regValue);
   }
   if (rc != BDM_RC_OK) {
      Logging::print("DiRegisterWrite(0x%X,%s) Failed, reason= %s\n",
            dnRegNumber, DSC_GetRegisterName(regNum), USBDM_GetErrorString(rc));
      return setErrorState(DI_ERR_NONFATAL, rc);
   }
   return setErrorState(DI_OK);
}

//! 2.2.6.2 Read Value from Register
//!
//! @param dnRegNumber
//! @param drvValue
//!
USBDM_GDI_API
DiReturnT DiRegisterRead ( DiUInt32T         dnRegNumber,
                           pDiRegisterValueT drvValue ) {
unsigned long dataValue = 0xDEADBEEF;
DSC_Registers_t regNum = mapReg(dnRegNumber);
USBDM_ErrorCode rc = BDM_RC_OK;

//   Logging::print("DiRegisterRead(reg# 0x%X(%d))\n", regNum, regNum);
//   Logging::print("DiRegisterRead(%s)\n", DSC_GetRegisterName(regNum));

   CHECK_ERROR_STATE();

   if (regNum == DSC_UnknownReg) {
      dataValue = dnRegNumber;
//      rc = BDM_RC_OK;
      rc = BDM_RC_ILLEGAL_PARAMS;
   }
   else {
      rc = DSC_ReadRegister(regNum, &dataValue);
   }
   if (rc != BDM_RC_OK) {
      Logging::print("DiRegisterRead(0x%X,%s) Failed, reason= %s\n",
            dnRegNumber, DSC_GetRegisterName(regNum), USBDM_GetErrorString(rc));
      return setErrorState(DI_ERR_NONFATAL, rc);
   }
   *drvValue = (U32c)dataValue;
   Logging::print("DiRegisterRead(0x%X,%s) => %lX\n", dnRegNumber, DSC_GetRegisterName(regNum), dataValue);
   return setErrorState(DI_OK);
}

//! 2.2.6.3 Create Register Class
//!
//! @param pdnRegClassId
//! @param pdnRegisterId
//! @param dnRegisterIdEntries
//!
USBDM_GDI_API
DiReturnT DiRegisterClassCreate ( DiUInt32T *pdnRegClassId,
                                  DiUInt32T *pdnRegisterId,
                                  DiUInt32T dnRegisterIdEntries ) {

   Logging::print("DiRegisterClassCreate() - not implemented\n");
   return setErrorState(DI_ERR_NOTSUPPORTED);
}

//! 2.2.6.4 Delete Register Class
//!
//! @param dnRegClassId
//!
USBDM_GDI_API
DiReturnT DiRegisterClassDelete ( DiUInt32T dnRegClassId ) {

   Logging::print("DiRegisterClassDelete() - not implemented\n");
   return setErrorState(DI_ERR_NOTSUPPORTED);
}


//! 2.2.6.5 Write Value to Register Class
//!
//! @param dnRegClassId
//! @param pdrvClassValue
//!
USBDM_GDI_API
DiReturnT DiRegisterClassWrite ( DiUInt32T         dnRegClassId,
                                  DiRegisterValueT  *pdrvClassValue ) {

   Logging::print("DiRegisterClassWrite() - not implemented\n");
   return setErrorState(DI_ERR_NOTSUPPORTED);
}

//! 2.2.6.6 Read Value from Register Class
//!
//! @param dnRegClassId
//! @param pdrvClassValue
//!
USBDM_GDI_API
DiReturnT DiRegisterClassRead ( DiUInt32T          dnRegClassId,
                                 DiRegisterValueT   *pdrvClassValue ) {

   Logging::print("DiRegisterClassRead() - not implemented\n");
   return setErrorState(DI_ERR_NOTSUPPORTED);
}

//!  2.2.7.1 Set Breakpoint
//!
//! @param pdnBreakpointId
//! @param dbBreakpoint
//!
USBDM_GDI_API
DiReturnT DiBreakpointSet ( DiBpResultT *pdnBreakpointId,
                             DiBpT        dbBreakpoint ) {

   Logging::print("DiBreakpointSet() - not implemented\n");
   return setErrorState(DI_ERR_NOTSUPPORTED);
}

//!  2.2.7.2 Clear a Breakpoint
//!
//! @param dnBreakpointId
//!
USBDM_GDI_API
DiReturnT DiBreakpointClear ( DiUInt32T dnBreakpointId ) {

   Logging::print("DiBreakpointClear() - not implemented\n");
   return setErrorState(DI_ERR_NOTSUPPORTED);
}

//!  2.2.7.3 Clear All Breakpoints
//!
USBDM_GDI_API
DiReturnT DiBreakpointClearAll ( void ) {

   Logging::print("DiBreakpointClearAll() - not implemented\n");
   return setErrorState(DI_ERR_NOTSUPPORTED);
}

//!  2.2.8.1 Reset
//!
USBDM_GDI_API
DiReturnT DiExecResetChild ( void ) {
USBDM_ErrorCode rc;
   LOGGING;

   CHECK_ERROR_STATE();

   // ToDo: should implement a more sophisticated strategy?
#if TARGET == MC56F80xx
   rc = DSC_TargetReset((TargetMode_t)(RESET_DEFAULT|RESET_SPECIAL));
   if (rc != BDM_RC_OK) {
      return setErrorState(DI_ERR_NONFATAL, rc);
   }
   rc = DSC_Connect();
#elif TARGET == ARM
   rc = USBDM_TargetReset((TargetMode_t)(RESET_DEFAULT|RESET_SPECIAL));
   if (rc != BDM_RC_OK) {
      rc = USBDM_TargetReset((TargetMode_t)(RESET_HARDWARE|RESET_SPECIAL));
   }
   if (rc != BDM_RC_OK) {
      return setErrorState(DI_ERR_NONFATAL, rc);
   }
   rc = USBDM_Connect();
#elif (TARGET == CFV1) || (TARGET == HCS08) || (TARGET == RS08)
   USBDM_TargetHalt(); // Make sure target is awake - may be sleeping due to STOP/WAIT instruction
   USBDM_TargetReset((TargetMode_t)(RESET_DEFAULT|RESET_SPECIAL));
   rc = targetConnect(initialConnectOptions);
#else
   rc = USBDM_TargetReset((TargetMode_t)(RESET_DEFAULT|RESET_SPECIAL));
   if (rc != BDM_RC_OK) {
      return setErrorState(DI_ERR_NONFATAL, rc);
   }
   rc = USBDM_Connect();
   // Read status to clear reset flag
   USBDMStatus_t dummy;
   USBDM_GetBDMStatus(&dummy);
#endif
   if (rc != BDM_RC_OK) {
      return setErrorState(DI_ERR_NONFATAL, rc);
   }
   // Reset into special mode doesn't correctly set the reset vector on these targets
   // Do manually
   if (pcWritten) {
#if TARGET == MC56F80xx
      Logging::print("Fixing initial PC = 0x%08X\n", pcResetValue);
      DSC_WriteRegister(DSC_RegPC, pcResetValue);
#elif (TARGET == CFV1)
      Logging::print("Fixing initial PC = 0x%08X\n", pcResetValue);
      USBDM_WriteCReg(CFV1_CRegPC, pcResetValue);
#elif TARGET == HCS08
      Logging::print("Fixing initial PC = 0x%08X\n", pcResetValue);
      USBDM_WriteReg(HCS08_RegPC, pcResetValue);
#endif
   }
   return setErrorState(DI_OK);
}

//!  2.2.8.2 Execute a Single Step
//!
//! @param dnNrInstructions
//!
USBDM_GDI_API
DiReturnT DiExecSingleStep ( DiUInt32T dnNrInstructions ) {
USBDM_ErrorCode BDMrc;

   Logging::print("DiExecSingleStep(%d)\n", dnNrInstructions);

   CHECK_ERROR_STATE();

#if (TARGET == MC56F80xx)
   BDMrc = DSC_TargetStepN(dnNrInstructions);
#else
   if (dnNrInstructions>1) {
      Logging::print("DiExecSingleStep() - Only a single step is supported!\n");
      return setErrorState(DI_ERR_PARAM, ("Only a single step is allowed"));
   }
   BDMrc = USBDM_TargetStep();
#endif

   if (BDMrc != BDM_RC_OK) {
      return setErrorState(DI_ERR_NONFATAL, BDMrc);
   }
   return setErrorState(DI_OK);
}

//!  2.2.8.3 Continue Execution Until
//!
//! @param addrUntil
//!
USBDM_GDI_API
DiReturnT DiExecContinueUntil ( DiAddrT addrUntil ) {

   Logging::print("DiExecContinueUntil() - not implemented\n");
   return setErrorState(DI_ERR_NOTSUPPORTED);
}

//!  2.2.8.4 Continue Execution
//!
USBDM_GDI_API
DiReturnT DiExecContinue ( void ) {
   USBDM_ErrorCode BDMrc;

   Logging::print("DiExecContinue()\n");

   CHECK_ERROR_STATE();

#if TARGET == CFV1
   // Catch Address/Illegal instruction errors as BDM halts
   USBDM_WriteDReg(CFV1_DRegCSR2byte, CFV1_CSR2_COPHR|CFV1_CSR2_IADHR|CFV1_CSR2_IOPHR);
#endif
#if TARGET == CFVx
   // Fix in case actually STOPPED but CW doesn't know it
   USBDM_TargetHalt();
#endif
#if TARGET == MC56F80xx
   BDMrc = DSC_TargetGo();
#else
   BDMrc = USBDM_TargetGo();
   if (BDMrc != BDM_RC_OK) {
      targetConnect(softConnectOptions);
      BDMrc = USBDM_TargetGo();
   }
#endif
   // Ignore BDM_RC_CF_ILLEGAL_COMMAND as may occur when in STOP low-power modes
   if ((BDMrc != BDM_RC_OK) && (BDMrc != BDM_RC_CF_ILLEGAL_COMMAND)) {
      return setErrorState(DI_ERR_NONFATAL, BDMrc);
   }
   return setErrorState(DI_OK);
}

//!  2.2.8.5 Continue Execution in Background
//!
USBDM_GDI_API
DiReturnT DiExecContinueBackground ( void ) {

   Logging::print("DiExecContinueBackground() - not implemented\n");
   return setErrorState(DI_ERR_NOTSUPPORTED);
}

//!  2.2.8.6 Get DI Execution/Exit Status
//!
//! @param pdesExitStatus
//!
//! @return \n
//!     DI_OK              => OK \n
//!     DI_ERR_FATAL       => Error see \ref currentErrorString
//!
USBDM_GDI_API
DiReturnT DiExecGetStatus ( pDiExitStatusT pdesExitStatus ) {
   LOGGING;
   USBDM_ErrorCode BDMrc;
   static DiExitCauseT lastStatus = DI_WAIT_USER;
   static int pollCount = 0;
   OnceStatus_t onceStatus;

//   Logging::print("DiExecGetStatus()\n");

   // Defaults
   pdesExitStatus->dscCause = DI_WAIT_UNKNOWN;
   pdesExitStatus->dwBpId   = 0x1000400; // bkpt ID?
   pdesExitStatus->szReason = (DiStringT)"unknown state";

// Removed as prevents CW retry strategy
//   CHECK_ERROR_STATE();

   // Update status or autoconnect (includes status update)
//   if (bdmOptions.autoReconnect)
//      DIrc = targetConnect();
//   else
//      DIrc = getBDMStatus(&USBDMStatus);

//   if (DIrc != DI_OK) {
//      Logging::print("DiExecGetStatus()=> connect()/getStatus() failed\n");
//      return DIrc;
//   }

   pdesExitStatus->szReason = (DiStringT)getBDMStatusName(&USBDMStatus);

   BDMrc = DSC_GetStatus(&onceStatus);
   if (BDMrc != BDM_RC_OK) {
      Logging::print("DiExecGetStatus() - Failed, BDMrc=%s\n", USBDM_GetErrorString(BDMrc));
      return setErrorState(DI_ERR_NONFATAL, BDMrc);
   }

   switch (onceStatus) {
   case stopMode:
   case debugMode:
   case unknownMode:
   default:
      // Halted - in debug halted mode
      pdesExitStatus->dscCause = DI_WAIT_UNKNOWN; // for DSC
      pdesExitStatus->szReason = (DiStringT)"Debug Halted";
      if ((lastStatus != pdesExitStatus->dscCause) || (pollCount++>20)) {
         pollCount = 0;
         Logging::print("DiExecGetStatus() - %s\n", DSC_GetOnceStatusName(onceStatus));
         Logging::print("DiExecGetStatus() status change => DI_WAIT_MISCELLANEOUS, (%s)\n",
               pdesExitStatus->szReason);
      }
      break;
   case executeMode :
   case externalAccessMode:
      // Processor executing
      pdesExitStatus->dscCause = DI_WAIT_RUNNING;
      pdesExitStatus->szReason = (DiStringT)"Running";
      if ((lastStatus != pdesExitStatus->dscCause) || (pollCount++>20)) {
         pollCount = 0;
         Logging::print("DiExecGetStatus() - %s\n", DSC_GetOnceStatusName(onceStatus));
         Logging::print("DiExecGetStatus() status change => DI_WAIT_RUNNING, (%s)\n",
               pdesExitStatus->szReason);
      }
      break;
   }
   lastStatus = pdesExitStatus->dscCause;
   return setErrorState(DI_OK);
}

//!  2.2.8.7 Stop Execution
//!
USBDM_GDI_API
DiReturnT DiExecStop ( void ) {
   USBDM_ErrorCode BDMrc = BDM_RC_OK;

   Logging::print("DiExecStop()\n");

   CHECK_ERROR_STATE();

#if TARGET == ARM
   targetConnect(softConnectOptions);
   BDMrc = USBDM_TargetHalt();
#elif TARGET == MC56F80xx
   targetConnect(softConnectOptions);
   BDMrc = DSC_TargetHalt();
#elif TARGET == CFVx
   targetConnect(softConnectOptions);
   // Coldfire V2 devices are really broken
   // BKPT should wake a sleeping target but doesn't
   // The following retries in the hope that a target interrupt might occur at a convenient moment!
   int retry =10;
   do {
      USBDM_ControlPins(PIN_BKPT_LOW);
      milliSleep(100);
      USBDM_ControlPins(PIN_RELEASE);
      BDMrc = targetConnect(retryNever);
   } while ((BDMrc != BDM_RC_OK) && (retry-->0));
#else
   BDMrc = USBDM_TargetHalt();
   if (BDMrc != BDM_RC_OK) {
      targetConnect(softConnectOptions);
      BDMrc = USBDM_TargetHalt();
   }
#endif
   if (BDMrc != BDM_RC_OK) {
      return setErrorState(DI_ERR_NONFATAL, BDMrc);
   }
   return setErrorState(DI_OK);
}

//!  2.2.9.1 Switch Trace System On/Off
//!
//! @param fOn
//!
USBDM_GDI_API
DiReturnT DiTraceSwitchOn ( DiBoolT fOn ) {

   Logging::print("DiTraceSwitchOn() - not implemented\n");
   return setErrorState(DI_ERR_NOTSUPPORTED);
}

//!  2.2.9.2 Get Instruction Trace
//!
//! @param dnNrInstr
//! @param pditInstrTrace
//!
USBDM_GDI_API
DiReturnT DiTraceGetInstructions ( DiUInt32T       dnNrInstr,
                                    pDiInstrTraceT  pditInstrTrace ) {

   Logging::print("DiTraceGetInstructions() - not implemented\n");
   return setErrorState(DI_ERR_NOTSUPPORTED);
}

//!  2.2.9.3 Get Raw Trace
//!
//! @param dnNrFrames
//! @param dttTraceType
//! @param PrintRawTrace
//!
USBDM_GDI_API
DiReturnT DiTracePrintRawInfo ( DiUInt32T       dnNrFrames,
                                 DiTraceTypeT    dttTraceType,
                                 PrintRawTraceF  PrintRawTrace ) {

   Logging::print("DiTracePrintRawInfo() - not implemented\n");
   return setErrorState(DI_ERR_NOTSUPPORTED);
}

//!  2.2.9.4 Get Number of New Trace Frames
//!
//! @param dttTraceType
//! @param dnNrMaxFrames
//! @param pdnfNewFrames
//!
USBDM_GDI_API
DiReturnT DiTraceGetNrOfNewFrames ( DiTraceTypeT   dttTraceType,
                                     DiUInt32T      dnNrMaxFrames,
                                     pDiNewFramesT  pdnfNewFrames ) {

   Logging::print("DiTraceGetNrOfNewFrames() - not implemented\n");
   return setErrorState(DI_ERR_NOTSUPPORTED);
}

//!  2.2.10.1 Switch Coverage On/Off
//!
//! @param fOn
//!
USBDM_GDI_API
DiReturnT DiCoverageSwitchOn ( DiBoolT fOn ) {

   Logging::print("DiCoverageSwitchOn() - not implemented\n");
   return setErrorState(DI_ERR_NOTSUPPORTED);
}

//!  2.2.10.2 Get Coverage Information
//!
//! @param daStart
//! @param dnSize
//! @param pdcCoverage
//!
USBDM_GDI_API
DiReturnT DiCoverageGetInfo ( DiAddrT     daStart,
                               DiUInt32T   dnSize,
                               DiCoverageT *pdcCoverage ) {

   Logging::print("DiCoverageGetInfo() - not implemented\n");
   return setErrorState(DI_ERR_NOTSUPPORTED);
}

//!  2.2.11.1 Switch Profiling On/Off
//!
//! @param fOn
//!
USBDM_GDI_API
DiReturnT DiProfilingSwitchOn ( DiBoolT fOn ) {

   Logging::print("DiProfilingSwitchOn() - not implemented\n");
   return setErrorState(DI_ERR_NOTSUPPORTED);
}

//!  2.2.11.2 Get Profiling Information
//!
//! @param pdpProfile
//!
USBDM_GDI_API
DiReturnT DiProfileGetInfo ( DiProfileT *pdpProfile ) {

   Logging::print("DiProfileGetInfo() - not implemented\n");
   return setErrorState(DI_ERR_NOTSUPPORTED);
}

//!  2.2.12.1 Open a State Resource
//!
//! @param pdnStateHandle
//! @param szStateName
//!
USBDM_GDI_API
DiReturnT DiStateOpen ( DiUInt32T      *pdnStateHandle,
                        DiConstStringT  szStateName ) {

   Logging::print("DiStateOpen() - not implemented\n");
   return setErrorState(DI_ERR_NOTSUPPORTED);
}

//!  2.2.12.2 Save DI State
//!
//! @param dnStateHandle
//! @param dnIndex
//!
USBDM_GDI_API
DiReturnT DiStateSave ( DiUInt32T dnStateHandle,
                         DiUInt32T dnIndex ) {

   Logging::print("DiStateSave() - not implemented\n");
   return setErrorState(DI_ERR_NOTSUPPORTED);
}

//!  2.2.12.3 Restore DI State
//!
//! @param dnStateHandle
//! @param dnIndex
//!
USBDM_GDI_API
DiReturnT DiStateRestore ( DiUInt32T dnStateHandle,
                            DiUInt32T dnIndex ) {

   Logging::print("DiStateRestore() - not implemented\n");
   return setErrorState(DI_ERR_NOTSUPPORTED);
}

//!  2.2.12.4 Close a State Resource
//!
//! @param fDelete
//!
USBDM_GDI_API
DiReturnT DiStateClose ( DiBoolT fDelete ) {

   Logging::print("DiStateClose() - not implemented\n");
   return setErrorState(DI_ERR_NOTSUPPORTED);
}

//!  2.2.13 Get Acceptable Communication Settings
//!
//! @param dccType
//! @param szAttr
//! @param pszEntries
//! @param pReserved
//!
USBDM_GDI_API
DiReturnT DiCommGetAcceptableSettings ( DiCommChannelT   dccType,
                                        DiConstStringT   szAttr,
                                        DiConstStringT   *pszEntries[],
                                        void             *pReserved ) {
static const unsigned int maxUSBDMDevices = 20;
static const char *options[maxUSBDMDevices+1];
static const char *const possibleOptions[maxUSBDMDevices+1] = {
   "USBDM #1",
   "USBDM #2",
   "USBDM #3",
   "USBDM #4",
   "USBDM #5",
   "USBDM #6",
   "USBDM #7",
   "USBDM #8",
   "USBDM #9",
   "USBDM #10",
   "USBDM #11",
   "USBDM #12",
   "USBDM #13",
   "USBDM #14",
   "USBDM #15",
   "USBDM #16",
   "USBDM #17",
   "USBDM #18",
   "USBDM #19",
   "USBDM #20",
   NULL,
};

//   Logging::print("DiCommGetAcceptableSettings() dccType = %x\n", dccType);
//   Logging::print("DiCommGetAcceptableSettings() szAttr = \'%s\'\n", szAttr);

   if (dccType == DI_COMM_PARALLEL) {
      unsigned int deviceCount;
      unsigned int index=0;
      USBDM_ErrorCode BDMrc = USBDM_FindDevices(&deviceCount);
      if (BDMrc != BDM_RC_OK)
         return setErrorState(DI_ERR_COMMUNICATION, ("No USBDM Devices found"));
      if (deviceCount>maxUSBDMDevices)
         deviceCount = maxUSBDMDevices;
      for (index=0; index<deviceCount; index++) {
         options[index] = possibleOptions[index];
      }
      options[deviceCount] = NULL;
      Logging::print("DiCommGetAcceptableSettings(DI_COMM_PARALLEL) => DI_OK\n");
      *pszEntries = options;
      return setErrorState(DI_OK);
   }
   else {
      Logging::print("DiCommGetAcceptableSettings(\?\?) => DI_ERR_NONFATAL\n");
      return setErrorState(DI_ERR_NONFATAL);
   }
}

//!  2.2.16.1 Enumerate Execution Environments
//!
//! @param pdExecEnv
//!
USBDM_GDI_API
DiReturnT DiMeeEnumExecEnv ( DiExecEnvT *pdExecEnv ) {

   Logging::print("DiMeeEnumExecEnv() - not implemented\n");
   return setErrorState(DI_ERR_NOTSUPPORTED);
}

//!  2.2.16.2 Connect to Execution Environment
//!
//! @param dnExecId
//!
USBDM_GDI_API
DiReturnT DiMeeConnect ( DiUInt32T dnExecId ) {

#ifdef USE_MEE
   Logging::print("DiMeeConnect(%d)\n", dnExecId);

   // Initial connect is treated differently
   USBDM_ErrorCode bdmRc = initialConnect();
   if (bdmRc != BDM_RC_OK) {
      Logging::print("DiMeeConnect() - Failed - %s\n", currentErrorString);
      return setErrorState(DI_ERR_COMMUNICATION, bdmRc);
   }
   return setErrorState(DI_OK);
#else
      Logging::print("DiMeeConnect() - not implemented\n");
      return setErrorState(DI_ERR_NOTSUPPORTED);
#endif
}

//!  2.2.16.3 Get Features Supported by Execution Environment
//!
//! @param dnExecId
//! @param pdfFeatures
//!
USBDM_GDI_API
DiReturnT DiMeeGetFeatures ( DiUInt32T    dnExecId,
                             pDiFeaturesT pdfFeatures ) {

   Logging::print("DiMeeGetFeatures() - not implemented\n");
   return setErrorState(DI_ERR_NOTSUPPORTED);
}

//!  2.2.16.4 Configure I/O System to Execution Environment
//!
//! @param dnExecId
//! @param pdcCommSetup
//!
USBDM_GDI_API
DiReturnT DiMeeInitIO ( DiUInt32T      dnExecId,
                        pDiCommSetupT  pdcCommSetup ) {

   Logging::print("DiMeeInitIO(%d) - not implemented\n", dnExecId);
   return setErrorState(DI_ERR_NOTSUPPORTED);
}

//!  2.2.16.5 Set Current Connection
//!
//! @param dnExecId
//!
USBDM_GDI_API
DiReturnT DiMeeSelect ( DiUInt32T dnExecId ) {

   Logging::print("DiMeeSelect(%d) - dummy\n", dnExecId);
   return setErrorState(DI_OK);
}

//!  2.2.16.6 Disconnect from Execution Environment
//!
//! @param dnExecId
//! @param fClose
//!
USBDM_GDI_API
DiReturnT DiMeeDisconnect ( DiUInt32T  dnExecId,
                            DiBoolT    fClose ) {

   Logging::print("DiMeeDisconnect(%d, %s) - dummy\n", dnExecId, fClose?"T":"F");
   return setErrorState(DI_OK);
}

//!  2.2.16.7 Connect to CPU
//!
//! @param dnCpuId
//!
USBDM_GDI_API
DiReturnT DiCpuSelect ( DiUInt32T dnCpuId ) {

   Logging::print("DiCpuSelect() - not implemented\n");
   return setErrorState(DI_ERR_NOTSUPPORTED);
}

//!  2.2.16.8 Get Current CPU
//!
//! @param dnCpuId
//!
USBDM_GDI_API
DiReturnT DiCpuCurrent ( DiUInt32T *dnCpuId ) {

   Logging::print("DiCpuCurrent() - not implemented\n");
   return setErrorState(DI_ERR_NOTSUPPORTED);
}

//!  2.2.17 Future GDI Extensions
//!
//! @param information
//!
USBDM_GDI_API
DiReturnT DiProcess ( void *information ) {
#if 0
   uint8_t  *info8  = (uint8_t*) information;
   uint32_t *info32 = (uint32_t*)information;

   uint32_t *dataPtr = (uint32_t *)info32[1];

   Logging::print("DiProcess() - &information = %p \n"
                  "               information = %p \n"
                  "              &DiProcess() = %p \n",
                  &information, information, &DiProcess);

   Logging::print("information[0] = %p \n"
                  "information[1] = %p \n"
                  "information[2] = %p \n",
                  info32[0],info32[1],info32[2]);

   Logging::print("DiProcess() - not implemented, data = \n"
                  "=================================================================\n");
   Logging::printDump(info8, 0x100, (uint32_t)info8);
   Logging::print("=================================================================\n");

   Logging::print("DiProcess() - data = \n"
                  "=================================================================\n");
   Logging::printDump((uint8_t*)dataPtr, 0x100, (uint32_t)dataPtr);
   Logging::print("=================================================================\n");
#endif
   return setErrorState(DI_ERR_NOTSUPPORTED);
}

#ifdef LEGACY
//===================================================================

class minimalApp : public  wxApp {
   DECLARE_CLASS( minimalApp )

public:
   minimalApp() :
      wxApp() {
      fprintf(stderr, "minimalApp::minimalApp()\n");
   }

   ~minimalApp(){
      fprintf(stderr, "minimalApp::~minimalApp()\n");
   }

   bool OnInit() {
      fprintf(stderr, "minimalApp::OnInit()\n");
      return true;
   }

   int OnExit() {
      fprintf(stderr, "minimalApp::OnExit()\n");
      return 0;
   }
};

IMPLEMENT_APP_NO_MAIN( minimalApp )
IMPLEMENT_CLASS( minimalApp, wxApp )

static bool wxInitializationDone = false;
#endif

bool usbdm_gdi_dll_open(void) {
#ifdef LEGACY
   int argc = 1;
   char  arg0[] = "usbdm";
   char *argv[]={arg0, NULL};
   wxApp *savewxApp = wxTheApp;

   if (wxTheApp == NULL) {
      wxInitializationDone = wxEntryStart(argc, argv);
      fprintf(stderr, "usbdm_gdi_dll_open() - wxTheApp = %p\n", wxTheApp);
      fprintf(stderr, "usbdm_gdi_dll_open() - AppName = %s\n", (const char *)wxTheApp->GetAppName().c_str());
   }
   if (wxInitializationDone)
      fprintf(stderr, "usbdm_gdi_dll_open() - wxEntryStart() successful\n");
   else {
      fprintf(stderr, "usbdm_gdi_dll_open() - wxEntryStart() failed\n");
      return false;
   }
#endif
#if TARGET == HCS08
   Logging::openLogFile("GDI_HCS08.log");
#elif TARGET == RS08
   Logging::openLogFile("GDI_RS08.log");
#elif TARGET == HCS12
   Logging::openLogFile("GDI_HCS12.log");
#elif TARGET == CFV1
   Logging::openLogFile("GDI_CFV1.log");
#elif TARGET == CFVx
   Logging::openLogFile("GDI_CFVx.log");
#elif TARGET == MC56F80xx
   Logging::openLogFile("GDI_DSC.log");
#elif TARGET == JTAG
   Logging::openLogFile("GDI_JTAG.log");
#elif TARGET == ARM
   Logging::openLogFile("GDI_ARM.log");
#else
   #error Unexpected Target
#endif
   Logging::setLoggingLevel(100);
   Logging::print("usbdm_gdi_dll_open() - wxEntryStart() successful\n");
#ifdef LEGACY
   Logging::print("usbdm_gdi_dll_open() - savewxApp = %p\n", savewxApp);
   Logging::print("usbdm_gdi_dll_open() - wxTheApp  = %p\n", wxTheApp);
#endif
   return true;
}

bool usbdm_gdi_dll_close(void) {
   Logging::print("usbdm_gdi_dll_close()\n");
#ifdef LEGACY
#ifdef _WIN32
   if (wxInitializationDone) {
      fprintf(stderr, "usbdm_gdi_dll_close() - Doing wxEntryCleanup()\n");
      wxEntryCleanup();
      fprintf(stderr, "usbdm_gdi_dll_close() - Done wxEntryCleanup()\n");
   }
#endif
#endif
   Logging::closeLogFile();
   return true;
}

#ifdef __unix__

#if   TARGET == ARM
#define GDI_DLL_NAME "usbdm-arm-gdi-debug.so"
#elif TARGET == CFV1
#define GDI_DLL_NAME "usbdm-cfv1-gdi-debug.so"
#elif TARGET == CFVx
#define GDI_DLL_NAME "usbdm-cfvx-gdi-debug.so"
#elif TARGET == MC56F80xx
#define GDI_DLL_NAME "usbdm-dsc-gdi-debug.so"
#elif TARGET == HCS08
#define GDI_DLL_NAME "usbdm-hcs08-gdi-debug.so"
#elif TARGET == HCS12
#define GDI_DLL_NAME "usbdm-hcs12-gdi-debug.so"
#elif TARGET == RS08
#define GDI_DLL_NAME "usbdm-rs08-gdi-debug.so"
#endif

extern "C"
void
__attribute__ ((constructor))
gdi_dll_initialize(void) {
   static void *libHandle = NULL;

   // Lock Library in memory!
   if (libHandle == NULL) {
      libHandle = dlopen(GDI_DLL_NAME, RTLD_NOW|RTLD_NODELETE);
      if (libHandle == NULL) {
         fprintf(stderr, "gdi_dll_initialize() - Library failed to lock %s\n", dlerror());
         return;
      }
      else
         fprintf(stderr, "gdi_dll_initialize() - Library locked OK\n");
   }
   else
      fprintf(stderr, "gdi_dll_initialize() - Library already locked\n");
}

extern "C"
void
__attribute__ ((destructor))
gdi_dll_uninitialize(void) {
}
#else
#include <windef.h>
extern "C" WINAPI __declspec(dllexport)
BOOL DllMain(HINSTANCE _hDLLInst,
                     DWORD fdwReason,
                     LPVOID lpvReserved) {

   switch (fdwReason) {
      case DLL_PROCESS_ATTACH:
         Logging::print("DLL_PROCESS_ATTACH\n");
//         dll_initialize();
         break;
      case DLL_PROCESS_DETACH:
         Logging::print("DLL_PROCESS_DETACH - closeBDM()\n");
         closeBDM();
         Logging::print("DLL_PROCESS_DETACH - usbdm_gdi_dll_close()\n");
         usbdm_gdi_dll_close();
         Logging::print("DLL_PROCESS_DETACH - done\n");
//         dll_uninitialize();
         break;
      case DLL_THREAD_ATTACH:
//         Logging::print("DLL_THREAD_ATTACH\n");
//         dll_initialize(_hDLLInst);
         break;
      case DLL_THREAD_DETACH:
//         Logging::print("DLL_THREAD_DETACH\n");
//         dll_uninitialize();
         break;
   }
   return TRUE;
}
#endif

#ifdef _WIN32
// This creates aliases for the the GDI routines with underscores in the names
#include "GDI_underscores.h"
#endif
