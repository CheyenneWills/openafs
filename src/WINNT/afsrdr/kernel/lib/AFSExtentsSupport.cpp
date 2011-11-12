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
// File: AFSCommSupport.cpp
//
#include "AFSCommon.h"

#define AFS_MAX_FCBS_TO_DROP 10

static AFSExtent *ExtentFor( PLIST_ENTRY le, ULONG SkipList );
static AFSExtent *NextExtent( AFSExtent *Extent, ULONG SkipList );
static ULONG ExtentsMasks[AFS_NUM_EXTENT_LISTS] = AFS_EXTENTS_MASKS;
static VOID VerifyExtentsLists(AFSFcb *Fcb);
static AFSExtent *DirtyExtentFor(PLIST_ENTRY le);

LIST_ENTRY *
AFSEntryForOffset( IN AFSFcb *Fcb,
                   IN PLARGE_INTEGER Offset);


//
// Returns with Extents lock EX and no one using them.
//

VOID
AFSLockForExtentsTrim( IN AFSFcb *Fcb)
{
    NTSTATUS          ntStatus;
    AFSNonPagedFcb *pNPFcb = Fcb->NPFcb;

    AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                  AFS_TRACE_LEVEL_VERBOSE,
                  "AFSLockForExtentsTrim Acuiring Fcb extents lock %08lX EXCL %08lX\n",
                  &pNPFcb->Specific.File.ExtentsResource,
                  PsGetCurrentThread());

    AFSAcquireExcl( &pNPFcb->Specific.File.ExtentsResource, TRUE );

    return;
}

//
// return FALSE *or* with Extents lock EX and noone using them
//
BOOLEAN
AFSLockForExtentsTrimNoWait( IN AFSFcb *Fcb)
{
    AFSNonPagedFcb *pNPFcb = Fcb->NPFcb;

    AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                  AFS_TRACE_LEVEL_VERBOSE,
                  "AFSLockForExtentsTrimNoWait Attempting to acquiring Fcb extent lock %08lX EXCL %08lX\n",
                  &pNPFcb->Specific.File.ExtentsResource,
                  PsGetCurrentThread());

    if (!AFSAcquireExcl( &pNPFcb->Specific.File.ExtentsResource, FALSE ))
    {
        //
        // Couldn't lock immediately
        //

        AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSLockForExtentsTrimNoWait Refused to wait for Fcb extent lock %08lX EXCL %08lX\n",
                      &pNPFcb->Specific.File.ExtentsResource,
                      PsGetCurrentThread());

        return FALSE;
    }

    return TRUE;
}
//
// Pull all the extents away from the FCB.
//
BOOLEAN
AFSTearDownFcbExtents( IN AFSFcb *Fcb )
{
    BOOLEAN             bFoundExtents = FALSE;
    AFSNonPagedFcb      *pNPFcb = Fcb->NPFcb;
    LIST_ENTRY          *le;
    AFSExtent           *pEntry;
    ULONG                ulCount = 0, ulReleaseCount = 0, ulProcessCount = 0;
    size_t               sz;
    AFSReleaseExtentsCB *pRelease = NULL;
    BOOLEAN              locked = FALSE;
    NTSTATUS             ntStatus;
    AFSDeviceExt        *pControlDevExt = (AFSDeviceExt *)AFSControlDeviceObject->DeviceExtension;

    __Enter
    {

        //
        // Ensure that no one is working with the extents and grab the
        // lock
        //

        AFSLockForExtentsTrim( Fcb );

        locked = TRUE;

        if (0 == Fcb->Specific.File.ExtentCount)
        {
            try_return ( ntStatus = STATUS_SUCCESS);
        }

        //
        // Release a max of 100 extents at a time
        //

        sz = sizeof( AFSReleaseExtentsCB ) + (AFS_MAXIMUM_EXTENT_RELEASE_COUNT * sizeof ( AFSFileExtentCB ));

        pRelease = (AFSReleaseExtentsCB*) AFSExAllocatePoolWithTag( NonPagedPool,
                                                                    sz,
                                                                    AFS_EXTENT_RELEASE_TAG);
        if (NULL == pRelease)
        {

            try_return ( ntStatus = STATUS_INSUFFICIENT_RESOURCES );
        }

        le = Fcb->Specific.File.ExtentsLists[AFS_EXTENTS_LIST].Flink;

        ulCount = Fcb->Specific.File.ExtentCount;

        while( ulReleaseCount < ulCount)
        {

            bFoundExtents = TRUE;

            RtlZeroMemory( pRelease,
                           sizeof( AFSReleaseExtentsCB ) +
                           (AFS_MAXIMUM_EXTENT_RELEASE_COUNT * sizeof ( AFSFileExtentCB )));

            if( ulCount - ulReleaseCount <= AFS_MAXIMUM_EXTENT_RELEASE_COUNT)
            {
                ulProcessCount = ulCount - ulReleaseCount;
            }
            else
            {
                ulProcessCount = AFS_MAXIMUM_EXTENT_RELEASE_COUNT;
            }

            pRelease->Flags = AFS_EXTENT_FLAG_RELEASE;
            pRelease->ExtentCount = ulProcessCount;

            //
            // Update the metadata for this call
            //

            pRelease->AllocationSize = Fcb->ObjectInformation->EndOfFile;
            pRelease->CreateTime = Fcb->ObjectInformation->CreationTime;
            pRelease->ChangeTime = Fcb->ObjectInformation->ChangeTime;
            pRelease->LastAccessTime = Fcb->ObjectInformation->LastAccessTime;
            pRelease->LastWriteTime = Fcb->ObjectInformation->LastWriteTime;

            ulProcessCount = 0;

            while (ulProcessCount < pRelease->ExtentCount)
            {
                pEntry = ExtentFor( le, AFS_EXTENTS_LIST );

                pRelease->FileExtents[ulProcessCount].Flags = AFS_EXTENT_FLAG_RELEASE;

#if GEN_MD5
                RtlCopyMemory( pRelease->FileExtents[ulProcessCount].MD5,
                               pEntry->MD5,
                               sizeof(pEntry->MD5));

                pRelease->FileExtents[ulProcessCount].Flags |= AFS_EXTENT_FLAG_MD5_SET;
#endif

                if( BooleanFlagOn( pEntry->Flags, AFS_EXTENT_DIRTY))
                {

                    AFSAcquireExcl( &pNPFcb->Specific.File.DirtyExtentsListLock,
                                    TRUE);

                    if( BooleanFlagOn( pEntry->Flags, AFS_EXTENT_DIRTY))
                    {

                        LONG dirtyCount;

                        AFSRemoveEntryDirtyList( Fcb,
                                                 pEntry);

                        pRelease->FileExtents[ulProcessCount].Flags |= AFS_EXTENT_FLAG_DIRTY;

                        dirtyCount = InterlockedDecrement( &Fcb->Specific.File.ExtentsDirtyCount);

                        ASSERT( dirtyCount >= 0);
                    }

                    AFSReleaseResource( &pNPFcb->Specific.File.DirtyExtentsListLock);
                }

                AFSDbgLogMsg( AFS_SUBSYSTEM_EXTENT_PROCESSING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSTearDownFcbExtents Releasing extent %p fid %08lX-%08lX-%08lX-%08lX Offset %08lX-%08lX Len %08lX\n",
                              pEntry,
                              Fcb->ObjectInformation->FileId.Cell,
                              Fcb->ObjectInformation->FileId.Volume,
                              Fcb->ObjectInformation->FileId.Vnode,
                              Fcb->ObjectInformation->FileId.Unique,
                              pEntry->FileOffset.HighPart,
                              pEntry->FileOffset.LowPart,
                              pEntry->Size);

                pRelease->FileExtents[ulProcessCount].Length = pEntry->Size;
                pRelease->FileExtents[ulProcessCount].DirtyLength = pEntry->Size;
                pRelease->FileExtents[ulProcessCount].DirtyOffset = 0;
                pRelease->FileExtents[ulProcessCount].CacheOffset = pEntry->CacheOffset;
                pRelease->FileExtents[ulProcessCount].FileOffset = pEntry->FileOffset;

                InterlockedExchangeAdd( &pControlDevExt->Specific.Control.ExtentsHeldLength, -((LONG)(pEntry->Size/1024)));

                InterlockedExchangeAdd( &Fcb->Specific.File.ExtentLength, -((LONG)(pEntry->Size/1024)));

                ASSERT( pEntry->ActiveCount == 0);

                ulProcessCount ++;
                le = le->Flink;
                AFSExFreePool( pEntry);

                InterlockedDecrement( &Fcb->Specific.File.ExtentCount);

                if( InterlockedDecrement( &pControlDevExt->Specific.Control.ExtentCount) == 0)
                {

                    KeSetEvent( &pControlDevExt->Specific.Control.ExtentsHeldEvent,
                                0,
                                FALSE);
                }
            }

            //
            // Send the request down.  We cannot send this down
            // asynchronously - if we did that we could request them
            // back before the service got this request and then this
            // request would be a corruption.
            //

            sz = sizeof( AFSReleaseExtentsCB ) + (ulProcessCount * sizeof ( AFSFileExtentCB ));

            ntStatus = AFSProcessRequest( AFS_REQUEST_TYPE_RELEASE_FILE_EXTENTS,
                                          AFS_REQUEST_FLAG_SYNCHRONOUS,
                                          &Fcb->AuthGroup,
                                          NULL,
                                          &Fcb->ObjectInformation->FileId,
                                          pRelease,
                                          sz,
                                          NULL,
                                          NULL);

            if( !NT_SUCCESS(ntStatus))
            {

                //
                // Regardless of whether or not the AFSProcessRequest() succeeded, the extents
                // were released (if AFS_EXTENT_FLAG_RELEASE was set).  Log the error so it is known.
                //

                AFSDbgLogMsg( AFS_SUBSYSTEM_EXTENT_PROCESSING,
                              AFS_TRACE_LEVEL_ERROR,
                              "AFSTearDownFcbExtents AFS_REQUEST_TYPE_RELEASE_FILE_EXTENTS failed fid %08lX-%08lX-%08lX-%08lX Status %08lX\n",
                              Fcb->ObjectInformation->FileId.Cell,
                              Fcb->ObjectInformation->FileId.Volume,
                              Fcb->ObjectInformation->FileId.Vnode,
                              Fcb->ObjectInformation->FileId.Unique,
                              ntStatus);

            }

            ulReleaseCount += ulProcessCount;
        }

        //
        // Reinitialize the skip lists
        //

        ASSERT( Fcb->Specific.File.ExtentCount == 0);

        for (ULONG i = 0; i < AFS_NUM_EXTENT_LISTS; i++)
        {
            InitializeListHead(&Fcb->Specific.File.ExtentsLists[i]);
        }

        //
        // Reinitialize the dirty list as well
        //

        AFSAcquireExcl( &pNPFcb->Specific.File.DirtyExtentsListLock,
                        TRUE);

        ASSERT( Fcb->Specific.File.ExtentsDirtyCount == 0);

        Fcb->NPFcb->Specific.File.DirtyListHead = NULL;
        Fcb->NPFcb->Specific.File.DirtyListTail = NULL;

        AFSReleaseResource( &pNPFcb->Specific.File.DirtyExtentsListLock);

        Fcb->NPFcb->Specific.File.ExtentsRequestStatus = STATUS_SUCCESS;

try_exit:

        if (locked)
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                          AFS_TRACE_LEVEL_VERBOSE,
                          "AFSTearDownFcbExtents Releasing Fcb extent lock %08lX thread %08lX\n",
                          &Fcb->NPFcb->Specific.File.ExtentsResource,
                          PsGetCurrentThread());

            AFSReleaseResource( &Fcb->NPFcb->Specific.File.ExtentsResource );
        }

        if (pRelease)
        {

            AFSExFreePool( pRelease);
        }
    }

    return bFoundExtents;
}

static PAFSExtent
ExtentForOffsetInList( IN AFSFcb *Fcb,
                       IN LIST_ENTRY *List,
                       IN ULONG ListNumber,
                       IN PLARGE_INTEGER Offset)
{
    //
    // Return the extent that maps the offset, that
    //   - Contains the offset
    //   - or is immediately ahead of the offset (in this list)
    //   - otherwise return NULL.
    //

    PLIST_ENTRY  pLe = List;
    AFSExtent   *pPrevious = NULL;

    ASSERT( ExIsResourceAcquiredLite( &Fcb->NPFcb->Specific.File.ExtentsResource ));

    while (pLe != &Fcb->Specific.File.ExtentsLists[ListNumber])
    {
        AFSExtent *entry;

        entry = ExtentFor( pLe, ListNumber );

        if( entry == NULL)
        {
            return entry;
        }

        if (Offset->QuadPart < entry->FileOffset.QuadPart)
        {
            //
            // Offset is ahead of entry.  Return previous
            //
            return pPrevious;
        }

        if (Offset->QuadPart >= (entry->FileOffset.QuadPart + entry->Size))
        {
            //
            // We start after this extent - carry on round
            //
            pPrevious = entry;
            pLe = pLe->Flink;
            continue;
        }

        //
        // Otherwise its a match
        //

        return entry;
    }

    //
    // Got to the end.  Return Previous
    //
    return pPrevious;
}

BOOLEAN
AFSExtentContains( IN AFSExtent *Extent, IN PLARGE_INTEGER Offset)
{
    if (NULL == Extent)
    {
        return FALSE;
    }
    return (Extent->FileOffset.QuadPart <= Offset->QuadPart &&
            (Extent->FileOffset.QuadPart + Extent->Size) > Offset->QuadPart);
}


//
// Return the extent that contains the offset
//
PAFSExtent
AFSExtentForOffsetHint( IN AFSFcb *Fcb,
                        IN PLARGE_INTEGER Offset,
                        IN BOOLEAN ReturnPrevious,
                        IN AFSExtent *Hint)
{
    AFSExtent *pPrevious = Hint;
    LIST_ENTRY *pLe;
    LONG i;

    ASSERT( ExIsResourceAcquiredLite( &Fcb->NPFcb->Specific.File.ExtentsResource ));

#if AFS_VALIDATE_EXTENTS
    VerifyExtentsLists(Fcb);
#endif

    //
    // So we will go across the skip lists until we find an
    // appropriate entry (previous or direct match).  If it's a match
    // we are done, other wise we start on the next layer down
    //
    for (i = AFS_NUM_EXTENT_LISTS-1; i >= AFS_EXTENTS_LIST; i--)
    {
        if (NULL == pPrevious)
        {
            //
            // We haven't found anything in the previous layers
            //
            pLe = Fcb->Specific.File.ExtentsLists[i].Flink;
        }
        else if (NULL == pPrevious->Lists[i].Flink)
        {
            ASSERT(AFS_EXTENTS_LIST != i);
            //
            // The hint doesn't exist at this level, next one down
            //
            continue;
        }
        else
        {
            //
            // take the previous into the next
            //
            pLe = &pPrevious->Lists[i];
        }

        pPrevious = ExtentForOffsetInList( Fcb, pLe, i, Offset);

        if (NULL != pPrevious && AFSExtentContains(pPrevious, Offset))
        {
            //
            // Found it immediately.  Stop here
            //
            return pPrevious;
        }
    }

    if (NULL == pPrevious || ReturnPrevious )
    {
        return pPrevious;
    }

    ASSERT( !AFSExtentContains(pPrevious, Offset) );

    return NULL;
}

LIST_ENTRY *
AFSEntryForOffset( IN AFSFcb *Fcb,
                   IN PLARGE_INTEGER Offset)
{
    AFSExtent *pPrevious = NULL;
    LIST_ENTRY *pLe;
    LONG i;

    ASSERT( ExIsResourceAcquiredLite( &Fcb->NPFcb->Specific.File.ExtentsResource ));

#if AFS_VALIDATE_EXTENTS
    VerifyExtentsLists(Fcb);
#endif

    //
    // So we will go across the skip lists until we find an
    // appropriate entry (previous or direct match).  If it's a match
    // we are done, other wise we start on the next layer down
    //
    for (i = AFS_NUM_EXTENT_LISTS-1; i >= AFS_EXTENTS_LIST; i--)
    {
        if (NULL == pPrevious)
        {
            //
            // We haven't found anything in the previous layers
            //
            pLe = Fcb->Specific.File.ExtentsLists[i].Flink;
        }
        else if (NULL == pPrevious->Lists[i].Flink)
        {
            ASSERT(AFS_EXTENTS_LIST != i);
            //
            // The hint doesn't exist at this level, next one down
            //
            continue;
        }
        else
        {
            //
            // take the previous into the next
            //
            pLe = &pPrevious->Lists[i];
        }

        pPrevious = ExtentForOffsetInList( Fcb, pLe, i, Offset);

        if (NULL != pPrevious && AFSExtentContains(pPrevious, Offset))
        {
            //
            // Found it immediately.  Stop here
            //
            return pLe;
        }
    }

    return NULL;
}

PAFSExtent
AFSExtentForOffset( IN AFSFcb *Fcb,
                    IN PLARGE_INTEGER Offset,
                    IN BOOLEAN ReturnPrevious)
{
    return AFSExtentForOffsetHint(Fcb, Offset, ReturnPrevious, NULL);
}


BOOLEAN AFSDoExtentsMapRegion(IN AFSFcb *Fcb,
                              IN PLARGE_INTEGER Offset,
                              IN ULONG Size,
                              IN OUT AFSExtent **FirstExtent,
                              OUT AFSExtent **LastExtent)
{
    //
    // Return TRUE region is completely mapped.  FALSE
    // otherwise.  If the region isn't mapped then the last
    // extent to map part of the region is returned.
    //
    // *LastExtent as input is where to start looking.
    // *LastExtent as output is either the extent which
    //  contains the Offset, or the last one which doesn't
    //
    AFSExtent *entry;
    AFSExtent *newEntry;
    BOOLEAN retVal = FALSE;

    __Enter
    {

        AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSDoExtentsMapRegion Acquiring Fcb extent lock %08lX SHARED %08lX\n",
                      &Fcb->NPFcb->Specific.File.ExtentsResource,
                      PsGetCurrentThread());

        AFSAcquireShared( &Fcb->NPFcb->Specific.File.ExtentsResource, TRUE );

        entry = AFSExtentForOffsetHint(Fcb, Offset, TRUE, *FirstExtent);
        *FirstExtent = entry;

        if (NULL == entry || !AFSExtentContains(entry, Offset))
        {
            try_return (retVal = FALSE);
        }

        ASSERT(Offset->QuadPart >= entry->FileOffset.QuadPart);

        while (TRUE)
        {
            if ((entry->FileOffset.QuadPart + entry->Size) >=
                                                (Offset->QuadPart + Size))
            {
                //
                // The end is inside the extent
                //
                try_return (retVal = TRUE);
            }

            if (entry->Lists[AFS_EXTENTS_LIST].Flink == &Fcb->Specific.File.ExtentsLists[AFS_EXTENTS_LIST])
            {
                //
                // Run out of extents
                //
                try_return (retVal = FALSE);
            }

            newEntry = NextExtent( entry, AFS_EXTENTS_LIST );

            if (newEntry->FileOffset.QuadPart !=
                (entry->FileOffset.QuadPart + entry->Size))
            {
                //
                // Gap
                //
                try_return (retVal = FALSE);
            }

            entry = newEntry;
        }

try_exit:

        AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSDoExtentsMapRegion Releasing Fcb extent lock %08lX SHARED %08lX\n",
                      &Fcb->NPFcb->Specific.File.ExtentsResource,
                      PsGetCurrentThread());

        AFSReleaseResource( &Fcb->NPFcb->Specific.File.ExtentsResource );

        *LastExtent = entry;
    }

    return retVal;
}

//
// Given an FCB and an Offset we look to see whether there extents to
// Map them all.  If there are then we return TRUE to fullymapped
// and *FirstExtent points to the first extent to map the extent.
// If not then we return FALSE, but we request the extents to be mapped.
// Further *FirstExtent (if non null) is the last extent which doesn't
// map the extent.
//
// Finally on the way *in* if *FirstExtent is non null it is where we start looking
//

NTSTATUS
AFSRequestExtents( IN AFSFcb *Fcb,
                   IN PLARGE_INTEGER Offset,
                   IN ULONG Size,
                   OUT BOOLEAN *FullyMapped)
{

    AFSDeviceExt        *pDevExt = (AFSDeviceExt *)AFSRDRDeviceObject->DeviceExtension;
    NTSTATUS             ntStatus = STATUS_SUCCESS;
    AFSExtent           *pExtent;
    AFSRequestExtentsCB  request;
    AFSNonPagedFcb      *pNPFcb = Fcb->NPFcb;
    AFSExtent           *pFirstExtent;
    LARGE_INTEGER        liAlignedOffset;
    ULONG                ulAlignedLength = 0;
    LARGE_INTEGER        liTimeOut;

    //
    // Check our extents, then fire off a request if we need to.
    // We start off knowing nothing about where we will go.
    //
    pFirstExtent = NULL;
    pExtent = NULL;

    *FullyMapped = AFSDoExtentsMapRegion( Fcb, Offset, Size, &pFirstExtent, &pExtent );

    if (*FullyMapped)
    {

        ASSERT(AFSExtentContains(pFirstExtent, Offset));
        LARGE_INTEGER end = *Offset;
        end.QuadPart += (Size-1);
        ASSERT(AFSExtentContains(pExtent, &end));

        return STATUS_SUCCESS;
    }

    //
    // So we need to queue a request. Since we will be clearing the
    // ExtentsRequestComplete event we need to do with with the lock
    // EX
    //

    liTimeOut.QuadPart = -(50000000);

    while (TRUE)
    {
        if (!NT_SUCCESS( pNPFcb->Specific.File.ExtentsRequestStatus))
        {

            //
            // If this isn't the same process which caused the failure then try to request them again
            //

            if( Fcb->Specific.File.ExtentRequestProcessId == (ULONGLONG)PsGetCurrentProcessId())
            {
                ntStatus = pNPFcb->Specific.File.ExtentsRequestStatus;

                break;
            }

            pNPFcb->Specific.File.ExtentsRequestStatus = STATUS_SUCCESS;
        }

        ntStatus = KeWaitForSingleObject( &pNPFcb->Specific.File.ExtentsRequestComplete,
                                          Executive,
                                          KernelMode,
                                          FALSE,
                                          &liTimeOut);
        if (!NT_SUCCESS(ntStatus))
        {

            //
            // try again
            //

            continue;
        }

        //
        // Lock resource EX and look again
        //

        AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSRequestExtents Acquiring Fcb extent lock %08lX EXCL %08lX\n",
                      &pNPFcb->Specific.File.ExtentsResource,
                      PsGetCurrentThread());

        AFSAcquireExcl( &pNPFcb->Specific.File.ExtentsResource, TRUE );

        if (!NT_SUCCESS( pNPFcb->Specific.File.ExtentsRequestStatus))
        {

            //
            // If this isn't the same process which caused the failure then try to request them again
            //

            if( Fcb->Specific.File.ExtentRequestProcessId == (ULONGLONG)PsGetCurrentProcessId())
            {
                ntStatus = pNPFcb->Specific.File.ExtentsRequestStatus;

                AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSRequestExtents Releasing Fcb extent lock %08lX EXCL %08lX\n",
                              &pNPFcb->Specific.File.ExtentsResource,
                              PsGetCurrentThread());

                AFSReleaseResource( &pNPFcb->Specific.File.ExtentsResource );

                break;
            }

            pNPFcb->Specific.File.ExtentsRequestStatus = STATUS_SUCCESS;
        }

        if( KeReadStateEvent( &pNPFcb->Specific.File.ExtentsRequestComplete) ||
            ntStatus == STATUS_TIMEOUT)
        {

            ntStatus = pNPFcb->Specific.File.ExtentsRequestStatus;

            if( !NT_SUCCESS( ntStatus))
            {

                //
                // If this isn't the same process which caused the failure then try to request them again
                //

                if( Fcb->Specific.File.ExtentRequestProcessId == (ULONGLONG)PsGetCurrentProcessId())
                {
                    AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                                  AFS_TRACE_LEVEL_VERBOSE,
                                  "AFSRequestExtents Releasing Fcb extent lock %08lX EXCL %08lX\n",
                                  &pNPFcb->Specific.File.ExtentsResource,
                                  PsGetCurrentThread());

                    AFSReleaseResource( &pNPFcb->Specific.File.ExtentsResource );
                }
                else
                {

                    pNPFcb->Specific.File.ExtentsRequestStatus = STATUS_SUCCESS;

                    ntStatus = STATUS_SUCCESS;
                }
            }

            break;
        }

        AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSRequestExtents Releasing Fcb extent lock %08lX EXCL %08lX\n",
                      &pNPFcb->Specific.File.ExtentsResource,
                      PsGetCurrentThread());

        AFSReleaseResource( &pNPFcb->Specific.File.ExtentsResource );
    }

    if (!NT_SUCCESS(ntStatus))
    {

        return ntStatus;
    }

    __Enter
    {
        //
        // We have the lock Ex and there is no filling going on.
        // Check again to see whether things have moved since we last
        // checked.  Since we haven't locked against pinning, we will
        // reset here.
        //

        pFirstExtent = NULL;

        *FullyMapped = AFSDoExtentsMapRegion(Fcb, Offset, Size, &pFirstExtent, &pExtent);

        if (*FullyMapped)
        {

            ASSERT(AFSExtentContains(pFirstExtent, Offset));
            LARGE_INTEGER end = *Offset;
            end.QuadPart += (Size-1);
            ASSERT(AFSExtentContains(pExtent, &end));

            try_return (ntStatus = STATUS_SUCCESS);
        }

        RtlZeroMemory( &request,
                       sizeof( AFSRequestExtentsCB));

        //
        // Align the request
        //

        ulAlignedLength = Size;

        liAlignedOffset = *Offset;

        if( liAlignedOffset.QuadPart % pDevExt->Specific.RDR.CacheBlockSize != 0)
        {

            liAlignedOffset.QuadPart = (ULONGLONG)( (ULONGLONG)(liAlignedOffset.QuadPart / pDevExt->Specific.RDR.CacheBlockSize) * (ULONGLONG)pDevExt->Specific.RDR.CacheBlockSize);

            ulAlignedLength += (ULONG)(Offset->QuadPart - liAlignedOffset.QuadPart);
        }

        if( ulAlignedLength % pDevExt->Specific.RDR.CacheBlockSize != 0)
        {

            ulAlignedLength = (ULONG)(((ulAlignedLength / pDevExt->Specific.RDR.CacheBlockSize) + 1) * pDevExt->Specific.RDR.CacheBlockSize);
        }

        request.ByteOffset = liAlignedOffset;
        request.Length = ulAlignedLength;

        if( !AFSIsExtentRequestQueued( &Fcb->ObjectInformation->FileId,
                                       &request.ByteOffset,
                                       request.Length))
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_EXTENT_PROCESSING,
                          AFS_TRACE_LEVEL_VERBOSE,
                          "AFSRequestExtents Request extents for fid %08lX-%08lX-%08lX-%08lX Offset %08lX Len %08lX Thread %08lX\n",
                          Fcb->ObjectInformation->FileId.Cell,
                          Fcb->ObjectInformation->FileId.Volume,
                          Fcb->ObjectInformation->FileId.Vnode,
                          Fcb->ObjectInformation->FileId.Unique,
                          request.ByteOffset.LowPart,
                          request.Length,
                          PsGetCurrentThread());

            ntStatus = AFSProcessRequest( AFS_REQUEST_TYPE_REQUEST_FILE_EXTENTS,
                                          0,
                                          &Fcb->AuthGroup,
                                          NULL,
                                          &Fcb->ObjectInformation->FileId,
                                          &request,
                                          sizeof( AFSRequestExtentsCB ),
                                          NULL,
                                          NULL);

            if( NT_SUCCESS( ntStatus))
            {
                KeClearEvent( &pNPFcb->Specific.File.ExtentsRequestComplete );
            }
        }
        else
        {

            KeClearEvent( &pNPFcb->Specific.File.ExtentsRequestComplete );
        }

try_exit:

        if (NT_SUCCESS( ntStatus ))
        {
            KeQueryTickCount( &Fcb->Specific.File.LastExtentAccess );
        }

        AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSRequestExtents Releasing Fcb extent lock %08lX EXCL %08lX\n",
                      &pNPFcb->Specific.File.ExtentsResource,
                      PsGetCurrentThread());

        AFSReleaseResource( &pNPFcb->Specific.File.ExtentsResource );
    }

    return ntStatus;
}

NTSTATUS
AFSRequestExtentsAsync( IN AFSFcb *Fcb,
                        IN PLARGE_INTEGER Offset,
                        IN ULONG Size)
{

    AFSDeviceExt        *pDevExt = (AFSDeviceExt *)AFSRDRDeviceObject->DeviceExtension;
    NTSTATUS             ntStatus = STATUS_SUCCESS;
    AFSExtent           *pExtent = NULL;
    AFSRequestExtentsCB  request;
    AFSNonPagedFcb      *pNPFcb = Fcb->NPFcb;
    AFSExtent           *pFirstExtent = NULL;
    LARGE_INTEGER        liAlignedOffset;
    ULONG                ulAlignedLength = 0;
    BOOLEAN              bRegionMapped = FALSE;

    __Enter
    {

        ASSERT( !ExIsResourceAcquiredLite( &pNPFcb->Specific.File.ExtentsResource ));

        //
        // If the service set a failure on the file since the last
        // CreateFile was issued, return it now.
        //

        if (!NT_SUCCESS( pNPFcb->Specific.File.ExtentsRequestStatus))
        {

            //
            // If this isn't the same process which caused the failure then try to request them again
            //

            if( Fcb->Specific.File.ExtentRequestProcessId == (ULONGLONG)PsGetCurrentProcessId())
            {
                try_return( ntStatus = pNPFcb->Specific.File.ExtentsRequestStatus);
            }

            pNPFcb->Specific.File.ExtentsRequestStatus = STATUS_SUCCESS;
        }

        //
        // Check if we are already mapped
        //

        bRegionMapped = AFSDoExtentsMapRegion( Fcb, Offset, Size, &pFirstExtent, &pExtent);

        if( bRegionMapped)
        {

            try_return( ntStatus = STATUS_SUCCESS);
        }

        //
        // Align our request on extent size boundary
        //

        ulAlignedLength = Size;

        liAlignedOffset = *Offset;

        if( liAlignedOffset.QuadPart % pDevExt->Specific.RDR.CacheBlockSize != 0)
        {

            liAlignedOffset.QuadPart = (ULONGLONG)( (ULONGLONG)(liAlignedOffset.QuadPart / pDevExt->Specific.RDR.CacheBlockSize) * (ULONGLONG)pDevExt->Specific.RDR.CacheBlockSize);

            ulAlignedLength += (ULONG)(Offset->QuadPart - liAlignedOffset.QuadPart);
        }

        if( ulAlignedLength % pDevExt->Specific.RDR.CacheBlockSize != 0)
        {

            ulAlignedLength = (ULONG)(((ulAlignedLength / pDevExt->Specific.RDR.CacheBlockSize) + 1) * pDevExt->Specific.RDR.CacheBlockSize);
        }

        RtlZeroMemory( &request,
                       sizeof( AFSRequestExtentsCB));

        request.ByteOffset = liAlignedOffset;
        request.Length = ulAlignedLength;

        if( !AFSIsExtentRequestQueued( &Fcb->ObjectInformation->FileId,
                                       &request.ByteOffset,
                                       request.Length))
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_EXTENT_PROCESSING,
                          AFS_TRACE_LEVEL_VERBOSE,
                          "AFSRequestExtentsAsync Request extents for fid %08lX-%08lX-%08lX-%08lX Offset %08lX Len %08lX Thread %08lX\n",
                          Fcb->ObjectInformation->FileId.Cell,
                          Fcb->ObjectInformation->FileId.Volume,
                          Fcb->ObjectInformation->FileId.Vnode,
                          Fcb->ObjectInformation->FileId.Unique,
                          request.ByteOffset.LowPart,
                          request.Length,
                          PsGetCurrentThread());

            ntStatus = AFSProcessRequest( AFS_REQUEST_TYPE_REQUEST_FILE_EXTENTS,
                                          0,
                                          &Fcb->AuthGroup,
                                          NULL,
                                          &Fcb->ObjectInformation->FileId,
                                          &request,
                                          sizeof( AFSRequestExtentsCB ),
                                          NULL,
                                          NULL);

            if( NT_SUCCESS( ntStatus))
            {

                KeClearEvent( &pNPFcb->Specific.File.ExtentsRequestComplete );
            }
        }
        else
        {

            KeClearEvent( &pNPFcb->Specific.File.ExtentsRequestComplete );
        }

try_exit:

        NOTHING;
    }

    return ntStatus;
}

NTSTATUS
AFSProcessExtentsResult( IN AFSFcb *Fcb,
                         IN ULONG   Count,
                         IN AFSFileExtentCB *Result)
{
    NTSTATUS          ntStatus = STATUS_SUCCESS;
    AFSFileExtentCB  *pFileExtents = Result;
    AFSExtent        *pExtent;
    LIST_ENTRY       *le;
    AFSNonPagedFcb   *pNPFcb = Fcb->NPFcb;
    ULONG             fileExtentsUsed = 0;
    BOOLEAN           bFoundExtent = FALSE;
    LIST_ENTRY       *pSkipEntries[AFS_NUM_EXTENT_LISTS] = { 0 };
    AFSDeviceExt     *pControlDevExt = (AFSDeviceExt *)AFSControlDeviceObject->DeviceExtension;

    //
    // Grab the extents exclusive for the duration
    //

    AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                  AFS_TRACE_LEVEL_VERBOSE,
                  "AFSProcessExtentsResult Acquiring Fcb extent lock %08lX EXCL %08lX\n",
                  &pNPFcb->Specific.File.ExtentsResource,
                  PsGetCurrentThread());

    AFSAcquireExcl( &pNPFcb->Specific.File.ExtentsResource, TRUE );

    __Enter
    {

        //
        // Find where to put the extents
        //
        for (ULONG i = AFS_EXTENTS_LIST; i < AFS_NUM_EXTENT_LISTS; i++)
        {

            pSkipEntries[i] = Fcb->Specific.File.ExtentsLists[i].Flink;
        }

        le = pSkipEntries[AFS_EXTENTS_LIST];

        if (le == &Fcb->Specific.File.ExtentsLists[AFS_EXTENTS_LIST])
        {
            //
            // No extents.  Insert at head of list (which is where the skip lists point!)
            //
            pExtent = NULL;
        }
        else if (0 != pFileExtents->FileOffset.QuadPart)
        {
            //
            // We want to find the best extents immediately *behind* this offset
            //
            LARGE_INTEGER offset = pFileExtents->FileOffset;

            //
            // Ask in the top skip list first, then work down
            //
            for (LONG i = AFS_NUM_EXTENT_LISTS-1; i >= AFS_EXTENTS_LIST; i--)
            {
                pExtent = ExtentForOffsetInList( Fcb,
                                                 pSkipEntries[i],
                                                 i,
                                                 &offset);

                if (NULL == pExtent)
                {
                    //
                    // No dice.  Header has to become the head of the list
                    //
                    pSkipEntries[i] = &Fcb->Specific.File.ExtentsLists[i];
                    //
                    // And as  a loop invariant we should never have found an extent
                    //
                    ASSERT(!bFoundExtent);
                }
                else
                {
                    //
                    // pExtent is where to start to insert at this level
                    //
                    pSkipEntries[i] = &pExtent->Lists[i];

                    //
                    // And also where to start to look at the next level
                    //

                    if (i > AFS_EXTENTS_LIST)
                    {
                        pSkipEntries[i-1] = &pExtent->Lists[i-1];
                    }
                    bFoundExtent = TRUE;
                }
            }

            if (NULL == pExtent)
            {
                pExtent = ExtentFor( le, AFS_EXTENTS_LIST);
                le = le->Blink;
            }
            else
            {
                le = pExtent->Lists[AFS_EXTENTS_LIST].Blink;
            }
        }
        else
        {
            //
            // Looking at offset 0, so we must start at the beginning
            //

            pExtent = ExtentFor(le, AFS_EXTENTS_LIST);
            le = le->Blink;

            //
            // And set up the skip lists
            //

            for (ULONG i = AFS_EXTENTS_LIST; i < AFS_NUM_EXTENT_LISTS; i++)
            {
                pSkipEntries[i] = &Fcb->Specific.File.ExtentsLists[i];
            }
        }

        while (fileExtentsUsed < Count)
        {

            //
            // Loop invariant - le points to where to insert after and
            // pExtent points to le->fLink
            //

            ASSERT (NULL == pExtent ||
                    le->Flink == &pExtent->Lists[AFS_EXTENTS_LIST]);

            if (NULL == pExtent ||
                pExtent->FileOffset.QuadPart > pFileExtents->FileOffset.QuadPart)
            {
                //
                // We need to insert a new extent at le.  Start with
                // some sanity check on spanning
                //
                if (NULL != pExtent &&
                    ((pFileExtents->FileOffset.QuadPart + pFileExtents->Length) >
                     pExtent->FileOffset.QuadPart))
                {
                    //
                    // File Extents overlaps pExtent
                    //
                    ASSERT( (pFileExtents->FileOffset.QuadPart + pFileExtents->Length) <=
                            pExtent->FileOffset.QuadPart);

                    try_return (ntStatus = STATUS_INVALID_PARAMETER);
                }

                //
                // File offset is entirely in front of this extent.  Create
                // a new one (remember le is the previous list entry)
                //
                pExtent = (AFSExtent *) AFSExAllocatePoolWithTag( NonPagedPool,
                                                                  sizeof( AFSExtent),
                                                                  AFS_EXTENT_TAG );
                if (NULL  == pExtent)
                {

                    ASSERT( FALSE);

                    try_return (ntStatus = STATUS_INSUFFICIENT_RESOURCES );
                }

                RtlZeroMemory( pExtent, sizeof( AFSExtent ));

                pExtent->FileOffset = pFileExtents->FileOffset;
                pExtent->CacheOffset = pFileExtents->CacheOffset;
                pExtent->Size = pFileExtents->Length;

                AFSDbgLogMsg( AFS_SUBSYSTEM_EXTENT_PROCESSING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSProcessExtentsResult Received extent for fid %08lX-%08lX-%08lX-%08lX File Offset %I64X Cache Offset %I64X Len %08lX\n",
                              Fcb->ObjectInformation->FileId.Cell,
                              Fcb->ObjectInformation->FileId.Volume,
                              Fcb->ObjectInformation->FileId.Vnode,
                              Fcb->ObjectInformation->FileId.Unique,
                              pFileExtents->FileOffset.QuadPart,
                              pFileExtents->CacheOffset.QuadPart,
                              pFileExtents->Length);

                InterlockedExchangeAdd( &pControlDevExt->Specific.Control.ExtentsHeldLength, (LONG)(pExtent->Size/1024));

                InterlockedExchangeAdd( &Fcb->Specific.File.ExtentLength, (LONG)(pExtent->Size/1024));

                InterlockedIncrement( &Fcb->Specific.File.ExtentCount);

                if( InterlockedIncrement( &pControlDevExt->Specific.Control.ExtentCount) == 1)
                {

                    KeClearEvent( &pControlDevExt->Specific.Control.ExtentsHeldEvent);
                }

                //
                // Insert into list
                //
                InsertHeadList(le, &pExtent->Lists[AFS_EXTENTS_LIST]);
                ASSERT(le->Flink == &pExtent->Lists[AFS_EXTENTS_LIST]);
                ASSERT(0 == (pExtent->FileOffset.LowPart & ExtentsMasks[AFS_EXTENTS_LIST]));

                //
                // Do not move the cursor - we will do it next time
                //

                //
                // And into the (upper) skip lists - Again, do not move the cursor
                //
                for (ULONG i = AFS_NUM_EXTENT_LISTS-1; i > AFS_EXTENTS_LIST; i--)
                {
                    if (0 == (pExtent->FileOffset.LowPart & ExtentsMasks[i]))
                    {
                        InsertHeadList(pSkipEntries[i], &pExtent->Lists[i]);
#if AFS_VALIDATE_EXTENTS
                        VerifyExtentsLists(Fcb);
#endif
                    }
                }
            }
            else if (pExtent->FileOffset.QuadPart == pFileExtents->FileOffset.QuadPart)
            {

                if (pExtent->Size != pFileExtents->Length)
                {

                    ASSERT (pExtent->Size == pFileExtents->Length);

                    try_return (ntStatus = STATUS_INVALID_PARAMETER);
                }

                //
                // Move both cursors forward.
                //
                // First the extent pointer
                //
                fileExtentsUsed++;
                le = &pExtent->Lists[AFS_EXTENTS_LIST];

                //
                // Then the skip lists cursors forward if needed
                //
                for (ULONG i = AFS_NUM_EXTENT_LISTS-1; i > AFS_EXTENTS_LIST; i--)
                {
                    if (0 == (pExtent->FileOffset.LowPart & ExtentsMasks[i]))
                    {
                        //
                        // Check sanity before
                        //
#if AFS_VALIDATE_EXTENTS
                        VerifyExtentsLists(Fcb);
#endif

                        //
                        // Skip list should point to us
                        //
                        //ASSERT(pSkipEntries[i] == &pExtent->Lists[i]);
                        //
                        // Move forward cursor
                        //
                        pSkipEntries[i] = pSkipEntries[i]->Flink;
                        //
                        // Check sanity before
                        //
#if AFS_VALIDATE_EXTENTS
                        VerifyExtentsLists(Fcb);
#endif
                    }
                }

                //
                // And then the cursor in the supplied array
                //

                pFileExtents++;

                //
                // setup pExtent if there is one
                //
                if (le->Flink != &Fcb->Specific.File.ExtentsLists[AFS_EXTENTS_LIST])
                {
                    pExtent = NextExtent( pExtent, AFS_EXTENTS_LIST ) ;
                }
                else
                {
                    pExtent = NULL;
                }
            }
            else
            {

                ASSERT( pExtent->FileOffset.QuadPart < pFileExtents->FileOffset.QuadPart );

                //
                // Sanity check on spanning
                //
                if ((pExtent->FileOffset.QuadPart + pExtent->Size) >
                    pFileExtents->FileOffset.QuadPart)
                {

                    ASSERT( (pExtent->FileOffset.QuadPart + pExtent->Size) <=
                            pFileExtents->FileOffset.QuadPart);

                    try_return (ntStatus = STATUS_INVALID_PARAMETER);
                }

                //
                // Move le and pExtent forward
                //
                le = &pExtent->Lists[AFS_EXTENTS_LIST];

                /*
                //
                // Then the check the skip lists cursors
                //
                for (ULONG i = AFS_NUM_EXTENT_LISTS-1; i > AFS_EXTENTS_LIST; i--)
                {
                    if (0 == (pFileExtents->FileOffset.LowPart & ExtentsMasks[i]))
                    {
                        //
                        // Three options:
                        //    - empty list (pSkipEntries[i]->Flink == pSkipEntries[i]->Flink == fcb->lists[i]
                        //    - We are the last on the list (pSkipEntries[i]->Flink == fcb->lists[i])
                        //    - We are not the last on the list.  In that case we have to be strictly less than
                        //      that extent.
                        if (pSkipEntries[i]->Flink != &Fcb->Specific.File.ExtentsLists[i]) {

                            AFSExtent *otherExtent = ExtentFor(pSkipEntries[i]->Flink, i);
                            ASSERT(pFileExtents->FileOffset.QuadPart < otherExtent->FileOffset.QuadPart);
                        }
                    }
                }
                */

                //
                // setup pExtent if there is one
                //

                if (le->Flink != &Fcb->Specific.File.ExtentsLists[AFS_EXTENTS_LIST])
                {
                    pExtent = NextExtent( pExtent, AFS_EXTENTS_LIST ) ;
                }
                else
                {
                    pExtent = NULL;
                }
            }
        }

        //
        // All done, signal that we are done drop the lock, exit
        //

try_exit:

        if( !NT_SUCCESS( ntStatus))
        {

            //
            // If we failed the service is going to drop all extents so trim away the
            // set given to us
            //

            AFSTrimSpecifiedExtents( Fcb,
                                     Count,
                                     Result);
        }

#if AFS_VALIDATE_EXTENTS
        VerifyExtentsLists(Fcb);
#endif

        KeSetEvent( &pNPFcb->Specific.File.ExtentsRequestComplete,
                    0,
                    FALSE);

        AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSProcessExtentsResult Releasing Fcb extent lock %08lX EXCL %08lX\n",
                      &pNPFcb->Specific.File.ExtentsResource,
                      PsGetCurrentThread());

        AFSReleaseResource( &pNPFcb->Specific.File.ExtentsResource );
    }

    return ntStatus;
}

NTSTATUS
AFSProcessSetFileExtents( IN AFSSetFileExtentsCB *SetExtents )
{
    AFSFcb       *pFcb = NULL;
    AFSVolumeCB  *pVolumeCB = NULL;
    NTSTATUS      ntStatus = STATUS_SUCCESS;
    AFSDeviceExt *pDevExt = (AFSDeviceExt *)AFSRDRDeviceObject->DeviceExtension;
    ULONGLONG     ullIndex = 0;
    AFSObjectInfoCB *pObjectInfo = NULL;

    __Enter
    {

        AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSProcessSetFileExtents Acquiring RDR VolumeTreeLock lock %08lX SHARED %08lX\n",
                      &pDevExt->Specific.RDR.VolumeTreeLock,
                      PsGetCurrentThread());

        AFSAcquireShared( &pDevExt->Specific.RDR.VolumeTreeLock, TRUE);

        AFSDbgLogMsg( AFS_SUBSYSTEM_EXTENT_PROCESSING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSProcessSetFileExtents Set extents for fid %08lX-%08lX-%08lX-%08lX\n",
                      SetExtents->FileId.Cell,
                      SetExtents->FileId.Volume,
                      SetExtents->FileId.Vnode,
                      SetExtents->FileId.Unique);

        //
        // Locate the volume node
        //

        ullIndex = AFSCreateHighIndex( &SetExtents->FileId);

        ntStatus = AFSLocateHashEntry( pDevExt->Specific.RDR.VolumeTree.TreeHead,
                                       ullIndex,
                                       (AFSBTreeEntry **)&pVolumeCB);

        if( pVolumeCB != NULL)
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                          AFS_TRACE_LEVEL_VERBOSE,
                          "AFSProcessSetFileExtents Acquiring VolumeRoot FileIDTree.TreeLock lock %08lX SHARED %08lX\n",
                          pVolumeCB->ObjectInfoTree.TreeLock,
                          PsGetCurrentThread());

            InterlockedIncrement( &pVolumeCB->VolumeReferenceCount);
        }

        AFSReleaseResource( &pDevExt->Specific.RDR.VolumeTreeLock);

        if( !NT_SUCCESS( ntStatus) ||
            pVolumeCB == NULL)
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_EXTENT_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSProcessSetFileExtents Set extents for fid %08lX-%08lX-%08lX-%08lX Failed to locate volume Status %08lX\n",
                          SetExtents->FileId.Cell,
                          SetExtents->FileId.Volume,
                          SetExtents->FileId.Vnode,
                          SetExtents->FileId.Unique,
                          ntStatus);

            try_return( ntStatus = STATUS_UNSUCCESSFUL);
        }

        AFSAcquireShared( pVolumeCB->ObjectInfoTree.TreeLock,
                          TRUE);

        InterlockedDecrement( &pVolumeCB->VolumeReferenceCount);

        //
        // Now locate the Object in this volume
        //

        ullIndex = AFSCreateLowIndex( &SetExtents->FileId);

        ntStatus = AFSLocateHashEntry( pVolumeCB->ObjectInfoTree.TreeHead,
                                       ullIndex,
                                       (AFSBTreeEntry **)&pObjectInfo);

        if( pObjectInfo != NULL)
        {

            //
            // Reference the node so it won't be torn down
            //

            InterlockedIncrement( &pObjectInfo->ObjectReferenceCount);

            AFSDbgLogMsg( AFS_SUBSYSTEM_OBJECT_REF_COUNTING,
                          AFS_TRACE_LEVEL_VERBOSE,
                          "AFSProcessSetFileExtents Increment count on object %08lX Cnt %d\n",
                          pObjectInfo,
                          pObjectInfo->ObjectReferenceCount);
        }

        AFSReleaseResource( pVolumeCB->ObjectInfoTree.TreeLock);

        if( !NT_SUCCESS( ntStatus) ||
            pObjectInfo == NULL)
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_EXTENT_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSProcessSetFileExtents Set extents for hash %I64X fid %08lX-%08lX-%08lX-%08lX Failed to locate file in volume %08lX\n",
                          ullIndex,
                          SetExtents->FileId.Cell,
                          SetExtents->FileId.Volume,
                          SetExtents->FileId.Vnode,
                          SetExtents->FileId.Unique,
                          pVolumeCB);

            try_return( ntStatus = STATUS_UNSUCCESSFUL);
        }

        pFcb = pObjectInfo->Fcb;

        //
        // If we have a result failure then don't bother trying to set the extents
        //

        if( SetExtents->ResultStatus != STATUS_SUCCESS)
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_EXTENT_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSProcessSetFileExtents Set extents failure fid %08lX-%08lX-%08lX-%08lX ResultStatus %08lX\n",
                          SetExtents->FileId.Cell,
                          SetExtents->FileId.Volume,
                          SetExtents->FileId.Vnode,
                          SetExtents->FileId.Unique,
                          SetExtents->ResultStatus);

            AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                          AFS_TRACE_LEVEL_VERBOSE,
                          "AFSProcessSetFileExtents Acquiring Fcb extents lock %08lX EXCL %08lX\n",
                          &pFcb->NPFcb->Specific.File.ExtentsResource,
                          PsGetCurrentThread());

            AFSAcquireExcl( &pFcb->NPFcb->Specific.File.ExtentsResource,
                            TRUE);

            pFcb->NPFcb->Specific.File.ExtentsRequestStatus = STATUS_CANCELLED;

            KeSetEvent( &pFcb->NPFcb->Specific.File.ExtentsRequestComplete,
                        0,
                        FALSE);

            AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                          AFS_TRACE_LEVEL_VERBOSE,
                          "AFSProcessSetFileExtents Releasing Fcb extent lock %08lX EXCL %08lX\n",
                          &pFcb->NPFcb->Specific.File.ExtentsResource,
                          PsGetCurrentThread());

            AFSReleaseResource( &pFcb->NPFcb->Specific.File.ExtentsResource);

            try_return( ntStatus);
        }

        ntStatus = AFSProcessExtentsResult ( pFcb,
                                             SetExtents->ExtentCount,
                                             SetExtents->FileExtents );

try_exit:

        if( pObjectInfo != NULL)
        {

            InterlockedDecrement( &pObjectInfo->ObjectReferenceCount);

            AFSDbgLogMsg( AFS_SUBSYSTEM_OBJECT_REF_COUNTING,
                          AFS_TRACE_LEVEL_VERBOSE,
                          "AFSProcessSetFileExtents Decrement count on object %08lX Cnt %d\n",
                          pObjectInfo,
                          pObjectInfo->ObjectReferenceCount);
        }
    }

    return ntStatus;
}

//
// Helper fuctions for Usermode initiation of release of extents
//
NTSTATUS
AFSReleaseSpecifiedExtents( IN  AFSReleaseFileExtentsCB *Extents,
                            IN  AFSFcb *Fcb,
                            OUT AFSFileExtentCB *FileExtents,
                            IN  ULONG BufferSize,
                            OUT ULONG *ExtentCount,
                            OUT BOOLEAN *DirtyExtents)
{
    AFSExtent           *pExtent;
    LIST_ENTRY          *le;
    LIST_ENTRY          *leNext;
    ULONG                ulExtentCount = 0;
    NTSTATUS             ntStatus = STATUS_SUCCESS;
    BOOLEAN              bReleaseAll = FALSE;
    AFSDeviceExt        *pControlDevExt = (AFSDeviceExt *)AFSControlDeviceObject->DeviceExtension;

    __Enter
    {
        ASSERT( ExIsResourceAcquiredExclusiveLite( &Fcb->NPFcb->Specific.File.ExtentsResource));

        if (BufferSize < (Extents->ExtentCount * sizeof( AFSFileExtentCB)))
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_EXTENT_PROCESSING,
                          AFS_TRACE_LEVEL_VERBOSE,
                          "AFSReleaseSpecifiedExtents Buffer too small\n");

            try_return( ntStatus = STATUS_BUFFER_TOO_SMALL);
        }

        RtlZeroMemory( FileExtents, BufferSize);
        *ExtentCount = 0;

        *DirtyExtents = FALSE;

        //
        // iterate until we have dealt with all we were asked for or
        // are at the end of the list.  Note that this deals (albeit
        // badly) with out of order extents
        //

        pExtent = AFSExtentForOffset( Fcb,
                                      &Extents->FileExtents[0].FileOffset,
                                      FALSE);

        if (NULL == pExtent)
        {
            le = Fcb->Specific.File.ExtentsLists[AFS_EXTENTS_LIST].Flink;
        }
        else
        {
            le = &pExtent->Lists[AFS_EXTENTS_LIST];
        }
        ulExtentCount = 0;

        if( BooleanFlagOn( Extents->Flags, AFS_RELEASE_EXTENTS_FLAGS_RELEASE_ALL) ||
            ( Extents->FileId.Cell   == 0 &&
              Extents->FileId.Volume == 0 &&
              Extents->FileId.Vnode  == 0 &&
              Extents->FileId.Unique == 0))
        {

            bReleaseAll = TRUE;
        }

        while( le != &Fcb->Specific.File.ExtentsLists[AFS_EXTENTS_LIST] &&
               ulExtentCount < Extents->ExtentCount)

        {

            pExtent = ExtentFor( le, AFS_EXTENTS_LIST);

            if( !bReleaseAll)
            {

                if( pExtent->FileOffset.QuadPart < Extents->FileExtents[ulExtentCount].FileOffset.QuadPart)
                {
                    //
                    // Skip forward through the extent list until we get
                    // to the one we want
                    //
                    le = le->Flink;

                    continue;
                }
                else if (pExtent->FileOffset.QuadPart > Extents->FileExtents[ulExtentCount].FileOffset.QuadPart)
                {
                    //
                    // We don't have the extent asked for so return UNKNOWN
                    //

                    AFSDbgLogMsg( AFS_SUBSYSTEM_EXTENT_PROCESSING,
                                  AFS_TRACE_LEVEL_VERBOSE,
                                  "AFSReleaseSpecifiedExtents Located UNKNOWN extent Offset %I64X Len %08lX\n",
                                  Extents->FileExtents[ulExtentCount].FileOffset.QuadPart,
                                  Extents->FileExtents[ulExtentCount].Length);

                    FileExtents[*ExtentCount].Flags = AFS_EXTENT_FLAG_UNKNOWN;

                    FileExtents[*ExtentCount].Length = 0;
                    FileExtents[*ExtentCount].CacheOffset.QuadPart = 0;
                    FileExtents[*ExtentCount].FileOffset = Extents->FileExtents[ulExtentCount].FileOffset;

                    *ExtentCount = (*ExtentCount) + 1;

                    ulExtentCount++;

                    //
                    // Reset where we are looking
                    //

                    le = Fcb->Specific.File.ExtentsLists[AFS_EXTENTS_LIST].Flink;

                    continue;
                }
                else if( pExtent->ActiveCount > 0)
                {

                    AFSDbgLogMsg( AFS_SUBSYSTEM_EXTENT_PROCESSING,
                                  AFS_TRACE_LEVEL_VERBOSE,
                                  "AFSReleaseSpecifiedExtents Located IN_USE extent Offset %I64X Len %08lX\n",
                                  Extents->FileExtents[ulExtentCount].FileOffset.QuadPart,
                                  Extents->FileExtents[ulExtentCount].Length);

                    FileExtents[*ExtentCount].Flags = AFS_EXTENT_FLAG_IN_USE;

                    FileExtents[*ExtentCount].Length = 0;
                    FileExtents[*ExtentCount].CacheOffset.QuadPart = 0;
                    FileExtents[*ExtentCount].FileOffset = Extents->FileExtents[ulExtentCount].FileOffset;

                    *ExtentCount = (*ExtentCount) + 1;

                    ulExtentCount++;

                    //
                    // Reset where we are looking
                    //

                    le = Fcb->Specific.File.ExtentsLists[AFS_EXTENTS_LIST].Flink;

                    continue;
                }
            }
            else
            {

                //
                // If the extent is currently active then skip it
                //

                if( pExtent->ActiveCount > 0)
                {

                    le = le->Flink;

                    continue;
                }
            }

            FileExtents[*ExtentCount].Flags = AFS_EXTENT_FLAG_RELEASE;

            FileExtents[*ExtentCount].Length = pExtent->Size;
            FileExtents[*ExtentCount].DirtyLength = pExtent->Size;
            FileExtents[*ExtentCount].DirtyOffset = 0;
            FileExtents[*ExtentCount].CacheOffset = pExtent->CacheOffset;
            FileExtents[*ExtentCount].FileOffset = pExtent->FileOffset;

            AFSDbgLogMsg( AFS_SUBSYSTEM_EXTENT_PROCESSING,
                          AFS_TRACE_LEVEL_VERBOSE,
                          "AFSReleaseSpecifiedExtents Releasing extent %p fid %08lX-%08lX-%08lX-%08lX Offset %I64X Len %08lX\n",
                          pExtent,
                          Fcb->ObjectInformation->FileId.Cell,
                          Fcb->ObjectInformation->FileId.Volume,
                          Fcb->ObjectInformation->FileId.Vnode,
                          Fcb->ObjectInformation->FileId.Unique,
                          FileExtents[*ExtentCount].FileOffset.QuadPart,
                          FileExtents[*ExtentCount].Length);

            if( BooleanFlagOn( pExtent->Flags, AFS_EXTENT_DIRTY))
            {

                AFSAcquireExcl( &Fcb->NPFcb->Specific.File.DirtyExtentsListLock,
                                TRUE);

                if( BooleanFlagOn( pExtent->Flags, AFS_EXTENT_DIRTY))
                {

                    AFSRemoveEntryDirtyList( Fcb,
                                             pExtent);

                    FileExtents[*ExtentCount].Flags |= AFS_EXTENT_FLAG_DIRTY;

                    InterlockedDecrement( &Fcb->Specific.File.ExtentsDirtyCount);

                    *DirtyExtents = TRUE;
                }

                AFSReleaseResource( &Fcb->NPFcb->Specific.File.DirtyExtentsListLock);
            }

            //
            // move forward all three cursors
            //
            le = le->Flink;
            ulExtentCount ++;
            *ExtentCount = (*ExtentCount) + 1;

            //
            // And unpick
            //
            for (ULONG i = 0; i < AFS_NUM_EXTENT_LISTS; i ++)
            {
                if (NULL != pExtent->Lists[i].Flink && !IsListEmpty(&pExtent->Lists[i]))
                {
                    RemoveEntryList( &pExtent->Lists[i] );
                }
            }

            InterlockedExchangeAdd( &pControlDevExt->Specific.Control.ExtentsHeldLength, -((LONG)(pExtent->Size/1024)));

            InterlockedExchangeAdd( &Fcb->Specific.File.ExtentLength, -((LONG)(pExtent->Size/1024)));

            //
            // and free
            //
            AFSExFreePool( pExtent);

            InterlockedDecrement( &Fcb->Specific.File.ExtentCount);

            if( InterlockedDecrement( &pControlDevExt->Specific.Control.ExtentCount) == 0)
            {

                KeSetEvent( &pControlDevExt->Specific.Control.ExtentsHeldEvent,
                            0,
                            FALSE);
            }
        }

try_exit:

        NOTHING;
    }

    return ntStatus;
}

AFSFcb*
AFSFindFcbToClean(ULONG IgnoreTime, AFSFcb *LastFcb, BOOLEAN Block)
{

    AFSFcb *pFcb = NULL;
    AFSVolumeCB *pVolumeCB = NULL;
    AFSDeviceExt *pRDRDeviceExt = NULL;
    AFSDeviceExt *pControlDeviceExt = NULL;
    BOOLEAN bLocatedEntry = FALSE;
    AFSObjectInfoCB *pCurrentObject = NULL;
    BOOLEAN bReleaseVolumeListLock = FALSE;

    pRDRDeviceExt = (AFSDeviceExt *)AFSRDRDeviceObject->DeviceExtension;
    pControlDeviceExt = (AFSDeviceExt *)AFSControlDeviceObject->DeviceExtension;

    AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                  AFS_TRACE_LEVEL_VERBOSE,
                  "AFSFindFcbToClean Acquiring RDR VolumeListLock lock %08lX SHARED %08lX\n",
                  &pRDRDeviceExt->Specific.RDR.VolumeListLock,
                  PsGetCurrentThread());

    AFSAcquireShared( &pRDRDeviceExt->Specific.RDR.VolumeListLock,
                      TRUE);

    bReleaseVolumeListLock = TRUE;

    pVolumeCB = pRDRDeviceExt->Specific.RDR.VolumeListHead;

    while( pVolumeCB != NULL)
    {

        //
        // The Volume list may move under our feet.  Lock it.
        //

        AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSFindFcbToClean Acquiring VolumeRoot ObjectInfoTree lock %08lX SHARED %08lX\n",
                      pVolumeCB->ObjectInfoTree.TreeLock,
                      PsGetCurrentThread());

        InterlockedIncrement( &pVolumeCB->VolumeReferenceCount);

        AFSReleaseResource( &pRDRDeviceExt->Specific.RDR.VolumeListLock);

        bReleaseVolumeListLock = FALSE;

        AFSAcquireShared( pVolumeCB->ObjectInfoTree.TreeLock,
                          TRUE);

        InterlockedDecrement( &pVolumeCB->VolumeReferenceCount);

        if( NULL == LastFcb)
        {

            pCurrentObject = pVolumeCB->ObjectInfoListHead;
        }
        else
        {

            pCurrentObject = (AFSObjectInfoCB *)LastFcb->ObjectInformation->ListEntry.fLink;
        }

        pFcb = NULL;

        while( pCurrentObject != NULL)
        {

            pFcb = (AFSFcb *)pCurrentObject->Fcb;

            //
            // If the FCB is a candidate we try to lock it (but without waiting - which
            // means we are deadlock free
            //

            if( pFcb != NULL &&
                pFcb->Header.NodeTypeCode == AFS_FILE_FCB)
            {

                if( Block)
                {

                    AFSLockForExtentsTrim( pFcb);
                }
                else
                {

                    if( !AFSLockForExtentsTrimNoWait( pFcb))
                    {

                        pCurrentObject = (AFSObjectInfoCB *)pCurrentObject->ListEntry.fLink;

                        pFcb = NULL;

                        continue;
                    }
                }

                //
                // Need to be sure there are no current flushes in the queue
                //

                if( pFcb->Specific.File.ExtentCount == 0)
                {

                    AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                                  AFS_TRACE_LEVEL_VERBOSE,
                                  "AFSFindFcbToClean Releasing Fcb extent lock %08lX thread %08lX\n",
                                  &pFcb->NPFcb->Specific.File.ExtentsResource,
                                  PsGetCurrentThread());

                    AFSReleaseResource( &pFcb->NPFcb->Specific.File.ExtentsResource);

                    pCurrentObject = (AFSObjectInfoCB *)pCurrentObject->ListEntry.fLink;

                    pFcb = NULL;

                    continue;
                }

                if( pFcb->Specific.File.QueuedFlushCount > 0)
                {

                    AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                                  AFS_TRACE_LEVEL_VERBOSE,
                                  "AFSFindFcbToClean Releasing Fcb extent lock %08lX thread %08lX\n",
                                  &pFcb->NPFcb->Specific.File.ExtentsResource,
                                  PsGetCurrentThread());

                    AFSReleaseResource(&pFcb->NPFcb->Specific.File.ExtentsResource);

                    if( Block)
                    {
                        AFSWaitOnQueuedFlushes( pFcb);
                    }
                    else
                    {

                        pCurrentObject = (AFSObjectInfoCB *)pCurrentObject->ListEntry.fLink;
                    }

                    pFcb = NULL;

                    continue;
                }

                if( pFcb->OpenHandleCount > 0)
                {

                    AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                                  AFS_TRACE_LEVEL_VERBOSE,
                                  "AFSFindFcbToClean Releasing Fcb extent lock %08lX thread %08lX\n",
                                  &pFcb->NPFcb->Specific.File.ExtentsResource,
                                  PsGetCurrentThread());

                    AFSReleaseResource(&pFcb->NPFcb->Specific.File.ExtentsResource);

                    pCurrentObject = (AFSObjectInfoCB *)pCurrentObject->ListEntry.fLink;

                    pFcb = NULL;

                    continue;
                }

                //
                // A hit a very palpable hit.  Pin it
                //

                InterlockedIncrement( &pCurrentObject->ObjectReferenceCount);

                AFSDbgLogMsg( AFS_SUBSYSTEM_OBJECT_REF_COUNTING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSFindFcbToClean Increment count on Fcb %08lX Cnt %d\n",
                              pCurrentObject,
                              pCurrentObject->ObjectReferenceCount);

                bLocatedEntry = TRUE;

                break;
            }

            pCurrentObject = (AFSObjectInfoCB *)pCurrentObject->ListEntry.fLink;

            pFcb = NULL;
        }

        AFSReleaseResource( pVolumeCB->ObjectInfoTree.TreeLock);

        if( bLocatedEntry)
        {
            break;
        }

        AFSAcquireShared( &pRDRDeviceExt->Specific.RDR.VolumeListLock,
                          TRUE);

        bReleaseVolumeListLock = TRUE;

        pVolumeCB = (AFSVolumeCB *)pVolumeCB->ListEntry.fLink;
    }

    if( bReleaseVolumeListLock)
    {

        AFSReleaseResource( &pRDRDeviceExt->Specific.RDR.VolumeListLock);
    }

    return pFcb;
}

NTSTATUS
AFSProcessExtentFailure( PIRP Irp)
{
    AFSExtentFailureCB                *pFailureCB = NULL;
    NTSTATUS                           ntStatus = STATUS_SUCCESS;
    AFSDeviceExt                      *pDevExt = (AFSDeviceExt *) AFSRDRDeviceObject->DeviceExtension;
    PIO_STACK_LOCATION                 pIrpSp = IoGetCurrentIrpStackLocation( Irp);
    AFSVolumeCB                       *pVolumeCB = NULL;
    ULONGLONG                          ullIndex = 0;
    AFSObjectInfoCB                   *pObjectInfo = NULL;

    __Enter
    {
        if( pIrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof( AFSExtentFailureCB))
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_IO_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSProcessExtentFailure Input buffer too small\n");

            try_return( ntStatus = STATUS_INVALID_PARAMETER);
        }

        pFailureCB = (AFSExtentFailureCB *)Irp->AssociatedIrp.SystemBuffer;

        AFSDbgLogMsg( AFS_SUBSYSTEM_IO_PROCESSING,
                      AFS_TRACE_LEVEL_ERROR,
                      "AFSProcessExtentFailure Service Reports Failure fid %08lX-%08lX-%08lX-%08lX Status %08lX\n",
                      pFailureCB->FileId.Cell,
                      pFailureCB->FileId.Volume,
                      pFailureCB->FileId.Vnode,
                      pFailureCB->FileId.Unique,
                      pFailureCB->FailureStatus);

        AFSAcquireShared( &pDevExt->Specific.RDR.VolumeTreeLock, TRUE);

        //
        // Locate the volume node
        //

        ullIndex = AFSCreateHighIndex( &pFailureCB->FileId);

        ntStatus = AFSLocateHashEntry( pDevExt->Specific.RDR.VolumeTree.TreeHead,
                                       ullIndex,
                                       (AFSBTreeEntry **)&pVolumeCB);

        if( pVolumeCB != NULL)
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                          AFS_TRACE_LEVEL_VERBOSE,
                          "AFSProcessExtentFailure Acquiring VolumeRoot FileIDTree.TreeLock lock %08lX SHARED %08lX\n",
                          pVolumeCB->ObjectInfoTree.TreeLock,
                          PsGetCurrentThread());

            InterlockedIncrement( &pVolumeCB->VolumeReferenceCount);
        }

        AFSReleaseResource( &pDevExt->Specific.RDR.VolumeTreeLock);

        if( !NT_SUCCESS( ntStatus) ||
            pVolumeCB == NULL)
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_IO_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSProcessExtentFailure Invalid volume index %I64X status %08X\n",
                          ullIndex, ntStatus);

            try_return( ntStatus = STATUS_UNSUCCESSFUL);
        }

        AFSAcquireShared( pVolumeCB->ObjectInfoTree.TreeLock,
                          TRUE);

        InterlockedDecrement( &pVolumeCB->VolumeReferenceCount);

        //
        // Now locate the Object in this volume
        //

        ullIndex = AFSCreateLowIndex( &pFailureCB->FileId);

        ntStatus = AFSLocateHashEntry( pVolumeCB->ObjectInfoTree.TreeHead,
                                       ullIndex,
                                       (AFSBTreeEntry **)&pObjectInfo);

        if( pObjectInfo != NULL &&
            pObjectInfo->Fcb != NULL)
        {

            //
            // Reference the node so it won't be torn down
            //

            InterlockedIncrement( &pObjectInfo->ObjectReferenceCount);

            AFSDbgLogMsg( AFS_SUBSYSTEM_OBJECT_REF_COUNTING,
                          AFS_TRACE_LEVEL_VERBOSE,
                          "AFSProcessExtentFailure Increment count on object %08lX Cnt %d\n",
                          pObjectInfo,
                          pObjectInfo->ObjectReferenceCount);
        }

        AFSReleaseResource( pVolumeCB->ObjectInfoTree.TreeLock);

        if( !NT_SUCCESS( ntStatus) ||
            pObjectInfo == NULL ||
            pObjectInfo->Fcb == NULL)
        {

            if( pObjectInfo == NULL)
            {
                AFSDbgLogMsg( AFS_SUBSYSTEM_IO_PROCESSING,
                              AFS_TRACE_LEVEL_ERROR,
                              "AFSProcessExtentFailure Invalid file index %I64X\n",
                              ullIndex);
            }
            else
            {
                AFSDbgLogMsg( AFS_SUBSYSTEM_IO_PROCESSING,
                              AFS_TRACE_LEVEL_ERROR,
                              "AFSProcessExtentFailure Fcb dealocated for %I64X\n",
                              ullIndex);
            }

            try_return( ntStatus = STATUS_UNSUCCESSFUL);
        }

        AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSProcessExtentFailure Acquiring Fcb extent lock %08lX EXCL %08lX\n",
                      &pObjectInfo->Fcb->NPFcb->Specific.File.ExtentsResource,
                      PsGetCurrentThread());

        AFSAcquireExcl( &pObjectInfo->Fcb->NPFcb->Specific.File.ExtentsResource,
                        TRUE);

        pObjectInfo->Fcb->NPFcb->Specific.File.ExtentsRequestStatus = pFailureCB->FailureStatus;

        KeSetEvent( &pObjectInfo->Fcb->NPFcb->Specific.File.ExtentsRequestComplete,
                    0,
                    FALSE);

        AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSProcessExtentFailure Releasing Fcb extent lock %08lX EXCL %08lX\n",
                      &pObjectInfo->Fcb->NPFcb->Specific.File.ExtentsResource,
                      PsGetCurrentThread());

        AFSReleaseResource( &pObjectInfo->Fcb->NPFcb->Specific.File.ExtentsResource);

        InterlockedDecrement( &pObjectInfo->ObjectReferenceCount);

        AFSDbgLogMsg( AFS_SUBSYSTEM_OBJECT_REF_COUNTING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSProcessExtentFailure Decrement count on object %08lX Cnt %d\n",
                      pObjectInfo,
                      pObjectInfo->ObjectReferenceCount);

try_exit:

        NOTHING;
    }

    return ntStatus;
}

NTSTATUS
AFSProcessReleaseFileExtents( IN PIRP Irp)
{
    NTSTATUS                           ntStatus = STATUS_SUCCESS;
    PIO_STACK_LOCATION                 pIrpSp = IoGetCurrentIrpStackLocation( Irp);
    PFILE_OBJECT                       pFileObject = pIrpSp->FileObject;
    AFSFcb                            *pFcb = NULL;
    AFSVolumeCB                       *pVolumeCB = NULL;
    AFSDeviceExt                      *pDevExt;
    AFSReleaseFileExtentsCB           *pExtents;
    AFSReleaseFileExtentsResultCB     *pResult = NULL;
    AFSReleaseFileExtentsResultFileCB *pFile = NULL;
    ULONG                              ulSz = 0;
    ULONGLONG                          ullIndex = 0;
    AFSObjectInfoCB                   *pObjectInfo = NULL;
    BOOLEAN                            bLocked = FALSE;
    BOOLEAN                            bDirtyExtents = FALSE;

    __Enter
    {

        pDevExt = (AFSDeviceExt *) AFSRDRDeviceObject->DeviceExtension;

        pExtents = (AFSReleaseFileExtentsCB*) Irp->AssociatedIrp.SystemBuffer;

        if( pIrpSp->Parameters.DeviceIoControl.InputBufferLength <
                                            sizeof( AFSReleaseFileExtentsCB))
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_EXTENT_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSProcessReleaseFileExtents INPUT Buffer too small\n");

            try_return( ntStatus = STATUS_INVALID_PARAMETER );
        }

        if ( pIrpSp->Parameters.DeviceIoControl.OutputBufferLength <
                                        sizeof(AFSReleaseFileExtentsResultCB))
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_EXTENT_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSProcessReleaseFileExtents OUTPUT Buffer too small [1]\n");

            //
            // Must have space for one extent in one file
            //

            try_return( ntStatus = STATUS_BUFFER_TOO_SMALL);
        }

        if (pExtents->ExtentCount == 0)
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_EXTENT_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSProcessReleaseFileExtents Extent count zero\n");

            try_return( ntStatus = STATUS_INVALID_PARAMETER);
        }

        if (pExtents->FileId.Cell   != 0 ||
            pExtents->FileId.Volume != 0 ||
            pExtents->FileId.Vnode  != 0 ||
            pExtents->FileId.Unique != 0)
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_EXTENT_PROCESSING,
                          AFS_TRACE_LEVEL_VERBOSE,
                          "AFSProcessReleaseFileExtents Processing FID %08lX:%08lX:%08lX:%08lX\n",
                          pExtents->FileId.Cell,
                          pExtents->FileId.Volume,
                          pExtents->FileId.Vnode,
                          pExtents->FileId.Unique);

            if( pIrpSp->Parameters.DeviceIoControl.InputBufferLength <
                            ( FIELD_OFFSET( AFSReleaseFileExtentsCB, ExtentCount) + sizeof(ULONG)) ||
                pIrpSp->Parameters.DeviceIoControl.InputBufferLength <
                            ( FIELD_OFFSET( AFSReleaseFileExtentsCB, ExtentCount) + sizeof(ULONG) +
                                                            sizeof (AFSFileExtentCB) * pExtents->ExtentCount))
            {

                AFSDbgLogMsg( AFS_SUBSYSTEM_EXTENT_PROCESSING,
                              AFS_TRACE_LEVEL_ERROR,
                              "AFSProcessReleaseFileExtents Buffer too small for FID %08lX:%08lx:%08lX:%08lX\n",
                              pExtents->FileId.Cell,
                              pExtents->FileId.Volume,
                              pExtents->FileId.Vnode,
                              pExtents->FileId.Unique);

                try_return( ntStatus = STATUS_INVALID_PARAMETER );
            }

            AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                          AFS_TRACE_LEVEL_VERBOSE,
                          "AFSProcessReleaseFileExtents Acquiring RDR VolumeTreeLock lock %08lX SHARED %08lX\n",
                          &pDevExt->Specific.RDR.VolumeTreeLock,
                          PsGetCurrentThread());

            AFSAcquireShared( &pDevExt->Specific.RDR.VolumeTreeLock, TRUE);

            //
            // Locate the volume node
            //

            ullIndex = AFSCreateHighIndex( &pExtents->FileId);

            ntStatus = AFSLocateHashEntry( pDevExt->Specific.RDR.VolumeTree.TreeHead,
                                           ullIndex,
                                           (AFSBTreeEntry **)&pVolumeCB);

            if( pVolumeCB != NULL)
            {

                AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSProcessReleaseFileExtents Acquiring VolumeRoot FileIDTree.TreeLock lock %08lX SHARED %08lX\n",
                              pVolumeCB->ObjectInfoTree.TreeLock,
                              PsGetCurrentThread());

                InterlockedIncrement( &pVolumeCB->VolumeReferenceCount);
            }

            AFSReleaseResource( &pDevExt->Specific.RDR.VolumeTreeLock);

            if( !NT_SUCCESS( ntStatus) ||
                pVolumeCB == NULL)
            {

                AFSDbgLogMsg( AFS_SUBSYSTEM_EXTENT_PROCESSING,
                              AFS_TRACE_LEVEL_ERROR,
                              "AFSProcessReleaseFileExtents Invalid volume index %I64X status %08X\n",
                              ullIndex, ntStatus);

                try_return( ntStatus = STATUS_UNSUCCESSFUL);
            }

            AFSAcquireShared( pVolumeCB->ObjectInfoTree.TreeLock,
                              TRUE);

            InterlockedDecrement( &pVolumeCB->VolumeReferenceCount);

            //
            // Now locate the Object in this volume
            //

            ullIndex = AFSCreateLowIndex( &pExtents->FileId);

            ntStatus = AFSLocateHashEntry( pVolumeCB->ObjectInfoTree.TreeHead,
                                           ullIndex,
                                           (AFSBTreeEntry **)&pObjectInfo);

            if( pObjectInfo != NULL)
            {

                //
                // Reference the node so it won't be torn down
                //

                InterlockedIncrement( &pObjectInfo->ObjectReferenceCount);

                AFSDbgLogMsg( AFS_SUBSYSTEM_OBJECT_REF_COUNTING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSProcessReleaseFileExtents Increment count on object %08lX Cnt %d\n",
                              pObjectInfo,
                              pObjectInfo->ObjectReferenceCount);
            }

            AFSReleaseResource( pVolumeCB->ObjectInfoTree.TreeLock);

            if( !NT_SUCCESS( ntStatus) ||
                pObjectInfo == NULL)
            {

                AFSDbgLogMsg( AFS_SUBSYSTEM_EXTENT_PROCESSING,
                              AFS_TRACE_LEVEL_ERROR,
                              "AFSProcessReleaseFileExtents Invalid file index %I64X\n",
                              ullIndex);

                try_return( ntStatus = STATUS_UNSUCCESSFUL);
            }

            pFcb = pObjectInfo->Fcb;

            if( pFcb == NULL)
            {

                AFSDbgLogMsg( AFS_SUBSYSTEM_EXTENT_PROCESSING,
                              AFS_TRACE_LEVEL_ERROR,
                              "AFSProcessReleaseFileExtents Fcb not initialied (NO EXTENTS) for FID %08lX:%08lx:%08lX:%08lX\n",
                              pExtents->FileId.Cell,
                              pExtents->FileId.Volume,
                              pExtents->FileId.Vnode,
                              pExtents->FileId.Unique);

                try_return( ntStatus = STATUS_UNSUCCESSFUL);
            }

            AFSLockForExtentsTrim( pFcb );

            bLocked = TRUE;
        }
        else
        {

            //
            // Locate an Fcb to trim down
            //

            AFSDbgLogMsg( AFS_SUBSYSTEM_EXTENT_PROCESSING,
                          AFS_TRACE_LEVEL_VERBOSE,
                          "AFSProcessReleaseFileExtents Searching for a Fcb to Trim Down\n");

            pFcb = AFSFindFcbToClean( 0, NULL, FALSE);

            if( pFcb == NULL)
            {

                pFcb = AFSFindFcbToClean( 0, NULL, TRUE);
            }

            if( pFcb == NULL)
            {

                AFSDbgLogMsg( AFS_SUBSYSTEM_EXTENT_PROCESSING,
                              AFS_TRACE_LEVEL_ERROR,
                              "AFSProcessReleaseFileExtents Failed to locate Fcb for release ...\n");

                try_return( ntStatus = STATUS_UNSUCCESSFUL);
            }

            pObjectInfo = pFcb->ObjectInformation;

            bLocked = TRUE;
        }

        //
        // Allocate a scratch buffer to move in the extent information
        //

        ulSz = (pExtents->ExtentCount-1) * sizeof(AFSFileExtentCB);
        ulSz += sizeof(AFSReleaseFileExtentsResultCB);

        if (ulSz > pIrpSp->Parameters.DeviceIoControl.OutputBufferLength)
        {
            AFSDbgLogMsg( AFS_SUBSYSTEM_EXTENT_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSProcessReleaseFileExtents OUTPUT Buffer too small [2]\n");

            try_return( ntStatus = STATUS_BUFFER_TOO_SMALL );
        }

        pResult = (AFSReleaseFileExtentsResultCB*) AFSExAllocatePoolWithTag( PagedPool,
                                                                             ulSz,
                                                                             AFS_EXTENTS_RESULT_TAG);
        if (NULL == pResult)
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_IO_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSProcessReleaseFileExtents Failed to allocate result block\n");

            try_return( ntStatus = STATUS_INSUFFICIENT_RESOURCES );
        }

        //
        // Set up the header (for an array of one)
        //
        pResult->FileCount = 1;
        pResult->Flags = AFS_EXTENT_FLAG_RELEASE;
        ulSz -= FIELD_OFFSET(AFSReleaseFileExtentsResultCB, Files);

        //
        // Setup the first (and only) file
        //
        pFile = pResult->Files;
        pFile->FileId = pObjectInfo->FileId;
        pFile->Flags = AFS_EXTENT_FLAG_RELEASE;

        //
        // Stash away the auth group
        //

        RtlCopyMemory( &pFile->AuthGroup,
                       &pFcb->AuthGroup,
                       sizeof( GUID));

        //
        // Update the metadata for this call
        //

        pFile->AllocationSize = pFcb->ObjectInformation->EndOfFile;
        pFile->CreateTime = pFcb->ObjectInformation->CreationTime;
        pFile->ChangeTime = pFcb->ObjectInformation->ChangeTime;
        pFile->LastAccessTime = pFcb->ObjectInformation->LastAccessTime;
        pFile->LastWriteTime = pFcb->ObjectInformation->LastWriteTime;

        ulSz -= FIELD_OFFSET(AFSReleaseFileExtentsResultFileCB, FileExtents);

        ntStatus = AFSReleaseSpecifiedExtents( pExtents,
                                               pFcb,
                                               pFile->FileExtents,
                                               ulSz,
                                               &pFile->ExtentCount,
                                               &bDirtyExtents);

        if (!NT_SUCCESS(ntStatus))
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_EXTENT_PROCESSING,
                          AFS_TRACE_LEVEL_ERROR,
                          "AFSProcessReleaseFileExtents Failed to release extents Status %08lX\n",
                          ntStatus);

            try_return( ntStatus );
        }

        if( pExtents->ExtentCount == 0)
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_EXTENT_PROCESSING,
                          AFS_TRACE_LEVEL_WARNING,
                          "AFSProcessReleaseFileExtents Failed to release ANY extents\n");
        }

        ulSz = sizeof(AFSReleaseFileExtentsResultCB);

        if( pExtents->ExtentCount > 0)
        {
            ulSz += ((pExtents->ExtentCount-1) * sizeof(AFSFileExtentCB));
        }

        RtlCopyMemory( Irp->AssociatedIrp.SystemBuffer,
                       pResult,
                       ulSz);

try_exit:

        if( bLocked)
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                          AFS_TRACE_LEVEL_VERBOSE,
                          "AFSProcessReleaseFileExtents Releasing Fcb extent lock %08lX thread %08lX\n",
                          &pFcb->NPFcb->Specific.File.ExtentsResource,
                          PsGetCurrentThread());

            AFSReleaseResource( &pFcb->NPFcb->Specific.File.ExtentsResource );
        }

        if( NULL != pResult &&
            Irp->AssociatedIrp.SystemBuffer != pResult)
        {

            AFSExFreePool(pResult);
        }

        if (NT_SUCCESS(ntStatus))
        {
            Irp->IoStatus.Information = ulSz;
        }
        else
        {
            Irp->IoStatus.Information = 0;
        }

        Irp->IoStatus.Status = ntStatus;

        if( pObjectInfo != NULL)
        {

            InterlockedDecrement( &pObjectInfo->ObjectReferenceCount);

            AFSDbgLogMsg( AFS_SUBSYSTEM_OBJECT_REF_COUNTING,
                          AFS_TRACE_LEVEL_VERBOSE,
                          "AFSProcessReleaseFileExtents Decrement count on object %08lX Cnt %d\n",
                          pObjectInfo,
                          pObjectInfo->ObjectReferenceCount);
        }
    }

    return ntStatus;
}

NTSTATUS
AFSWaitForExtentMapping( AFSFcb *Fcb )
{
    NTSTATUS ntStatus = STATUS_SUCCESS;
    LARGE_INTEGER liTimeOut;

    __Enter
    {

        ASSERT( !ExIsResourceAcquiredLite( &Fcb->NPFcb->Specific.File.ExtentsResource ));

        if (!NT_SUCCESS( Fcb->NPFcb->Specific.File.ExtentsRequestStatus))
        {

            //
            // If this isn't the same process which caused the failure then try to request them again
            //

            if( Fcb->Specific.File.ExtentRequestProcessId == (ULONGLONG)PsGetCurrentProcessId())
            {
                try_return( ntStatus = Fcb->NPFcb->Specific.File.ExtentsRequestStatus);
            }

            Fcb->NPFcb->Specific.File.ExtentsRequestStatus = STATUS_SUCCESS;
        }

        liTimeOut.QuadPart = -(50000000);

        ntStatus = KeWaitForSingleObject( &Fcb->NPFcb->Specific.File.ExtentsRequestComplete,
                                          Executive,
                                          KernelMode,
                                          FALSE,
                                          &liTimeOut);

        if (!NT_SUCCESS( Fcb->NPFcb->Specific.File.ExtentsRequestStatus))
        {

            //
            // If this isn't the same process which caused the failure then try to request them again
            //

            if( Fcb->Specific.File.ExtentRequestProcessId == (ULONGLONG)PsGetCurrentProcessId())
            {
                try_return( ntStatus = Fcb->NPFcb->Specific.File.ExtentsRequestStatus);
            }

            Fcb->NPFcb->Specific.File.ExtentsRequestStatus = STATUS_SUCCESS;
        }

        if( ntStatus == STATUS_TIMEOUT)
        {

            ntStatus = STATUS_SUCCESS;
        }

try_exit:

        NOTHING;
    }

    return ntStatus;
}

NTSTATUS
AFSFlushExtents( IN AFSFcb *Fcb)
{
    AFSNonPagedFcb      *pNPFcb = Fcb->NPFcb;
    AFSExtent           *pExtent, *pNextExtent;
    LIST_ENTRY          *le;
    AFSReleaseExtentsCB *pRelease = NULL;
    ULONG                count = 0;
    ULONG                initialDirtyCount = 0;
    BOOLEAN              bExtentsLocked = FALSE;
    ULONG                total = 0;
    ULONG                sz = 0;
    NTSTATUS             ntStatus = STATUS_SUCCESS;
    LARGE_INTEGER        liLastFlush;
    AFSExtent           *pDirtyListHead = NULL, *pDirtyListTail = NULL;
    AFSDeviceExt        *pControlDevExt = (AFSDeviceExt *)AFSControlDeviceObject->DeviceExtension;

    ASSERT( Fcb->Header.NodeTypeCode == AFS_FILE_FCB);

    //
    // Save, then reset the flush time
    //

    liLastFlush = Fcb->Specific.File.LastServerFlush;

    KeQueryTickCount( &Fcb->Specific.File.LastServerFlush);

    __Enter
    {

        //
        // Lock extents while we count and set up the array to send to
        // the service
        //

        AFSLockForExtentsTrim( Fcb);

        bExtentsLocked = TRUE;

        InterlockedIncrement( &Fcb->Specific.File.QueuedFlushCount);

        //
        // Clear our queued flush event
        //

        KeClearEvent( &Fcb->NPFcb->Specific.File.QueuedFlushEvent);

        //
        // Look for a start in the list to flush entries
        //

        total = count;

        sz = sizeof( AFSReleaseExtentsCB ) + (AFS_MAXIMUM_EXTENT_RELEASE_COUNT * sizeof ( AFSFileExtentCB ));

        pRelease = (AFSReleaseExtentsCB*) AFSExAllocatePoolWithTag( NonPagedPool,
                                                                    sz,
                                                                    AFS_EXTENT_RELEASE_TAG);
        if( NULL == pRelease)
        {

            try_return ( ntStatus = STATUS_INSUFFICIENT_RESOURCES );
        }

        initialDirtyCount = Fcb->Specific.File.ExtentsDirtyCount;

        while( Fcb->Specific.File.ExtentsDirtyCount > 0)
        {

            pRelease->Flags = AFS_EXTENT_FLAG_DIRTY;

            if( BooleanFlagOn( Fcb->Flags, AFS_FCB_FILE_CLOSED))
            {

                pRelease->Flags |= AFS_EXTENT_FLAG_FLUSH;
            }

            //
            // Update the metadata for this call
            //

            pRelease->AllocationSize = Fcb->ObjectInformation->EndOfFile;
            pRelease->CreateTime = Fcb->ObjectInformation->CreationTime;
            pRelease->ChangeTime = Fcb->ObjectInformation->ChangeTime;
            pRelease->LastAccessTime = Fcb->ObjectInformation->LastAccessTime;
            pRelease->LastWriteTime = Fcb->ObjectInformation->LastWriteTime;

            count = 0;

            AFSAcquireExcl( &pNPFcb->Specific.File.DirtyExtentsListLock,
                            TRUE);

            pExtent = (AFSExtent *)pNPFcb->Specific.File.DirtyListHead;

            while( count < AFS_MAXIMUM_EXTENT_RELEASE_COUNT)
            {

                if ( pExtent == NULL)
                {

                    break;
                }

                pNextExtent = (AFSExtent *)pExtent->DirtyList.fLink;

                if ( pExtent->ActiveCount > 0)
                {
                    pExtent = pNextExtent;
                    continue;
                }

                AFSRemoveEntryDirtyList( Fcb, pExtent);

                pExtent->DirtyList.fLink = NULL;
                pExtent->DirtyList.bLink = NULL;

                InterlockedDecrement( &Fcb->Specific.File.ExtentsDirtyCount);

                //
                // Clear the flag in advance of the write. If we do
                // things this was we know that the clear is
                // pessimistic (any write which happens from now on
                // will set the flag dirty again).
                //

                pExtent->Flags &= ~AFS_EXTENT_DIRTY;

                pRelease->FileExtents[count].Flags = AFS_EXTENT_FLAG_DIRTY;

                pRelease->FileExtents[count].Length = pExtent->Size;
                pRelease->FileExtents[count].DirtyLength = pExtent->Size;
                pRelease->FileExtents[count].DirtyOffset = 0;
                pRelease->FileExtents[count].CacheOffset = pExtent->CacheOffset;
                pRelease->FileExtents[count].FileOffset = pExtent->FileOffset;

#if GEN_MD5
                RtlCopyMemory( pRelease->FileExtents[count].MD5,
                               pExtent->MD5,
                               sizeof(pExtent->MD5));

                pRelease->FileExtents[count].Flags |= AFS_EXTENT_FLAG_MD5_SET;
#endif

                AFSDbgLogMsg( AFS_SUBSYSTEM_EXTENT_PROCESSING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSFlushExtents Releasing DIRTY extent %p fid %08lX-%08lX-%08lX-%08lX Offset %I64X Len %08lX\n",
                              pExtent,
                              Fcb->ObjectInformation->FileId.Cell,
                              Fcb->ObjectInformation->FileId.Volume,
                              Fcb->ObjectInformation->FileId.Vnode,
                              Fcb->ObjectInformation->FileId.Unique,
                              pExtent->FileOffset.QuadPart,
                              pExtent->Size);

                pRelease->FileExtents[count].Flags |= AFS_EXTENT_FLAG_RELEASE;

                //
                // Need to pull this extent from the main list as well
                //

                for (ULONG i = 0; i < AFS_NUM_EXTENT_LISTS; i ++)
                {
                    if (NULL != pExtent->Lists[i].Flink && !IsListEmpty(&pExtent->Lists[i]))
                    {
                        RemoveEntryList( &pExtent->Lists[i] );
                    }
                }

                InterlockedExchangeAdd( &pControlDevExt->Specific.Control.ExtentsHeldLength, -((LONG)(pExtent->Size/1024)));

                InterlockedExchangeAdd( &Fcb->Specific.File.ExtentLength, -((LONG)(pExtent->Size/1024)));

                AFSExFreePool( pExtent);

                InterlockedDecrement( &Fcb->Specific.File.ExtentCount);

                if( InterlockedDecrement( &pControlDevExt->Specific.Control.ExtentCount) == 0)
                {

                    KeSetEvent( &pControlDevExt->Specific.Control.ExtentsHeldEvent,
                                0,
                                FALSE);
                }

                count ++;

                pExtent = pNextExtent;
            }

            AFSReleaseResource( &pNPFcb->Specific.File.DirtyExtentsListLock);

            //
            // If we are done then get out
            //

            if( count == 0)
            {

                AFSDbgLogMsg( AFS_SUBSYSTEM_EXTENT_PROCESSING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSFlushExtents No more dirty extents found\n");

                break;
            }

            //
            // Fire off the request synchronously
            //

            sz = sizeof( AFSReleaseExtentsCB ) + (count * sizeof ( AFSFileExtentCB ));

            pRelease->ExtentCount = count;

            AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                          AFS_TRACE_LEVEL_VERBOSE,
                          "AFSFlushExtents Releasing(1) Fcb extents lock %08lX SHARED %08lX\n",
                          &pNPFcb->Specific.File.ExtentsResource,
                          PsGetCurrentThread());

            AFSReleaseResource( &pNPFcb->Specific.File.ExtentsResource);
            bExtentsLocked = FALSE;

            KeSetEvent( &pNPFcb->Specific.File.FlushEvent,
                        0,
                        FALSE);

            ntStatus = AFSProcessRequest( AFS_REQUEST_TYPE_RELEASE_FILE_EXTENTS,
                                          AFS_REQUEST_FLAG_SYNCHRONOUS,
                                          &Fcb->AuthGroup,
                                          NULL,
                                          &Fcb->ObjectInformation->FileId,
                                          pRelease,
                                          sz,
                                          NULL,
                                          NULL);

            if( !NT_SUCCESS(ntStatus))
            {

                //
                // Regardless of whether or not the AFSProcessRequest() succeeded, the extents
                // were released (if AFS_EXTENT_FLAG_RELEASE was set).  Log the error so it is known.
                //

                AFSDbgLogMsg( AFS_SUBSYSTEM_EXTENT_PROCESSING,
                              AFS_TRACE_LEVEL_ERROR,
                              "AFSFlushExtents AFS_REQUEST_TYPE_RELEASE_FILE_EXTENTS failed fid %08lX-%08lX-%08lX-%08lX Status %08lX\n",
                              Fcb->ObjectInformation->FileId.Cell,
                              Fcb->ObjectInformation->FileId.Volume,
                              Fcb->ObjectInformation->FileId.Vnode,
                              Fcb->ObjectInformation->FileId.Unique,
                              ntStatus);

            }
            AFSLockForExtentsTrim( Fcb);

            bExtentsLocked = TRUE;
        }

try_exit:

        if( InterlockedDecrement( &Fcb->Specific.File.QueuedFlushCount) == 0)
        {

            KeSetEvent( &pNPFcb->Specific.File.QueuedFlushEvent,
                        0,
                        FALSE);
        }

        KeSetEvent( &pNPFcb->Specific.File.FlushEvent,
                    0,
                    FALSE);

        if (bExtentsLocked)
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                          AFS_TRACE_LEVEL_VERBOSE,
                          "AFSFlushExtents Releasing(2) Fcb extents lock %08lX SHARED %08lX\n",
                          &pNPFcb->Specific.File.ExtentsResource,
                          PsGetCurrentThread());

            AFSReleaseResource( &pNPFcb->Specific.File.ExtentsResource );
        }

        if (pRelease)
        {
            AFSExFreePool( pRelease);
        }
    }

    return ntStatus;
}

NTSTATUS
AFSReleaseExtentsWithFlush( IN AFSFcb *Fcb)
{
    AFSNonPagedFcb      *pNPFcb = Fcb->NPFcb;
    AFSExtent           *pExtent;
    LIST_ENTRY          *le;
    AFSReleaseExtentsCB *pRelease = NULL;
    ULONG                count = 0;
    ULONG                initialDirtyCount = 0;
    BOOLEAN              bExtentsLocked = FALSE;
    ULONG                total = 0;
    ULONG                sz = 0;
    NTSTATUS             ntStatus = STATUS_SUCCESS;
    LARGE_INTEGER        liLastFlush;
    ULONG                ulRemainingExtentLength = 0;
    AFSDeviceExt        *pControlDevExt = (AFSDeviceExt *)AFSControlDeviceObject->DeviceExtension;

    ASSERT( Fcb->Header.NodeTypeCode == AFS_FILE_FCB);

    //
    // Save, then reset the flush time
    //

    liLastFlush = Fcb->Specific.File.LastServerFlush;

    KeQueryTickCount( &Fcb->Specific.File.LastServerFlush);

    __Enter
    {

        //
        // Look for a start in the list to flush entries
        //

        total = count;

        sz = sizeof( AFSReleaseExtentsCB ) + (AFS_MAXIMUM_EXTENT_RELEASE_COUNT * sizeof ( AFSFileExtentCB ));

        pRelease = (AFSReleaseExtentsCB*) AFSExAllocatePoolWithTag( NonPagedPool,
                                                                    sz,
                                                                    AFS_EXTENT_RELEASE_TAG);
        if( NULL == pRelease)
        {

            try_return ( ntStatus = STATUS_INSUFFICIENT_RESOURCES );
        }

        if( Fcb->OpenHandleCount > 0)
        {

            //
            // Don't release everything ...
            //

            //
            // For now release everything
            //

            //ulRemainingExtentLength = 1500;
        }

        while( Fcb->Specific.File.ExtentLength > (LONG)ulRemainingExtentLength)
        {

            AFSLockForExtentsTrim( Fcb);

            bExtentsLocked = TRUE;

            pRelease->Flags = AFS_EXTENT_FLAG_RELEASE;

            //
            // Update the metadata for this call
            //

            pRelease->AllocationSize = Fcb->ObjectInformation->EndOfFile;
            pRelease->CreateTime = Fcb->ObjectInformation->CreationTime;
            pRelease->ChangeTime = Fcb->ObjectInformation->ChangeTime;
            pRelease->LastAccessTime = Fcb->ObjectInformation->LastAccessTime;
            pRelease->LastWriteTime = Fcb->ObjectInformation->LastWriteTime;

            count = 0;

            le = Fcb->Specific.File.ExtentsLists[AFS_EXTENTS_LIST].Flink;

            while( count < AFS_MAXIMUM_EXTENT_RELEASE_COUNT &&
                   le != &Fcb->Specific.File.ExtentsLists[AFS_EXTENTS_LIST])
            {

                pExtent = ExtentFor( le, AFS_EXTENTS_LIST);

                le = le->Flink;

                if( pExtent->ActiveCount > 0)
                {

                    continue;
                }

                pRelease->FileExtents[count].Flags = AFS_EXTENT_FLAG_RELEASE;

                AFSDbgLogMsg( AFS_SUBSYSTEM_EXTENT_PROCESSING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSReleaseExtentsWithFlush Releasing extent %p fid %08lX-%08lX-%08lX-%08lX Offset %I64X Len %08lX\n",
                              pExtent,
                              Fcb->ObjectInformation->FileId.Cell,
                              Fcb->ObjectInformation->FileId.Volume,
                              Fcb->ObjectInformation->FileId.Vnode,
                              Fcb->ObjectInformation->FileId.Unique,
                              pExtent->FileOffset.QuadPart,
                              pExtent->Size);

                pRelease->FileExtents[count].Length = pExtent->Size;
                pRelease->FileExtents[count].DirtyLength = pExtent->Size;
                pRelease->FileExtents[count].DirtyOffset = 0;
                pRelease->FileExtents[count].CacheOffset = pExtent->CacheOffset;
                pRelease->FileExtents[count].FileOffset = pExtent->FileOffset;

#if GEN_MD5
                RtlCopyMemory( pRelease->FileExtents[count].MD5,
                               pExtent->MD5,
                               sizeof(pExtent->MD5));

                pRelease->FileExtents[count].Flags |= AFS_EXTENT_FLAG_MD5_SET;
#endif

                if( BooleanFlagOn( pExtent->Flags, AFS_EXTENT_DIRTY))
                {

                    AFSAcquireExcl( &pNPFcb->Specific.File.DirtyExtentsListLock,
                                    TRUE);

                    if( BooleanFlagOn( pExtent->Flags, AFS_EXTENT_DIRTY))
                    {

                        AFSRemoveEntryDirtyList( Fcb,
                                                 pExtent);

                        pRelease->FileExtents[count].Flags |= AFS_EXTENT_FLAG_DIRTY;

                        InterlockedDecrement( &Fcb->Specific.File.ExtentsDirtyCount);
                    }

                    AFSReleaseResource( &pNPFcb->Specific.File.DirtyExtentsListLock);
                }

                //
                // Need to pull this extent from the main list as well
                //

                for (ULONG i = 0; i < AFS_NUM_EXTENT_LISTS; i ++)
                {
                    if (NULL != pExtent->Lists[i].Flink && !IsListEmpty(&pExtent->Lists[i]))
                    {
                        RemoveEntryList( &pExtent->Lists[i] );
                    }
                }

                InterlockedExchangeAdd( &pControlDevExt->Specific.Control.ExtentsHeldLength, -((LONG)(pExtent->Size/1024)));

                InterlockedExchangeAdd( &Fcb->Specific.File.ExtentLength, -((LONG)(pExtent->Size/1024)));

                AFSExFreePool( pExtent);

                InterlockedDecrement( &Fcb->Specific.File.ExtentCount);

                if( InterlockedDecrement( &pControlDevExt->Specific.Control.ExtentCount) == 0)
                {

                    KeSetEvent( &pControlDevExt->Specific.Control.ExtentsHeldEvent,
                                0,
                                FALSE);
                }

                count ++;
            }

            //
            // If we are done then get out
            //

            if( count == 0)
            {

                AFSDbgLogMsg( AFS_SUBSYSTEM_EXTENT_PROCESSING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSReleaseExtentsWithFlush No more dirty extents found\n");

                break;
            }

            //
            // Fire off the request synchronously
            //

            sz = sizeof( AFSReleaseExtentsCB ) + (count * sizeof ( AFSFileExtentCB ));

            pRelease->ExtentCount = count;

            //
            // Drop the extents lock for the duration of the call to
            // the network.  We have pinned the extents so, even
            // though we might get extents added during this period,
            // but none will be removed.  Hence we can carry on from
            // le.
            //

            AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                          AFS_TRACE_LEVEL_VERBOSE,
                          "AFSReleaseExtentsWithFlush Releasing Fcb extents lock %08lX thread %08lX\n",
                          &pNPFcb->Specific.File.ExtentsResource,
                          PsGetCurrentThread());

            AFSReleaseResource( &pNPFcb->Specific.File.ExtentsResource);
            bExtentsLocked = FALSE;

            ntStatus = AFSProcessRequest( AFS_REQUEST_TYPE_RELEASE_FILE_EXTENTS,
                                          AFS_REQUEST_FLAG_SYNCHRONOUS,
                                          &Fcb->AuthGroup,
                                          NULL,
                                          &Fcb->ObjectInformation->FileId,
                                          pRelease,
                                          sz,
                                          NULL,
                                          NULL);

            if( !NT_SUCCESS(ntStatus))
            {

                //
                // Regardless of whether or not the AFSProcessRequest() succeeded, the extents
                // were released (if AFS_EXTENT_FLAG_RELEASE was set).  Log the error so it is known.
                //

                AFSDbgLogMsg( AFS_SUBSYSTEM_EXTENT_PROCESSING,
                              AFS_TRACE_LEVEL_ERROR,
                              "AFSReleaseExtentsWithFlush AFS_REQUEST_TYPE_RELEASE_FILE_EXTENTS failed fid %08lX-%08lX-%08lX-%08lX Status %08lX\n",
                              Fcb->ObjectInformation->FileId.Cell,
                              Fcb->ObjectInformation->FileId.Volume,
                              Fcb->ObjectInformation->FileId.Vnode,
                              Fcb->ObjectInformation->FileId.Unique,
                              ntStatus);
            }
        }

try_exit:

        if (bExtentsLocked)
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                          AFS_TRACE_LEVEL_VERBOSE,
                          "AFSReleaseExtentsWithFlush Releasing Fcb extents lock %08lX thread %08lX\n",
                          &pNPFcb->Specific.File.ExtentsResource,
                          PsGetCurrentThread());

            AFSReleaseResource( &pNPFcb->Specific.File.ExtentsResource );
        }

        if (pRelease)
        {
            AFSExFreePool( pRelease);
        }
    }

    return ntStatus;
}

VOID
AFSMarkDirty( IN AFSFcb *Fcb,
              IN AFSExtent *StartExtent,
              IN ULONG ExtentsCount,
              IN LARGE_INTEGER *StartingByte)
{

    AFSNonPagedFcb *pNPFcb = Fcb->NPFcb;
    AFSExtent     *pExtent = StartExtent;
    AFSExtent     *pNextExtent, *pCurrentExtent = NULL;
    ULONG ulCount = 0;
    BOOLEAN bInsertTail = FALSE, bInsertHead = FALSE;

    AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                  AFS_TRACE_LEVEL_VERBOSE,
                  "AFSMarkDirty Acquiring Fcb extents lock %08lX SHARED %08lX\n",
                  &Fcb->NPFcb->Specific.File.ExtentsResource,
                  PsGetCurrentThread());

    AFSAcquireShared( &Fcb->NPFcb->Specific.File.ExtentsResource, TRUE);

    AFSAcquireExcl( &pNPFcb->Specific.File.DirtyExtentsListLock,
                    TRUE);

    //
    // Find the insertion point
    //

    if( pNPFcb->Specific.File.DirtyListHead == NULL)
    {

        bInsertTail = TRUE;
    }
    else if( StartingByte->QuadPart == 0)
    {

        bInsertHead = TRUE;
    }
    else
    {

        pCurrentExtent = pNPFcb->Specific.File.DirtyListHead;

        while( pCurrentExtent != NULL)
        {

            if( pCurrentExtent->FileOffset.QuadPart + pCurrentExtent->Size >= StartingByte->QuadPart ||
                pCurrentExtent->DirtyList.fLink == NULL)
            {

                break;
            }

            pCurrentExtent = (AFSExtent *)pCurrentExtent->DirtyList.fLink;
        }
    }

    while( ulCount < ExtentsCount)
    {

        pNextExtent = NextExtent( pExtent, AFS_EXTENTS_LIST);

        if( !BooleanFlagOn( pExtent->Flags, AFS_EXTENT_DIRTY))
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_EXTENT_PROCESSING,
                          AFS_TRACE_LEVEL_VERBOSE,
                          "AFSMarkDirty Marking extent offset %I64X Length %08lX DIRTY\n",
                          pExtent->FileOffset.QuadPart,
                          pExtent->Size);

            pExtent->DirtyList.fLink = NULL;
            pExtent->DirtyList.bLink = NULL;

            if( bInsertHead)
            {

                pExtent->DirtyList.fLink = (void *)pNPFcb->Specific.File.DirtyListHead;

                pExtent->DirtyList.bLink = NULL;

                pNPFcb->Specific.File.DirtyListHead->DirtyList.bLink = (void *)pExtent;

                pNPFcb->Specific.File.DirtyListHead = pExtent;

                pCurrentExtent = pExtent;

                bInsertHead = FALSE;
            }
            else if( bInsertTail)
            {

                if( pNPFcb->Specific.File.DirtyListHead == NULL)
                {

                    pNPFcb->Specific.File.DirtyListHead = pExtent;
                }
                else
                {

                    pNPFcb->Specific.File.DirtyListTail->DirtyList.fLink = (void *)pExtent;

                    pExtent->DirtyList.bLink = (void *)pNPFcb->Specific.File.DirtyListTail;
                }

                pNPFcb->Specific.File.DirtyListTail = pExtent;
            }
            else
            {

                pExtent->DirtyList.fLink = pCurrentExtent->DirtyList.fLink;
                pExtent->DirtyList.bLink = (void *)pCurrentExtent;

                if( pExtent->DirtyList.fLink == NULL)
                {

                    pNPFcb->Specific.File.DirtyListTail = pExtent;
                }
                else
                {

                    ((AFSExtent *)pExtent->DirtyList.fLink)->DirtyList.bLink = (void *)pExtent;
                }

                pCurrentExtent->DirtyList.fLink = (void *)pExtent;

                pCurrentExtent = pExtent;
            }

            pExtent->Flags |= AFS_EXTENT_DIRTY;

            //
            // Up the dirty count
            //

            InterlockedIncrement( &Fcb->Specific.File.ExtentsDirtyCount);
        }
        else
        {

            pCurrentExtent = pExtent;
        }

        AFSDbgLogMsg( AFS_SUBSYSTEM_EXTENT_ACTIVE_COUNTING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSMarkDirty Decrement count on extent %08lX Cnt %d\n",
                      pExtent,
                      pExtent->ActiveCount);

        ASSERT( pExtent->ActiveCount > 0);

        InterlockedDecrement( &pExtent->ActiveCount);

        pExtent = pNextExtent;

        ulCount++;
    }

    AFSReleaseResource( &pNPFcb->Specific.File.DirtyExtentsListLock);

    AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                  AFS_TRACE_LEVEL_VERBOSE,
                  "AFSMarkDirty Releasing Fcb extents lock %08lX SHARED %08lX\n",
                  &Fcb->NPFcb->Specific.File.ExtentsResource,
                  PsGetCurrentThread());

    AFSReleaseResource( &Fcb->NPFcb->Specific.File.ExtentsResource );

    return;
}

//
// Helper functions
//

static AFSExtent *ExtentFor(PLIST_ENTRY le, ULONG SkipList)
{
    return CONTAINING_RECORD( le, AFSExtent, Lists[SkipList] );
}

static AFSExtent *NextExtent(AFSExtent *Extent, ULONG SkipList)
{
    return ExtentFor(Extent->Lists[SkipList].Flink, SkipList);
}

static AFSExtent *DirtyExtentFor(PLIST_ENTRY le)
{
    return CONTAINING_RECORD( le, AFSExtent, DirtyList );
}

static VOID VerifyExtentsLists(AFSFcb *Fcb)
{
#if DBG > 0
    //
    // Check the ordering of the extents lists
    //
    ASSERT( ExIsResourceAcquiredLite( &Fcb->NPFcb->Specific.File.ExtentsResource ));

    ASSERT(Fcb->Specific.File.ExtentsLists[0].Flink != &Fcb->Specific.File.ExtentsLists[1]);

    for (ULONG listNo = 0; listNo < AFS_NUM_EXTENT_LISTS; listNo ++)
    {
        LARGE_INTEGER lastOffset;

        lastOffset.QuadPart = 0;

        for (PLIST_ENTRY pLe = Fcb->Specific.File.ExtentsLists[listNo].Flink;
             pLe != &Fcb->Specific.File.ExtentsLists[listNo];
             pLe = pLe->Flink)
        {
            AFSExtent *pExtent;

            pExtent = ExtentFor(pLe, listNo);

            if (listNo == 0) {
                ASSERT(pLe        != &Fcb->Specific.File.ExtentsLists[1] &&
                       pLe->Flink !=&Fcb->Specific.File.ExtentsLists[1] &&
                       pLe->Blink !=&Fcb->Specific.File.ExtentsLists[1]);
            }

            ASSERT(pLe->Flink->Blink == pLe);
            ASSERT(pLe->Blink->Flink == pLe);

            //
            // Should follow on from previous
            //
            ASSERT(pExtent->FileOffset.QuadPart >= lastOffset.QuadPart);
            lastOffset.QuadPart = pExtent->FileOffset.QuadPart + pExtent->Size;

            //
            // Should match alignment criteria
            //
            ASSERT( 0 == (pExtent->FileOffset.LowPart & ExtentsMasks[listNo]) );

            //
            // "lower" lists should be populated
            //
            for (LONG subListNo = listNo-1; subListNo > 0; subListNo --)
            {
                ASSERT( !IsListEmpty(&pExtent->Lists[subListNo]));
            }
        }
    }
#endif
}

void
AFSTrimExtents( IN AFSFcb *Fcb,
                IN PLARGE_INTEGER FileSize)
{

    AFSNonPagedFcb      *pNPFcb = Fcb->NPFcb;
    LIST_ENTRY          *le;
    AFSExtent           *pExtent;
    BOOLEAN              locked = FALSE;
    NTSTATUS             ntStatus = STATUS_SUCCESS;
    LARGE_INTEGER        liAlignedOffset = {0,0};
    AFSDeviceExt        *pDevExt = (AFSDeviceExt *)AFSRDRDeviceObject->DeviceExtension;
    AFSDeviceExt        *pControlDevExt = (AFSDeviceExt *)AFSControlDeviceObject->DeviceExtension;

    __Enter
    {

        //
        // Get an aligned offset
        //

        if( FileSize != NULL)
        {

            liAlignedOffset = *FileSize;
        }

        if( liAlignedOffset.QuadPart > 0 &&
            liAlignedOffset.QuadPart % pDevExt->Specific.RDR.CacheBlockSize != 0)
        {

            //
            // Align UP to the next cache block size
            //

            liAlignedOffset.QuadPart = (ULONGLONG)( (ULONGLONG)((liAlignedOffset.QuadPart / pDevExt->Specific.RDR.CacheBlockSize) + 1) * (ULONGLONG)pDevExt->Specific.RDR.CacheBlockSize);
        }

        //
        // Ensure that no one is working with the extents and grab the
        // lock
        //

        AFSLockForExtentsTrim( Fcb);

        locked = TRUE;

        if( 0 == Fcb->Specific.File.ExtentCount)
        {

            //
            // Update the request extent status
            //

            Fcb->NPFcb->Specific.File.ExtentsRequestStatus = STATUS_SUCCESS;

            try_return( ntStatus = STATUS_SUCCESS);
        }

        //
        // We are truncating from a specific length in the file. If the offset
        // is non-zero then go find the first extent to remove
        //

        if( 0 == FileSize->QuadPart)
        {

            le = Fcb->Specific.File.ExtentsLists[AFS_EXTENTS_LIST].Flink;
        }
        else
        {

            pExtent = AFSExtentForOffset( Fcb,
                                          FileSize,
                                          TRUE);

            if( NULL == pExtent)
            {

                le = Fcb->Specific.File.ExtentsLists[AFS_EXTENTS_LIST].Flink;
            }
            else
            {
                le = &pExtent->Lists[AFS_EXTENTS_LIST];
            }
        }

        while( le != &Fcb->Specific.File.ExtentsLists[AFS_EXTENTS_LIST])
        {

            pExtent = ExtentFor( le, AFS_EXTENTS_LIST);

            //
            // Only trim down extents beyond the aligned offset
            //

            le = le->Flink;

            if( pExtent->FileOffset.QuadPart >= liAlignedOffset.QuadPart)
            {

                if( BooleanFlagOn( pExtent->Flags, AFS_EXTENT_DIRTY))
                {

                    AFSAcquireExcl( &pNPFcb->Specific.File.DirtyExtentsListLock,
                                    TRUE);

                    if( BooleanFlagOn( pExtent->Flags, AFS_EXTENT_DIRTY))
                    {
                        LONG dirtyCount;

                        AFSRemoveEntryDirtyList( Fcb,
                                                 pExtent);

                        dirtyCount = InterlockedDecrement( &Fcb->Specific.File.ExtentsDirtyCount);

                        ASSERT(dirtyCount >= 0);
                    }

                    AFSReleaseResource( &pNPFcb->Specific.File.DirtyExtentsListLock);
                }

                for (ULONG i = 0; i < AFS_NUM_EXTENT_LISTS; i ++)
                {
                    if (NULL != pExtent->Lists[i].Flink && !IsListEmpty(&pExtent->Lists[i]))
                    {
                        RemoveEntryList( &pExtent->Lists[i] );
                    }
                }

                AFSDbgLogMsg( AFS_SUBSYSTEM_EXTENT_PROCESSING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSTrimExtents Releasing extent %p fid %08lX-%08lX-%08lX-%08lX Offset %I64X Len %08lX\n",
                              pExtent,
                              Fcb->ObjectInformation->FileId.Cell,
                              Fcb->ObjectInformation->FileId.Volume,
                              Fcb->ObjectInformation->FileId.Vnode,
                              Fcb->ObjectInformation->FileId.Unique,
                              pExtent->FileOffset.QuadPart,
                              pExtent->Size);

                InterlockedExchangeAdd( &pControlDevExt->Specific.Control.ExtentsHeldLength, -((LONG)(pExtent->Size/1024)));

                InterlockedExchangeAdd( &Fcb->Specific.File.ExtentLength, -((LONG)(pExtent->Size/1024)));

                ASSERT( pExtent->ActiveCount == 0);

                //
                // and free
                //
                AFSExFreePool( pExtent);

                InterlockedDecrement( &Fcb->Specific.File.ExtentCount);

                if( InterlockedDecrement( &pControlDevExt->Specific.Control.ExtentCount) == 0)
                {

                    KeSetEvent( &pControlDevExt->Specific.Control.ExtentsHeldEvent,
                                0,
                                FALSE);
                }
            }
        }

        //
        // Update the request extent status
        //

        Fcb->NPFcb->Specific.File.ExtentsRequestStatus = STATUS_SUCCESS;

try_exit:

        if (locked)
        {

            AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                          AFS_TRACE_LEVEL_VERBOSE,
                          "AFSTrimExtents Releasing Fcb extents lock %08lX thread %08lX\n",
                          &Fcb->NPFcb->Specific.File.ExtentsResource,
                          PsGetCurrentThread());

            AFSReleaseResource( &Fcb->NPFcb->Specific.File.ExtentsResource );
        }
    }

    return;
}

void
AFSTrimSpecifiedExtents( IN AFSFcb *Fcb,
                         IN ULONG   Count,
                         IN AFSFileExtentCB *Result)
{

    AFSNonPagedFcb      *pNPFcb = Fcb->NPFcb;
    LIST_ENTRY          *le;
    AFSExtent           *pExtent;
    AFSFileExtentCB     *pFileExtents = Result;
    NTSTATUS             ntStatus = STATUS_SUCCESS;
    AFSDeviceExt        *pDevExt = (AFSDeviceExt *)AFSRDRDeviceObject->DeviceExtension;
    AFSDeviceExt        *pControlDevExt = (AFSDeviceExt *)AFSControlDeviceObject->DeviceExtension;

    __Enter
    {

        le = Fcb->Specific.File.ExtentsLists[AFS_EXTENTS_LIST].Flink;

        while( le != &Fcb->Specific.File.ExtentsLists[AFS_EXTENTS_LIST] &&
               Count > 0)
        {

            pExtent = ExtentFor( le, AFS_EXTENTS_LIST);

            //
            // Only trim down extents beyond the aligned offset
            //

            le = le->Flink;

            if( pExtent->FileOffset.QuadPart == pFileExtents->FileOffset.QuadPart)
            {

                if( BooleanFlagOn( pExtent->Flags, AFS_EXTENT_DIRTY))
                {

                    AFSAcquireExcl( &pNPFcb->Specific.File.DirtyExtentsListLock,
                                    TRUE);

                    if( BooleanFlagOn( pExtent->Flags, AFS_EXTENT_DIRTY))
                    {

                        LONG dirtyCount;

                        AFSRemoveEntryDirtyList( Fcb,
                                                 pExtent);

                        dirtyCount = InterlockedDecrement( &Fcb->Specific.File.ExtentsDirtyCount);

                        ASSERT( dirtyCount >= 0);

                    }

                    AFSReleaseResource( &pNPFcb->Specific.File.DirtyExtentsListLock);

                }

                for (ULONG i = 0; i < AFS_NUM_EXTENT_LISTS; i ++)
                {
                    if (NULL != pExtent->Lists[i].Flink && !IsListEmpty(&pExtent->Lists[i]))
                    {
                        RemoveEntryList( &pExtent->Lists[i] );
                    }
                }

                AFSDbgLogMsg( AFS_SUBSYSTEM_EXTENT_PROCESSING,
                              AFS_TRACE_LEVEL_VERBOSE,
                              "AFSTrimSpecifiedExtents Releasing extent %p fid %08lX-%08lX-%08lX-%08lX Offset %I64X Len %08lX\n",
                              pExtent,
                              Fcb->ObjectInformation->FileId.Cell,
                              Fcb->ObjectInformation->FileId.Volume,
                              Fcb->ObjectInformation->FileId.Vnode,
                              Fcb->ObjectInformation->FileId.Unique,
                              pExtent->FileOffset.QuadPart,
                              pExtent->Size);

                InterlockedExchangeAdd( &pControlDevExt->Specific.Control.ExtentsHeldLength, -((LONG)(pExtent->Size/1024)));

                InterlockedExchangeAdd( &Fcb->Specific.File.ExtentLength, -((LONG)(pExtent->Size/1024)));

                ASSERT( pExtent->ActiveCount == 0);

                //
                // and free
                //
                AFSExFreePool( pExtent);

                InterlockedDecrement( &Fcb->Specific.File.ExtentCount);

                if( InterlockedDecrement( &pControlDevExt->Specific.Control.ExtentCount) == 0)
                {

                    KeSetEvent( &pControlDevExt->Specific.Control.ExtentsHeldEvent,
                                0,
                                FALSE);
                }

                //
                // Next extent we are looking for
                //

                pFileExtents++;

                Count--;
            }
        }

        //
        // Update the request extent status
        //

        Fcb->NPFcb->Specific.File.ExtentsRequestStatus = STATUS_SUCCESS;
    }

    return;
}

void
AFSReferenceActiveExtents( IN AFSExtent *StartExtent,
                           IN ULONG ExtentsCount)
{

    AFSExtent     *pExtent = StartExtent;
    AFSExtent     *pNextExtent;
    ULONG          ulCount = 0;

    while( ulCount < ExtentsCount)
    {

        pNextExtent = NextExtent( pExtent, AFS_EXTENTS_LIST);

        InterlockedIncrement( &pExtent->ActiveCount);

        AFSDbgLogMsg( AFS_SUBSYSTEM_EXTENT_ACTIVE_COUNTING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSReferenceActiveExtents Increment count on extent %08lX Cnt %d\n",
                      pExtent,
                      pExtent->ActiveCount);

        pExtent = pNextExtent;

        ulCount++;
    }

    return;
}

void
AFSDereferenceActiveExtents( IN AFSExtent *StartExtent,
                             IN ULONG ExtentsCount)
{

    AFSExtent     *pExtent = StartExtent;
    AFSExtent     *pNextExtent;
    ULONG          ulCount = 0;

    while( ulCount < ExtentsCount)
    {

        pNextExtent = NextExtent( pExtent, AFS_EXTENTS_LIST);

        AFSDbgLogMsg( AFS_SUBSYSTEM_EXTENT_ACTIVE_COUNTING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSDereferenceActiveExtents Decrement count on extent %08lX Cnt %d\n",
                      pExtent,
                      pExtent->ActiveCount);

        ASSERT( pExtent->ActiveCount > 0);

        InterlockedDecrement( &pExtent->ActiveCount);

        pExtent = pNextExtent;

        ulCount++;
    }

    return;
}

void
AFSRemoveEntryDirtyList( IN AFSFcb *Fcb,
                         IN AFSExtent *Extent)
{

    if( Extent->DirtyList.fLink == NULL)
    {

        Fcb->NPFcb->Specific.File.DirtyListTail = (AFSExtent *)Extent->DirtyList.bLink;

        if( Fcb->NPFcb->Specific.File.DirtyListTail != NULL)
        {

            Fcb->NPFcb->Specific.File.DirtyListTail->DirtyList.fLink = NULL;
        }
    }
    else
    {

        ((AFSExtent *)Extent->DirtyList.fLink)->DirtyList.bLink = Extent->DirtyList.bLink;
    }

    if( Extent->DirtyList.bLink == NULL)
    {

        Fcb->NPFcb->Specific.File.DirtyListHead = (AFSExtent *)Extent->DirtyList.fLink;

        if( Fcb->NPFcb->Specific.File.DirtyListHead != NULL)
        {

            Fcb->NPFcb->Specific.File.DirtyListHead->DirtyList.bLink = NULL;
        }
    }
    else
    {

        ((AFSExtent *)Extent->DirtyList.bLink)->DirtyList.fLink = Extent->DirtyList.fLink;
    }


    return;
}

#if GEN_MD5
void
AFSSetupMD5Hash( IN AFSFcb *Fcb,
                 IN AFSExtent *StartExtent,
                 IN ULONG ExtentsCount,
                 IN void *SystemBuffer,
                 IN LARGE_INTEGER *ByteOffset,
                 IN ULONG ByteCount)
{

    NTSTATUS ntStatus = STATUS_SUCCESS;
    AFSNonPagedFcb *pNPFcb = Fcb->NPFcb;
    AFSExtent     *pExtent = StartExtent;
    AFSExtent     *pNextExtent, *pCurrentExtent = NULL;
    ULONG ulCount = 0;
    char *pCurrentBuffer = (char *)SystemBuffer;
    char *pMD5Buffer = NULL;
    ULONG ulCurrentLen = 0;
    void *pExtentBuffer = NULL;
    LARGE_INTEGER liByteOffset;
    ULONG ulBytesRead = 0;

    __Enter
    {

        AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSSetupMD5Hash Acquiring Fcb extents lock %08lX SHARED %08lX\n",
                      &Fcb->NPFcb->Specific.File.ExtentsResource,
                      PsGetCurrentThread());

        AFSAcquireShared( &Fcb->NPFcb->Specific.File.ExtentsResource, TRUE);

        liByteOffset.QuadPart = ByteOffset->QuadPart;

        while( ulCount < ExtentsCount)
        {

            RtlZeroMemory( pExtent->MD5,
                           sizeof( pExtent->MD5));

            pNextExtent = NextExtent( pExtent, AFS_EXTENTS_LIST);

            if( liByteOffset.QuadPart == pExtent->FileOffset.QuadPart &&
                ByteCount < pExtent->Size)
            {

                if( pExtentBuffer == NULL)
                {

                    pExtentBuffer = AFSExAllocatePoolWithTag( PagedPool,
                                                              pExtent->Size,
                                                              AFS_GENERIC_MEMORY_9_TAG);

                    if( pExtentBuffer == NULL)
                    {

                        break;
                    }
                }

                RtlZeroMemory( pExtentBuffer,
                               pExtent->Size);

                RtlCopyMemory( pExtentBuffer,
                               pCurrentBuffer,
                               ByteCount);

                pMD5Buffer = (char *)pExtentBuffer;

                ulCurrentLen = ByteCount;
            }
            else if( liByteOffset.QuadPart != pExtent->FileOffset.QuadPart)
            {

                pExtentBuffer = AFSExAllocatePoolWithTag( PagedPool,
                                                          pExtent->Size,
                                                          AFS_GENERIC_MEMORY_10_TAG);

                if( pExtentBuffer == NULL)
                {

                    break;
                }

                RtlZeroMemory( pExtentBuffer,
                               pExtent->Size);

                if( BooleanFlagOn( AFSLibControlFlags, AFS_REDIR_LIB_FLAGS_NONPERSISTENT_CACHE))
                {

#ifdef AMD64
                    RtlCopyMemory( pExtentBuffer,
                                   ((char *)AFSLibCacheBaseAddress + pExtent->CacheOffset.QuadPart),
                                   pExtent->Size);
#else
                    ASSERT( pExtent->CacheOffset.HighPart == 0);
                    RtlCopyMemory( pExtentBuffer,
                                   ((char *)AFSLibCacheBaseAddress + pExtent->CacheOffset.LowPart),
                                   pExtent->Size);
#endif

                    ulBytesRead = pExtent->Size;
                }
                else
                {

                    ntStatus = AFSReadCacheFile( pExtentBuffer,
                                                 &pExtent->CacheOffset,
                                                 pExtent->Size,
                                                 &ulBytesRead);

                    if( !NT_SUCCESS( ntStatus))
                    {
                        break;
                    }
                }

                pMD5Buffer = (char *)pExtentBuffer;

                ulCurrentLen = min( ByteCount, pExtent->Size - (ULONG)(liByteOffset.QuadPart - pExtent->FileOffset.QuadPart));

                RtlCopyMemory( (void *)((char *)pExtentBuffer + (ULONG)(liByteOffset.QuadPart - pExtent->FileOffset.QuadPart)),
                               pCurrentBuffer,
                               ulCurrentLen);
            }
            else
            {

                ulCurrentLen = pExtent->Size;

                pMD5Buffer = pCurrentBuffer;
            }

            AFSGenerateMD5( pMD5Buffer,
                            pExtent->Size,
                            pExtent->MD5);

            pExtent = pNextExtent;

            ulCount++;

            ByteCount -= ulCurrentLen;

            pCurrentBuffer += ulCurrentLen;

            liByteOffset.QuadPart += ulCurrentLen;
        }

        AFSDbgLogMsg( AFS_SUBSYSTEM_LOCK_PROCESSING,
                      AFS_TRACE_LEVEL_VERBOSE,
                      "AFSSetupMD5Hash Releasing Fcb extents lock %08lX SHARED %08lX\n",
                      &Fcb->NPFcb->Specific.File.ExtentsResource,
                      PsGetCurrentThread());

        AFSReleaseResource( &Fcb->NPFcb->Specific.File.ExtentsResource );

        if( pExtentBuffer != NULL)
        {

            AFSExFreePool( pExtentBuffer);
        }
    }

    return;
}
#endif

