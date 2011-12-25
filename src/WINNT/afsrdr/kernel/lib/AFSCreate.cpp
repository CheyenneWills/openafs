/*
 * Copyright (c) 2008, 2009, 2010, 2011 Kernel Drivers, LLC.
 * Copyright (c) 2009, 2010, 2011 Your File System, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice,
 *   this list of conditions and the following disclaimer in the
 *   documentation
 *   and/or other materials provided with the distribution.
 * - Neither the names of Kernel Drivers, LLC and Your File System, Inc.
 *   nor the names of their contributors may be used to endorse or promote
 *   products derived from this software without specific prior written
 *   permission from Kernel Drivers, LLC and Your File System, Inc.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

//
// File: AFSCreate.cpp
//

#include "AFSCommon.h"

//
// Function: AFSCreate
//
// Description:
//
//      This function is the dispatch handler for the IRP_MJ_CREATE requests. It makes the determination to
//      which interface this request is destined.
//
// Return:
//
//      A status is returned for the function. The Irp completion processing is handled in the specific
//      interface handler.
//

NTSTATUS
AFSCreate( IN PDEVICE_OBJECT LibDeviceObject,
           IN PIRP Irp)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    IO_STACK_LOCATION  *pIrpSp;
    FILE_OBJECT        *pFileObject = NULL;

    __try
    {

        pIrpSp = IoGetCurrentIrpStackLocation( Irp);
        pFileObject = pIrpSp->FileObject;

        if( pFileObject == NULL ||
            pFileObject->FileName.Buffer == NULL)
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_VERBOSE,
                          "AFSCreate (%08lX) Processing control device open request\n",
                          Irp);

            ntStatus = AFSControlDeviceCreate( Irp);

            try_return( ntStatus);
        }

        if( AFSRDRDeviceObject == NULL)
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_VERBOSE,
                          "AFSCreate (%08lX) Invalid request to open before library is initialized\n",
                          Irp);

            try_return( ntStatus = STATUS_DEVICE_NOT_READY);
        }

        ntStatus = AFSCommonCreate( AFSRDRDeviceObject,
                                    Irp);

try_exit:

        NOTHING;
    }
    __except( AFSExceptionFilter( GetExceptionCode(), GetExceptionInformation()) )
    {

        AFSDbgLogMsg( 0,
                      0,
                      "EXCEPTION - AFSCreate\n");

        ntStatus = STATUS_ACCESS_DENIED;
    }

    //
    // Complete the request
    //

    AFSCompleteRequest( Irp,
                          ntStatus);

    return ntStatus;
}

NTSTATUS
AFSCommonCreate( IN PDEVICE_OBJECT DeviceObject,
                 IN PIRP Irp)
{

    NTSTATUS            ntStatus = STATUS_SUCCESS;
    UNICODE_STRING      uniFileName;
    ULONG               ulCreateDisposition = 0;
    ULONG               ulOptions = 0;
    BOOLEAN             bNoIntermediateBuffering = FALSE;
    FILE_OBJECT        *pFileObject = NULL;
    IO_STACK_LOCATION  *pIrpSp;
    AFSFcb             *pFcb = NULL;
    AFSCcb             *pCcb = NULL;
    AFSDeviceExt       *pDeviceExt = NULL;
    BOOLEAN             bOpenTargetDirectory = FALSE, bReleaseVolume = FALSE;
    PACCESS_MASK        pDesiredAccess = NULL;
    UNICODE_STRING      uniComponentName, uniPathName, uniRootFileName, uniParsedFileName;
    UNICODE_STRING      uniSubstitutedPathName;
    UNICODE_STRING      uniRelativeName;
    AFSNameArrayHdr    *pNameArray = NULL;
    AFSVolumeCB        *pVolumeCB = NULL;
    AFSDirectoryCB     *pParentDirectoryCB = NULL, *pDirectoryCB = NULL;
    ULONG               ulParseFlags = 0;
    GUID                stAuthGroup;
    ULONG               ulNameProcessingFlags = 0;
    BOOLEAN             bOpenedReparsePoint = FALSE;

    __Enter
    {

        pIrpSp = IoGetCurrentIrpStackLocation( Irp);
        pDeviceExt = (AFSDeviceExt *)DeviceObject->DeviceExtension;
        ulCreateDisposition = (pIrpSp->Parameters.Create.Options >> 24) & 0x000000ff;
        ulOptions = pIrpSp->Parameters.Create.Options;
        bNoIntermediateBuffering = BooleanFlagOn( ulOptions, FILE_NO_INTERMEDIATE_BUFFERING);
        bOpenTargetDirectory = BooleanFlagOn( pIrpSp->Flags, SL_OPEN_TARGET_DIRECTORY);
        pFileObject = pIrpSp->FileObject;
        pDesiredAccess = &pIrpSp->Parameters.Create.SecurityContext->DesiredAccess;

        uniFileName.Length = uniFileName.MaximumLength = 0;
        uniFileName.Buffer = NULL;

        uniRootFileName.Length = uniRootFileName.MaximumLength = 0;
        uniRootFileName.Buffer = NULL;

        uniParsedFileName.Length = uniParsedFileName.MaximumLength = 0;
        uniParsedFileName.Buffer = NULL;

        uniSubstitutedPathName.Buffer = NULL;
        uniSubstitutedPathName.Length = 0;

        uniRelativeName.Buffer = NULL;
        uniRelativeName.Length = 0;

        if( AFSGlobalRoot == NULL)
        {
            try_return( ntStatus = STATUS_DEVICE_NOT_READY);
        }

        RtlZeroMemory( &stAuthGroup,
                       sizeof( GUID));

        AFSRetrieveAuthGroupFnc( (ULONGLONG)PsGetCurrentProcessId(),
                                 (ULONGLONG)PsGetCurrentThreadId(),
                                  &stAuthGroup);

        if( !BooleanFlagOn( AFSGlobalRoot->ObjectInformation.Flags, AFS_OBJECT_FLAGS_DIRECTORY_ENUMERATED))
        {

            ntStatus = AFSEnumerateGlobalRoot( &stAuthGroup);

            if( !NT_SUCCESS( ntStatus))
            {

                AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                              AFS_TRACE_LEVEL_ERROR,
                              "AFSCommonCreate Failed to enumerate global root Status %08lX\n",
                              ntStatus);

                try_return( ntStatus);
            }
        }

        //
        // If we are in shutdown mode then fail the request
        //

        if( BooleanFlagOn( pDeviceExt->DeviceFlags, AFS_DEVICE_FLAG_REDIRECTOR_SHUTDOWN))
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_WARNING,
                          "AFSCommonCreate (%08lX) Open request after shutdown\n",
                          Irp);

            try_return( ntStatus = STATUS_TOO_LATE);
        }

        //
        // Go and parse the name for processing.
        // If ulParseFlags is returned with AFS_PARSE_FLAG_FREE_FILE_BUFFER set,
        // then we are responsible for releasing the uniRootFileName.Buffer.
        //

        ntStatus = AFSParseName( Irp,
                                 &stAuthGroup,
                                 &uniFileName,
                                 &uniParsedFileName,
                                 &uniRootFileName,
                                 &ulParseFlags,
                                 &pVolumeCB,
                                 &pParentDirectoryCB,
                                 &pNameArray);

        if( !NT_SUCCESS( ntStatus))
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          uniFileName.Length > 0 ? AFS_TRACE_LEVEL_ERROR : AFS_TRACE_LEVEL_VERBOSE,
                          "AFSCommonCreate (%08lX) Failed to parse name \"%wZ\" Status %08lX\n",
                          Irp,
                          &uniFileName,
                          ntStatus);

            try_return( ntStatus);
        }

        //
        // Check for STATUS_REPARSE
        //

        if( ntStatus == STATUS_REPARSE)
        {

            //
            // Update the information and return
            //

            Irp->IoStatus.Information = IO_REPARSE;

            try_return( ntStatus);
        }

        //
        // If the returned volume cb is NULL then we are dealing with the \\Server\GlobalRoot
        // name
        //

        if( pVolumeCB == NULL)
        {

            //
            // Remove any leading or trailing slashes
            //

            if( uniFileName.Length >= sizeof( WCHAR) &&
                uniFileName.Buffer[ (uniFileName.Length/sizeof( WCHAR)) - 1] == L'\\')
            {

                uniFileName.Length -= sizeof( WCHAR);
            }

            if( uniFileName.Length >= sizeof( WCHAR) &&
                uniFileName.Buffer[ 0] == L'\\')
            {

                uniFileName.Buffer = &uniFileName.Buffer[ 1];

                uniFileName.Length -= sizeof( WCHAR);
            }

            //
            // If there is a remaining portion returned for this request then
            // check if it is for the PIOCtl interface
            //

            if( uniFileName.Length > 0)
            {

                //
                // We don't accept any other opens off of the AFS Root
                //

                ntStatus = STATUS_OBJECT_NAME_NOT_FOUND;

                //
                // If this is an open on "_._AFS_IOCTL_._" then perform handling on it accordingly
                //

                if( RtlCompareUnicodeString( &AFSPIOCtlName,
                                             &uniFileName,
                                             TRUE) == 0)
                {

                    ntStatus = AFSOpenIOCtlFcb( Irp,
                                                &stAuthGroup,
                                                AFSGlobalRoot->DirectoryCB,
                                                &pFcb,
                                                &pCcb);

                    if( !NT_SUCCESS( ntStatus))
                    {

                        AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                                      AFS_TRACE_LEVEL_ERROR,
                                      "AFSCommonCreate Failed to open root IOCtl Fcb Status %08lX\n",
                                      ntStatus);
                    }
                }
                else if( pParentDirectoryCB != NULL &&
                         pParentDirectoryCB->ObjectInformation->FileType == AFS_FILE_TYPE_SPECIAL_SHARE_NAME)
                {

                    ntStatus = AFSOpenSpecialShareFcb( Irp,
                                                       &stAuthGroup,
                                                       pParentDirectoryCB,
                                                       &pFcb,
                                                       &pCcb);

                    if( !NT_SUCCESS( ntStatus))
                    {

                        AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                                      AFS_TRACE_LEVEL_ERROR,
                                      "AFSCommonCreate Failed to open special share Fcb Status %08lX\n",
                                      ntStatus);
                    }
                }

                try_return( ntStatus);
            }

            ntStatus = AFSOpenAFSRoot( Irp,
                                       &pFcb,
                                       &pCcb);

            if( !NT_SUCCESS( ntStatus))
            {

                AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                              AFS_TRACE_LEVEL_ERROR,
                              "AFSCommonCreate Failed to open root Status %08lX\n",
                              ntStatus);

                InterlockedDecrement( &AFSGlobalRoot->DirectoryCB->OpenReferenceCount);

                AFSDbgLogMsg( AFS_SUBSYSTEM_DIRENTRY_REF_COUNTING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSCreate Decrement1 count on &wZ DE %p Ccb %p Cnt %d\n",
                              &AFSGlobalRoot->DirectoryCB->NameInformation.FileName,
                              AFSGlobalRoot->DirectoryCB,
                              NULL,
                              AFSGlobalRoot->DirectoryCB->OpenReferenceCount);
            }

            try_return( ntStatus);
        }

        //
        // We have a reference on the root volume
        //

        bReleaseVolume = TRUE;

        //
        // Attempt to locate the node in the name tree if this is not a target
        // open and the target is not the root
        //

        uniComponentName.Length = 0;
        uniComponentName.Buffer = NULL;

        if( uniFileName.Length > sizeof( WCHAR) ||
            uniFileName.Buffer[ 0] != L'\\')
        {

            if( !AFSValidNameFormat( &uniFileName))
            {

                ntStatus = STATUS_OBJECT_NAME_NOT_FOUND;

                AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSCommonCreate (%08lX) Invalid name %wZ Status %08lX\n",
                              Irp,
                              &uniFileName,
                              ntStatus);

                try_return( ntStatus);
            }

            //
            // Opening a reparse point directly?
            //

            ulNameProcessingFlags = AFS_LOCATE_FLAGS_SUBSTITUTE_NAME;

            if( BooleanFlagOn( ulOptions, FILE_OPEN_REPARSE_POINT))
            {
                ulNameProcessingFlags |= (AFS_LOCATE_FLAGS_NO_MP_TARGET_EVAL |
                                          AFS_LOCATE_FLAGS_NO_SL_TARGET_EVAL |
                                          AFS_LOCATE_FLAGS_NO_DFS_LINK_EVAL);
            }

            uniSubstitutedPathName = uniRootFileName;

            ntStatus = AFSLocateNameEntry( &stAuthGroup,
                                           pFileObject,
                                           &uniRootFileName,
                                           &uniParsedFileName,
                                           pNameArray,
                                           ulNameProcessingFlags,
                                           &pVolumeCB,
                                           &pParentDirectoryCB,
                                           &pDirectoryCB,
                                           &uniComponentName);

            if( !NT_SUCCESS( ntStatus) &&
                ntStatus != STATUS_OBJECT_NAME_NOT_FOUND)
            {

                if ( uniSubstitutedPathName.Buffer == uniRootFileName.Buffer)
                {
                    uniSubstitutedPathName.Buffer = NULL;
                }

                //
                // The routine above released the root while walking the
                // branch
                //

                AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSCommonCreate (%08lX) Failed to locate name entry for %wZ Status %08lX\n",
                              Irp,
                              &uniFileName,
                              ntStatus);

                //
                // We released any root volume locks in the above on failure
                //

                bReleaseVolume = FALSE;

                try_return( ntStatus);
            }

            //
            // Check for STATUS_REPARSE
            //

            if( ntStatus == STATUS_REPARSE)
            {

                uniSubstitutedPathName.Buffer = NULL;

                //
                // Update the information and return
                //

                Irp->IoStatus.Information = IO_REPARSE;

                //
                // We released the volume lock above
                //

                bReleaseVolume = FALSE;

                try_return( ntStatus);
            }

            //
            // If we re-allocated the name, then update our substitute name
            //

            if( uniSubstitutedPathName.Buffer != uniRootFileName.Buffer)
            {

                uniSubstitutedPathName = uniRootFileName;
            }
            else
            {

                uniSubstitutedPathName.Buffer = NULL;
            }

            //
            // Check for a symlink access
            //

            if( ntStatus == STATUS_OBJECT_NAME_NOT_FOUND &&
                pParentDirectoryCB != NULL)
            {

                UNICODE_STRING uniFinalComponent;

                uniFinalComponent.Length = 0;
                uniFinalComponent.MaximumLength = 0;
                uniFinalComponent.Buffer = NULL;

                AFSRetrieveFinalComponent( &uniFileName,
                                           &uniFinalComponent);

                ntStatus = AFSCheckSymlinkAccess( pParentDirectoryCB,
                                                  &uniFinalComponent);

                if( !NT_SUCCESS( ntStatus) &&
                    ntStatus != STATUS_OBJECT_NAME_NOT_FOUND)
                {

                    AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                                  AFS_TRACE_LEVEL_VERBOSE,
                                  "AFSCommonCreate (%08lX) Failing access to symlink %wZ Status %08lX\n",
                                  Irp,
                                  &uniFileName,
                                  ntStatus);

                    try_return( ntStatus);
                }
            }
        }

        //
        // If we have no parent then this is a root open, be sure there is a directory entry
        // for the root
        //

        else if( pParentDirectoryCB == NULL &&
                 pDirectoryCB == NULL)
        {

            pDirectoryCB = pVolumeCB->DirectoryCB;
        }

        if( bOpenTargetDirectory)
        {

            //
            // If we have a directory cb for the entry then dereference it and reference the parent
            //

            if( pDirectoryCB != NULL)
            {

                //
                // Perform in this order to prevent thrashing
                //

                InterlockedIncrement( &pParentDirectoryCB->OpenReferenceCount);

                AFSDbgLogMsg( AFS_SUBSYSTEM_DIRENTRY_REF_COUNTING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSCreate Increment1 count on %wZ DE %p Ccb %p Cnt %d\n",
                              &pParentDirectoryCB->NameInformation.FileName,
                              pParentDirectoryCB,
                              NULL,
                              pParentDirectoryCB->OpenReferenceCount);

                //
                // Do NOT decrement the reference count on the pDirectoryCB yet.
                // The BackupEntry below might drop the count to zero leaving
                // the entry subject to being deleted and we need some of the
                // contents during later processing
                //

                AFSBackupEntry( pNameArray);
            }

            //
            // OK, open the target directory
            //

            if( uniComponentName.Length == 0)
            {
                AFSRetrieveFinalComponent( &uniFileName,
                                           &uniComponentName);
            }

            ntStatus = AFSOpenTargetDirectory( Irp,
                                               pVolumeCB,
                                               pParentDirectoryCB,
                                               pDirectoryCB,
                                               &uniComponentName,
                                               &pFcb,
                                               &pCcb);
            if( pDirectoryCB != NULL)
            {
                //
                // It is now safe to drop the Reference Count
                //
                InterlockedDecrement( &pDirectoryCB->OpenReferenceCount);

                AFSDbgLogMsg( AFS_SUBSYSTEM_DIRENTRY_REF_COUNTING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSCreate Decrement2 count on %wZ DE %p Ccb %p Cnt %d\n",
                              &pDirectoryCB->NameInformation.FileName,
                              pDirectoryCB,
                              NULL,
                              pDirectoryCB->OpenReferenceCount);
            }

            if( !NT_SUCCESS( ntStatus))
            {

                AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                              AFS_TRACE_LEVEL_ERROR,
                              "AFSCommonCreate Failed to open target directory %wZ Status %08lX\n",
                              &pParentDirectoryCB->NameInformation.FileName,
                              ntStatus);

                //
                // Decrement the reference on the parent
                //

                InterlockedDecrement( &pParentDirectoryCB->OpenReferenceCount);

                AFSDbgLogMsg( AFS_SUBSYSTEM_DIRENTRY_REF_COUNTING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSCreate Decrement3 count on %wZ DE %p Ccb %p Cnt %d\n",
                              &pParentDirectoryCB->NameInformation.FileName,
                              pParentDirectoryCB,
                              NULL,
                              pParentDirectoryCB->OpenReferenceCount);
            }

            try_return( ntStatus);
        }

        if ( BooleanFlagOn( ulOptions, FILE_OPEN_REPARSE_POINT))
        {

            if( pDirectoryCB == NULL ||
                !BooleanFlagOn( pDirectoryCB->ObjectInformation->FileAttributes, FILE_ATTRIBUTE_REPARSE_POINT))
            {
                AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSCommonCreate (%08lX) Reparse open request but attribute not set for %wZ DirCB %p Type %08lX\n",
                              Irp,
                              &uniFileName,
                              pDirectoryCB,
                              pDirectoryCB ? pDirectoryCB->ObjectInformation->FileType : 0);
            }
            else
            {
                AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSCommonCreate (%08lX) Opening as reparse point %wZ Type %08lX\n",
                              Irp,
                              &uniFileName,
                              pDirectoryCB->ObjectInformation->FileType);

                bOpenedReparsePoint = TRUE;
            }
        }

        //
        // Based on the options passed in, process the file accordingly.
        //

        if( ulCreateDisposition == FILE_CREATE ||
            ( ( ulCreateDisposition == FILE_OPEN_IF ||
                ulCreateDisposition == FILE_OVERWRITE_IF) &&
              pDirectoryCB == NULL))
        {

            if( uniComponentName.Length == 0 ||
                pDirectoryCB != NULL)
            {

                //
                // We traversed the entire path so we found each entry,
                // fail with collision
                //

                AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSCommonCreate Object name collision on create of %wZ Status %08lX\n",
                              &pDirectoryCB->NameInformation.FileName,
                              ntStatus);

                if( pDirectoryCB != NULL)
                {

                    InterlockedDecrement( &pDirectoryCB->OpenReferenceCount);

                    AFSDbgLogMsg( AFS_SUBSYSTEM_DIRENTRY_REF_COUNTING,
                                  AFS_TRACE_LEVEL_VERBOSE,
                                  "AFSCreate Decrement4 count on %wZ DE %p Ccb %p Cnt %d\n",
                                  &pDirectoryCB->NameInformation.FileName,
                                  pDirectoryCB,
                                  NULL,
                                  pDirectoryCB->OpenReferenceCount);
                }
                else
                {

                    InterlockedDecrement( &pParentDirectoryCB->OpenReferenceCount);

                    AFSDbgLogMsg( AFS_SUBSYSTEM_DIRENTRY_REF_COUNTING,
                                  AFS_TRACE_LEVEL_VERBOSE,
                                  "AFSCreate Decrement5 count on %wZ DE %p Ccb %p Cnt %d\n",
                                  &pParentDirectoryCB->NameInformation.FileName,
                                  pParentDirectoryCB,
                                  NULL,
                                  pParentDirectoryCB->OpenReferenceCount);
                }

                try_return( ntStatus = STATUS_OBJECT_NAME_COLLISION);
            }

            //
            // OK, go and create the node
            //

            ntStatus = AFSProcessCreate( Irp,
                                         &stAuthGroup,
                                         pVolumeCB,
                                         pParentDirectoryCB,
                                         &uniFileName,
                                         &uniComponentName,
                                         &uniRootFileName,
                                         &pFcb,
                                         &pCcb);

            if( !NT_SUCCESS( ntStatus))
            {

                AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                              AFS_TRACE_LEVEL_ERROR,
                              "AFSCommonCreate Failed to create of %wZ in directory %wZ Status %08lX\n",
                              &uniComponentName,
                              &pParentDirectoryCB->NameInformation.FileName,
                              ntStatus);
            }

            //
            // Dereference the parent entry
            //

            InterlockedDecrement( &pParentDirectoryCB->OpenReferenceCount);

            AFSDbgLogMsg( AFS_SUBSYSTEM_DIRENTRY_REF_COUNTING,
                          AFS_TRACE_LEVEL_VERBOSE,
                          "AFSCreate Decrement6 count on %wZ DE %p Ccb %p Cnt %d\n",
                          &pParentDirectoryCB->NameInformation.FileName,
                          pParentDirectoryCB,
                          NULL,
                          pParentDirectoryCB->OpenReferenceCount);

            try_return( ntStatus);
        }

        //
        // We should not have an extra component except for PIOCtl opens
        //

        if( uniComponentName.Length > 0)
        {

            //
            // If this is an open on "_._AFS_IOCTL_._" then perform handling on it accordingly
            //

            if( RtlCompareUnicodeString( &AFSPIOCtlName,
                                         &uniComponentName,
                                         TRUE) == 0)
            {

                ntStatus = AFSOpenIOCtlFcb( Irp,
                                            &stAuthGroup,
                                            pParentDirectoryCB,
                                            &pFcb,
                                            &pCcb);

                if( !NT_SUCCESS( ntStatus))
                {

                    AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                                  AFS_TRACE_LEVEL_ERROR,
                                  "AFSCommonCreate Failed to IOCtl open on %wZ Status %08lX\n",
                                  &uniComponentName,
                                  ntStatus);
                }
            }
            else
            {

                AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSCommonCreate (%08lX) File %wZ name not found\n",
                              Irp,
                              &uniFileName);

                ntStatus = STATUS_OBJECT_NAME_NOT_FOUND;
            }

            if( !NT_SUCCESS( ntStatus))
            {

                //
                // Dereference the parent entry
                //

                if( pDirectoryCB != NULL)
                {

                    InterlockedDecrement( &pDirectoryCB->OpenReferenceCount);

                    AFSDbgLogMsg( AFS_SUBSYSTEM_DIRENTRY_REF_COUNTING,
                                  AFS_TRACE_LEVEL_VERBOSE,
                                  "AFSCreate Decrement7a count on %wZ DE %p Ccb %p Cnt %d\n",
                                  &pDirectoryCB->NameInformation.FileName,
                                  pDirectoryCB,
                                  NULL,
                                  pDirectoryCB->OpenReferenceCount);
                }
                else
                {

                    InterlockedDecrement( &pParentDirectoryCB->OpenReferenceCount);

                    AFSDbgLogMsg( AFS_SUBSYSTEM_DIRENTRY_REF_COUNTING,
                                  AFS_TRACE_LEVEL_VERBOSE,
                                  "AFSCreate Decrement7b count on %wZ DE %p Ccb %p Cnt %d\n",
                                  &pParentDirectoryCB->NameInformation.FileName,
                                  pParentDirectoryCB,
                                  NULL,
                                  pParentDirectoryCB->OpenReferenceCount);
                }
            }

            try_return( ntStatus);
        }

        //
        // For root opens the parent will be NULL
        //

        if( pParentDirectoryCB == NULL)
        {

            //
            // Check for the delete on close flag for the root
            //

            if( BooleanFlagOn( ulOptions, FILE_DELETE_ON_CLOSE ))
            {

                AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                              AFS_TRACE_LEVEL_ERROR,
                              "AFSCommonCreate (%08lX) Attempt to open root as delete on close\n",
                              Irp);

                InterlockedDecrement( &pDirectoryCB->OpenReferenceCount);

                AFSDbgLogMsg( AFS_SUBSYSTEM_DIRENTRY_REF_COUNTING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSCreate Decrement8 count on %wZ DE %p Ccb %p Cnt %d\n",
                              &pDirectoryCB->NameInformation.FileName,
                              pDirectoryCB,
                              NULL,
                              pDirectoryCB->OpenReferenceCount);

                try_return( ntStatus = STATUS_CANNOT_DELETE);
            }

            //
            // If this is the target directory, then bail
            //

            if( bOpenTargetDirectory)
            {

                AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                              AFS_TRACE_LEVEL_ERROR,
                              "AFSCommonCreate (%08lX) Attempt to open root as target directory\n",
                              Irp);

                InterlockedDecrement( &pDirectoryCB->OpenReferenceCount);

                AFSDbgLogMsg( AFS_SUBSYSTEM_DIRENTRY_REF_COUNTING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSCreate Decrement9 count on %wZ DE %p Ccb %p Cnt %d\n",
                              &pDirectoryCB->NameInformation.FileName,
                              pDirectoryCB,
                              NULL,
                              pDirectoryCB->OpenReferenceCount);

                try_return( ntStatus = STATUS_INVALID_PARAMETER);
            }

            //
            // Go and open the root of the volume
            //

            ntStatus = AFSOpenRoot( Irp,
                                    pVolumeCB,
                                    &stAuthGroup,
                                    &pFcb,
                                    &pCcb);

            if( !NT_SUCCESS( ntStatus))
            {

                AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                              AFS_TRACE_LEVEL_ERROR,
                              "AFSCommonCreate Failed to open volume root %08lX-%08lX Status %08lX\n",
                              pVolumeCB->ObjectInformation.FileId.Cell,
                              pVolumeCB->ObjectInformation.FileId.Volume,
                              ntStatus);

                InterlockedDecrement( &pDirectoryCB->OpenReferenceCount);

                AFSDbgLogMsg( AFS_SUBSYSTEM_DIRENTRY_REF_COUNTING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSCreate Decrement10 count on %wZ DE %p Ccb %p Cnt %d\n",
                              &pDirectoryCB->NameInformation.FileName,
                              pDirectoryCB,
                              NULL,
                              pDirectoryCB->OpenReferenceCount);
            }

            try_return( ntStatus);
        }

        //
        // At this point if we have no pDirectoryCB it was not found.
        //

        if( pDirectoryCB == NULL)
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSCommonCreate Failing access to %wZ\n",
                          &uniFileName);

            try_return( ntStatus = STATUS_OBJECT_NAME_NOT_FOUND);
        }

        if( ulCreateDisposition == FILE_OVERWRITE ||
            ulCreateDisposition == FILE_SUPERSEDE ||
            ulCreateDisposition == FILE_OVERWRITE_IF)
        {

            //
            // Go process a file for overwrite or supersede.
            //

            ntStatus = AFSProcessOverwriteSupersede( DeviceObject,
                                                     Irp,
                                                     pVolumeCB,
                                                     &stAuthGroup,
                                                     pParentDirectoryCB,
                                                     pDirectoryCB,
                                                     &pFcb,
                                                     &pCcb);

            if( !NT_SUCCESS( ntStatus))
            {

                AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                              AFS_TRACE_LEVEL_ERROR,
                              "AFSCommonCreate Failed overwrite/supersede on %wZ Status %08lX\n",
                              &pDirectoryCB->NameInformation.FileName,
                              ntStatus);

                InterlockedDecrement( &pDirectoryCB->OpenReferenceCount);

                AFSDbgLogMsg( AFS_SUBSYSTEM_DIRENTRY_REF_COUNTING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSCreate Decrement11 count on %wZ DE %p Ccb %p Cnt %d\n",
                              &pDirectoryCB->NameInformation.FileName,
                              pDirectoryCB,
                              NULL,
                              pDirectoryCB->OpenReferenceCount);
            }

            try_return( ntStatus);
        }

        //
        // Trying to open the file
        //

        ntStatus = AFSProcessOpen( Irp,
                                   &stAuthGroup,
                                   pVolumeCB,
                                   pParentDirectoryCB,
                                   pDirectoryCB,
                                   &pFcb,
                                   &pCcb);

        if( !NT_SUCCESS( ntStatus))
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSCommonCreate Failed open on %wZ Status %08lX\n",
                          &pDirectoryCB->NameInformation.FileName,
                          ntStatus);

            InterlockedDecrement( &pDirectoryCB->OpenReferenceCount);

            AFSDbgLogMsg( AFS_SUBSYSTEM_DIRENTRY_REF_COUNTING,
                          AFS_TRACE_LEVEL_VERBOSE,
                          "AFSCreate Decrement12 count on %wZ DE %p Ccb %p Cnt %d\n",
                          &pDirectoryCB->NameInformation.FileName,
                          pDirectoryCB,
                          NULL,
                          pDirectoryCB->OpenReferenceCount);
        }

try_exit:

        if( NT_SUCCESS( ntStatus) &&
            ntStatus != STATUS_REPARSE)
        {

            if( pCcb != NULL)
            {

                RtlCopyMemory( &pCcb->AuthGroup,
                               &stAuthGroup,
                               sizeof( GUID));

                //
                // If we have a substitute name, then use it
                //

                if( uniSubstitutedPathName.Buffer != NULL)
                {

                    pCcb->FullFileName = uniSubstitutedPathName;

                    SetFlag( pCcb->Flags, CCB_FLAG_FREE_FULL_PATHNAME);

                    ClearFlag( ulParseFlags, AFS_PARSE_FLAG_FREE_FILE_BUFFER);
                }
                else
                {

                    pCcb->FullFileName = uniRootFileName;

                    if( BooleanFlagOn( ulParseFlags, AFS_PARSE_FLAG_FREE_FILE_BUFFER))
                    {

                        SetFlag( pCcb->Flags, CCB_FLAG_FREE_FULL_PATHNAME);

                        ClearFlag( ulParseFlags, AFS_PARSE_FLAG_FREE_FILE_BUFFER);
                    }
                }

                if( bOpenedReparsePoint)
                {
                    SetFlag( pCcb->Flags, CCB_FLAG_MASK_OPENED_REPARSE_POINT);
                }

                AFSDbgLogMsg( AFS_SUBSYSTEM_DIRENTRY_REF_COUNTING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSCreate Count on %wZ DE %p Ccb %p Cnt %d\n",
                              &pCcb->DirectoryCB->NameInformation.FileName,
                              pCcb->DirectoryCB,
                              pCcb,
                              pCcb->DirectoryCB->OpenReferenceCount);

                ASSERT( pCcb->DirectoryCB->OpenReferenceCount > 0);

                pCcb->CurrentDirIndex = 0;

                if( !BooleanFlagOn( ulParseFlags, AFS_PARSE_FLAG_ROOT_ACCESS))
                {

                    SetFlag( pCcb->Flags, CCB_FLAG_RETURN_RELATIVE_ENTRIES);
                }

                //
                // Save off the name array for this instance
                //

                pCcb->NameArray = pNameArray;

                pNameArray = NULL;
            }

            //
            // If we make it here then init the FO for the request.
            //

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_VERBOSE_2,
                          "AFSCommonCreate (%08lX) FileObject %08lX FsContext %08lX FsContext2 %08lX\n",
                          Irp,
                          pFileObject,
                          pFcb,
                          pCcb);

            pFileObject->FsContext = (void *)pFcb;

            pFileObject->FsContext2 = (void *)pCcb;

            if( pFcb != NULL)
            {

                ASSERT( pFcb->OpenHandleCount > 0);

                ClearFlag( pFcb->Flags, AFS_FCB_FILE_CLOSED);

                //
                // For files perform additional processing
                //

                if( pFcb->Header.NodeTypeCode == AFS_FILE_FCB)
                {
                    pFileObject->SectionObjectPointer = &pFcb->NPFcb->SectionObjectPointers;
                }

                //
                // If the user did not request nobuffering then mark the FO as cacheable
                //

                if( bNoIntermediateBuffering)
                {

                    pFileObject->Flags |= FO_NO_INTERMEDIATE_BUFFERING;
                }
                else
                {

                    pFileObject->Flags |= FO_CACHE_SUPPORTED;
                }

                //
                // If the file was opened for execution then we need to set the bit in the FO
                //

                if( BooleanFlagOn( *pDesiredAccess,
                                   FILE_EXECUTE))
                {

                    SetFlag( pFileObject->Flags, FO_FILE_FAST_IO_READ);
                }

                //
                // Update the last access time
                //

                KeQuerySystemTime( &pFcb->ObjectInformation->LastAccessTime);

                if( pCcb != NULL)
                {
                    AFSInsertCcb( pFcb,
                                  pCcb);
                }
            }
            else
            {

                AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                              AFS_TRACE_LEVEL_ERROR,
                              "AFSCommonCreate (%08lX) Returning with NULL Fcb FileObject %08lX FsContext %08lX FsContext2 %08lX\n",
                              Irp,
                              pFileObject,
                              pFcb,
                              pCcb);
            }
        }
        else
        {
            if( NT_SUCCESS( ntStatus) &&
                ntStatus == STATUS_REPARSE)
            {

                AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                              AFS_TRACE_LEVEL_ERROR,
                              "AFSCommonCreate (%08lX) STATUS_REPARSE FileObject %08lX FsContext %08lX FsContext2 %08lX\n",
                              Irp,
                              pFileObject,
                              pFcb,
                              pCcb);
            }

            //
            // Free up the sub name if we have one
            //

            if( uniSubstitutedPathName.Buffer != NULL)
            {

                AFSExFreePool( uniSubstitutedPathName.Buffer);

                ClearFlag( ulParseFlags, AFS_PARSE_FLAG_FREE_FILE_BUFFER);
            }
        }

        //
        // Free up the name array ...
        //

        if( pNameArray != NULL)
        {

            AFSFreeNameArray( pNameArray);
        }

        if( BooleanFlagOn( ulParseFlags, AFS_PARSE_FLAG_FREE_FILE_BUFFER))
        {

            AFSExFreePool( uniRootFileName.Buffer);
        }

        if( bReleaseVolume)
        {

            InterlockedDecrement( &pVolumeCB->VolumeReferenceCount);

            AFSDbgLogMsg( AFS_SUBSYSTEM_VOLUME_REF_COUNTING,
                          AFS_TRACE_LEVEL_VERBOSE,
                          "AFSCommonCreate Decrement count on Volume %08lX Cnt %d\n",
                          pVolumeCB,
                          pVolumeCB->VolumeReferenceCount);
        }

        //
        // Setup the Irp for completion, the Information has been set previously
        //

        Irp->IoStatus.Status = ntStatus;
    }

    return ntStatus;
}

NTSTATUS
AFSOpenRedirector( IN PIRP Irp,
                   IN AFSFcb **Fcb,
                   IN AFSCcb **Ccb)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;

    __Enter
    {

        //
        // Initialize the Ccb for the file.
        //

        ntStatus = AFSInitCcb( Ccb);

        if( !NT_SUCCESS( ntStatus))
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSOpenRedirector (%08lX) Failed to allocate Ccb\n",
                          Irp);

            try_return( ntStatus);
        }

        //
        // Setup the Ccb
        //

        (*Ccb)->DirectoryCB = AFSRedirectorRoot->DirectoryCB;

        //
        // Increment the open count on this Fcb
        //

        InterlockedIncrement( &AFSRedirectorRoot->RootFcb->OpenReferenceCount);

        AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSOpenRedirector Increment count on Fcb %08lX Cnt %d\n",
                      AFSRedirectorRoot->RootFcb,
                      AFSRedirectorRoot->RootFcb->OpenReferenceCount);

        InterlockedIncrement( &AFSRedirectorRoot->RootFcb->OpenHandleCount);

        AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSOpenRedirector Increment handle count on Fcb %08lX Cnt %d\n",
                      AFSRedirectorRoot->RootFcb,
                      AFSRedirectorRoot->RootFcb->OpenHandleCount);

        *Fcb = AFSRedirectorRoot->RootFcb;

        InterlockedIncrement( &(*Ccb)->DirectoryCB->OpenReferenceCount);

        //
        // Return the open result for this file
        //

        Irp->IoStatus.Information = FILE_OPENED;

try_exit:

        NOTHING;
    }

    return ntStatus;
}

NTSTATUS
AFSOpenAFSRoot( IN PIRP Irp,
                IN AFSFcb **Fcb,
                IN AFSCcb **Ccb)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;

    __Enter
    {

        //
        // Initialize the Ccb for the file.
        //

        ntStatus = AFSInitCcb( Ccb);

        if( !NT_SUCCESS( ntStatus))
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSOpenAFSRoot (%08lX) Failed to allocate Ccb\n",
                          Irp);

            try_return( ntStatus);
        }

        //
        // Setup the Ccb
        //

        (*Ccb)->DirectoryCB = AFSGlobalRoot->DirectoryCB;

        //
        // Increment the open count on this Fcb
        //

        InterlockedIncrement( &AFSGlobalRoot->RootFcb->OpenReferenceCount);

        AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSOpenAFSRoot Increment count on Fcb %08lX Cnt %d\n",
                      AFSGlobalRoot->RootFcb,
                      AFSGlobalRoot->RootFcb->OpenReferenceCount);

        InterlockedIncrement( &AFSGlobalRoot->RootFcb->OpenHandleCount);

        AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSOpenAFSRoot Increment handle count on Fcb %08lX Cnt %d\n",
                      AFSGlobalRoot->RootFcb,
                      AFSGlobalRoot->RootFcb->OpenHandleCount);

        *Fcb = AFSGlobalRoot->RootFcb;

        //
        // Return the open result for this file
        //

        Irp->IoStatus.Information = FILE_OPENED;

try_exit:

        NOTHING;
    }

    return ntStatus;
}

NTSTATUS
AFSOpenRoot( IN PIRP Irp,
             IN AFSVolumeCB *VolumeCB,
             IN GUID *AuthGroup,
             OUT AFSFcb **RootFcb,
             OUT AFSCcb **Ccb)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    PFILE_OBJECT pFileObject = NULL;
    PIO_STACK_LOCATION pIrpSp = IoGetCurrentIrpStackLocation( Irp);
    PACCESS_MASK pDesiredAccess = NULL;
    USHORT usShareAccess;
    BOOLEAN bAllocatedCcb = FALSE;
    BOOLEAN bReleaseFcb = FALSE;
    AFSFileOpenCB   stOpenCB;
    AFSFileOpenResultCB stOpenResultCB;
    ULONG       ulResultLen = 0;

    __Enter
    {

        pDesiredAccess = &pIrpSp->Parameters.Create.SecurityContext->DesiredAccess;
        usShareAccess = pIrpSp->Parameters.Create.ShareAccess;

        pFileObject = pIrpSp->FileObject;

        //
        // Check if we should go and retrieve updated information for the node
        //

        ntStatus = AFSValidateEntry( VolumeCB->DirectoryCB,
                                     AuthGroup,
                                     TRUE,
                                     FALSE);

        if( !NT_SUCCESS( ntStatus))
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSOpenRoot (%08lX) Failed to validate root entry Status %08lX\n",
                          Irp,
                          ntStatus);

            try_return( ntStatus);
        }

        //
        // Check with the service that we can open the file
        //

        RtlZeroMemory( &stOpenCB,
                       sizeof( AFSFileOpenCB));

        stOpenCB.DesiredAccess = *pDesiredAccess;

        stOpenCB.ShareAccess = usShareAccess;

        stOpenResultCB.GrantedAccess = 0;

        ulResultLen = sizeof( AFSFileOpenResultCB);

        ntStatus = AFSProcessRequest( AFS_REQUEST_TYPE_OPEN_FILE,
                                      AFS_REQUEST_FLAG_SYNCHRONOUS | AFS_REQUEST_FLAG_HOLD_FID,
                                      AuthGroup,
                                      NULL,
                                      &VolumeCB->ObjectInformation.FileId,
                                      (void *)&stOpenCB,
                                      sizeof( AFSFileOpenCB),
                                      (void *)&stOpenResultCB,
                                      &ulResultLen);

        if( !NT_SUCCESS( ntStatus))
        {

            UNICODE_STRING uniGUID;

            uniGUID.Length = 0;
            uniGUID.MaximumLength = 0;
            uniGUID.Buffer = NULL;

            if( AuthGroup != NULL)
            {
                RtlStringFromGUID( *AuthGroup,
                                   &uniGUID);
            }       

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSOpenRoot (%08lX) Failed open in service volume %08lX-%08lX AuthGroup %wZ Status %08lX\n",
                          Irp,
                          VolumeCB->ObjectInformation.FileId.Cell,
                          VolumeCB->ObjectInformation.FileId.Volume,
                          &uniGUID,
                          ntStatus);

            if( AuthGroup != NULL)
            {
                RtlFreeUnicodeString( &uniGUID);
            }

            try_return( ntStatus);
        }

        //
        // If the entry is not initialized then do it now
        //

        if( !BooleanFlagOn( VolumeCB->ObjectInformation.Flags, AFS_OBJECT_FLAGS_DIRECTORY_ENUMERATED))
        {

            AFSAcquireExcl( VolumeCB->ObjectInformation.Specific.Directory.DirectoryNodeHdr.TreeLock,
                            TRUE);

            if( !BooleanFlagOn( VolumeCB->ObjectInformation.Flags, AFS_OBJECT_FLAGS_DIRECTORY_ENUMERATED))
            {

                ntStatus = AFSEnumerateDirectory( AuthGroup,
                                                  &VolumeCB->ObjectInformation,
                                                  TRUE);

                if( !NT_SUCCESS( ntStatus))
                {

                    AFSReleaseResource( VolumeCB->ObjectInformation.Specific.Directory.DirectoryNodeHdr.TreeLock);

                    AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                                  AFS_TRACE_LEVEL_ERROR,
                                  "AFSOpenRoot (%08lX) Failed to enumerate directory Status %08lX\n",
                                  Irp,
                                  ntStatus);

                    try_return( ntStatus);
                }

                SetFlag( VolumeCB->ObjectInformation.Flags, AFS_OBJECT_FLAGS_DIRECTORY_ENUMERATED);
            }

            AFSReleaseResource( VolumeCB->ObjectInformation.Specific.Directory.DirectoryNodeHdr.TreeLock);
        }

        //
        // If the root fcb has been initialized then check access otherwise
        // init the volume fcb
        //

        if( VolumeCB->RootFcb == NULL)
        {

            ntStatus = AFSInitRootFcb( (ULONGLONG)PsGetCurrentProcessId(),
                                       VolumeCB);

            if( !NT_SUCCESS( ntStatus))
            {

                try_return( ntStatus);
            }
        }
        else
        {

            AFSAcquireExcl( VolumeCB->RootFcb->Header.Resource,
                            TRUE);
        }

        bReleaseFcb = TRUE;

        //
        // If there are current opens on the Fcb, check the access.
        //

        if( VolumeCB->RootFcb->OpenHandleCount > 0)
        {

            ntStatus = IoCheckShareAccess( *pDesiredAccess,
                                           usShareAccess,
                                           pFileObject,
                                           &VolumeCB->RootFcb->ShareAccess,
                                           FALSE);

            if( !NT_SUCCESS( ntStatus))
            {

                AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                              AFS_TRACE_LEVEL_ERROR,
                              "AFSOpenRoot (%08lX) Access check failure Status %08lX\n",
                              Irp,
                              ntStatus);

                try_return( ntStatus);
            }
        }

        //
        // Initialize the Ccb for the file.
        //

        ntStatus = AFSInitCcb( Ccb);

        if( !NT_SUCCESS( ntStatus))
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSOpenRoot (%08lX) Failed to allocate Ccb Status %08lX\n",
                          Irp,
                          ntStatus);

            try_return( ntStatus);
        }

        bAllocatedCcb = TRUE;

        //
        // Setup the ccb
        //

        (*Ccb)->DirectoryCB = VolumeCB->DirectoryCB;

        (*Ccb)->GrantedAccess = *pDesiredAccess;

        //
        // OK, update the share access on the fileobject
        //

        if( VolumeCB->RootFcb->OpenHandleCount > 0)
        {

            IoUpdateShareAccess( pFileObject,
                                 &VolumeCB->RootFcb->ShareAccess);
        }
        else
        {

            //
            // Set the access
            //

            IoSetShareAccess( *pDesiredAccess,
                              usShareAccess,
                              pFileObject,
                              &VolumeCB->RootFcb->ShareAccess);
        }

        //
        // Increment the open count on this Fcb
        //

        InterlockedIncrement( &VolumeCB->RootFcb->OpenReferenceCount);

        AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSOpenRoot Increment count on Fcb %08lX Cnt %d\n",
                      VolumeCB->RootFcb,
                      VolumeCB->RootFcb->OpenReferenceCount);

        InterlockedIncrement( &VolumeCB->RootFcb->OpenHandleCount);

        AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSOpenRoot Increment handle count on Fcb %08lX Cnt %d\n",
                      VolumeCB->RootFcb,
                      VolumeCB->RootFcb->OpenHandleCount);

        //
        // Indicate the object is held
        //

        SetFlag( VolumeCB->ObjectInformation.Flags, AFS_OBJECT_HELD_IN_SERVICE);

        //
        // Return the open result for this file
        //

        Irp->IoStatus.Information = FILE_OPENED;

        *RootFcb = VolumeCB->RootFcb;

try_exit:

        if( bReleaseFcb)
        {

            AFSReleaseResource( VolumeCB->RootFcb->Header.Resource);
        }

        if( !NT_SUCCESS( ntStatus))
        {

            if( bAllocatedCcb)
            {

                AFSRemoveCcb( NULL,
                              *Ccb);

                *Ccb = NULL;
            }

            Irp->IoStatus.Information = 0;
        }
    }

    return ntStatus;
}

NTSTATUS
AFSProcessCreate( IN PIRP               Irp,
                  IN GUID              *AuthGroup,
                  IN AFSVolumeCB       *VolumeCB,
                  IN AFSDirectoryCB    *ParentDirCB,
                  IN PUNICODE_STRING    FileName,
                  IN PUNICODE_STRING    ComponentName,
                  IN PUNICODE_STRING    FullFileName,
                  OUT AFSFcb          **Fcb,
                  OUT AFSCcb          **Ccb)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    PFILE_OBJECT pFileObject = NULL;
    PIO_STACK_LOCATION pIrpSp = IoGetCurrentIrpStackLocation( Irp);
    ULONG ulOptions = 0;
    ULONG ulShareMode = 0;
    ULONG ulAccess = 0;
    ULONG ulAttributes = 0;
    LARGE_INTEGER   liAllocationSize = {0,0};
    BOOLEAN bFileCreated = FALSE, bReleaseFcb = FALSE, bAllocatedCcb = FALSE;
    BOOLEAN bAllocatedFcb = FALSE;
    PACCESS_MASK pDesiredAccess = NULL;
    USHORT usShareAccess;
    AFSDirectoryCB *pDirEntry = NULL;
    AFSObjectInfoCB *pParentObjectInfo = NULL;
    AFSObjectInfoCB *pObjectInfo = NULL;

    __Enter
    {

        pDesiredAccess = &pIrpSp->Parameters.Create.SecurityContext->DesiredAccess;
        usShareAccess = pIrpSp->Parameters.Create.ShareAccess;

        pFileObject = pIrpSp->FileObject;

        //
        // Extract out the options
        //

        ulOptions = pIrpSp->Parameters.Create.Options;

        //
        // We pass all attributes they want to apply to the file to the create
        //

        ulAttributes = pIrpSp->Parameters.Create.FileAttributes;

        //
        // If this is a directory create then set the attribute correctly
        //

        if( ulOptions & FILE_DIRECTORY_FILE)
        {

            ulAttributes |= FILE_ATTRIBUTE_DIRECTORY;
        }

        AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSProcessCreate (%08lX) Creating file %wZ Attributes %08lX\n",
                      Irp,
                      FullFileName,
                      ulAttributes);

        if( BooleanFlagOn( VolumeCB->VolumeInformation.Characteristics, FILE_READ_ONLY_DEVICE))
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSProcessCreate Request failed due to read only volume %wZ\n",
                          FullFileName);

            try_return( ntStatus = STATUS_ACCESS_DENIED);
        }

        pParentObjectInfo = ParentDirCB->ObjectInformation;

        //
        // Allocate and insert the direntry into the parent node
        //

        ntStatus = AFSCreateDirEntry( AuthGroup,
                                      pParentObjectInfo,
                                      ParentDirCB,
                                      FileName,
                                      ComponentName,
                                      ulAttributes,
                                      &pDirEntry);

        if( !NT_SUCCESS( ntStatus))
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSProcessCreate (%08lX) Failed to create directory entry %wZ Status %08lX\n",
                          Irp,
                          FullFileName,
                          ntStatus);

            try_return( ntStatus);
        }

        bFileCreated = TRUE;

        pObjectInfo = pDirEntry->ObjectInformation;

        if( BooleanFlagOn( pObjectInfo->Flags, AFS_OBJECT_FLAGS_NOT_EVALUATED) ||
            pObjectInfo->FileType == AFS_FILE_TYPE_UNKNOWN)
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_VERBOSE,
                          "AFSProcessCreate (%08lX) Evaluating object %wZ FID %08lX-%08lX-%08lX-%08lX\n",
                          Irp,
                          &pDirEntry->NameInformation.FileName,
                          pObjectInfo->FileId.Cell,
                          pObjectInfo->FileId.Volume,
                          pObjectInfo->FileId.Vnode,
                          pObjectInfo->FileId.Unique);

            ntStatus = AFSEvaluateNode( AuthGroup,
                                        pDirEntry);

            if( !NT_SUCCESS( ntStatus))
            {

                AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                              AFS_TRACE_LEVEL_ERROR,
                              "AFSProcessCreate (%08lX) Failed to evaluate object %wZ FID %08lX-%08lX-%08lX-%08lX Status %08lX\n",
                              Irp,
                              &pDirEntry->NameInformation.FileName,
                              pObjectInfo->FileId.Cell,
                              pObjectInfo->FileId.Volume,
                              pObjectInfo->FileId.Vnode,
                              pObjectInfo->FileId.Unique,
                              ntStatus);

                try_return( ntStatus);
            }

            ClearFlag( pObjectInfo->Flags, AFS_OBJECT_FLAGS_NOT_EVALUATED);
        }

        //
        // We may have raced and the Fcb is already created
        //

        if( pObjectInfo->Fcb != NULL)
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_VERBOSE,
                          "AFSProcessCreate (%08lX) Not allocating Fcb for file %wZ\n",
                          Irp,
                          FullFileName);

            *Fcb = pObjectInfo->Fcb;

            AFSAcquireExcl( &(*Fcb)->NPFcb->Resource,
                            TRUE);
        }
        else
        {

            //
            // Allocate and initialize the Fcb for the file.
            //

            ntStatus = AFSInitFcb( pDirEntry,
                                   Fcb);

            if( !NT_SUCCESS( ntStatus))
            {

                AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                              AFS_TRACE_LEVEL_ERROR,
                              "AFSProcessCreate (%08lX) Failed to initialize fcb %wZ Status %08lX\n",
                              Irp,
                              FullFileName,
                              ntStatus);

                try_return( ntStatus);
            }

            bAllocatedFcb = TRUE;
        }

        bReleaseFcb = TRUE;

        //
        // Initialize the Ccb for the file.
        //

        ntStatus = AFSInitCcb( Ccb);

        if( !NT_SUCCESS( ntStatus))
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSProcessCreate (%08lX) Failed to initialize ccb %wZ Status %08lX\n",
                          Irp,
                          FullFileName,
                          ntStatus);

            try_return( ntStatus);
        }

        bAllocatedCcb = TRUE;

        //
        // Initialize the Ccb
        //

        (*Ccb)->DirectoryCB = pDirEntry;

        (*Ccb)->GrantedAccess = *pDesiredAccess;

        //
        // If this is a file, update the headers filesizes.
        //

        if( (*Fcb)->Header.NodeTypeCode == AFS_FILE_FCB)
        {

            //
            // Update the sizes with the information passed in
            //

            (*Fcb)->Header.AllocationSize.QuadPart  = pObjectInfo->AllocationSize.QuadPart;
            (*Fcb)->Header.FileSize.QuadPart        = pObjectInfo->EndOfFile.QuadPart;
            (*Fcb)->Header.ValidDataLength.QuadPart = pObjectInfo->EndOfFile.QuadPart;

            //
            // Notify the system of the addition
            //

            AFSFsRtlNotifyFullReportChange( pParentObjectInfo,
                                            *Ccb,
									        (ULONG)FILE_NOTIFY_CHANGE_FILE_NAME,
									        (ULONG)FILE_ACTION_ADDED);

            (*Fcb)->NPFcb->Specific.File.ExtentsRequestStatus = STATUS_SUCCESS;
        }
        else if( (*Fcb)->Header.NodeTypeCode == AFS_DIRECTORY_FCB)
        {

            //
            // This is a new directory node so indicate it has been enumerated
            //

            SetFlag( pObjectInfo->Flags, AFS_OBJECT_FLAGS_DIRECTORY_ENUMERATED);

            //
            // And the parent directory entry
            //

            KeQuerySystemTime( &pParentObjectInfo->ChangeTime);

            //
            // Notify the system of the addition
            //

            AFSFsRtlNotifyFullReportChange( pParentObjectInfo,
                                            *Ccb,
									        (ULONG)FILE_NOTIFY_CHANGE_DIR_NAME,
									        (ULONG)FILE_ACTION_ADDED);
        }
        else if( (*Fcb)->Header.NodeTypeCode == AFS_MOUNT_POINT_FCB ||
                 (*Fcb)->Header.NodeTypeCode == AFS_SYMBOLIC_LINK_FCB ||
                 (*Fcb)->Header.NodeTypeCode == AFS_DFS_LINK_FCB)
        {

            //
            // And the parent directory entry
            //

            KeQuerySystemTime( &pParentObjectInfo->ChangeTime);

            //
            // Notify the system of the addition
            //

            AFSFsRtlNotifyFullReportChange( pParentObjectInfo,
                                            *Ccb,
									        (ULONG)FILE_NOTIFY_CHANGE_DIR_NAME,
									        (ULONG)FILE_ACTION_ADDED);
        }

        //
        // Save off the access for the open
        //

        IoSetShareAccess( *pDesiredAccess,
                          usShareAccess,
                          pFileObject,
                          &(*Fcb)->ShareAccess);

        //
        // Increment the open count on this Fcb
        //

        InterlockedIncrement( &(*Fcb)->OpenReferenceCount);

        AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSProcessCreate Increment count on Fcb %08lX Cnt %d\n",
                      *Fcb,
                      (*Fcb)->OpenReferenceCount);

        InterlockedIncrement( &(*Fcb)->OpenHandleCount);

        AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSProcessCreate Increment handle count on Fcb %08lX Cnt %d\n",
                      (*Fcb),
                      (*Fcb)->OpenHandleCount);

        //
        // Increment the open reference and handle on the parent node
        //

        InterlockedIncrement( &pObjectInfo->ParentObjectInformation->Specific.Directory.ChildOpenHandleCount);

        AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSProcessCreate Increment child open handle count on Parent object %08lX Cnt %d\n",
                      pObjectInfo->ParentObjectInformation,
                      pObjectInfo->ParentObjectInformation->Specific.Directory.ChildOpenHandleCount);

        InterlockedIncrement( &pObjectInfo->ParentObjectInformation->Specific.Directory.ChildOpenReferenceCount);

        AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSProcessCreate Increment child open ref count on Parent object %08lX Cnt %d\n",
                      pObjectInfo->ParentObjectInformation,
                      pObjectInfo->ParentObjectInformation->Specific.Directory.ChildOpenReferenceCount);

        if( ulOptions & FILE_DELETE_ON_CLOSE)
        {

            //
            // Mark it for delete on close
            //

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_VERBOSE,
                          "AFSProcessCreate (%08lX) Setting PENDING_DELETE flag in DirEntry %p Name %wZ\n",
                          Irp,
                          pDirEntry,
                          FullFileName);

            SetFlag( pDirEntry->Flags, AFS_DIR_ENTRY_PENDING_DELETE);
        }

        //
        // Indicate the object is locked in the service
        //

        SetFlag( pObjectInfo->Flags, AFS_OBJECT_HELD_IN_SERVICE);

        //
        // Return the open result for this file
        //

        Irp->IoStatus.Information = FILE_CREATED;

try_exit:

        //
        // If we created the Fcb we need to release the resources
        //

        if( bReleaseFcb)
        {

            AFSReleaseResource( &(*Fcb)->NPFcb->Resource);
        }

        if( !NT_SUCCESS( ntStatus))
        {

            if( bFileCreated)
            {

                AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSProcessCreate Create failed, removing DE %p from aprent object %p Status %08lX\n",
                              pDirEntry,
                              pParentObjectInfo,
                              ntStatus);

                //
                // Remove the dir entry from the parent
                //

                AFSAcquireExcl( pParentObjectInfo->Specific.Directory.DirectoryNodeHdr.TreeLock,
                                TRUE);

                SetFlag( pDirEntry->Flags, AFS_DIR_ENTRY_DELETED);

                //
                // Decrement the reference added during initialization of the DE
                //

                InterlockedDecrement( &pDirEntry->OpenReferenceCount);

                AFSDbgLogMsg( AFS_SUBSYSTEM_DIRENTRY_REF_COUNTING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSProcessCreate Decrement count on %wZ DE %p Cnt %d\n",
                              &pDirEntry->NameInformation.FileName,
                              pDirEntry,
                              pDirEntry->OpenReferenceCount);

                //
                // Pull the directory entry from the parent
                //

                AFSRemoveDirNodeFromParent( pParentObjectInfo,
                                            pDirEntry,
                                            FALSE); // Leave it in the enum list so the worker cleans it up

                AFSNotifyDelete( pDirEntry,
                                 AuthGroup,
                                 FALSE);

                //
                // Tag the parent as needing verification
                //

                SetFlag( pParentObjectInfo->Flags, AFS_OBJECT_FLAGS_VERIFY);

                AFSReleaseResource( pParentObjectInfo->Specific.Directory.DirectoryNodeHdr.TreeLock);
            }

            if( bAllocatedCcb)
            {

                AFSRemoveCcb( NULL,
                              *Ccb);
            }

            if( bAllocatedFcb)
            {

                AFSRemoveFcb( pObjectInfo->Fcb);

                pObjectInfo->Fcb = NULL;
            }

            *Fcb = NULL;

            *Ccb = NULL;
        }
    }

    return ntStatus;
}

NTSTATUS
AFSOpenTargetDirectory( IN PIRP Irp,
                        IN AFSVolumeCB *VolumeCB,
                        IN AFSDirectoryCB *ParentDirectoryCB,
                        IN AFSDirectoryCB *TargetDirectoryCB,
                        IN UNICODE_STRING *TargetName,
                        OUT AFSFcb **Fcb,
                        OUT AFSCcb **Ccb)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    PFILE_OBJECT pFileObject = NULL;
    PIO_STACK_LOCATION pIrpSp = IoGetCurrentIrpStackLocation( Irp);
    PACCESS_MASK pDesiredAccess = NULL;
    USHORT usShareAccess;
    BOOLEAN bAllocatedCcb = FALSE;
    BOOLEAN bReleaseFcb = FALSE, bAllocatedFcb = FALSE;
    AFSObjectInfoCB *pParentObject = NULL, *pTargetObject = NULL;
    UNICODE_STRING uniTargetName;

    __Enter
    {

        pDesiredAccess = &pIrpSp->Parameters.Create.SecurityContext->DesiredAccess;
        usShareAccess = pIrpSp->Parameters.Create.ShareAccess;

        pFileObject = pIrpSp->FileObject;

        AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSOpenTargetDirectory (%08lX) Processing file %wZ\n",
                      Irp,
                      TargetName);

        pParentObject = ParentDirectoryCB->ObjectInformation;

        if( pParentObject->FileType != AFS_FILE_TYPE_DIRECTORY)
        {

            try_return( ntStatus = STATUS_INVALID_PARAMETER);
        }

        //
        // Make sure we have an Fcb for the access
        //

        if( pParentObject->Fcb != NULL)
        {

            *Fcb = pParentObject->Fcb;

            AFSAcquireExcl( &(*Fcb)->NPFcb->Resource,
                            TRUE);
        }
        else
        {

            //
            // Allocate and initialize the Fcb for the file.
            //

            ntStatus = AFSInitFcb( ParentDirectoryCB,
                                   Fcb);

            if( !NT_SUCCESS( ntStatus))
            {

                AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                              AFS_TRACE_LEVEL_ERROR,
                              "AFSProcessCreate (%08lX) Failed to initialize fcb %wZ Status %08lX\n",
                              Irp,
                              &ParentDirectoryCB->NameInformation.FileName,
                              ntStatus);

                try_return( ntStatus);
            }

            bAllocatedFcb = TRUE;
        }

        bReleaseFcb = TRUE;

        //
        // If there are current opens on the Fcb, check the access.
        //

        if( pParentObject->Fcb->OpenHandleCount > 0)
        {

            ntStatus = IoCheckShareAccess( *pDesiredAccess,
                                           usShareAccess,
                                           pFileObject,
                                           &pParentObject->Fcb->ShareAccess,
                                           FALSE);

            if( !NT_SUCCESS( ntStatus))
            {

                AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                              AFS_TRACE_LEVEL_ERROR,
                              "AFSOpenTargetDirectory (%08lX) Access check failure %wZ Status %08lX\n",
                              Irp,
                              &ParentDirectoryCB->NameInformation.FileName,
                              ntStatus);

                try_return( ntStatus);
            }
        }

        //
        // Initialize the Ccb for the file.
        //

        ntStatus = AFSInitCcb( Ccb);

        if( !NT_SUCCESS( ntStatus))
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSOpenTargetDirectory (%08lX) Failed to initialize ccb %wZ Status %08lX\n",
                          Irp,
                          &ParentDirectoryCB->NameInformation.FileName,
                          ntStatus);

            try_return( ntStatus);
        }

        bAllocatedCcb = TRUE;

        //
        // Initialize the Ccb
        //

        (*Ccb)->DirectoryCB = ParentDirectoryCB;

        (*Ccb)->GrantedAccess = *pDesiredAccess;

        if( TargetDirectoryCB != NULL &&
            FsRtlAreNamesEqual( &TargetDirectoryCB->NameInformation.FileName,
                                TargetName,
                                FALSE,
                                NULL))
        {

            Irp->IoStatus.Information = FILE_EXISTS;

            uniTargetName = TargetDirectoryCB->NameInformation.FileName;
        }
        else
        {

            Irp->IoStatus.Information = FILE_DOES_NOT_EXIST;

            uniTargetName = *TargetName;
        }

        //
        // Update the filename in the fileobject for rename processing
        //

        RtlCopyMemory( pFileObject->FileName.Buffer,
                       uniTargetName.Buffer,
                       uniTargetName.Length);

        pFileObject->FileName.Length = uniTargetName.Length;

        //
        // OK, update the share access on the fileobject
        //

        if( pParentObject->Fcb->OpenHandleCount > 0)
        {

            IoUpdateShareAccess( pFileObject,
                                 &pParentObject->Fcb->ShareAccess);
        }
        else
        {

            //
            // Set the access
            //

            IoSetShareAccess( *pDesiredAccess,
                              usShareAccess,
                              pFileObject,
                              &pParentObject->Fcb->ShareAccess);
        }

        //
        // Increment the open count on this Fcb
        //

        InterlockedIncrement( &pParentObject->Fcb->OpenReferenceCount);

        AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSOpenTargetDirectory Increment count on Fcb %08lX Cnt %d\n",
                      pParentObject->Fcb,
                      pParentObject->Fcb->OpenReferenceCount);

        InterlockedIncrement( &pParentObject->Fcb->OpenHandleCount);

        AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSOpenTargetDirectory Increment handle count on Fcb %08lX Cnt %d\n",
                      pParentObject->Fcb,
                      pParentObject->Fcb->OpenHandleCount);

        //
        // Increment the open reference and handle on the parent node
        //

        if( pParentObject->ParentObjectInformation != NULL)
        {

            InterlockedIncrement( &pParentObject->ParentObjectInformation->Specific.Directory.ChildOpenHandleCount);

            AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                          AFS_TRACE_LEVEL_VERBOSE,
                          "AFSOpenTargetDirectory Increment child open handle count on Parent object %08lX Cnt %d\n",
                          pParentObject->ParentObjectInformation,
                          pParentObject->ParentObjectInformation->Specific.Directory.ChildOpenHandleCount);

            InterlockedIncrement( &pParentObject->ParentObjectInformation->Specific.Directory.ChildOpenReferenceCount);

            AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                          AFS_TRACE_LEVEL_VERBOSE,
                          "AFSOpenTargetDirectory Increment child open ref count on Parent object %08lX Cnt %d\n",
                          pParentObject->ParentObjectInformation,
                          pParentObject->ParentObjectInformation->Specific.Directory.ChildOpenReferenceCount);
        }

try_exit:

        if( bReleaseFcb)
        {

            AFSReleaseResource( &pParentObject->Fcb->NPFcb->Resource);
        }

        if( !NT_SUCCESS( ntStatus))
        {

            if( bAllocatedCcb)
            {

                AFSRemoveCcb( NULL,
                              *Ccb);
            }

            *Ccb = NULL;

            if( bAllocatedFcb)
            {

                AFSRemoveFcb( pParentObject->Fcb);

                pParentObject->Fcb = NULL;
            }

            *Fcb = NULL;
        }
    }

    return ntStatus;
}

NTSTATUS
AFSProcessOpen( IN PIRP Irp,
                IN GUID *AuthGroup,
                IN AFSVolumeCB *VolumeCB,
                IN AFSDirectoryCB *ParentDirCB,
                IN AFSDirectoryCB *DirectoryCB,
                OUT AFSFcb **Fcb,
                OUT AFSCcb **Ccb)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    PFILE_OBJECT pFileObject = NULL;
    PIO_STACK_LOCATION pIrpSp = IoGetCurrentIrpStackLocation( Irp);
    PACCESS_MASK pDesiredAccess = NULL;
    USHORT usShareAccess;
    BOOLEAN bAllocatedCcb = FALSE, bReleaseFcb = FALSE, bAllocatedFcb = FALSE;
    ULONG ulAdditionalFlags = 0, ulOptions = 0;
    AFSFileOpenCB   stOpenCB;
    AFSFileOpenResultCB stOpenResultCB;
    ULONG       ulResultLen = 0;
    AFSObjectInfoCB *pParentObjectInfo = NULL;
    AFSObjectInfoCB *pObjectInfo = NULL;
    ULONG       ulFileAccess = 0;
    AFSFileAccessReleaseCB stReleaseFileAccess;

    __Enter
    {

        pDesiredAccess = &pIrpSp->Parameters.Create.SecurityContext->DesiredAccess;
        usShareAccess = pIrpSp->Parameters.Create.ShareAccess;

        pFileObject = pIrpSp->FileObject;

        pParentObjectInfo = ParentDirCB->ObjectInformation;

        pObjectInfo = DirectoryCB->ObjectInformation;

        //
        // Check if the entry is pending a deletion
        //

        if( BooleanFlagOn( DirectoryCB->Flags, AFS_DIR_ENTRY_PENDING_DELETE))
        {

            ntStatus = STATUS_DELETE_PENDING;

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSProcessOpen (%08lX) Entry pending delete %wZ Status %08lX\n",
                          Irp,
                          &DirectoryCB->NameInformation.FileName,
                          ntStatus);

            try_return( ntStatus);
        }

        //
        // Extract out the options
        //

        ulOptions = pIrpSp->Parameters.Create.Options;

        //
        // Check if we should go and retrieve updated information for the node
        //

        ntStatus = AFSValidateEntry( DirectoryCB,
                                     AuthGroup,
                                     TRUE,
                                     FALSE);

        if( !NT_SUCCESS( ntStatus))
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSProcessOpen (%08lX) Failed to validate entry %wZ Status %08lX\n",
                          Irp,
                          &DirectoryCB->NameInformation.FileName,
                          ntStatus);

            try_return( ntStatus);
        }

        //
        // If this is marked for delete on close then be sure we can delete the entry
        //

        if( BooleanFlagOn( ulOptions, FILE_DELETE_ON_CLOSE))
        {

            ntStatus = AFSNotifyDelete( DirectoryCB,
                                        AuthGroup,
                                        TRUE);

            if( !NT_SUCCESS( ntStatus))
            {

                ntStatus = STATUS_CANNOT_DELETE;

                AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                              AFS_TRACE_LEVEL_ERROR,
                              "AFSProcessOpen (%08lX) Cannot delete entry %wZ marked for delete on close Status %08lX\n",
                              Irp,
                              &DirectoryCB->NameInformation.FileName,
                              ntStatus);

                try_return( ntStatus);
            }
        }

        //
        // Be sure we have an Fcb for the current object
        //

        if( pObjectInfo->Fcb == NULL)
        {

            ntStatus = AFSInitFcb( DirectoryCB,
                                   &pObjectInfo->Fcb);

            if( !NT_SUCCESS( ntStatus))
            {

                AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                              AFS_TRACE_LEVEL_ERROR,
                              "AFSProcessOpen (%08lX) Failed to init fcb on %wZ Status %08lX\n",
                              Irp,
                              &DirectoryCB->NameInformation.FileName,
                              ntStatus);

                try_return( ntStatus);
            }

            bAllocatedFcb = TRUE;
        }
        else
        {

            AFSAcquireExcl( pObjectInfo->Fcb->Header.Resource,
                            TRUE);
        }

        bReleaseFcb = TRUE;

        //
        // Reference the Fcb so it won't go away while we call into the service for processing
        //

        InterlockedIncrement( &pObjectInfo->Fcb->OpenReferenceCount);

        AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSProcessOpen Increment count on Fcb %08lX Cnt %d\n",
                      pObjectInfo->Fcb,
                      pObjectInfo->Fcb->OpenReferenceCount);

        //
        // Check access on the entry
        //

        if( pObjectInfo->Fcb->OpenHandleCount > 0)
        {

            ntStatus = IoCheckShareAccess( *pDesiredAccess,
                                           usShareAccess,
                                           pFileObject,
                                           &pObjectInfo->Fcb->ShareAccess,
                                           FALSE);

            if( !NT_SUCCESS( ntStatus))
            {

                AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                              AFS_TRACE_LEVEL_ERROR,
                              "AFSProcessOpen (%08lX) Failed to check share access on %wZ Status %08lX\n",
                              Irp,
                              &DirectoryCB->NameInformation.FileName,
                              ntStatus);

                try_return( ntStatus);
            }
        }

        //
        // Additional checks
        //

        if( pObjectInfo->Fcb->Header.NodeTypeCode == AFS_FILE_FCB)
        {

            //
            // If the caller is asking for write access then try to flush the image section
            //

            if( FlagOn( *pDesiredAccess, FILE_WRITE_DATA) ||
                BooleanFlagOn(ulOptions, FILE_DELETE_ON_CLOSE))
            {

                if( !MmFlushImageSection( &pObjectInfo->Fcb->NPFcb->SectionObjectPointers,
                                          MmFlushForWrite))
                {

                    ntStatus = BooleanFlagOn(ulOptions, FILE_DELETE_ON_CLOSE) ? STATUS_CANNOT_DELETE :
                                                                            STATUS_SHARING_VIOLATION;

                    AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                                  AFS_TRACE_LEVEL_ERROR,
                                  "AFSProcessOpen (%08lX) Failed to flush image section %wZ Status %08lX\n",
                                  Irp,
                                  &DirectoryCB->NameInformation.FileName,
                                  ntStatus);

                    try_return( ntStatus);
                }
            }

            if( BooleanFlagOn( ulOptions, FILE_DIRECTORY_FILE))
            {

                ntStatus = STATUS_NOT_A_DIRECTORY;

                AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                              AFS_TRACE_LEVEL_ERROR,
                              "AFSProcessOpen (%08lX) Attempt to open file as directory %wZ Status %08lX\n",
                              Irp,
                              &DirectoryCB->NameInformation.FileName,
                              ntStatus);

                try_return( ntStatus);
            }

            pObjectInfo->Fcb->NPFcb->Specific.File.ExtentsRequestStatus = STATUS_SUCCESS;
        }
        else if( pObjectInfo->Fcb->Header.NodeTypeCode == AFS_DIRECTORY_FCB ||
                 pObjectInfo->Fcb->Header.NodeTypeCode == AFS_ROOT_FCB)
        {

            if( BooleanFlagOn( ulOptions, FILE_NON_DIRECTORY_FILE))
            {

                ntStatus = STATUS_FILE_IS_A_DIRECTORY;

                AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                              AFS_TRACE_LEVEL_ERROR,
                              "AFSProcessOpen (%08lX) Attempt to open directory as file %wZ Status %08lX\n",
                              Irp,
                              &DirectoryCB->NameInformation.FileName,
                              ntStatus);

                try_return( ntStatus);
            }
        }
        else if( pObjectInfo->Fcb->Header.NodeTypeCode == AFS_MOUNT_POINT_FCB ||
                 pObjectInfo->Fcb->Header.NodeTypeCode == AFS_SYMBOLIC_LINK_FCB ||
                 pObjectInfo->Fcb->Header.NodeTypeCode == AFS_DFS_LINK_FCB)
        {

        }
        else
        {
            ASSERT( FALSE);
            try_return( ntStatus = STATUS_UNSUCCESSFUL);
        }

        //
        // Check with the service that we can open the file
        //

        stOpenCB.ParentId = pParentObjectInfo->FileId;

        stOpenCB.DesiredAccess = *pDesiredAccess;

        stOpenCB.ShareAccess = usShareAccess;

        stOpenCB.ProcessId = (ULONGLONG)PsGetCurrentProcessId();

        stOpenCB.Identifier = (ULONGLONG)pFileObject;

        stOpenResultCB.GrantedAccess = 0;

        ulResultLen = sizeof( AFSFileOpenResultCB);

        ntStatus = AFSProcessRequest( AFS_REQUEST_TYPE_OPEN_FILE,
                                      AFS_REQUEST_FLAG_SYNCHRONOUS | AFS_REQUEST_FLAG_HOLD_FID,
                                      AuthGroup,
                                      &DirectoryCB->NameInformation.FileName,
                                      &pObjectInfo->FileId,
                                      (void *)&stOpenCB,
                                      sizeof( AFSFileOpenCB),
                                      (void *)&stOpenResultCB,
                                      &ulResultLen);

        if( !NT_SUCCESS( ntStatus))
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSProcessOpen (%08lX) Failed open in service %wZ Status %08lX\n",
                          Irp,
                          &DirectoryCB->NameInformation.FileName,
                          ntStatus);

            try_return( ntStatus);
        }

        //
        // Save the granted access in case we need to release it below
        //

        ulFileAccess = stOpenResultCB.FileAccess;

        //
        // Check if there is a conflict
        //

        if( !AFSCheckAccess( *pDesiredAccess,
                             stOpenResultCB.GrantedAccess,
                             BooleanFlagOn( DirectoryCB->ObjectInformation->FileAttributes, FILE_ATTRIBUTE_DIRECTORY)))
        {

            ntStatus = STATUS_ACCESS_DENIED;

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSProcessOpen (%08lX) Failed to check access from service Desired %08lX Granted %08lX Entry %wZ Status %08lX\n",
                          Irp,
                          *pDesiredAccess,
                          stOpenResultCB.GrantedAccess,
                          &DirectoryCB->NameInformation.FileName,
                          ntStatus);

            try_return( ntStatus);
        }

        //
        // Initialize the Ccb for the file.
        //

        ntStatus = AFSInitCcb( Ccb);

        if( !NT_SUCCESS( ntStatus))
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSProcessOpen (%08lX) Failed to initialize ccb %wZ Status %08lX\n",
                          Irp,
                          &DirectoryCB->NameInformation.FileName,
                          ntStatus);

            try_return( ntStatus);
        }

        bAllocatedCcb = TRUE;

        (*Ccb)->DirectoryCB = DirectoryCB;

        (*Ccb)->FileAccess = ulFileAccess;

        (*Ccb)->GrantedAccess = *pDesiredAccess;

        //
        // Perform the access check on the target if this is a mount point or symlink
        //

        if( pObjectInfo->Fcb->OpenHandleCount > 0)
        {

            IoUpdateShareAccess( pFileObject,
                                 &pObjectInfo->Fcb->ShareAccess);
        }
        else
        {

            //
            // Set the access
            //

            IoSetShareAccess( *pDesiredAccess,
                              usShareAccess,
                              pFileObject,
                              &pObjectInfo->Fcb->ShareAccess);
        }

        //
        // Increment the open count on this Fcb
        //

        InterlockedIncrement( &pObjectInfo->Fcb->OpenReferenceCount);

        AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSProcessOpen Increment2 count on Fcb %08lX Cnt %d\n",
                      pObjectInfo->Fcb,
                      pObjectInfo->Fcb->OpenReferenceCount);

        InterlockedIncrement( &pObjectInfo->Fcb->OpenHandleCount);

        AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSProcessOpen Increment handle count on Fcb %08lX Cnt %d\n",
                      pObjectInfo->Fcb,
                      pObjectInfo->Fcb->OpenHandleCount);

        //
        // Increment the open reference and handle on the parent node
        //

        InterlockedIncrement( &pObjectInfo->ParentObjectInformation->Specific.Directory.ChildOpenHandleCount);

        AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSProcessOpen Increment child open handle count on Parent object %08lX Cnt %d\n",
                      pObjectInfo->ParentObjectInformation,
                      pObjectInfo->ParentObjectInformation->Specific.Directory.ChildOpenHandleCount);

        InterlockedIncrement( &pObjectInfo->ParentObjectInformation->Specific.Directory.ChildOpenReferenceCount);

        AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSProcessOpen Increment child open ref count on Parent object %08lX Cnt %d\n",
                      pObjectInfo->ParentObjectInformation,
                      pObjectInfo->ParentObjectInformation->Specific.Directory.ChildOpenReferenceCount);

        if( BooleanFlagOn( ulOptions, FILE_DELETE_ON_CLOSE))
        {

            //
            // Mark it for delete on close
            //

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_VERBOSE,
                          "AFSProcessOpen (%08lX) Setting PENDING_DELETE flag in DirEntry %p Name %wZ\n",
                          Irp,
                          DirectoryCB,
                          &DirectoryCB->NameInformation.FileName);

            SetFlag( DirectoryCB->Flags, AFS_DIR_ENTRY_PENDING_DELETE);
        }

        //
        // Indicate the object is held
        //

        SetFlag( pObjectInfo->Flags, AFS_OBJECT_HELD_IN_SERVICE);

        //
        // Return the open result for this file
        //

        Irp->IoStatus.Information = FILE_OPENED;

        *Fcb = pObjectInfo->Fcb;

try_exit:

        if( bReleaseFcb)
        {

            //
            // Remove the reference we added initially
            //

            InterlockedDecrement( &pObjectInfo->Fcb->OpenReferenceCount);

            AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                          AFS_TRACE_LEVEL_VERBOSE,
                          "AFSProcessOpen Decrement count on Fcb %08lX Cnt %d\n",
                          pObjectInfo->Fcb,
                          pObjectInfo->Fcb->OpenReferenceCount);

            AFSReleaseResource( pObjectInfo->Fcb->Header.Resource);
        }

        if( !NT_SUCCESS( ntStatus))
        {

            if ( ulFileAccess > 0)
            {

                stReleaseFileAccess.ProcessId = (ULONGLONG)PsGetCurrentProcessId();

                stReleaseFileAccess.FileAccess = ulFileAccess;

                stReleaseFileAccess.Identifier = (ULONGLONG)pFileObject;

                AFSProcessRequest( AFS_REQUEST_TYPE_RELEASE_FILE_ACCESS,
                                   AFS_REQUEST_FLAG_SYNCHRONOUS,
                                   AuthGroup,
                                   &DirectoryCB->NameInformation.FileName,
                                   &pObjectInfo->FileId,
                                   (void *)&stReleaseFileAccess,
                                   sizeof( AFSFileAccessReleaseCB),
                                   NULL,
                                   NULL);
            }

            if( bAllocatedCcb)
            {

                AFSRemoveCcb( NULL,
                              *Ccb);
            }

            *Ccb = NULL;

            if( bAllocatedFcb)
            {

                AFSRemoveFcb( pObjectInfo->Fcb);

                pObjectInfo->Fcb = NULL;
            }

            *Fcb = NULL;
        }
    }

    return ntStatus;
}

NTSTATUS
AFSProcessOverwriteSupersede( IN PDEVICE_OBJECT DeviceObject,
                              IN PIRP           Irp,
                              IN AFSVolumeCB   *VolumeCB,
                              IN GUID          *AuthGroup,
                              IN AFSDirectoryCB *ParentDirCB,
                              IN AFSDirectoryCB *DirectoryCB,
                              OUT AFSFcb       **Fcb,
                              OUT AFSCcb       **Ccb)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    PIO_STACK_LOCATION pIrpSp = IoGetCurrentIrpStackLocation( Irp);
    PFILE_OBJECT pFileObject = NULL;
    LARGE_INTEGER liZero = {0,0};
    BOOLEAN bReleasePaging = FALSE, bReleaseFcb = FALSE;
    ULONG   ulAttributes = 0;
    LARGE_INTEGER liTime;
    ULONG ulCreateDisposition = 0;
    BOOLEAN bAllocatedCcb = FALSE, bAllocatedFcb = FALSE;
    PACCESS_MASK pDesiredAccess = NULL;
    USHORT usShareAccess;
    AFSObjectInfoCB *pParentObjectInfo = NULL;
    AFSObjectInfoCB *pObjectInfo = NULL;

    __Enter
    {

        pDesiredAccess = &pIrpSp->Parameters.Create.SecurityContext->DesiredAccess;
        usShareAccess = pIrpSp->Parameters.Create.ShareAccess;

        pFileObject = pIrpSp->FileObject;

        ulAttributes = pIrpSp->Parameters.Create.FileAttributes;

        ulCreateDisposition = (pIrpSp->Parameters.Create.Options >> 24) & 0x000000ff;

        if( BooleanFlagOn( VolumeCB->VolumeInformation.Characteristics, FILE_READ_ONLY_DEVICE))
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSProcessOverwriteSupersede Request failed on %wZ due to read only volume\n",
                          Irp,
                          &DirectoryCB->NameInformation.FileName);

            try_return( ntStatus = STATUS_ACCESS_DENIED);
        }

        pParentObjectInfo = ParentDirCB->ObjectInformation;

        pObjectInfo = DirectoryCB->ObjectInformation;

        //
        // Check if we should go and retrieve updated information for the node
        //

        ntStatus = AFSValidateEntry( DirectoryCB,
                                     AuthGroup,
                                     TRUE,
                                     FALSE);

        if( !NT_SUCCESS( ntStatus))
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSProcessOverwriteSupersede (%08lX) Failed to validate entry %wZ Status %08lX\n",
                          Irp,
                          &DirectoryCB->NameInformation.FileName,
                          ntStatus);

            try_return( ntStatus);
        }

        //
        // Be sure we have an Fcb for the object block
        //

        if( pObjectInfo->Fcb == NULL)
        {

            ntStatus = AFSInitFcb( DirectoryCB,
                                   Fcb);

            if( !NT_SUCCESS( ntStatus))
            {

                AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                              AFS_TRACE_LEVEL_ERROR,
                              "AFSProcessOverwriteSupersede (%08lX) Failed to initialize fcb %wZ Status %08lX\n",
                              Irp,
                              &DirectoryCB->NameInformation.FileName,
                              ntStatus);

                try_return( ntStatus);
            }

            bAllocatedFcb = TRUE;
        }
        else
        {

            AFSAcquireExcl( pObjectInfo->Fcb->Header.Resource,
                            TRUE);
        }

        bReleaseFcb = TRUE;

        //
        // Reference the Fcb so it won't go away while processing the request
        //

        InterlockedIncrement( &pObjectInfo->Fcb->OpenReferenceCount);

        AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSProcessOverwriteSupersede Increment count on Fcb %08lX Cnt %d\n",
                      pObjectInfo->Fcb,
                      pObjectInfo->Fcb->OpenReferenceCount);

        //
        // Check access on the entry
        //

        if( pObjectInfo->Fcb->OpenHandleCount > 0)
        {

            ntStatus = IoCheckShareAccess( *pDesiredAccess,
                                           usShareAccess,
                                           pFileObject,
                                           &pObjectInfo->Fcb->ShareAccess,
                                           FALSE);

            if( !NT_SUCCESS( ntStatus))
            {

                AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                              AFS_TRACE_LEVEL_ERROR,
                              "AFSProcessOverwriteSupersede (%08lX) Access check failure %wZ Status %08lX\n",
                              Irp,
                              &DirectoryCB->NameInformation.FileName,
                              ntStatus);

                try_return( ntStatus);
            }
        }

        //
        //  Before we actually truncate, check to see if the purge
        //  is going to fail.
        //

        if( !MmCanFileBeTruncated( &pObjectInfo->Fcb->NPFcb->SectionObjectPointers,
                                   &liZero))
        {

            ntStatus = STATUS_USER_MAPPED_FILE;

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSProcessOverwriteSupersede (%08lX) File user mapped %wZ Status %08lX\n",
                          Irp,
                          &DirectoryCB->NameInformation.FileName,
                          ntStatus);

            try_return( ntStatus);
        }

        //
        // Initialize the Ccb for the file.
        //

        ntStatus = AFSInitCcb( Ccb);

        if( !NT_SUCCESS( ntStatus))
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSProcessOverwriteSupersede (%08lX) Failed to initialize ccb %wZ Status %08lX\n",
                          Irp,
                          &DirectoryCB->NameInformation.FileName,
                          ntStatus);

            try_return( ntStatus);
        }

        bAllocatedCcb = TRUE;

        //
        // Initialize the Ccb
        //

        (*Ccb)->DirectoryCB = DirectoryCB;

        (*Ccb)->GrantedAccess = *pDesiredAccess;

        //
        // Need to purge any data currently in the cache
        //

        CcPurgeCacheSection( &pObjectInfo->Fcb->NPFcb->SectionObjectPointers,
                             NULL,
                             0,
                             FALSE);

        pObjectInfo->Fcb->Header.FileSize.QuadPart = 0;
        pObjectInfo->Fcb->Header.ValidDataLength.QuadPart = 0;
        pObjectInfo->Fcb->Header.AllocationSize.QuadPart = 0;

        pObjectInfo->EndOfFile.QuadPart = 0;
        pObjectInfo->AllocationSize.QuadPart = 0;

        //
        // Trim down the extents. We do this BEFORE telling the service
        // the file is truncated since there is a potential race between
        // a worker thread releasing extents and us trimming
        //

        AFSTrimExtents( pObjectInfo->Fcb,
                        &pObjectInfo->Fcb->Header.FileSize);

        KeQuerySystemTime( &pObjectInfo->ChangeTime);

        KeQuerySystemTime( &pObjectInfo->LastAccessTime);

        //KeQuerySystemTime( &pObjectInfo->CreationTime);

        KeQuerySystemTime( &pObjectInfo->LastWriteTime);

        ntStatus = AFSUpdateFileInformation( &pParentObjectInfo->FileId,
                                             pObjectInfo,
                                             AuthGroup);

        if( !NT_SUCCESS( ntStatus))
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSProcessOverwriteSupersede (%08lX) Failed to update file information %wZ Status %08lX\n",
                          Irp,
                          &DirectoryCB->NameInformation.FileName,
                          ntStatus);

            try_return( ntStatus);
        }

        AFSAcquireExcl( pObjectInfo->Fcb->Header.PagingIoResource,
                        TRUE);

        bReleasePaging = TRUE;

        pFileObject->SectionObjectPointer = &pObjectInfo->Fcb->NPFcb->SectionObjectPointers;

        pFileObject->FsContext = (void *)pObjectInfo->Fcb;

        pFileObject->FsContext2 = (void *)*Ccb;

        //
        // Set the update flag accordingly
        //

        SetFlag( pObjectInfo->Fcb->Flags, AFS_FCB_FLAG_FILE_MODIFIED |
                                          AFS_FCB_FLAG_UPDATE_CREATE_TIME |
                                          AFS_FCB_FLAG_UPDATE_CHANGE_TIME |
                                          AFS_FCB_FLAG_UPDATE_ACCESS_TIME |
                                          AFS_FCB_FLAG_UPDATE_LAST_WRITE_TIME);

        CcSetFileSizes( pFileObject,
                        (PCC_FILE_SIZES)&pObjectInfo->Fcb->Header.AllocationSize);

        AFSReleaseResource( pObjectInfo->Fcb->Header.PagingIoResource);

        bReleasePaging = FALSE;

        ulAttributes |= FILE_ATTRIBUTE_ARCHIVE;

        if( ulCreateDisposition == FILE_SUPERSEDE)
        {

            pObjectInfo->FileAttributes = ulAttributes;

        }
        else
        {

            pObjectInfo->FileAttributes |= ulAttributes;
        }

        //
        // Save off the access for the open
        //

        if( pObjectInfo->Fcb->OpenHandleCount > 0)
        {

            IoUpdateShareAccess( pFileObject,
                                 &pObjectInfo->Fcb->ShareAccess);
        }
        else
        {

            //
            // Set the access
            //

            IoSetShareAccess( *pDesiredAccess,
                              usShareAccess,
                              pFileObject,
                              &pObjectInfo->Fcb->ShareAccess);
        }

        //
        // Return the correct action
        //

        if( ulCreateDisposition == FILE_SUPERSEDE)
        {

            Irp->IoStatus.Information = FILE_SUPERSEDED;
        }
        else
        {

            Irp->IoStatus.Information = FILE_OVERWRITTEN;
        }

        //
        // Increment the open count on this Fcb.
        //

        InterlockedIncrement( &pObjectInfo->Fcb->OpenReferenceCount);

        AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSProcessOverwriteSupersede Increment2 count on Fcb %08lX Cnt %d\n",
                      pObjectInfo->Fcb,
                      pObjectInfo->Fcb->OpenReferenceCount);

        InterlockedIncrement( &pObjectInfo->Fcb->OpenHandleCount);

        AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSProcessOverwriteSupersede Increment handle count on Fcb %08lX Cnt %d\n",
                      pObjectInfo->Fcb,
                      pObjectInfo->Fcb->OpenHandleCount);

        //
        // Increment the open reference and handle on the parent node
        //

        InterlockedIncrement( &pObjectInfo->ParentObjectInformation->Specific.Directory.ChildOpenHandleCount);

        AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSProcessOverwriteSupersede Increment child open handle count on Parent object %08lX Cnt %d\n",
                      pObjectInfo->ParentObjectInformation,
                      pObjectInfo->ParentObjectInformation->Specific.Directory.ChildOpenHandleCount);

        InterlockedIncrement( &pObjectInfo->ParentObjectInformation->Specific.Directory.ChildOpenReferenceCount);

        AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSProcessOverwriteSupersede Increment child open ref count on Parent object %08lX Cnt %d\n",
                      pObjectInfo->ParentObjectInformation,
                      pObjectInfo->ParentObjectInformation->Specific.Directory.ChildOpenReferenceCount);

        *Fcb = pObjectInfo->Fcb;

try_exit:

        if( bReleasePaging)
        {

            AFSReleaseResource( pObjectInfo->Fcb->Header.PagingIoResource);
        }

        if( bReleaseFcb)
        {

            //
            // Remove the reference we added above to prevent tear down
            //

            InterlockedDecrement( &pObjectInfo->Fcb->OpenReferenceCount);

            AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                          AFS_TRACE_LEVEL_VERBOSE,
                          "AFSProcessOverwriteSupersede Decrement count on Fcb %08lX Cnt %d\n",
                          pObjectInfo->Fcb,
                          pObjectInfo->Fcb->OpenReferenceCount);

            AFSReleaseResource( pObjectInfo->Fcb->Header.Resource);
        }

        if( !NT_SUCCESS( ntStatus))
        {

            if( bAllocatedCcb)
            {

                AFSRemoveCcb( NULL,
                              *Ccb);
            }

            *Ccb = NULL;

            if( bAllocatedFcb)
            {

                AFSRemoveFcb( pObjectInfo->Fcb);

                pObjectInfo->Fcb = NULL;
            }

            *Fcb = NULL;
        }
    }

    return ntStatus;
}

NTSTATUS
AFSControlDeviceCreate( IN PIRP Irp)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;

    __Enter
    {

        //
        // For now, jsut let the open happen
        //

        Irp->IoStatus.Information = FILE_OPENED;
    }

    return ntStatus;
}

NTSTATUS
AFSOpenIOCtlFcb( IN PIRP Irp,
                 IN GUID *AuthGroup,
                 IN AFSDirectoryCB *ParentDirCB,
                 OUT AFSFcb **Fcb,
                 OUT AFSCcb **Ccb)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    PFILE_OBJECT pFileObject = NULL;
    PIO_STACK_LOCATION pIrpSp = IoGetCurrentIrpStackLocation( Irp);
    BOOLEAN bReleaseFcb = FALSE, bAllocatedCcb = FALSE, bAllocatedFcb = FALSE;
    UNICODE_STRING uniFullFileName;
    AFSPIOCtlOpenCloseRequestCB stPIOCtlOpen;
    AFSFileID stFileID;
    AFSObjectInfoCB *pParentObjectInfo = NULL;

    __Enter
    {

        pFileObject = pIrpSp->FileObject;

        pParentObjectInfo = ParentDirCB->ObjectInformation;

        //
        // If we haven't initialized the PIOCtl DirectoryCB for this directory then do it now
        //

        if( pParentObjectInfo->Specific.Directory.PIOCtlDirectoryCB == NULL)
        {

            ntStatus = AFSInitPIOCtlDirectoryCB( pParentObjectInfo);

            if( !NT_SUCCESS( ntStatus))
            {

                try_return( ntStatus);
            }
        }

        if( pParentObjectInfo->Specific.Directory.PIOCtlDirectoryCB->ObjectInformation->Fcb == NULL)
        {

            //
            // Allocate and initialize the Fcb for the file.
            //

            ntStatus = AFSInitFcb( pParentObjectInfo->Specific.Directory.PIOCtlDirectoryCB,
                                   Fcb);

            if( !NT_SUCCESS( ntStatus))
            {

                AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                              AFS_TRACE_LEVEL_ERROR,
                              "AFSOpenIOCtlFcb (%08lX) Failed to initialize fcb Status %08lX\n",
                              Irp,
                              ntStatus);

                try_return( ntStatus);
            }

            bAllocatedFcb = TRUE;
        }
        else
        {

            *Fcb = pParentObjectInfo->Specific.Directory.PIOCtlDirectoryCB->ObjectInformation->Fcb;

            AFSAcquireExcl( &(*Fcb)->NPFcb->Resource,
                            TRUE);
        }

        bReleaseFcb = TRUE;

        //
        // Initialize the Ccb for the file.
        //

        ntStatus = AFSInitCcb( Ccb);

        if( !NT_SUCCESS( ntStatus))
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSOpenIOCtlFcb (%08lX) Failed to initialize ccb Status %08lX\n",
                          Irp,
                          ntStatus);

            try_return( ntStatus);
        }

        bAllocatedCcb = TRUE;

        //
        // Setup the Ccb
        //

        (*Ccb)->DirectoryCB = pParentObjectInfo->Specific.Directory.PIOCtlDirectoryCB;

        //
        // Set the PIOCtl index
        //

        (*Ccb)->RequestID = InterlockedIncrement( &pParentObjectInfo->Specific.Directory.OpenRequestIndex);

        RtlZeroMemory( &stPIOCtlOpen,
                       sizeof( AFSPIOCtlOpenCloseRequestCB));

        stPIOCtlOpen.RequestId = (*Ccb)->RequestID;

        stPIOCtlOpen.RootId = pParentObjectInfo->VolumeCB->ObjectInformation.FileId;

        RtlZeroMemory( &stFileID,
                       sizeof( AFSFileID));

        //
        // The parent directory FID of the node
        //

        stFileID = pParentObjectInfo->FileId;

        //
        // Issue the open request to the service
        //

        ntStatus = AFSProcessRequest( AFS_REQUEST_TYPE_PIOCTL_OPEN,
                                      AFS_REQUEST_FLAG_SYNCHRONOUS,
                                      AuthGroup,
                                      NULL,
                                      &stFileID,
                                      (void *)&stPIOCtlOpen,
                                      sizeof( AFSPIOCtlOpenCloseRequestCB),
                                      NULL,
                                      NULL);

        if( !NT_SUCCESS( ntStatus))
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSOpenIOCtlFcb (%08lX) Failed service open Status %08lX\n",
                          Irp,
                          ntStatus);

            try_return( ntStatus);
        }

        //
        // Reference the directory entry
        //

        InterlockedIncrement( &((*Ccb)->DirectoryCB->OpenReferenceCount));

        AFSDbgLogMsg( AFS_SUBSYSTEM_DIRENTRY_REF_COUNTING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSOpenIOCtlFcb Increment count on %wZ DE %p Ccb %p Cnt %d\n",
                      &(*Ccb)->DirectoryCB->NameInformation.FileName,
                      (*Ccb)->DirectoryCB,
                      (*Ccb),
                      (*Ccb)->DirectoryCB->OpenReferenceCount);

        //
        // Increment the open reference and handle on the node
        //

        InterlockedIncrement( &(*Fcb)->OpenReferenceCount);

        AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSOpenIOCtlFcb Increment count on Fcb %08lX Cnt %d\n",
                      (*Fcb),
                      (*Fcb)->OpenReferenceCount);

        InterlockedIncrement( &(*Fcb)->OpenHandleCount);

        AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSOpenIOCtlFcb Increment handle count on Fcb %08lX Cnt %d\n",
                      (*Fcb),
                      (*Fcb)->OpenHandleCount);

        //
        // Increment the open reference and handle on the parent node
        //

        InterlockedIncrement( &pParentObjectInfo->Specific.Directory.ChildOpenHandleCount);

        AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSOpenIOCtlFcb Increment child open handle count on Parent object %08lX Cnt %d\n",
                      pParentObjectInfo,
                      pParentObjectInfo->Specific.Directory.ChildOpenHandleCount);

        InterlockedIncrement( &pParentObjectInfo->Specific.Directory.ChildOpenReferenceCount);

        AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSOpenIOCtlFcb Increment child open ref count on Parent object %08lX Cnt %d\n",
                      pParentObjectInfo,
                      pParentObjectInfo->Specific.Directory.ChildOpenReferenceCount);

        //
        // Return the open result for this file
        //

        Irp->IoStatus.Information = FILE_OPENED;

try_exit:

        //
        //Dereference the passed in parent since the returned dir entry
        // is already referenced
        //

        InterlockedDecrement( &ParentDirCB->OpenReferenceCount);

        AFSDbgLogMsg( AFS_SUBSYSTEM_DIRENTRY_REF_COUNTING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSOpenIOCtlFcb Decrement count on %wZ DE %p Ccb %p Cnt %d\n",
                      &ParentDirCB->NameInformation.FileName,
                      ParentDirCB,
                      NULL,
                      ParentDirCB->OpenReferenceCount);

        //
        // If we created the Fcb we need to release the resources
        //

        if( bReleaseFcb)
        {

            AFSReleaseResource( &(*Fcb)->NPFcb->Resource);
        }

        if( !NT_SUCCESS( ntStatus))
        {

            if( bAllocatedCcb)
            {

                AFSRemoveCcb( NULL,
                              *Ccb);

                *Ccb = NULL;
            }

            if( bAllocatedFcb)
            {

                //
                // Need to tear down this Fcb since it is not in the tree for the worker thread
                //

                AFSRemoveFcb( *Fcb);

                pParentObjectInfo->Specific.Directory.PIOCtlDirectoryCB->ObjectInformation->Fcb = NULL;
            }

            *Fcb = NULL;

            *Ccb = NULL;
        }
    }

    return ntStatus;
}

NTSTATUS
AFSOpenSpecialShareFcb( IN PIRP Irp,
                        IN GUID *AuthGroup,
                        IN AFSDirectoryCB *DirectoryCB,
                        OUT AFSFcb **Fcb,
                        OUT AFSCcb **Ccb)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    PFILE_OBJECT pFileObject = NULL;
    PIO_STACK_LOCATION pIrpSp = IoGetCurrentIrpStackLocation( Irp);
    BOOLEAN bReleaseFcb = FALSE, bAllocatedCcb = FALSE, bAllocateFcb = FALSE;
    AFSObjectInfoCB *pParentObjectInfo = NULL;
    AFSPipeOpenCloseRequestCB stPipeOpen;

    __Enter
    {

        pFileObject = pIrpSp->FileObject;

        AFSDbgLogMsg( AFS_SUBSYSTEM_PIPE_PROCESSING,
                      AFS_TRACE_LEVEL_VERBOSE_2,
                      "AFSOpenSpecialShareFcb (%08lX) Processing Share %wZ open\n",
                      Irp,
                      &DirectoryCB->NameInformation.FileName);

        pParentObjectInfo = DirectoryCB->ObjectInformation->ParentObjectInformation;

        if( DirectoryCB->ObjectInformation->Fcb == NULL)
        {

            //
            // Allocate and initialize the Fcb for the file.
            //

            ntStatus = AFSInitFcb( DirectoryCB,
                                   Fcb);

            if( !NT_SUCCESS( ntStatus))
            {

                AFSDbgLogMsg( AFS_SUBSYSTEM_PIPE_PROCESSING,
                              AFS_TRACE_LEVEL_ERROR,
                              "AFSOpenSpecialShareFcb (%08lX) Failed to initialize fcb Status %08lX\n",
                              Irp,
                              ntStatus);

                try_return( ntStatus);
            }

            bAllocateFcb = TRUE;
        }
        else
        {

            *Fcb = DirectoryCB->ObjectInformation->Fcb;

            AFSAcquireExcl( &(*Fcb)->NPFcb->Resource,
                            TRUE);
        }

        bReleaseFcb = TRUE;

        //
        // Initialize the Ccb for the file.
        //

        ntStatus = AFSInitCcb( Ccb);

        if( !NT_SUCCESS( ntStatus))
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_PIPE_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSOpenSpecialShareFcb (%08lX) Failed to initialize ccb Status %08lX\n",
                          Irp,
                          ntStatus);

            try_return( ntStatus);
        }

        bAllocatedCcb = TRUE;

        //
        // Setup the Ccb
        //

        (*Ccb)->DirectoryCB = DirectoryCB;

        //
        // Call the service to open the share
        //

        (*Ccb)->RequestID = InterlockedIncrement( &pParentObjectInfo->Specific.Directory.OpenRequestIndex);

        RtlZeroMemory( &stPipeOpen,
                       sizeof( AFSPipeOpenCloseRequestCB));

        stPipeOpen.RequestId = (*Ccb)->RequestID;

        stPipeOpen.RootId = pParentObjectInfo->VolumeCB->ObjectInformation.FileId;

        //
        // Issue the open request to the service
        //

        ntStatus = AFSProcessRequest( AFS_REQUEST_TYPE_PIPE_OPEN,
                                      AFS_REQUEST_FLAG_SYNCHRONOUS,
                                      AuthGroup,
                                      &DirectoryCB->NameInformation.FileName,
                                      NULL,
                                      (void *)&stPipeOpen,
                                      sizeof( AFSPipeOpenCloseRequestCB),
                                      NULL,
                                      NULL);

        if( !NT_SUCCESS( ntStatus))
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_PIPE_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSOpenSpecialShareFcb (%08lX) Failed service open Status %08lX\n",
                          Irp,
                          ntStatus);

            try_return( ntStatus);
        }

        //
        // Increment the open count on this Fcb
        //

        InterlockedIncrement( &(*Fcb)->OpenReferenceCount);

        AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSOpenSpecialShareFcb Increment count on Fcb %08lX Cnt %d\n",
                      (*Fcb),
                      (*Fcb)->OpenReferenceCount);

        InterlockedIncrement( &(*Fcb)->OpenHandleCount);

        AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSOpenSpecialShareFcb Increment handle count on Fcb %08lX Cnt %d\n",
                      (*Fcb),
                      (*Fcb)->OpenHandleCount);

        //
        // Increment the open reference and handle on the parent node
        //

        InterlockedIncrement( &pParentObjectInfo->Specific.Directory.ChildOpenHandleCount);

        AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSOpenSpecialShareFcb Increment child open handle count on Parent object %08lX Cnt %d\n",
                      pParentObjectInfo,
                      pParentObjectInfo->Specific.Directory.ChildOpenHandleCount);

        InterlockedIncrement( &pParentObjectInfo->Specific.Directory.ChildOpenReferenceCount);

        AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSOpenSpecialShareFcb Increment child open ref count on Parent object %08lX Cnt %d\n",
                      pParentObjectInfo,
                      pParentObjectInfo->Specific.Directory.ChildOpenReferenceCount);

        //
        // Return the open result for this file
        //

        Irp->IoStatus.Information = FILE_OPENED;

try_exit:

        if( bReleaseFcb)
        {

            AFSReleaseResource( &(*Fcb)->NPFcb->Resource);
        }

        if( !NT_SUCCESS( ntStatus))
        {

            if( bAllocatedCcb)
            {

                AFSRemoveCcb( NULL,
                              *Ccb);

                *Ccb = NULL;
            }

            if( bAllocateFcb)
            {

                //
                // Need to tear down this Fcb since it is not in the tree for the worker thread
                //

                AFSRemoveFcb( *Fcb);

                DirectoryCB->ObjectInformation->Fcb = NULL;
            }

            *Fcb = NULL;

            *Ccb = NULL;
        }
    }

    return ntStatus;
}
