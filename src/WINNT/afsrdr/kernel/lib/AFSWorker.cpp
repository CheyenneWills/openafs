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
// File: AFSWorker.cpp
//

#include "AFSCommon.h"

//
// Function: AFSInitializeWorkerPool
//
// Description:
//
//      This function initializes the worker thread pool
//
// Return:
//
//      A status is returned for the function
//

NTSTATUS
AFSInitializeWorkerPool()
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    AFSWorkQueueContext        *pCurrentWorker = NULL, *pLastWorker = NULL;
    AFSDeviceExt *pDevExt = NULL;

    __Enter
    {

        pDevExt = (AFSDeviceExt *)AFSLibraryDeviceObject->DeviceExtension;

        //
        // Initialize the worker threads.
        //

        pDevExt->Specific.Library.WorkerCount = 0;

        KeInitializeEvent( &pDevExt->Specific.Library.WorkerQueueHasItems,
                           NotificationEvent,
                           FALSE);

        //
        // Initialize the queue resource
        //

        ExInitializeResourceLite( &pDevExt->Specific.Library.QueueLock);

        while( pDevExt->Specific.Library.WorkerCount < AFS_WORKER_COUNT)
        {

            pCurrentWorker = (AFSWorkQueueContext *)AFSLibExAllocatePoolWithTag( NonPagedPool,
                                                                                 sizeof( AFSWorkQueueContext),
                                                                                 AFS_WORKER_CB_TAG);

            if( pCurrentWorker == NULL)
            {

                AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                              AFS_TRACE_LEVEL_ERROR,
                              "AFSInitializeWorkerPool Failed to allocate worker context\n");

                ntStatus = STATUS_INSUFFICIENT_RESOURCES;

                break;
            }

            RtlZeroMemory( pCurrentWorker,
                           sizeof( AFSWorkQueueContext));

            ntStatus = AFSInitWorkerThread( pCurrentWorker,
                                            (PKSTART_ROUTINE)AFSWorkerThread);

            if( !NT_SUCCESS( ntStatus))
            {

                AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                              AFS_TRACE_LEVEL_ERROR,
                              "AFSInitializeWorkerPool Failed to initialize worker thread Status %08lX\n", ntStatus);

                ExFreePool( pCurrentWorker);

                break;
            }

            if( pDevExt->Specific.Library.PoolHead == NULL)
            {

                pDevExt->Specific.Library.PoolHead = pCurrentWorker;
            }
            else
            {

                pLastWorker->fLink = pCurrentWorker;
            }

            pLastWorker = pCurrentWorker;

            pDevExt->Specific.Library.WorkerCount++;
        }

        //
        // If there was a failure but there is at least one worker, then go with it.
        //

        if( !NT_SUCCESS( ntStatus) &&
            pDevExt->Specific.Library.WorkerCount == 0)
        {

            try_return( ntStatus);
        }

        ntStatus = STATUS_SUCCESS;

        //
        // Now our IO Worker queue
        //

        pDevExt->Specific.Library.IOWorkerCount = 0;

        KeInitializeEvent( &pDevExt->Specific.Library.IOWorkerQueueHasItems,
                           NotificationEvent,
                           FALSE);

        //
        // Initialize the queue resource
        //

        ExInitializeResourceLite( &pDevExt->Specific.Library.IOQueueLock);

        while( pDevExt->Specific.Library.IOWorkerCount < AFS_IO_WORKER_COUNT)
        {

            pCurrentWorker = (AFSWorkQueueContext *)AFSLibExAllocatePoolWithTag( NonPagedPool,
                                                                                 sizeof( AFSWorkQueueContext),
                                                                                 AFS_WORKER_CB_TAG);

            if( pCurrentWorker == NULL)
            {

                AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                              AFS_TRACE_LEVEL_ERROR,
                              "AFSInitializeWorkerPool Failed to allocate IO worker context\n");

                ntStatus = STATUS_INSUFFICIENT_RESOURCES;

                break;
            }

            RtlZeroMemory( pCurrentWorker,
                           sizeof( AFSWorkQueueContext));

            ntStatus = AFSInitWorkerThread( pCurrentWorker,
                                            (PKSTART_ROUTINE)AFSIOWorkerThread);

            if( !NT_SUCCESS( ntStatus))
            {

                AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                              AFS_TRACE_LEVEL_ERROR,
                              "AFSInitializeWorkerPool Failed to initialize IO worker thread Status %08lX\n", ntStatus);

                ExFreePool( pCurrentWorker);

                break;
            }

            if( pDevExt->Specific.Library.IOPoolHead == NULL)
            {

                pDevExt->Specific.Library.IOPoolHead = pCurrentWorker;
            }
            else
            {

                pLastWorker->fLink = pCurrentWorker;
            }

            pLastWorker = pCurrentWorker;

            pDevExt->Specific.Library.IOWorkerCount++;
        }

        //
        // If there was a failure but there is at least one worker, then go with it.
        //

        if( !NT_SUCCESS( ntStatus) &&
            pDevExt->Specific.Library.IOWorkerCount == 0)
        {

            try_return( ntStatus);
        }

try_exit:

        if( !NT_SUCCESS( ntStatus))
        {

            //
            // Failed to initialize the pool so tear it down
            //

            AFSRemoveWorkerPool();
        }
    }

    return ntStatus;
}

//
// Function: AFSRemoveWorkerPool
//
// Description:
//
//      This function tears down the worker thread pool
//
// Return:
//
//      A status is returned for the function
//

NTSTATUS
AFSRemoveWorkerPool()
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    ULONG index = 0;
    AFSWorkQueueContext        *pCurrentWorker = NULL, *pNextWorker = NULL;
    AFSDeviceExt *pDevExt = NULL;

    pDevExt = (AFSDeviceExt *)AFSLibraryDeviceObject->DeviceExtension;

    //
    // Loop through the workers shutting them down
    //

    pCurrentWorker = pDevExt->Specific.Library.PoolHead;

    while( index < pDevExt->Specific.Library.WorkerCount)
    {

        ntStatus = AFSShutdownWorkerThread( pCurrentWorker);

        pNextWorker = pCurrentWorker->fLink;

        ExFreePool( pCurrentWorker);

        pCurrentWorker = pNextWorker;

        if( pCurrentWorker == NULL)
        {

            break;
        }

        index++;
    }

    pDevExt->Specific.Library.PoolHead = NULL;

    ExDeleteResourceLite( &pDevExt->Specific.Library.QueueLock);

    //
    // Loop through the IO workers shutting them down
    //

    pCurrentWorker = pDevExt->Specific.Library.IOPoolHead;

    index = 0;

    while( index < pDevExt->Specific.Library.IOWorkerCount)
    {

        ntStatus = AFSShutdownIOWorkerThread( pCurrentWorker);

        pNextWorker = pCurrentWorker->fLink;

        ExFreePool( pCurrentWorker);

        pCurrentWorker = pNextWorker;

        if( pCurrentWorker == NULL)
        {

            break;
        }

        index++;
    }

    pDevExt->Specific.Library.IOPoolHead = NULL;

    ExDeleteResourceLite( &pDevExt->Specific.Library.IOQueueLock);

    return ntStatus;
}

NTSTATUS
AFSInitVolumeWorker( IN AFSVolumeCB *VolumeCB)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    AFSWorkQueueContext *pWorker = &VolumeCB->VolumeWorkerContext;
    HANDLE hThread;
    AFSDeviceExt *pControlDeviceExt = (AFSDeviceExt *)AFSControlDeviceObject->DeviceExtension;
    PKSTART_ROUTINE pStartRoutine = NULL;

    __Enter
    {

        if( VolumeCB == AFSGlobalRoot)
        {

            pStartRoutine = AFSPrimaryVolumeWorkerThread;
        }
        else
        {

            pStartRoutine = AFSVolumeWorkerThread;
        }

        //
        // Initialize the worker thread
        //

        KeInitializeEvent( &pWorker->WorkerThreadReady,
                           NotificationEvent,
                           FALSE);

        //
        // Set the worker to process requests
        //

        pWorker->State = AFS_WORKER_PROCESS_REQUESTS;

        //
        // Launch the thread
        //

        ntStatus =  PsCreateSystemThread( &hThread,
                                          0,
                                          NULL,
                                          NULL,
                                          NULL,
                                          pStartRoutine,
                                          (void *)VolumeCB);

        if( NT_SUCCESS( ntStatus))
        {

            ObReferenceObjectByHandle( hThread,
                                       GENERIC_READ | GENERIC_WRITE,
                                       NULL,
                                       KernelMode,
                                       (PVOID *)&pWorker->WorkerThreadObject,
                                       NULL);

            ntStatus = KeWaitForSingleObject( &pWorker->WorkerThreadReady,
                                              Executive,
                                              KernelMode,
                                              FALSE,
                                              NULL);

            if( InterlockedIncrement( &pControlDeviceExt->Specific.Control.VolumeWorkerThreadCount) > 0)
            {

                KeClearEvent( &pControlDeviceExt->Specific.Control.VolumeWorkerCloseEvent);
            }

            ZwClose( hThread);
        }
    }

    return ntStatus;
}

//
// Function: AFSInitWorkerThread
//
// Description:
//
//      This function initializes a worker thread in the pool
//
// Return:
//
//      A status is returned for the function
//

NTSTATUS
AFSInitWorkerThread( IN AFSWorkQueueContext *PoolContext,
                     IN PKSTART_ROUTINE WorkerRoutine)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    HANDLE Handle;

    //
    // INitialize the worker signal thread
    //

    KeInitializeEvent( &PoolContext->WorkerThreadReady,
                       NotificationEvent,
                       FALSE);

    //
    // Set the worker to process requests
    //

    PoolContext->State = AFS_WORKER_PROCESS_REQUESTS;

    //
    // Launch the thread
    //

    ntStatus =  PsCreateSystemThread( &Handle,
                                      0,
                                      NULL,
                                      NULL,
                                      NULL,
                                      WorkerRoutine,
                                      (void *)PoolContext);

    if( NT_SUCCESS( ntStatus))
    {

        ObReferenceObjectByHandle( Handle,
                                   GENERIC_READ | GENERIC_WRITE,
                                   NULL,
                                   KernelMode,
                                   (PVOID *)&PoolContext->WorkerThreadObject,
                                   NULL);

        ntStatus = KeWaitForSingleObject( &PoolContext->WorkerThreadReady,
                                          Executive,
                                          KernelMode,
                                          FALSE,
                                          NULL);

        ZwClose( Handle);
    }

    return ntStatus;
}

NTSTATUS
AFSShutdownVolumeWorker( IN AFSVolumeCB *VolumeCB)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    AFSWorkQueueContext *pWorker = &VolumeCB->VolumeWorkerContext;

    if( pWorker->WorkerThreadObject != NULL &&
        BooleanFlagOn( pWorker->State, AFS_WORKER_INITIALIZED))
    {

        //
        // Clear the 'keep processing' flag
        //

        ClearFlag( pWorker->State, AFS_WORKER_PROCESS_REQUESTS);

        ntStatus = KeWaitForSingleObject( pWorker->WorkerThreadObject,
                                          Executive,
                                          KernelMode,
                                          FALSE,
                                          NULL);

        ObDereferenceObject( pWorker->WorkerThreadObject);

        pWorker->WorkerThreadObject = NULL;
    }

    return ntStatus;
}

//
// Function: AFSShutdownWorkerThread
//
// Description:
//
//      This function shusdown a worker thread in the pool
//
// Return:
//
//      A status is returned for the function
//

NTSTATUS
AFSShutdownWorkerThread( IN AFSWorkQueueContext *PoolContext)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    AFSDeviceExt *pDeviceExt = (AFSDeviceExt *)AFSLibraryDeviceObject->DeviceExtension;

    if( PoolContext->WorkerThreadObject != NULL &&
        BooleanFlagOn( PoolContext->State, AFS_WORKER_INITIALIZED))
    {

        //
        // Clear the 'keep processing' flag
        //

        ClearFlag( PoolContext->State, AFS_WORKER_PROCESS_REQUESTS);

        //
        // Wake up the thread if it is a sleep
        //

        KeSetEvent( &pDeviceExt->Specific.Library.WorkerQueueHasItems,
                    0,
                    FALSE);

        ntStatus = KeWaitForSingleObject( PoolContext->WorkerThreadObject,
                                          Executive,
                                          KernelMode,
                                          FALSE,
                                          NULL);

        ObDereferenceObject( PoolContext->WorkerThreadObject);

        PoolContext->WorkerThreadObject = NULL;
    }

    return ntStatus;
}

//
// Function: AFSShutdownIOWorkerThread
//
// Description:
//
//      This function shutsdown an IO worker thread in the pool
//
// Return:
//
//      A status is returned for the function
//

NTSTATUS
AFSShutdownIOWorkerThread( IN AFSWorkQueueContext *PoolContext)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    AFSDeviceExt *pDeviceExt = (AFSDeviceExt *)AFSLibraryDeviceObject->DeviceExtension;

    if( PoolContext->WorkerThreadObject != NULL &&
        BooleanFlagOn( PoolContext->State, AFS_WORKER_INITIALIZED))
    {

        //
        // Clear the 'keep processing' flag
        //

        ClearFlag( PoolContext->State, AFS_WORKER_PROCESS_REQUESTS);

        //
        // Wake up the thread if it is a sleep
        //

        KeSetEvent( &pDeviceExt->Specific.Library.IOWorkerQueueHasItems,
                    0,
                    FALSE);

        ntStatus = KeWaitForSingleObject( PoolContext->WorkerThreadObject,
                                          Executive,
                                          KernelMode,
                                          FALSE,
                                          NULL);

        ObDereferenceObject( PoolContext->WorkerThreadObject);

        PoolContext->WorkerThreadObject = NULL;
    }

    return ntStatus;
}

//
// Function: AFSWorkerThread
//
// Description:
//
//      This is the worker thread entry point.
//
// Return:
//
//      A status is returned for the function
//

void
AFSWorkerThread( IN PVOID Context)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    AFSWorkQueueContext *pPoolContext = (AFSWorkQueueContext *)Context;
    AFSWorkItem *pWorkItem;
    BOOLEAN freeWorkItem = TRUE;
    BOOLEAN exitThread = FALSE;
    AFSDeviceExt *pLibraryDevExt = NULL;

    pLibraryDevExt = (AFSDeviceExt *)AFSLibraryDeviceObject->DeviceExtension;

    //
    // Indicate that we are initialized and ready
    //

    KeSetEvent( &pPoolContext->WorkerThreadReady,
                0,
                FALSE);


    //
    // Indicate we are initialized
    //

    SetFlag( pPoolContext->State, AFS_WORKER_INITIALIZED);

    while( BooleanFlagOn( pPoolContext->State, AFS_WORKER_PROCESS_REQUESTS))
    {

        ntStatus = KeWaitForSingleObject( &pLibraryDevExt->Specific.Library.WorkerQueueHasItems,
                                          Executive,
                                          KernelMode,
                                          FALSE,
                                          NULL);

        if( !NT_SUCCESS( ntStatus))
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSWorkerThread Wait for queue items failed Status %08lX\n", ntStatus);
        }
        else
        {

            pWorkItem = AFSRemoveWorkItem();

            if( pWorkItem != NULL)
            {

                freeWorkItem = TRUE;

                //
                // Switch on the type of work item to process
                //

                switch( pWorkItem->RequestType)
                {

                    case AFS_WORK_FLUSH_FCB:
                    {

                        ntStatus = AFSFlushExtents( pWorkItem->Specific.Fcb.Fcb);

                        if( !NT_SUCCESS( ntStatus))
                        {

                            AFSReleaseExtentsWithFlush( pWorkItem->Specific.Fcb.Fcb);
                        }

                        ASSERT( pWorkItem->Specific.Fcb.Fcb->OpenReferenceCount != 0);

                        InterlockedDecrement( &pWorkItem->Specific.Fcb.Fcb->OpenReferenceCount);

                        break;
                    }

                    case AFS_WORK_ASYNCH_READ:
                    {

                        ASSERT( pWorkItem->Specific.AsynchIo.CallingProcess != NULL);

                        (VOID) AFSCommonRead( pWorkItem->Specific.AsynchIo.Device,
                                              pWorkItem->Specific.AsynchIo.Irp,
                                              pWorkItem->Specific.AsynchIo.CallingProcess);

                        break;
                    }

                    case AFS_WORK_ASYNCH_WRITE:
                    {

                        ASSERT( pWorkItem->Specific.AsynchIo.CallingProcess != NULL);

                        (VOID) AFSCommonWrite( pWorkItem->Specific.AsynchIo.Device,
                                               pWorkItem->Specific.AsynchIo.Irp,
                                               pWorkItem->Specific.AsynchIo.CallingProcess);
                        break;
                    }

                    case AFS_WORK_ENUMERATE_GLOBAL_ROOT:
                    {

                        AFSEnumerateGlobalRoot( NULL);

                        break;
                    }

                    case AFS_WORK_START_IOS:
                    {

                        freeWorkItem = TRUE;

                        break;
                    }

                    default:

                        AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                                      AFS_TRACE_LEVEL_ERROR,
                                      "AFSWorkerThread Unknown request type %d\n", pWorkItem->RequestType);

                        break;
                }

                if( freeWorkItem)
                {

                    ExFreePoolWithTag( pWorkItem, AFS_WORK_ITEM_TAG);
                }
            }
        }
    } // worker thread loop

    PsTerminateSystemThread( 0);

    return;
}

void
AFSIOWorkerThread( IN PVOID Context)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    AFSWorkQueueContext *pPoolContext = (AFSWorkQueueContext *)Context;
    AFSWorkItem *pWorkItem;
    BOOLEAN freeWorkItem = TRUE;
    BOOLEAN exitThread = FALSE;
    AFSDeviceExt *pLibraryDevExt = NULL, *pRdrDevExt = NULL;

    pLibraryDevExt = (AFSDeviceExt *)AFSLibraryDeviceObject->DeviceExtension;

    //
    // Indicate that we are initialized and ready
    //

    KeSetEvent( &pPoolContext->WorkerThreadReady,
                0,
                FALSE);


    //
    // Indicate we are initialized
    //

    SetFlag( pPoolContext->State, AFS_WORKER_INITIALIZED);

    while( BooleanFlagOn( pPoolContext->State, AFS_WORKER_PROCESS_REQUESTS))
    {

        ntStatus = KeWaitForSingleObject( &pLibraryDevExt->Specific.Library.IOWorkerQueueHasItems,
                                          Executive,
                                          KernelMode,
                                          FALSE,
                                          NULL);

        if( !NT_SUCCESS( ntStatus))
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSIOWorkerThread Wait for queue items failed Status %08lX\n", ntStatus);
        }
        else
        {

            pWorkItem = AFSRemoveIOWorkItem();

            if( pWorkItem != NULL)
            {

                freeWorkItem = TRUE;

                //
                // Switch on the type of work item to process
                //

                switch( pWorkItem->RequestType)
                {

                    case AFS_WORK_START_IOS:
                    {

                        pRdrDevExt = (AFSDeviceExt *)AFSRDRDeviceObject->DeviceExtension;

                        //
                        // The final status is in the gather io
                        //

                        ntStatus = AFSStartIos( pWorkItem->Specific.CacheAccess.CacheFileObject,
                                                pWorkItem->Specific.CacheAccess.FunctionCode,
                                                pWorkItem->Specific.CacheAccess.RequestFlags,
                                                pWorkItem->Specific.CacheAccess.IoRuns,
                                                pWorkItem->Specific.CacheAccess.RunCount,
                                                pWorkItem->Specific.CacheAccess.GatherIo);

                        //
                        // Regardless of the status we we do the complete - there may
                        // be IOs in flight
                        // Decrement the count - setting the event if we were told
                        // to. This may trigger completion.
                        //

                        AFSCompleteIo( pWorkItem->Specific.CacheAccess.GatherIo, ntStatus );

                        freeWorkItem = TRUE;

                        break;
                    }

                    default:

                        AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                                      AFS_TRACE_LEVEL_ERROR,
                                      "AFSWorkerThread Unknown request type %d\n", pWorkItem->RequestType);

                        break;
                }

                if( freeWorkItem)
                {

                    ExFreePoolWithTag( pWorkItem, AFS_WORK_ITEM_TAG);
                }
            }
        }
    } // worker thread loop

    PsTerminateSystemThread( 0);

    return;
}

void
AFSPrimaryVolumeWorkerThread( IN PVOID Context)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    AFSWorkQueueContext *pPoolContext = (AFSWorkQueueContext *)&AFSGlobalRoot->VolumeWorkerContext;
    AFSDeviceExt *pControlDeviceExt = NULL;
    AFSDeviceExt *pRDRDeviceExt = NULL;
    BOOLEAN exitThread = FALSE;
    LARGE_INTEGER DueTime;
    LONG TimeOut;
    KTIMER Timer;
    BOOLEAN bFoundOpenEntry = FALSE;
    AFSObjectInfoCB *pCurrentObject = NULL, *pNextObject = NULL, *pCurrentChildObject = NULL;
    AFSDirectoryCB *pCurrentDirEntry = NULL, *pNextDirEntry = NULL;
    BOOLEAN bReleaseVolumeLock = FALSE;
    AFSVolumeCB *pVolumeCB = NULL, *pNextVolume = NULL;
    LARGE_INTEGER liCurrentTime;
    BOOLEAN bVolumeObject = FALSE;

    pControlDeviceExt = (AFSDeviceExt *)AFSControlDeviceObject->DeviceExtension;

    pRDRDeviceExt = (AFSDeviceExt *)AFSRDRDeviceObject->DeviceExtension;

    AFSDbgLogMsg( AFS_SUBSYSTEM_CLEANUP_PROCESSING,
                  AFS_TRACE_LEVEL_VERBOSE,
                  "AFSPrimaryVolumeWorkerThread Initialized\n");

    //
    // Initialize the timer for the worker thread
    //

    DueTime.QuadPart = -(5000);

    TimeOut = 5000;

    KeInitializeTimerEx( &Timer,
                         SynchronizationTimer);

    KeSetTimerEx( &Timer,
                  DueTime,
                  TimeOut,
                  NULL);

    //
    // Indicate that we are initialized and ready
    //

    KeSetEvent( &pPoolContext->WorkerThreadReady,
                0,
                FALSE);

    //
    // Indicate we are initialized
    //

    SetFlag( pPoolContext->State, AFS_WORKER_INITIALIZED);

    while( BooleanFlagOn( pPoolContext->State, AFS_WORKER_PROCESS_REQUESTS))
    {

        KeWaitForSingleObject( &Timer,
                               Executive,
                               KernelMode,
                               FALSE,
                               NULL);

        //
        // This is the primary volume worker so it will traverse the volume list
        // looking for cleanup or volumes requiring private workers
        //

        AFSAcquireShared( &pRDRDeviceExt->Specific.RDR.VolumeListLock,
                          TRUE);

        pVolumeCB = pRDRDeviceExt->Specific.RDR.VolumeListHead;

        while( pVolumeCB != NULL)
        {

            if( pVolumeCB == AFSGlobalRoot ||
                !AFSAcquireExcl( pVolumeCB->VolumeLock,
                                 FALSE))
            {

                pVolumeCB = (AFSVolumeCB *)pVolumeCB->ListEntry.fLink;

                continue;
            }

            if( pVolumeCB->ObjectInfoListHead == NULL &&
                pVolumeCB != AFSGlobalRoot)
            {

                AFSReleaseResource( pVolumeCB->VolumeLock);

                AFSReleaseResource( &pRDRDeviceExt->Specific.RDR.VolumeListLock);

                AFSAcquireExcl( pRDRDeviceExt->Specific.RDR.VolumeTree.TreeLock,
                                TRUE);

                AFSAcquireExcl( &pRDRDeviceExt->Specific.RDR.VolumeListLock,
                                TRUE);

                if( !AFSAcquireExcl( pVolumeCB->VolumeLock,
                                     FALSE))
                {

                    AFSConvertToShared( &pRDRDeviceExt->Specific.RDR.VolumeListLock);

                    AFSReleaseResource( pRDRDeviceExt->Specific.RDR.VolumeTree.TreeLock);

                    pVolumeCB = (AFSVolumeCB *)pVolumeCB->ListEntry.fLink;

                    continue;
                }

                KeQueryTickCount( &liCurrentTime);

                pNextVolume = (AFSVolumeCB *)pVolumeCB->ListEntry.fLink;

                if( pVolumeCB->ObjectInfoListHead == NULL &&
                    pVolumeCB->DirectoryCB->OpenReferenceCount == 0 &&
                    pVolumeCB->VolumeReferenceCount == 1 &&
                    ( pVolumeCB->RootFcb == NULL ||
                      pVolumeCB->RootFcb->OpenReferenceCount == 0) &&
                    pVolumeCB->ObjectInformation.ObjectReferenceCount == 0)
                {

                    if( pVolumeCB->RootFcb != NULL)
                    {

                        AFSRemoveRootFcb( pVolumeCB->RootFcb);
                    }

                    AFSRemoveVolume( pVolumeCB);
                }
                else
                {

                    AFSReleaseResource( pVolumeCB->VolumeLock);
                }

                AFSConvertToShared( &pRDRDeviceExt->Specific.RDR.VolumeListLock);

                AFSReleaseResource( pRDRDeviceExt->Specific.RDR.VolumeTree.TreeLock);

                pVolumeCB = pNextVolume;

                continue;
            }

            //
            // Don't need this lock anymore now that we have a volume cb to work with
            //

            AFSReleaseResource( &pRDRDeviceExt->Specific.RDR.VolumeListLock);

            //
            // For now we only need the volume lock shared
            //

            AFSConvertToShared( pVolumeCB->VolumeLock);

            if( AFSAcquireShared( pVolumeCB->ObjectInfoTree.TreeLock,
                                  FALSE))
            {

                pCurrentObject = pVolumeCB->ObjectInfoListHead;

                pNextObject = NULL;

                bReleaseVolumeLock = TRUE;

                while( pCurrentObject != NULL)
                {

                    if( pCurrentObject != &pVolumeCB->ObjectInformation)
                    {

                        pNextObject = (AFSObjectInfoCB *)pCurrentObject->ListEntry.fLink;

                        if( pNextObject == NULL &&
                            pVolumeCB != AFSGlobalRoot)  // Don't free up the root of the global
                        {

                            pNextObject = &pVolumeCB->ObjectInformation;
                        }

                        bVolumeObject = FALSE;
                    }
                    else
                    {

                        pNextObject = NULL;

                        bVolumeObject = TRUE;
                    }

                    if( pCurrentObject->FileType == AFS_FILE_TYPE_DIRECTORY &&
                        !BooleanFlagOn( pRDRDeviceExt->DeviceFlags, AFS_DEVICE_FLAG_REDIRECTOR_SHUTDOWN))  // If we are in shutdown mode skip directories
                    {

                        //
                        // If this object is deleted then remove it from the parent, if we can
                        //

                        if( BooleanFlagOn( pCurrentObject->Flags, AFS_OBJECT_FLAGS_DELETED) &&
                            pCurrentObject->ObjectReferenceCount == 0 &&
                            ( pCurrentObject->Fcb == NULL ||
                              pCurrentObject->Fcb->OpenReferenceCount == 0) &&
                            pCurrentObject->Specific.Directory.DirectoryNodeListHead == NULL &&
                            pCurrentObject->Specific.Directory.ChildOpenReferenceCount == 0)
                        {

                            AFSReleaseResource( pVolumeCB->ObjectInfoTree.TreeLock);

                            if( AFSAcquireExcl( pVolumeCB->ObjectInfoTree.TreeLock,
                                                FALSE))
                            {

                                if( pCurrentObject->Fcb != NULL)
                                {

                                    AFSRemoveFcb( pCurrentObject->Fcb);
                                }

                                if( pCurrentObject->Specific.Directory.PIOCtlDirectoryCB != NULL)
                                {

                                    if( pCurrentObject->Specific.Directory.PIOCtlDirectoryCB->ObjectInformation->Fcb != NULL)
                                    {

                                        AFSRemoveFcb( pCurrentObject->Specific.Directory.PIOCtlDirectoryCB->ObjectInformation->Fcb);
                                    }

                                    AFSDeleteObjectInfo( pCurrentObject->Specific.Directory.PIOCtlDirectoryCB->ObjectInformation);

                                    ExFreePool( pCurrentObject->Specific.Directory.PIOCtlDirectoryCB);
                                }

                                AFSDbgLogMsg( AFS_SUBSYSTEM_CLEANUP_PROCESSING,
                                              AFS_TRACE_LEVEL_VERBOSE,
                                              "AFSPrimaryWorker Deleting deleted object %08lX\n",
                                                                    pCurrentObject);

                                AFSDeleteObjectInfo( pCurrentObject);

                                AFSConvertToShared( pVolumeCB->ObjectInfoTree.TreeLock);

                                pCurrentObject = pNextObject;

                                continue;
                            }
                            else
                            {

                                bReleaseVolumeLock = FALSE;

                                break;
                            }
                        }

                        if( pCurrentObject->Specific.Directory.ChildOpenReferenceCount > 0 ||
                            ( pCurrentObject->Fcb != NULL &&
                              pCurrentObject->Fcb->OpenReferenceCount > 0) ||
                            pCurrentObject->Specific.Directory.DirectoryNodeListHead == NULL)
                        {

                            pCurrentObject = pNextObject;

                            continue;
                        }

                        if( !AFSAcquireShared( pCurrentObject->Specific.Directory.DirectoryNodeHdr.TreeLock,
                                               FALSE))
                        {

                            pCurrentObject = pNextObject;

                            continue;
                        }

                        KeQueryTickCount( &liCurrentTime);

                        pCurrentDirEntry = pCurrentObject->Specific.Directory.DirectoryNodeListHead;

                        while( pCurrentDirEntry != NULL)
                        {

                            if( pCurrentDirEntry->OpenReferenceCount > 0 ||
                                ( pCurrentDirEntry->ObjectInformation->Fcb != NULL &&
                                  pCurrentDirEntry->ObjectInformation->Fcb->OpenReferenceCount > 0) ||
                                liCurrentTime.QuadPart <= pCurrentDirEntry->ObjectInformation->LastAccessCount.QuadPart ||
                                liCurrentTime.QuadPart - pCurrentDirEntry->ObjectInformation->LastAccessCount.QuadPart <
                                                                        pControlDeviceExt->Specific.Control.ObjectLifeTimeCount.QuadPart ||
                                ( pCurrentDirEntry->ObjectInformation->FileType == AFS_FILE_TYPE_DIRECTORY &&
                                   ( pCurrentDirEntry->ObjectInformation->Specific.Directory.DirectoryNodeListHead != NULL ||
                                     pCurrentDirEntry->ObjectInformation->Specific.Directory.ChildOpenReferenceCount > 0)) ||
                                ( pCurrentDirEntry->ObjectInformation->FileType == AFS_FILE_TYPE_FILE &&
                                  pCurrentDirEntry->ObjectInformation->Fcb != NULL &&
                                  pCurrentDirEntry->ObjectInformation->Fcb->Specific.File.ExtentsDirtyCount > 0))
                            {

                                break;
                            }

                            pCurrentDirEntry = (AFSDirectoryCB *)pCurrentDirEntry->ListEntry.fLink;
                        }

                        if( pCurrentDirEntry != NULL)
                        {

                            AFSReleaseResource( pCurrentObject->Specific.Directory.DirectoryNodeHdr.TreeLock);

                            pCurrentObject = pNextObject;

                            continue;
                        }

                        AFSReleaseResource( pCurrentObject->Specific.Directory.DirectoryNodeHdr.TreeLock);

                        AFSReleaseResource( pVolumeCB->ObjectInfoTree.TreeLock);

                        //
                        // Now acquire the locks excl
                        //

                        if( AFSAcquireExcl( pCurrentObject->Specific.Directory.DirectoryNodeHdr.TreeLock,
                                            FALSE))
                        {

                            if( AFSAcquireExcl( pVolumeCB->ObjectInfoTree.TreeLock,
                                                FALSE))
                            {

                                if( pCurrentObject->Specific.Directory.ChildOpenReferenceCount > 0)
                                {

                                    AFSReleaseResource( pCurrentObject->Specific.Directory.DirectoryNodeHdr.TreeLock);

                                    AFSConvertToShared( pVolumeCB->ObjectInfoTree.TreeLock);

                                    pCurrentObject = pNextObject;

                                    continue;
                                }

                                KeQueryTickCount( &liCurrentTime);

                                pCurrentDirEntry = pCurrentObject->Specific.Directory.DirectoryNodeListHead;

                                while( pCurrentDirEntry != NULL)
                                {

                                    if( pCurrentDirEntry->OpenReferenceCount > 0 ||
                                        ( pCurrentDirEntry->ObjectInformation->Fcb != NULL &&
                                          pCurrentDirEntry->ObjectInformation->Fcb->OpenReferenceCount > 0) ||
                                        liCurrentTime.QuadPart <= pCurrentDirEntry->ObjectInformation->LastAccessCount.QuadPart ||
                                        liCurrentTime.QuadPart - pCurrentDirEntry->ObjectInformation->LastAccessCount.QuadPart <
                                                                                pControlDeviceExt->Specific.Control.ObjectLifeTimeCount.QuadPart ||
                                        ( pCurrentDirEntry->ObjectInformation->FileType == AFS_FILE_TYPE_DIRECTORY &&
                                          ( pCurrentDirEntry->ObjectInformation->Specific.Directory.DirectoryNodeListHead != NULL ||
                                            pCurrentDirEntry->ObjectInformation->Specific.Directory.ChildOpenReferenceCount > 0)) ||
                                        ( pCurrentDirEntry->ObjectInformation->FileType == AFS_FILE_TYPE_FILE &&
                                          pCurrentDirEntry->ObjectInformation->Fcb != NULL &&
                                          pCurrentDirEntry->ObjectInformation->Fcb->Specific.File.ExtentsDirtyCount > 0))
                                    {

                                        break;
                                    }

                                    pCurrentDirEntry = (AFSDirectoryCB *)pCurrentDirEntry->ListEntry.fLink;
                                }

                                if( pCurrentDirEntry != NULL)
                                {

                                    AFSReleaseResource( pCurrentObject->Specific.Directory.DirectoryNodeHdr.TreeLock);

                                    AFSConvertToShared( pVolumeCB->ObjectInfoTree.TreeLock);

                                    pCurrentObject = pNextObject;

                                    continue;
                                }

                                pCurrentDirEntry = pCurrentObject->Specific.Directory.DirectoryNodeListHead;

                                while( pCurrentDirEntry != NULL)
                                {

                                    pNextDirEntry = (AFSDirectoryCB *)pCurrentDirEntry->ListEntry.fLink;

                                    pCurrentChildObject = pCurrentDirEntry->ObjectInformation;

                                    AFSDbgLogMsg( AFS_SUBSYSTEM_CLEANUP_PROCESSING,
                                                  AFS_TRACE_LEVEL_VERBOSE,
                                                  "AFSPrimaryWorker Deleting DE %wZ Object %08lX\n",
                                                                    &pCurrentDirEntry->NameInformation.FileName,
                                                                    pCurrentChildObject);

                                    AFSDeleteDirEntry( pCurrentObject,
                                                       pCurrentDirEntry);

                                    if( pCurrentChildObject->ObjectReferenceCount == 0)
                                    {

                                        if( pCurrentChildObject->Fcb != NULL)
                                        {

                                            if( pCurrentChildObject->FileType == AFS_FILE_TYPE_FILE)
                                            {

                                                AFSCleanupFcb( pCurrentChildObject->Fcb,
                                                               TRUE);
                                            }

                                            AFSRemoveFcb( pCurrentChildObject->Fcb);
                                        }

                                        if( pCurrentChildObject->FileType == AFS_FILE_TYPE_DIRECTORY &&
                                            pCurrentChildObject->Specific.Directory.PIOCtlDirectoryCB != NULL)
                                        {

                                            if( pCurrentChildObject->Specific.Directory.PIOCtlDirectoryCB->ObjectInformation->Fcb != NULL)
                                            {

                                                AFSRemoveFcb( pCurrentChildObject->Specific.Directory.PIOCtlDirectoryCB->ObjectInformation->Fcb);
                                            }

                                            AFSDeleteObjectInfo( pCurrentChildObject->Specific.Directory.PIOCtlDirectoryCB->ObjectInformation);

                                            ExDeleteResourceLite( &pCurrentChildObject->Specific.Directory.PIOCtlDirectoryCB->NonPaged->Lock);

                                            ExFreePool( pCurrentChildObject->Specific.Directory.PIOCtlDirectoryCB->NonPaged);

                                            ExFreePool( pCurrentChildObject->Specific.Directory.PIOCtlDirectoryCB);
                                        }

                                        AFSDbgLogMsg( AFS_SUBSYSTEM_CLEANUP_PROCESSING,
                                                      AFS_TRACE_LEVEL_VERBOSE,
                                                      "AFSPrimaryWorker Deleting object %08lX\n",
                                                                            pCurrentChildObject);

                                        AFSDeleteObjectInfo( pCurrentChildObject);
                                    }

                                    pCurrentDirEntry = pNextDirEntry;
                                }

                                pCurrentObject->Specific.Directory.DirectoryNodeListHead = NULL;

                                pCurrentObject->Specific.Directory.DirectoryNodeListTail = NULL;

                                pCurrentObject->Specific.Directory.ShortNameTree = NULL;

                                pCurrentObject->Specific.Directory.DirectoryNodeHdr.CaseSensitiveTreeHead = NULL;

                                pCurrentObject->Specific.Directory.DirectoryNodeHdr.CaseInsensitiveTreeHead = NULL;

                                pCurrentObject->Specific.Directory.DirectoryNodeCount = 0;

                                AFSDbgLogMsg( AFS_SUBSYSTEM_DIR_NODE_COUNT,
                                              AFS_TRACE_LEVEL_VERBOSE,
                                              "AFSPrimaryWorker Reset count to 0 on parent FID %08lX-%08lX-%08lX-%08lX\n",
                                                  pCurrentObject->FileId.Cell,
                                                  pCurrentObject->FileId.Volume,
                                                  pCurrentObject->FileId.Vnode,
                                                  pCurrentObject->FileId.Unique);


                                //
                                // Clear our enumerated flag on this object so we retrieve info again on next access
                                //

                                ClearFlag( pCurrentObject->Flags, AFS_OBJECT_FLAGS_DIRECTORY_ENUMERATED);

                                AFSReleaseResource( pCurrentObject->Specific.Directory.DirectoryNodeHdr.TreeLock);
                            }
                            else
                            {

                                AFSReleaseResource( pCurrentObject->Specific.Directory.DirectoryNodeHdr.TreeLock);

                                bReleaseVolumeLock = FALSE;

                                break;
                            }

                            AFSConvertToShared( pVolumeCB->ObjectInfoTree.TreeLock);
                        }
                        else
                        {

                            //
                            // Try to grab the volume lock again ... no problem if we don't
                            //

                            if( !AFSAcquireExcl( pVolumeCB->ObjectInfoTree.TreeLock,
                                                 FALSE))
                            {

                                bReleaseVolumeLock = FALSE;

                                break;
                            }
                        }

                        if( pCurrentObject != &pVolumeCB->ObjectInformation)
                        {

                            pCurrentObject = (AFSObjectInfoCB *)pCurrentObject->ListEntry.fLink;

                            if( pCurrentObject == NULL &&
                                pVolumeCB != AFSGlobalRoot)
                            {

                                pCurrentObject = &pVolumeCB->ObjectInformation;
                            }
                        }
                        else
                        {

                            pCurrentObject = NULL;
                        }

                        continue;
                    }
                    else if( pCurrentObject->FileType == AFS_FILE_TYPE_FILE)
                    {

                        if( BooleanFlagOn( pCurrentObject->Flags, AFS_OBJECT_FLAGS_DELETED) &&
                            pCurrentObject->ObjectReferenceCount == 0 &&
                            ( pCurrentObject->Fcb == NULL ||
                              pCurrentObject->Fcb->OpenReferenceCount == 0))
                        {

                            AFSReleaseResource( pVolumeCB->ObjectInfoTree.TreeLock);

                            if( AFSAcquireExcl( pVolumeCB->ObjectInfoTree.TreeLock,
                                                FALSE))
                            {

                                if( pCurrentObject->Fcb != NULL)
                                {

                                    AFSCleanupFcb( pCurrentObject->Fcb,
                                                   TRUE);

                                    AFSRemoveFcb( pCurrentObject->Fcb);
                                }

                                AFSDeleteObjectInfo( pCurrentObject);

                                AFSConvertToShared( pVolumeCB->ObjectInfoTree.TreeLock);

                                pCurrentObject = pNextObject;

                                continue;
                            }
                            else
                            {

                                bReleaseVolumeLock = FALSE;

                                break;
                            }
                        }
                        else if( pCurrentObject->Fcb != NULL)
                        {

                            AFSReleaseResource( pVolumeCB->ObjectInfoTree.TreeLock);

                            if( AFSAcquireExcl( pVolumeCB->ObjectInfoTree.TreeLock,
                                                FALSE))
                            {

                                AFSCleanupFcb( pCurrentObject->Fcb,
                                               FALSE);

                                AFSConvertToShared( pVolumeCB->ObjectInfoTree.TreeLock);

                                pCurrentObject = pNextObject;

                                continue;
                            }
                            else
                            {

                                bReleaseVolumeLock = FALSE;

                                break;
                            }
                        }
                    }

                    pCurrentObject = pNextObject;
                }

                if( bReleaseVolumeLock)
                {

                    AFSReleaseResource( pVolumeCB->ObjectInfoTree.TreeLock);
                }
            }

            //
            // Next volume cb
            //

            AFSReleaseResource( pVolumeCB->VolumeLock);

            AFSAcquireShared( &pRDRDeviceExt->Specific.RDR.VolumeListLock,
                              TRUE);

            pVolumeCB = (AFSVolumeCB *)pVolumeCB->ListEntry.fLink;
        }

        AFSReleaseResource( &pRDRDeviceExt->Specific.RDR.VolumeListLock);

    } // worker thread loop

    KeCancelTimer( &Timer);

    AFSDbgLogMsg( AFS_SUBSYSTEM_CLEANUP_PROCESSING,
                  AFS_TRACE_LEVEL_VERBOSE,
                  "AFSPrimaryVolumeWorkerThread Exiting\n");

    if( InterlockedDecrement( &pControlDeviceExt->Specific.Control.VolumeWorkerThreadCount) == 0)
    {

        KeSetEvent( &pControlDeviceExt->Specific.Control.VolumeWorkerCloseEvent,
                    0,
                    FALSE);
    }

    PsTerminateSystemThread( 0);

    return;
}

void
AFSVolumeWorkerThread( IN PVOID Context)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    AFSVolumeCB *pVolumeCB = (AFSVolumeCB * )Context;
    AFSWorkQueueContext *pPoolContext = (AFSWorkQueueContext *)&pVolumeCB->VolumeWorkerContext;
    AFSDeviceExt *pControlDeviceExt = NULL;
    AFSDeviceExt *pRDRDeviceExt = NULL;
    BOOLEAN exitThread = FALSE;
    LARGE_INTEGER DueTime;
    LONG TimeOut;
    KTIMER Timer;

    pControlDeviceExt = (AFSDeviceExt *)AFSControlDeviceObject->DeviceExtension;

    pRDRDeviceExt = (AFSDeviceExt *)AFSRDRDeviceObject->DeviceExtension;

    //
    // Initialize the timer for the worker thread
    //

    DueTime.QuadPart = -(5000);

    TimeOut = 5000;

    KeInitializeTimerEx( &Timer,
                         SynchronizationTimer);

    KeSetTimerEx( &Timer,
                  DueTime,
                  TimeOut,
                  NULL);

    //
    // Indicate that we are initialized and ready
    //

    KeSetEvent( &pPoolContext->WorkerThreadReady,
                0,
                FALSE);

    //
    // Indicate we are initialized
    //

    SetFlag( pPoolContext->State, AFS_WORKER_INITIALIZED);

    while( BooleanFlagOn( pPoolContext->State, AFS_WORKER_PROCESS_REQUESTS))
    {

        ntStatus = KeWaitForSingleObject( &Timer,
                                          Executive,
                                          KernelMode,
                                          FALSE,
                                          NULL);

        if( !NT_SUCCESS( ntStatus))
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSVolumeWorkerThread Wait for queue items failed Status %08lX\n", ntStatus);
        }
        else
        {

            //
            // If we are in shutdown mode and the dirty flag is clear then get out now
            //

            if( BooleanFlagOn( pRDRDeviceExt->DeviceFlags, AFS_DEVICE_FLAG_REDIRECTOR_SHUTDOWN))
            {

                break;
            }
        }
    } // worker thread loop

    KeCancelTimer( &Timer);

    if( InterlockedDecrement( &pControlDeviceExt->Specific.Control.VolumeWorkerThreadCount) == 0)
    {

        KeSetEvent( &pControlDeviceExt->Specific.Control.VolumeWorkerCloseEvent,
                    0,
                    FALSE);
    }

    PsTerminateSystemThread( 0);

    return;
}

NTSTATUS
AFSInsertWorkitem( IN AFSWorkItem *WorkItem)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    AFSDeviceExt *pDevExt = NULL;

    pDevExt = (AFSDeviceExt *)AFSLibraryDeviceObject->DeviceExtension;

    AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                  AFS_TRACE_LEVEL_VERBOSE,
                  "AFSInsertWorkitem Acquiring Control QueueLock lock %08lX EXCL %08lX\n",
                  &pDevExt->Specific.Library.QueueLock,
                  PsGetCurrentThread());

    AFSAcquireExcl( &pDevExt->Specific.Library.QueueLock,
                    TRUE);

    AFSDbgLogMsg( AFS_SUBSYSTEM_WORKER_PROCESSING,
                  AFS_TRACE_LEVEL_VERBOSE,
                  "AFSInsertWorkitem Inserting work item %08lX Count %08lX\n",
                  WorkItem,
                  InterlockedIncrement( &pDevExt->Specific.Library.QueueItemCount));

    if( pDevExt->Specific.Library.QueueTail != NULL) // queue already has nodes
    {

        pDevExt->Specific.Library.QueueTail->next = WorkItem;
    }
    else // first node
    {

        pDevExt->Specific.Library.QueueHead = WorkItem;
    }

    WorkItem->next = NULL;
    pDevExt->Specific.Library.QueueTail = WorkItem;

    // indicate that the queue has nodes
    KeSetEvent( &(pDevExt->Specific.Library.WorkerQueueHasItems),
                0,
                FALSE);

    AFSReleaseResource( &pDevExt->Specific.Library.QueueLock);

    return ntStatus;
}

NTSTATUS
AFSInsertIOWorkitem( IN AFSWorkItem *WorkItem)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    AFSDeviceExt *pDevExt = NULL;

    pDevExt = (AFSDeviceExt *)AFSLibraryDeviceObject->DeviceExtension;

    AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                  AFS_TRACE_LEVEL_VERBOSE,
                  "AFSInsertIOWorkitem Acquiring Control QueueLock lock %08lX EXCL %08lX\n",
                  &pDevExt->Specific.Library.IOQueueLock,
                  PsGetCurrentThread());

    AFSAcquireExcl( &pDevExt->Specific.Library.IOQueueLock,
                    TRUE);

    AFSDbgLogMsg( AFS_SUBSYSTEM_WORKER_PROCESSING,
                  AFS_TRACE_LEVEL_VERBOSE,
                  "AFSInsertWorkitem Inserting IO work item %08lX Count %08lX\n",
                  WorkItem,
                  InterlockedIncrement( &pDevExt->Specific.Library.IOQueueItemCount));

    if( pDevExt->Specific.Library.IOQueueTail != NULL) // queue already has nodes
    {

        pDevExt->Specific.Library.IOQueueTail->next = WorkItem;
    }
    else // first node
    {

        pDevExt->Specific.Library.IOQueueHead = WorkItem;
    }

    WorkItem->next = NULL;
    pDevExt->Specific.Library.IOQueueTail = WorkItem;

    // indicate that the queue has nodes
    KeSetEvent( &(pDevExt->Specific.Library.IOWorkerQueueHasItems),
                0,
                FALSE);

    AFSReleaseResource( &pDevExt->Specific.Library.IOQueueLock);

    return ntStatus;
}

NTSTATUS
AFSInsertWorkitemAtHead( IN AFSWorkItem *WorkItem)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    AFSDeviceExt *pDevExt = NULL;

    pDevExt = (AFSDeviceExt *)AFSLibraryDeviceObject->DeviceExtension;

    AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                  AFS_TRACE_LEVEL_VERBOSE,
                  "AFSInsertWorkitemAtHead Acquiring Control QueueLock lock %08lX EXCL %08lX\n",
                  &pDevExt->Specific.Library.QueueLock,
                  PsGetCurrentThread());

    AFSAcquireExcl( &pDevExt->Specific.Library.QueueLock,
                    TRUE);

    WorkItem->next = pDevExt->Specific.Library.QueueHead;

    pDevExt->Specific.Library.QueueHead = WorkItem;

    AFSDbgLogMsg( AFS_SUBSYSTEM_WORKER_PROCESSING,
                  AFS_TRACE_LEVEL_VERBOSE,
                  "AFSInsertWorkitemAtHead Inserting work item %08lX Count %08lX\n",
                  WorkItem,
                  InterlockedIncrement( &pDevExt->Specific.Library.QueueItemCount));

    //
    // indicate that the queue has nodes
    //

    KeSetEvent( &(pDevExt->Specific.Library.WorkerQueueHasItems),
                0,
                FALSE);

    AFSReleaseResource( &pDevExt->Specific.Library.QueueLock);

    return ntStatus;
}

AFSWorkItem *
AFSRemoveWorkItem()
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    AFSWorkItem        *pWorkItem = NULL;
    AFSDeviceExt *pDevExt = NULL;

    pDevExt = (AFSDeviceExt *)AFSLibraryDeviceObject->DeviceExtension;

    AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                  AFS_TRACE_LEVEL_VERBOSE,
                  "AFSRemoveWorkItem Acquiring Control QueueLock lock %08lX EXCL %08lX\n",
                  &pDevExt->Specific.Library.QueueLock,
                  PsGetCurrentThread());

    AFSAcquireExcl( &pDevExt->Specific.Library.QueueLock,
                    TRUE);

    if( pDevExt->Specific.Library.QueueHead != NULL) // queue has nodes
    {

        pWorkItem = pDevExt->Specific.Library.QueueHead;

        AFSDbgLogMsg( AFS_SUBSYSTEM_WORKER_PROCESSING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSRemoveWorkItem Removing work item %08lX Count %08lX Thread %08lX\n",
                      pWorkItem,
                      InterlockedDecrement( &pDevExt->Specific.Library.QueueItemCount),
                      PsGetCurrentThreadId());

        pDevExt->Specific.Library.QueueHead = pDevExt->Specific.Library.QueueHead->next;

        if( pDevExt->Specific.Library.QueueHead == NULL) // if queue just became empty
        {

            pDevExt->Specific.Library.QueueTail = NULL;

            KeResetEvent(&(pDevExt->Specific.Library.WorkerQueueHasItems));
        }
    }
    else
    {
        KeResetEvent(&(pDevExt->Specific.Library.WorkerQueueHasItems));
    }

    AFSReleaseResource( &pDevExt->Specific.Library.QueueLock);

    return pWorkItem;
}

AFSWorkItem *
AFSRemoveIOWorkItem()
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    AFSWorkItem        *pWorkItem = NULL;
    AFSDeviceExt *pDevExt = NULL;

    pDevExt = (AFSDeviceExt *)AFSLibraryDeviceObject->DeviceExtension;

    AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                  AFS_TRACE_LEVEL_VERBOSE,
                  "AFSRemoveIOWorkItem Acquiring Control QueueLock lock %08lX EXCL %08lX\n",
                  &pDevExt->Specific.Library.IOQueueLock,
                  PsGetCurrentThread());

    AFSAcquireExcl( &pDevExt->Specific.Library.IOQueueLock,
                    TRUE);

    if( pDevExt->Specific.Library.IOQueueHead != NULL) // queue has nodes
    {

        pWorkItem = pDevExt->Specific.Library.IOQueueHead;

        AFSDbgLogMsg( AFS_SUBSYSTEM_WORKER_PROCESSING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSRemoveWorkItem Removing work item %08lX Count %08lX Thread %08lX\n",
                      pWorkItem,
                      InterlockedDecrement( &pDevExt->Specific.Library.IOQueueItemCount),
                      PsGetCurrentThreadId());

        pDevExt->Specific.Library.IOQueueHead = pDevExt->Specific.Library.IOQueueHead->next;

        if( pDevExt->Specific.Library.IOQueueHead == NULL) // if queue just became empty
        {

            pDevExt->Specific.Library.IOQueueTail = NULL;

            KeResetEvent(&(pDevExt->Specific.Library.IOWorkerQueueHasItems));
        }
    }
    else
    {
        KeResetEvent(&(pDevExt->Specific.Library.IOWorkerQueueHasItems));
    }

    AFSReleaseResource( &pDevExt->Specific.Library.IOQueueLock);

    return pWorkItem;
}

NTSTATUS
AFSQueueWorkerRequest( IN AFSWorkItem *WorkItem)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    AFSDeviceExt *pDevExt = NULL;
    BOOLEAN bWait = BooleanFlagOn( WorkItem->RequestFlags, AFS_SYNCHRONOUS_REQUEST);

    //
    // Submit the work item to the worker
    //

    ntStatus = AFSInsertWorkitem( WorkItem);

    if( bWait)
    {

        //
        // Sync request so block on the work item event
        //

        ntStatus = KeWaitForSingleObject( &WorkItem->Event,
                                          Executive,
                                          KernelMode,
                                          FALSE,
                                          NULL);
    }

    return ntStatus;
}

NTSTATUS
AFSQueueIOWorkerRequest( IN AFSWorkItem *WorkItem)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    AFSDeviceExt *pDevExt = NULL;
    BOOLEAN bWait = BooleanFlagOn( WorkItem->RequestFlags, AFS_SYNCHRONOUS_REQUEST);

    //
    // Submit the work item to the worker
    //

    ntStatus = AFSInsertIOWorkitem( WorkItem);

    if( bWait)
    {

        //
        // Sync request so block on the work item event
        //

        ntStatus = KeWaitForSingleObject( &WorkItem->Event,
                                          Executive,
                                          KernelMode,
                                          FALSE,
                                          NULL);
    }

    return ntStatus;
}

NTSTATUS
AFSQueueWorkerRequestAtHead( IN AFSWorkItem *WorkItem)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    AFSDeviceExt *pDevExt = NULL;
    BOOLEAN bWait = BooleanFlagOn( WorkItem->RequestFlags, AFS_SYNCHRONOUS_REQUEST);

    //
    // Submit the work item to the worker
    //

    ntStatus = AFSInsertWorkitemAtHead( WorkItem);

    if( bWait)
    {

        //
        // Sync request so block on the work item event
        //

        ntStatus = KeWaitForSingleObject( &WorkItem->Event,
                                          Executive,
                                          KernelMode,
                                          FALSE,
                                          NULL);
    }

    return ntStatus;
}

NTSTATUS
AFSQueueFlushExtents( IN AFSFcb *Fcb)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    AFSDeviceExt *pRDRDeviceExt = (AFSDeviceExt *)AFSRDRDeviceObject->DeviceExtension;
    AFSWorkItem *pWorkItem = NULL;

    __try
    {

        AFSDbgLogMsg( AFS_SUBSYSTEM_WORKER_PROCESSING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSQueueFlushExtents Queuing request for FID %08lX-%08lX-%08lX-%08lX\n",
                      Fcb->ObjectInformation->FileId.Cell,
                      Fcb->ObjectInformation->FileId.Volume,
                      Fcb->ObjectInformation->FileId.Vnode,
                      Fcb->ObjectInformation->FileId.Unique);

        //
        // Increment our flush count here just to keep the number of items in the
        // queue down. We'll decrement it just below.
        //

        if( InterlockedIncrement( &Fcb->Specific.File.QueuedFlushCount) > 3)
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_WORKER_PROCESSING,
                          AFS_TRACE_LEVEL_VERBOSE,
                          "AFSQueueFlushExtents Max queued items for FID %08lX-%08lX-%08lX-%08lX\n",
                          Fcb->ObjectInformation->FileId.Cell,
                          Fcb->ObjectInformation->FileId.Volume,
                          Fcb->ObjectInformation->FileId.Vnode,
                          Fcb->ObjectInformation->FileId.Unique);

            try_return( ntStatus);
        }

        if( BooleanFlagOn( pRDRDeviceExt->DeviceFlags, AFS_DEVICE_FLAG_REDIRECTOR_SHUTDOWN))
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSQueueFlushExtents Failing request, in shutdown\n");

            try_return( ntStatus = STATUS_TOO_LATE);
        }

        //
        // Allocate our request structure and send it to the worker
        //

        pWorkItem = (AFSWorkItem *)AFSLibExAllocatePoolWithTag( NonPagedPool,
                                                                sizeof( AFSWorkItem),
                                                                AFS_WORK_ITEM_TAG);

        if( pWorkItem == NULL)
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSQueueFlushExtents Failed to allocate work item\n");

            try_return( ntStatus = STATUS_INSUFFICIENT_RESOURCES);
        }

        RtlZeroMemory( pWorkItem,
                       sizeof( AFSWorkItem));

        pWorkItem->Size = sizeof( AFSWorkItem);

        pWorkItem->ProcessID = (ULONGLONG)PsGetCurrentProcessId();

        pWorkItem->RequestType = AFS_WORK_FLUSH_FCB;

        pWorkItem->Specific.Fcb.Fcb = Fcb;

        InterlockedIncrement( &Fcb->OpenReferenceCount);

        AFSDbgLogMsg( AFS_SUBSYSTEM_FCB_REF_COUNTING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSQueueFlushExtents Increment count on Fcb %08lX Cnt %d\n",
                                                    Fcb,
                                                    Fcb->OpenReferenceCount);

        AFSDbgLogMsg( AFS_SUBSYSTEM_WORKER_PROCESSING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSQueueFlushExtents Workitem %08lX for FID %08lX-%08lX-%08lX-%08lX\n",
                      pWorkItem,
                      Fcb->ObjectInformation->FileId.Cell,
                      Fcb->ObjectInformation->FileId.Volume,
                      Fcb->ObjectInformation->FileId.Vnode,
                      Fcb->ObjectInformation->FileId.Unique);

        ntStatus = AFSQueueWorkerRequest( pWorkItem);

try_exit:

        AFSDbgLogMsg( AFS_SUBSYSTEM_WORKER_PROCESSING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSQueueFlushExtents Request complete Status %08lX FID %08lX-%08lX-%08lX-%08lX\n",
                      Fcb->ObjectInformation->FileId.Cell,
                      Fcb->ObjectInformation->FileId.Volume,
                      Fcb->ObjectInformation->FileId.Vnode,
                      Fcb->ObjectInformation->FileId.Unique,
                      ntStatus);

        //
        // Remove the count we added above
        //

        if( InterlockedDecrement( &Fcb->Specific.File.QueuedFlushCount) == 0)
        {

            KeSetEvent( &Fcb->NPFcb->Specific.File.QueuedFlushEvent,
                        0,
                        FALSE);
        }

        if( !NT_SUCCESS( ntStatus))
        {

            if( pWorkItem != NULL)
            {

                InterlockedDecrement( &Fcb->OpenReferenceCount);

                ExFreePool( pWorkItem);
            }

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSQueueFlushExtents Failed to queue request Status %08lX\n", ntStatus);
        }
    }
    __except( AFSExceptionFilter( GetExceptionCode(), GetExceptionInformation()) )
    {

        AFSDbgLogMsg( 0,
                      0,
                      "EXCEPTION - AFSQueueFlushExtents\n");
    }

    return ntStatus;
}

NTSTATUS
AFSQueueAsyncRead( IN PDEVICE_OBJECT DeviceObject,
                   IN PIRP Irp,
                   IN HANDLE CallerProcess)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    AFSWorkItem *pWorkItem = NULL;

    __try
    {

        AFSDbgLogMsg( AFS_SUBSYSTEM_WORKER_PROCESSING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSQueueAsyncRead Queuing request for Irp %08lX\n",
                      Irp);

        pWorkItem = (AFSWorkItem *) AFSLibExAllocatePoolWithTag( NonPagedPool,
                                                                 sizeof(AFSWorkItem),
                                                                 AFS_WORK_ITEM_TAG);
        if (NULL == pWorkItem)
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_IO_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSQueueAsyncRead Failed to allocate work item\n");

            try_return( ntStatus = STATUS_INSUFFICIENT_RESOURCES );
        }

        RtlZeroMemory( pWorkItem,
                       sizeof(AFSWorkItem));

        pWorkItem->Size = sizeof( AFSWorkItem);

        pWorkItem->RequestType = AFS_WORK_ASYNCH_READ;

        pWorkItem->Specific.AsynchIo.Device = DeviceObject;

        pWorkItem->Specific.AsynchIo.Irp = Irp;

        pWorkItem->Specific.AsynchIo.CallingProcess = CallerProcess;

        AFSDbgLogMsg( AFS_SUBSYSTEM_WORKER_PROCESSING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSQueueAsyncRead Workitem %08lX for Irp %08lX\n",
                      pWorkItem,
                      Irp);

        ntStatus = AFSQueueWorkerRequest( pWorkItem);

try_exit:

        AFSDbgLogMsg( AFS_SUBSYSTEM_WORKER_PROCESSING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSQueueAsyncRead Request for Irp %08lX complete Status %08lX\n",
                      Irp,
                      ntStatus);

        if( !NT_SUCCESS( ntStatus))
        {

            if( pWorkItem != NULL)
            {

                ExFreePool( pWorkItem);
            }

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSQueueAsyncRead Failed to queue request Status %08lX\n", ntStatus);
        }
    }
    __except( AFSExceptionFilter( GetExceptionCode(), GetExceptionInformation()) )
    {

        AFSDbgLogMsg( 0,
                      0,
                      "EXCEPTION - AFSQueueAsyncRead\n");
    }

    return ntStatus;
}

NTSTATUS
AFSQueueAsyncWrite( IN PDEVICE_OBJECT DeviceObject,
                    IN PIRP Irp,
                    IN HANDLE CallerProcess)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    AFSWorkItem *pWorkItem = NULL;

    __try
    {

        AFSDbgLogMsg( AFS_SUBSYSTEM_WORKER_PROCESSING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSQueueAsyncWrite Queuing request for Irp %08lX\n",
                      Irp);

        pWorkItem = (AFSWorkItem *) AFSLibExAllocatePoolWithTag( NonPagedPool,
                                                                 sizeof(AFSWorkItem),
                                                                 AFS_WORK_ITEM_TAG);
        if (NULL == pWorkItem)
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_IO_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSQueueAsyncWrite Failed to allocate work item\n");

            try_return( ntStatus = STATUS_INSUFFICIENT_RESOURCES );
        }

        RtlZeroMemory( pWorkItem,
                       sizeof(AFSWorkItem));

        pWorkItem->Size = sizeof( AFSWorkItem);

        pWorkItem->RequestType = AFS_WORK_ASYNCH_WRITE;

        pWorkItem->Specific.AsynchIo.Device = DeviceObject;

        pWorkItem->Specific.AsynchIo.Irp = Irp;

        pWorkItem->Specific.AsynchIo.CallingProcess = CallerProcess;

        AFSDbgLogMsg( AFS_SUBSYSTEM_WORKER_PROCESSING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSQueueAsyncWrite Workitem %08lX for Irp %08lX\n",
                      pWorkItem,
                      Irp);

        ntStatus = AFSQueueWorkerRequest( pWorkItem);

try_exit:

        AFSDbgLogMsg( AFS_SUBSYSTEM_WORKER_PROCESSING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSQueueAsyncWrite Request for Irp %08lX complete Status %08lX\n",
                      Irp,
                      ntStatus);

        if( !NT_SUCCESS( ntStatus))
        {

            if( pWorkItem != NULL)
            {

                ExFreePool( pWorkItem);
            }

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSQueueAsyncWrite Failed to queue request Status %08lX\n", ntStatus);
        }
    }
    __except( AFSExceptionFilter( GetExceptionCode(), GetExceptionInformation()) )
    {

        AFSDbgLogMsg( 0,
                      0,
                      "EXCEPTION - AFSQueueAsyncWrite\n");
    }

    return ntStatus;
}

NTSTATUS
AFSQueueGlobalRootEnumeration()
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    AFSWorkItem *pWorkItem = NULL;

    __try
    {

        pWorkItem = (AFSWorkItem *) AFSLibExAllocatePoolWithTag( NonPagedPool,
                                                                 sizeof(AFSWorkItem),
                                                                 AFS_WORK_ITEM_TAG);
        if (NULL == pWorkItem)
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_IO_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSQueueGlobalRootEnumeration Failed to allocate work item\n");

            try_return( ntStatus = STATUS_INSUFFICIENT_RESOURCES );
        }

        RtlZeroMemory( pWorkItem,
                       sizeof(AFSWorkItem));

        pWorkItem->Size = sizeof( AFSWorkItem);

        pWorkItem->RequestType = AFS_WORK_ENUMERATE_GLOBAL_ROOT;

        AFSDbgLogMsg( AFS_SUBSYSTEM_WORKER_PROCESSING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSQueueGlobalRootEnumeration Workitem %08lX\n",
                      pWorkItem);

        ntStatus = AFSQueueWorkerRequest( pWorkItem);

try_exit:

        AFSDbgLogMsg( AFS_SUBSYSTEM_WORKER_PROCESSING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSQueueGlobalRootEnumeration Request complete Status %08lX\n",
                      ntStatus);

        if( !NT_SUCCESS( ntStatus))
        {

            if( pWorkItem != NULL)
            {

                ExFreePool( pWorkItem);
            }

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSQueueGlobalRootEnumeration Failed to queue request Status %08lX\n",
                          ntStatus);
        }
    }
    __except( AFSExceptionFilter( GetExceptionCode(), GetExceptionInformation()) )
    {

        AFSDbgLogMsg( 0,
                      0,
                      "EXCEPTION - AFSQueueGlobalRootEnumeration\n");
    }

    return ntStatus;
}

NTSTATUS
AFSQueueStartIos( IN PFILE_OBJECT CacheFileObject,
                  IN UCHAR FunctionCode,
                  IN ULONG RequestFlags,
                  IN AFSIoRun *IoRuns,
                  IN ULONG RunCount,
                  IN AFSGatherIo *GatherIo)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    AFSDeviceExt *pRDRDeviceExt = (AFSDeviceExt *)AFSRDRDeviceObject->DeviceExtension;
    AFSWorkItem *pWorkItem = NULL;

    __try
    {

        if( BooleanFlagOn( pRDRDeviceExt->DeviceFlags, AFS_DEVICE_FLAG_REDIRECTOR_SHUTDOWN))
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSQueueStartIos Failing request, in shutdown\n");

            try_return( ntStatus = STATUS_TOO_LATE);
        }

        //
        // Allocate our request structure and send it to the worker
        //

        pWorkItem = (AFSWorkItem *)AFSLibExAllocatePoolWithTag( NonPagedPool,
                                                                sizeof( AFSWorkItem),
                                                                AFS_WORK_ITEM_TAG);

        if( pWorkItem == NULL)
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_FILE_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSQueueStartIos Failed to allocate work item\n");

            try_return( ntStatus = STATUS_INSUFFICIENT_RESOURCES);
        }

        RtlZeroMemory( pWorkItem,
                       sizeof( AFSWorkItem));

        KeInitializeEvent( &pWorkItem->Event,
                           NotificationEvent,
                           FALSE);

        pWorkItem->Size = sizeof( AFSWorkItem);

        pWorkItem->ProcessID = (ULONGLONG)PsGetCurrentProcessId();

        pWorkItem->RequestType = AFS_WORK_START_IOS;

        pWorkItem->Specific.CacheAccess.CacheFileObject = CacheFileObject;

        pWorkItem->Specific.CacheAccess.FunctionCode = FunctionCode;

        pWorkItem->Specific.CacheAccess.RequestFlags = RequestFlags;

        pWorkItem->Specific.CacheAccess.IoRuns = IoRuns;

        pWorkItem->Specific.CacheAccess.RunCount = RunCount;

        pWorkItem->Specific.CacheAccess.GatherIo = GatherIo;

        AFSDbgLogMsg( AFS_SUBSYSTEM_WORKER_PROCESSING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSQueueStartIos Queuing IO Workitem %08lX\n",
                      pWorkItem);

        ntStatus = AFSQueueIOWorkerRequest( pWorkItem);

try_exit:

        AFSDbgLogMsg( AFS_SUBSYSTEM_WORKER_PROCESSING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSQueueStartIos Request complete Status %08lX\n",
                      ntStatus);

        if( !NT_SUCCESS( ntStatus))
        {

            if( pWorkItem != NULL)
            {

                ExFreePool( pWorkItem);
            }
        }
    }
    __except( AFSExceptionFilter( GetExceptionCode(), GetExceptionInformation()) )
    {

        AFSDbgLogMsg( 0,
                      0,
                      "EXCEPTION - AFSQueueStartIos\n");
    }

    return ntStatus;
}
