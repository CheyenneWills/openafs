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
// File: AFSCleanup.cpp
//

#include "AFSCommon.h"

//
// Function: AFSCleanup
//
// Description:
//
//      This function is the IRP_MJ_CLEANUP dispatch handler
//
// Return:
//
//       A status is returned for the handling of this request
//

NTSTATUS
AFSCleanup( IN PDEVICE_OBJECT LibDeviceObject,
            IN PIRP Irp)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    AFSDeviceExt *pDeviceExt = NULL;
    IO_STACK_LOCATION *pIrpSp = IoGetCurrentIrpStackLocation( Irp);
    AFSFcb *pFcb = NULL;
    AFSCcb *pCcb = NULL;
    PFILE_OBJECT pFileObject = NULL;
    AFSFcb *pRootFcb = NULL;
    AFSDeviceExt *pControlDeviceExt = NULL;
    IO_STATUS_BLOCK stIoSB;
    AFSObjectInfoCB *pObjectInfo = NULL;
    AFSFileCleanupCB stFileCleanup;
    ULONG   ulNotificationFlags = 0;

    __try
    {

        if( AFSRDRDeviceObject == NULL)
        {

            //
            // Let this through, it's a cleanup on the library control device
            //

            try_return( ntStatus);
        }

        pDeviceExt = (AFSDeviceExt *)AFSRDRDeviceObject->DeviceExtension;
        pControlDeviceExt = (AFSDeviceExt *)AFSControlDeviceObject->DeviceExtension;

        //
        // Set some initial variables to make processing easier
        //

        pFileObject = pIrpSp->FileObject;

        pFcb = (AFSFcb *)pIrpSp->FileObject->FsContext;

        pCcb = (AFSCcb *)pIrpSp->FileObject->FsContext2;

        if( pFcb == NULL)
        {
            try_return( ntStatus);
        }

        pObjectInfo = pFcb->ObjectInformation;

        pRootFcb = pObjectInfo->VolumeCB->RootFcb;

        RtlZeroMemory( &stFileCleanup,
                       sizeof( AFSFileCleanupCB));

        stFileCleanup.ProcessId = (ULONGLONG)PsGetCurrentProcessId();

        stFileCleanup.Identifier = (ULONGLONG)pFileObject;

        //
        // Perform the cleanup functionality depending on the type of node it is
        //

        switch( pFcb->Header.NodeTypeCode)
        {

            case AFS_ROOT_ALL:
            {

                AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSCleanup Acquiring GlobalRoot lock %08lX EXCL %08lX\n",
                              &pFcb->NPFcb->Resource,
                              PsGetCurrentThread());

                AFSAcquireExcl( &pFcb->NPFcb->Resource,
                                  TRUE);

                ASSERT( pFcb->OpenHandleCount != 0);

                InterlockedDecrement( &pFcb->OpenHandleCount);

                AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSCleanup (RootAll) Decrement handle count on Fcb %08lX Cnt %d\n",
                              pFcb,
                              pFcb->OpenHandleCount);

                AFSReleaseResource( &pFcb->NPFcb->Resource);

                FsRtlNotifyCleanup( pControlDeviceExt->Specific.Control.NotifySync,
                                    &pControlDeviceExt->Specific.Control.DirNotifyList,
                                    pCcb);

                break;
            }

            case AFS_IOCTL_FCB:
            {

                AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSCleanup Acquiring PIOCtl lock %08lX EXCL %08lX\n",
                              &pFcb->NPFcb->Resource,
                              PsGetCurrentThread());

                AFSAcquireExcl( &pFcb->NPFcb->Resource,
                                  TRUE);

                ASSERT( pFcb->OpenHandleCount != 0);

                InterlockedDecrement( &pFcb->OpenHandleCount);

                AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSCleanup (IOCtl) Decrement handle count on Fcb %08lX Cnt %d\n",
                              pFcb,
                              pFcb->OpenHandleCount);

                //
                // Decrement the open child handle count
                //

                if( pObjectInfo->ParentObjectInformation != NULL &&
                    pObjectInfo->ParentObjectInformation->Specific.Directory.ChildOpenHandleCount > 0)
                {

                    InterlockedDecrement( &pObjectInfo->ParentObjectInformation->Specific.Directory.ChildOpenHandleCount);

                    AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                                  AFS_TRACE_LEVEL_VERBOSE,
                                  "AFSCleanup (IOCtl) Decrement child open handle count on Parent object %08lX Cnt %d\n",
                                  pObjectInfo->ParentObjectInformation,
                                  pObjectInfo->ParentObjectInformation->Specific.Directory.ChildOpenHandleCount);
                }

                //
                // And finally, release the Fcb if we acquired it.
                //

                AFSReleaseResource( &pFcb->NPFcb->Resource);

                break;
            }

            //
            // This Fcb represents a file
            //

            case AFS_FILE_FCB:
            {

                //
                // We may be performing some cleanup on the Fcb so grab it exclusive to ensure no collisions
                //

                AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSCleanup Acquiring Fcb lock %08lX EXCL %08lX\n",
                              &pFcb->NPFcb->Resource,
                              PsGetCurrentThread());

                AFSAcquireExcl( &pFcb->NPFcb->Resource,
                                TRUE);

                //
                // If the handle has write permission ...
                //

                if( (pCcb->GrantedAccess & FILE_WRITE_DATA) &&
                    CcIsFileCached( pIrpSp->FileObject))
                {

                    __try
                    {

                        CcFlushCache( &pFcb->NPFcb->SectionObjectPointers,
                                      NULL,
                                      0,
                                      &stIoSB);

                        if( !NT_SUCCESS( stIoSB.Status))
                        {

                            AFSDbgLogMsg( AFS_SUBSYSTEM_IO_PROCESSING,
                                          AFS_TRACE_LEVEL_ERROR,
                                          "AFSCleanup CcFlushCache failure %wZ FID %08lX-%08lX-%08lX-%08lX Status 0x%08lX Bytes 0x%08lX\n",
                                          &pCcb->FullFileName,
                                          pObjectInfo->FileId.Cell,
                                          pObjectInfo->FileId.Volume,
                                          pObjectInfo->FileId.Vnode,
                                          pObjectInfo->FileId.Unique,
                                          stIoSB.Status,
                                          stIoSB.Information);

                            ntStatus = stIoSB.Status;
                        }
                    }
                    __except( EXCEPTION_EXECUTE_HANDLER)
                    {

                        ntStatus = GetExceptionCode();
                    }
                }

                //
                // Uninitialize the cache map. This call is unconditional.
                //

                AFSDbgLogMsg( AFS_SUBSYSTEM_IO_PROCESSING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSCleanup Tearing down cache map for Fcb %08lX FileObject %08lX\n",
                              pFcb,
                              pFileObject);

                CcUninitializeCacheMap( pFileObject,
                                        NULL,
                                        NULL);

                //
                // Unlock all outstanding locks on the file, again, unconditionally
                //

                (VOID) FsRtlFastUnlockAll( &pFcb->Specific.File.FileLock,
                                           pFileObject,
                                           IoGetRequestorProcess( Irp),
                                           NULL);

                //
                // Tell the service to unlock all on the file
                //

                ulNotificationFlags |= AFS_REQUEST_FLAG_BYTE_RANGE_UNLOCK_ALL;

                //
                // Perform some final common processing
                //

                ASSERT( pFcb->OpenHandleCount != 0);

                InterlockedDecrement( &pFcb->OpenHandleCount);

                AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSCleanup (File) Decrement handle count on Fcb %08lX Cnt %d\n",
                              pFcb,
                              pFcb->OpenHandleCount);

                if( pFcb->ObjectInformation->ParentObjectInformation != NULL)
                {

                    stFileCleanup.ParentId = pFcb->ObjectInformation->ParentObjectInformation->FileId;
                }

                stFileCleanup.LastAccessTime = pObjectInfo->LastAccessTime;

                if( BooleanFlagOn( pFcb->Flags, AFS_FCB_FLAG_FILE_MODIFIED))
                {

                    stFileCleanup.AllocationSize = pObjectInfo->EndOfFile;

                    stFileCleanup.FileAttributes = pObjectInfo->FileAttributes;

                    if( BooleanFlagOn( pFcb->Flags, AFS_FCB_FLAG_UPDATE_CREATE_TIME))
                    {

                        stFileCleanup.CreateTime = pObjectInfo->CreationTime;

                        ClearFlag( pFcb->Flags, AFS_FCB_FLAG_UPDATE_CREATE_TIME);
                    }

                    if( BooleanFlagOn( pFcb->Flags, AFS_FCB_FLAG_UPDATE_CHANGE_TIME))
                    {

                        stFileCleanup.ChangeTime = pObjectInfo->ChangeTime;

                        ClearFlag( pFcb->Flags, AFS_FCB_FLAG_UPDATE_CHANGE_TIME);
                    }

                    if( BooleanFlagOn( pFcb->Flags, AFS_FCB_FLAG_UPDATE_ACCESS_TIME))
                    {

                        stFileCleanup.LastAccessTime = pObjectInfo->LastAccessTime;

                        ClearFlag( pFcb->Flags, AFS_FCB_FLAG_UPDATE_ACCESS_TIME);
                    }

                    if( BooleanFlagOn( pFcb->Flags, AFS_FCB_FLAG_UPDATE_LAST_WRITE_TIME))
                    {

                        stFileCleanup.LastWriteTime = pObjectInfo->LastWriteTime;

                        ClearFlag( pFcb->Flags, AFS_FCB_FLAG_UPDATE_LAST_WRITE_TIME | AFS_FCB_FLAG_UPDATE_WRITE_TIME);
                    }
                }

                if( BooleanFlagOn( pFcb->Flags, AFS_FCB_FLAG_UPDATE_WRITE_TIME))
                {

                    stFileCleanup.LastWriteTime = pObjectInfo->LastWriteTime;
                }

                //
                // If the count has dropped to zero and there is a pending delete
                // then delete the node
                //

                if( pFcb->OpenHandleCount == 0 &&
                    BooleanFlagOn( pCcb->DirectoryCB->Flags, AFS_DIR_ENTRY_PENDING_DELETE))
                {

                    //
                    // Stop anything possibly in process
                    //

                    AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                                  AFS_TRACE_LEVEL_VERBOSE,
                                  "AFSCleanup Acquiring Fcb extents lock %08lX EXCL %08lX\n",
                                  &pFcb->NPFcb->Specific.File.ExtentsResource,
                                  PsGetCurrentThread());

                    AFSAcquireExcl( &pObjectInfo->Fcb->NPFcb->Specific.File.ExtentsResource,
                                    TRUE);

                    pObjectInfo->Fcb->NPFcb->Specific.File.ExtentsRequestStatus = STATUS_FILE_DELETED;

                    KeSetEvent( &pObjectInfo->Fcb->NPFcb->Specific.File.ExtentsRequestComplete,
                                0,
                                FALSE);

                    AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                                  AFS_TRACE_LEVEL_VERBOSE,
                                  "AFSCleanup Releasing Fcb extents lock %08lX EXCL %08lX\n",
                                  &pFcb->NPFcb->Specific.File.ExtentsResource,
                                  PsGetCurrentThread());

                    AFSReleaseResource( &pObjectInfo->Fcb->NPFcb->Specific.File.ExtentsResource);

                    //
                    // Before telling the server about the deleted file, tear down all extents for
                    // the file
                    //

                    AFSTearDownFcbExtents( pFcb,
                                           &pCcb->AuthGroup);

                    ntStatus = STATUS_SUCCESS;

                    ulNotificationFlags |= AFS_REQUEST_FLAG_FILE_DELETED;

                    //
                    // Indicate the file access mode that is being released
                    //

                    stFileCleanup.FileAccess = pCcb->FileAccess;

                    //
                    // Push the request to the service
                    //

                    ntStatus = AFSProcessRequest( AFS_REQUEST_TYPE_CLEANUP_PROCESSING,
                                                  ulNotificationFlags | AFS_REQUEST_FLAG_SYNCHRONOUS,
                                                  &pCcb->AuthGroup,
                                                  &pCcb->DirectoryCB->NameInformation.FileName,
                                                  &pObjectInfo->FileId,
                                                  &stFileCleanup,
                                                  sizeof( AFSFileCleanupCB),
                                                  NULL,
                                                  NULL);

                    if( !NT_SUCCESS( ntStatus) &&
                        ntStatus != STATUS_OBJECT_NAME_NOT_FOUND)
                    {

                        AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                                      AFS_TRACE_LEVEL_ERROR,
                                      "AFSCleanup Failed to notify service of deleted file %wZ Status %08lX\n",
                                      &pCcb->FullFileName,
                                      ntStatus);

                        ntStatus = STATUS_SUCCESS;

                        ClearFlag( pCcb->DirectoryCB->Flags, AFS_DIR_ENTRY_PENDING_DELETE);
                    }
                    else
                    {

                        ntStatus = STATUS_SUCCESS;

                        AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                                      AFS_TRACE_LEVEL_VERBOSE,
                                      "AFSCleanup Setting DELETE flag in file %wZ Dir Entry %p\n",
                                      &pCcb->FullFileName,
                                      pCcb->DirectoryCB);

                        SetFlag( pCcb->DirectoryCB->Flags, AFS_DIR_ENTRY_DELETED);

                        ASSERT( pObjectInfo->ParentObjectInformation != NULL);

                        AFSFsRtlNotifyFullReportChange( pObjectInfo->ParentObjectInformation,
                                                        pCcb,
                                                        (ULONG)FILE_NOTIFY_CHANGE_FILE_NAME,
                                                        (ULONG)FILE_ACTION_REMOVED);

                        //
                        // Now that the service has the entry has deleted we need to remove it from the parent
                        // tree so another lookup on the node will fail
                        //

                        if( !BooleanFlagOn( pCcb->DirectoryCB->Flags, AFS_DIR_ENTRY_NOT_IN_PARENT_TREE))
                        {

                            AFSAcquireExcl( pObjectInfo->ParentObjectInformation->Specific.Directory.DirectoryNodeHdr.TreeLock,
                                            TRUE);

                            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                                          AFS_TRACE_LEVEL_VERBOSE,
                                          "AFSCleanup DE %p for %wZ removing entry\n",
                                          pCcb->DirectoryCB,
                                          &pCcb->DirectoryCB->NameInformation.FileName);

                            AFSRemoveNameEntry( pObjectInfo->ParentObjectInformation,
                                                pCcb->DirectoryCB);

                            AFSReleaseResource( pObjectInfo->ParentObjectInformation->Specific.Directory.DirectoryNodeHdr.TreeLock);
                        }
                        else
                        {

                            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                                          AFS_TRACE_LEVEL_VERBOSE,
                                          "AFSCleanup DE %p for %wZ NOT removing entry due to flag set\n",
                                          pCcb->DirectoryCB,
                                          &pCcb->DirectoryCB->NameInformation.FileName);
                        }
                    }
                }
                else
                {

                    if( BooleanFlagOn( pFcb->Flags, AFS_FCB_FLAG_FILE_MODIFIED))
                    {

                        ULONG ulNotifyFilter = 0;

                        ClearFlag( pFcb->Flags, AFS_FCB_FLAG_FILE_MODIFIED);

                        ulNotifyFilter |= (FILE_NOTIFY_CHANGE_ATTRIBUTES);

                        AFSFsRtlNotifyFullReportChange( pObjectInfo->ParentObjectInformation,
                                                        pCcb,
                                                        (ULONG)ulNotifyFilter,
                                                        (ULONG)FILE_ACTION_MODIFIED);
                    }

                    //
                    // Attempt to flush any dirty extents to the server. This may be a little
                    // aggressive, to flush whenever the handle is closed, but it ensures
                    // coherency.
                    //

                    if( (pCcb->GrantedAccess & FILE_WRITE_DATA) &&
                        pFcb->Specific.File.ExtentsDirtyCount != 0)
                    {

                        AFSFlushExtents( pFcb,
                                         &pCcb->AuthGroup);
                    }

                    if( pFcb->OpenHandleCount == 0)
                    {

                        //
                        // Wait for any outstanding queued flushes to complete
                        //

                        AFSWaitOnQueuedFlushes( pFcb);

                        ulNotificationFlags |= AFS_REQUEST_FLAG_FLUSH_FILE;
                    }

                    //
                    // Indicate the file access mode that is being released
                    //

                    stFileCleanup.FileAccess = pCcb->FileAccess;

                    //
                    // Push the request to the service
                    //

                    AFSProcessRequest( AFS_REQUEST_TYPE_CLEANUP_PROCESSING,
                                       ulNotificationFlags | AFS_REQUEST_FLAG_SYNCHRONOUS,
                                       &pCcb->AuthGroup,
                                       &pCcb->DirectoryCB->NameInformation.FileName,
                                       &pObjectInfo->FileId,
                                       &stFileCleanup,
                                       sizeof( AFSFileCleanupCB),
                                       NULL,
                                       NULL);
                }

                //
                // Remove the share access at this time since we may not get the close for sometime on this FO.
                //

                IoRemoveShareAccess( pFileObject,
                                     &pFcb->ShareAccess);

                //
                // We don't need the name array after the user closes the handle on the file
                //

                if( pCcb->NameArray != NULL)
                {

                    AFSFreeNameArray( pCcb->NameArray);

                    pCcb->NameArray = NULL;
                }

                //
                // Decrement the open child handle count
                //

                if( pObjectInfo->ParentObjectInformation != NULL)
                {

                    ASSERT( pObjectInfo->ParentObjectInformation->Specific.Directory.ChildOpenHandleCount > 0);

                    InterlockedDecrement( &pObjectInfo->ParentObjectInformation->Specific.Directory.ChildOpenHandleCount);

                    AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                                  AFS_TRACE_LEVEL_VERBOSE,
                                  "AFSCleanup (File) Decrement child open handle count on Parent object %08lX Cnt %d\n",
                                  pObjectInfo->ParentObjectInformation,
                                  pObjectInfo->ParentObjectInformation->Specific.Directory.ChildOpenHandleCount);
                }

                //
                // And finally, release the Fcb if we acquired it.
                //

                AFSReleaseResource( &pFcb->NPFcb->Resource);

                break;
            }

            //
            // Root or directory node
            //

            case AFS_ROOT_FCB:
            {

                //
                // Set the root Fcb to this node
                //

                pRootFcb = pFcb;

                //
                // Fall through to below
                //
            }

            case AFS_DIRECTORY_FCB:
            {

                //
                // We may be performing some cleanup on the Fcb so grab it exclusive to ensure no collisions
                //

                AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSCleanup Acquiring Dcb lock %08lX EXCL %08lX\n",
                              &pFcb->NPFcb->Resource,
                              PsGetCurrentThread());

                AFSAcquireExcl( &pFcb->NPFcb->Resource,
                                  TRUE);

                //
                // Perform some final common processing
                //

                ASSERT( pFcb->OpenHandleCount != 0);

                InterlockedDecrement( &pFcb->OpenHandleCount);

                AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSCleanup (Dir) Decrement handle count on Fcb %08lX Cnt %d\n",
                              pFcb,
                              pFcb->OpenHandleCount);

                if( pFcb->ObjectInformation->ParentObjectInformation != NULL)
                {

                    stFileCleanup.ParentId = pFcb->ObjectInformation->ParentObjectInformation->FileId;
                }

                stFileCleanup.LastAccessTime = pObjectInfo->LastAccessTime;

                if( BooleanFlagOn( pFcb->Flags, AFS_FCB_FLAG_FILE_MODIFIED))
                {

                    stFileCleanup.FileAttributes = pObjectInfo->FileAttributes;

                    if( BooleanFlagOn( pFcb->Flags, AFS_FCB_FLAG_UPDATE_CREATE_TIME))
                    {

                        stFileCleanup.CreateTime = pObjectInfo->CreationTime;

                        ClearFlag( pFcb->Flags, AFS_FCB_FLAG_UPDATE_CREATE_TIME);
                    }

                    if( BooleanFlagOn( pFcb->Flags, AFS_FCB_FLAG_UPDATE_CHANGE_TIME))
                    {

                        stFileCleanup.ChangeTime = pObjectInfo->ChangeTime;

                        ClearFlag( pFcb->Flags, AFS_FCB_FLAG_UPDATE_CHANGE_TIME);
                    }

                    if( BooleanFlagOn( pFcb->Flags, AFS_FCB_FLAG_UPDATE_ACCESS_TIME))
                    {

                        stFileCleanup.LastAccessTime = pObjectInfo->LastAccessTime;

                        ClearFlag( pFcb->Flags, AFS_FCB_FLAG_UPDATE_ACCESS_TIME);
                    }
                }

                //
                // If the count has dropped to zero and there is a pending delete
                // then delete the node
                //

                if( pFcb->OpenHandleCount == 0 &&
                    BooleanFlagOn( pCcb->DirectoryCB->Flags, AFS_DIR_ENTRY_PENDING_DELETE))
                {

                    //
                    // Try to notify the service about the delete
                    //

                    ulNotificationFlags |= AFS_REQUEST_FLAG_FILE_DELETED;

                    //
                    // Indicate the file access mode that is being released
                    //

                    stFileCleanup.FileAccess = pCcb->FileAccess;

                    //
                    // Push the request to the service
                    //

                    ntStatus = AFSProcessRequest( AFS_REQUEST_TYPE_CLEANUP_PROCESSING,
                                                  ulNotificationFlags | AFS_REQUEST_FLAG_SYNCHRONOUS,
                                                  &pCcb->AuthGroup,
                                                  &pCcb->DirectoryCB->NameInformation.FileName,
                                                  &pObjectInfo->FileId,
                                                  &stFileCleanup,
                                                  sizeof( AFSFileCleanupCB),
                                                  NULL,
                                                  NULL);

                    if( !NT_SUCCESS( ntStatus) &&
                        ntStatus != STATUS_OBJECT_NAME_NOT_FOUND)
                    {

                        AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                                      AFS_TRACE_LEVEL_ERROR,
                                      "AFSCleanup Failed to notify service of deleted directory %wZ Status %08lX\n",
                                      &pCcb->FullFileName,
                                      ntStatus);

                        ntStatus = STATUS_SUCCESS;

                        ClearFlag( pCcb->DirectoryCB->Flags, AFS_DIR_ENTRY_PENDING_DELETE);
                    }
                    else
                    {

                        ntStatus = STATUS_SUCCESS;

                        AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                                      AFS_TRACE_LEVEL_VERBOSE,
                                      "AFSCleanup Setting DELETE flag in directory %wZ Dir Entry %p\n",
                                      &pCcb->FullFileName,
                                      pCcb->DirectoryCB);

                        SetFlag( pCcb->DirectoryCB->Flags, AFS_DIR_ENTRY_DELETED);

                        ASSERT( pObjectInfo->ParentObjectInformation != NULL);

                        AFSFsRtlNotifyFullReportChange( pObjectInfo->ParentObjectInformation,
                                                        pCcb,
                                                        (ULONG)FILE_NOTIFY_CHANGE_FILE_NAME,
                                                        (ULONG)FILE_ACTION_REMOVED);

                        //
                        // Now that the service has the entry has deleted we need to remove it from the parent
                        // tree so another lookup on the node will fail
                        //

                        if( !BooleanFlagOn( pCcb->DirectoryCB->Flags, AFS_DIR_ENTRY_NOT_IN_PARENT_TREE))
                        {

                            AFSAcquireExcl( pObjectInfo->ParentObjectInformation->Specific.Directory.DirectoryNodeHdr.TreeLock,
                                            TRUE);

                            AFSRemoveNameEntry( pObjectInfo->ParentObjectInformation,
                                                pCcb->DirectoryCB);

                            AFSReleaseResource( pObjectInfo->ParentObjectInformation->Specific.Directory.DirectoryNodeHdr.TreeLock);
                        }
                        else
                        {

                            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                                          AFS_TRACE_LEVEL_VERBOSE,
                                          "AFSCleanup DE %p for %wZ NOT removing entry due to flag set\n",
                                          pCcb->DirectoryCB,
                                          &pCcb->DirectoryCB->NameInformation.FileName);
                        }
                    }
                }

                //
                // If there have been any updates to the node then push it to
                // the service
                //

                else
                {

                    if( BooleanFlagOn( pFcb->Flags, AFS_FCB_FLAG_FILE_MODIFIED))
                    {

                        ULONG ulNotifyFilter = 0;

                        ClearFlag( pFcb->Flags, AFS_FCB_FLAG_FILE_MODIFIED);

                        if(  pObjectInfo->ParentObjectInformation != NULL)
                        {

                            ulNotifyFilter |= (FILE_NOTIFY_CHANGE_ATTRIBUTES);

                            AFSFsRtlNotifyFullReportChange( pObjectInfo->ParentObjectInformation,
                                                            pCcb,
                                                            (ULONG)ulNotifyFilter,
                                                            (ULONG)FILE_ACTION_MODIFIED);
                        }
                    }

                    //
                    // Indicate the file access mode that is being released
                    //

                    stFileCleanup.FileAccess = pCcb->FileAccess;

                    AFSProcessRequest( AFS_REQUEST_TYPE_CLEANUP_PROCESSING,
                                       ulNotificationFlags | AFS_REQUEST_FLAG_SYNCHRONOUS,
                                       &pCcb->AuthGroup,
                                       &pCcb->DirectoryCB->NameInformation.FileName,
                                       &pObjectInfo->FileId,
                                       &stFileCleanup,
                                       sizeof( AFSFileCleanupCB),
                                       NULL,
                                       NULL);
                }

                //
                // Release the notification for this directory if there is one
                //

                FsRtlNotifyCleanup( pControlDeviceExt->Specific.Control.NotifySync,
                                    &pControlDeviceExt->Specific.Control.DirNotifyList,
                                    pCcb);

                //
                // Remove the share access at this time since we may not get the close for sometime on this FO.
                //

                IoRemoveShareAccess( pFileObject,
                                     &pFcb->ShareAccess);

                //
                // We don't need the name array after the user closes the handle on the file
                //

                if( pCcb->NameArray != NULL)
                {

                    AFSFreeNameArray( pCcb->NameArray);

                    pCcb->NameArray = NULL;
                }

                //
                // Decrement the open child handle count
                //

                if( pObjectInfo->ParentObjectInformation != NULL)
                {

                    ASSERT( pObjectInfo->ParentObjectInformation->Specific.Directory.ChildOpenHandleCount > 0);

                    InterlockedDecrement( &pObjectInfo->ParentObjectInformation->Specific.Directory.ChildOpenHandleCount);

                    AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                                  AFS_TRACE_LEVEL_VERBOSE,
                                  "AFSCleanup (Dir) Decrement child open handle count on Parent object %08lX Cnt %d\n",
                                  pObjectInfo->ParentObjectInformation,
                                  pObjectInfo->ParentObjectInformation->Specific.Directory.ChildOpenHandleCount);
                }

                //
                // And finally, release the Fcb if we acquired it.
                //

                AFSReleaseResource( &pFcb->NPFcb->Resource);

                break;
            }

            case AFS_SYMBOLIC_LINK_FCB:
            case AFS_MOUNT_POINT_FCB:
            case AFS_DFS_LINK_FCB:
            case AFS_INVALID_FCB:
            {

                //
                // We may be performing some cleanup on the Fcb so grab it exclusive to ensure no collisions
                //

                AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSCleanup (MP/SL) Acquiring Dcb lock %08lX EXCL %08lX\n",
                              &pFcb->NPFcb->Resource,
                              PsGetCurrentThread());

                AFSAcquireExcl( &pFcb->NPFcb->Resource,
                                  TRUE);

                //
                // Perform some final common processing
                //

                ASSERT( pFcb->OpenHandleCount != 0);

                InterlockedDecrement( &pFcb->OpenHandleCount);

                AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSCleanup (MP/SL) Decrement handle count on Fcb %08lX Cnt %d\n",
                              pFcb,
                              pFcb->OpenHandleCount);

                if( pFcb->ObjectInformation->ParentObjectInformation != NULL)
                {

                    stFileCleanup.ParentId = pFcb->ObjectInformation->ParentObjectInformation->FileId;
                }

                stFileCleanup.LastAccessTime = pObjectInfo->LastAccessTime;

                if( BooleanFlagOn( pFcb->Flags, AFS_FCB_FLAG_FILE_MODIFIED))
                {

                    stFileCleanup.FileAttributes = pObjectInfo->FileAttributes;

                    if( BooleanFlagOn( pFcb->Flags, AFS_FCB_FLAG_UPDATE_CREATE_TIME))
                    {

                        stFileCleanup.CreateTime = pObjectInfo->CreationTime;

                        ClearFlag( pFcb->Flags, AFS_FCB_FLAG_UPDATE_CREATE_TIME);
                    }

                    if( BooleanFlagOn( pFcb->Flags, AFS_FCB_FLAG_UPDATE_CHANGE_TIME))
                    {

                        stFileCleanup.ChangeTime = pObjectInfo->ChangeTime;

                        ClearFlag( pFcb->Flags, AFS_FCB_FLAG_UPDATE_CHANGE_TIME);
                    }

                    if( BooleanFlagOn( pFcb->Flags, AFS_FCB_FLAG_UPDATE_ACCESS_TIME))
                    {

                        stFileCleanup.LastAccessTime = pObjectInfo->LastAccessTime;

                        ClearFlag( pFcb->Flags, AFS_FCB_FLAG_UPDATE_ACCESS_TIME);
                    }
                }

                //
                // If the count has dropped to zero and there is a pending delete
                // then delete the node
                //

                if( pFcb->OpenHandleCount == 0 &&
                    BooleanFlagOn( pCcb->DirectoryCB->Flags, AFS_DIR_ENTRY_PENDING_DELETE))
                {

                    //
                    // Try to notify the service about the delete
                    //

                    ulNotificationFlags |= AFS_REQUEST_FLAG_FILE_DELETED;

                    //
                    // Indicate the file access mode that is being released
                    //

                    stFileCleanup.FileAccess = pCcb->FileAccess;

                    //
                    // Push the request to the service
                    //

                    ntStatus = AFSProcessRequest( AFS_REQUEST_TYPE_CLEANUP_PROCESSING,
                                                  ulNotificationFlags | AFS_REQUEST_FLAG_SYNCHRONOUS,
                                                  &pCcb->AuthGroup,
                                                  &pCcb->DirectoryCB->NameInformation.FileName,
                                                  &pObjectInfo->FileId,
                                                  &stFileCleanup,
                                                  sizeof( AFSFileCleanupCB),
                                                  NULL,
                                                  NULL);

                    if( !NT_SUCCESS( ntStatus) &&
                        ntStatus != STATUS_OBJECT_NAME_NOT_FOUND)
                    {

                        AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                                      AFS_TRACE_LEVEL_ERROR,
                                      "AFSCleanup Failed to notify service of deleted MP/SL %wZ Status %08lX\n",
                                      &pCcb->FullFileName,
                                      ntStatus);

                        ntStatus = STATUS_SUCCESS;

                        ClearFlag( pCcb->DirectoryCB->Flags, AFS_DIR_ENTRY_PENDING_DELETE);
                    }
                    else
                    {

                        ntStatus = STATUS_SUCCESS;

                        AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                                      AFS_TRACE_LEVEL_VERBOSE,
                                      "AFSCleanup Setting DELETE flag in MP/SL %wZ Dir Entry %p\n",
                                      &pCcb->FullFileName,
                                      pCcb->DirectoryCB);

                        SetFlag( pCcb->DirectoryCB->Flags, AFS_DIR_ENTRY_DELETED);

                        ASSERT( pObjectInfo->ParentObjectInformation != NULL);

                        AFSFsRtlNotifyFullReportChange( pObjectInfo->ParentObjectInformation,
                                                        pCcb,
                                                        (ULONG)FILE_NOTIFY_CHANGE_FILE_NAME,
                                                        (ULONG)FILE_ACTION_REMOVED);

                        //
                        // Now that the service has the entry has deleted we need to remove it from the parent
                        // tree so another lookup on the node will fail
                        //

                        if( !BooleanFlagOn( pCcb->DirectoryCB->Flags, AFS_DIR_ENTRY_NOT_IN_PARENT_TREE))
                        {

                            AFSAcquireExcl( pObjectInfo->ParentObjectInformation->Specific.Directory.DirectoryNodeHdr.TreeLock,
                                            TRUE);

                            AFSRemoveNameEntry( pObjectInfo->ParentObjectInformation,
                                                pCcb->DirectoryCB);

                            AFSReleaseResource( pObjectInfo->ParentObjectInformation->Specific.Directory.DirectoryNodeHdr.TreeLock);
                        }
                        else
                        {

                            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                                          AFS_TRACE_LEVEL_VERBOSE,
                                          "AFSCleanup DE %p for %wZ NOT removing entry due to flag set\n",
                                          pCcb->DirectoryCB,
                                          &pCcb->DirectoryCB->NameInformation.FileName);
                        }
                    }
                }

                //
                // If there have been any updates to the node then push it to
                // the service
                //

                else
                {

                    if( BooleanFlagOn( pFcb->Flags, AFS_FCB_FLAG_FILE_MODIFIED))
                    {

                        ULONG ulNotifyFilter = 0;

                        ClearFlag( pFcb->Flags, AFS_FCB_FLAG_FILE_MODIFIED);

                        if(  pObjectInfo->ParentObjectInformation != NULL)
                        {

                            ulNotifyFilter |= (FILE_NOTIFY_CHANGE_ATTRIBUTES);

                            AFSFsRtlNotifyFullReportChange( pObjectInfo->ParentObjectInformation,
                                                            pCcb,
                                                            (ULONG)ulNotifyFilter,
                                                            (ULONG)FILE_ACTION_MODIFIED);
                        }
                    }

                    //
                    // Indicate the file access mode that is being released
                    //

                    stFileCleanup.FileAccess = pCcb->FileAccess;

                    AFSProcessRequest( AFS_REQUEST_TYPE_CLEANUP_PROCESSING,
                                       ulNotificationFlags | AFS_REQUEST_FLAG_SYNCHRONOUS,
                                       &pCcb->AuthGroup,
                                       &pCcb->DirectoryCB->NameInformation.FileName,
                                       &pObjectInfo->FileId,
                                       &stFileCleanup,
                                       sizeof( AFSFileCleanupCB),
                                       NULL,
                                       NULL);
                }

                //
                // Remove the share access at this time since we may not get the close for sometime on this FO.
                //

                IoRemoveShareAccess( pFileObject,
                                     &pFcb->ShareAccess);

                //
                // We don't need the name array after the user closes the handle on the file
                //

                if( pCcb->NameArray != NULL)
                {

                    AFSFreeNameArray( pCcb->NameArray);

                    pCcb->NameArray = NULL;
                }

                //
                // Decrement the open child handle count
                //

                if( pObjectInfo->ParentObjectInformation != NULL)
                {

                    ASSERT( pObjectInfo->ParentObjectInformation->Specific.Directory.ChildOpenHandleCount > 0);

                    InterlockedDecrement( &pObjectInfo->ParentObjectInformation->Specific.Directory.ChildOpenHandleCount);

                    AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                                  AFS_TRACE_LEVEL_VERBOSE,
                                  "AFSCleanup (MP/SL) Decrement child open handle count on Parent object %08lX Cnt %d\n",
                                  pObjectInfo->ParentObjectInformation,
                                  pObjectInfo->ParentObjectInformation->Specific.Directory.ChildOpenHandleCount);
                }

                //
                // And finally, release the Fcb if we acquired it.
                //

                AFSReleaseResource( &pFcb->NPFcb->Resource);

                break;
            }

            case AFS_SPECIAL_SHARE_FCB:
            {

                AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSCleanup Acquiring SPECIAL SHARE lock %08lX EXCL %08lX\n",
                              &pFcb->NPFcb->Resource,
                              PsGetCurrentThread());

                AFSAcquireExcl( &pFcb->NPFcb->Resource,
                                TRUE);

                ASSERT( pFcb->OpenHandleCount != 0);

                InterlockedDecrement( &pFcb->OpenHandleCount);

                AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSCleanup (Share) Decrement handle count on Fcb %08lX Cnt %d\n",
                              pFcb,
                              pFcb->OpenHandleCount);

                //
                // Decrement the open child handle count
                //

                if( pObjectInfo->ParentObjectInformation != NULL &&
                    pObjectInfo->ParentObjectInformation->Specific.Directory.ChildOpenHandleCount > 0)
                {

                    InterlockedDecrement( &pObjectInfo->ParentObjectInformation->Specific.Directory.ChildOpenHandleCount);

                    AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                                  AFS_TRACE_LEVEL_VERBOSE,
                                  "AFSCleanup (Share) Decrement child open handle count on Parent object %08lX Cnt %d\n",
                                  pObjectInfo->ParentObjectInformation,
                                  pObjectInfo->ParentObjectInformation->Specific.Directory.ChildOpenHandleCount);
                }

                //
                // And finally, release the Fcb if we acquired it.
                //

                AFSReleaseResource( &pFcb->NPFcb->Resource);

                break;
            }

            default:

                AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                              AFS_TRACE_LEVEL_WARNING,
                              "AFSCleanup Processing unknown node type %d\n",
                              pFcb->Header.NodeTypeCode);

                break;
        }


try_exit:

        if( pFileObject != NULL)
        {

            //
            // Setup the fileobject flags to indicate cleanup is complete.
            //

            SetFlag( pFileObject->Flags, FO_CLEANUP_COMPLETE);
        }

        //
        // Complete the request
        //

        AFSCompleteRequest( Irp, ntStatus);
    }
    __except( AFSExceptionFilter( GetExceptionCode(), GetExceptionInformation()) )
    {

        AFSDbgLogMsg( 0,
                      0,
                      "EXCEPTION - AFSCleanup\n");
    }

    return ntStatus;
}
