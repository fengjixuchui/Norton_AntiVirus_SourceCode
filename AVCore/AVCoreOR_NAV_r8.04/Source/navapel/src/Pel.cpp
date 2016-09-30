// Copyright 1994 - 1998 Symantec, Peter Norton Product Group
//***************************************************************************
//
// Description:
//      This file defines functions for process exclusion.  The process
//      exclusion list is used by NAVAP to mark processes and threads which
//      are protected.  Events generated by protected processes and threads
//      will be ignored.
//
// Contains:
//      PEL_LockThreadDataPtr()           (local macro)
//      PEL_UnLockThreadDataPtr()         (local macro)
//
//      PELGetCurrentProcessID()          (local macro)
//
//      PELInit()
//      PELDeInit()
//
//      AddCurrentThreadToPEL1()
//      ForceCurrentThreadIntoPEL1()
//      IsCurrentThreadInPEL1()
//      DeleteCurrentThreadFromPEL1()
//
//      AddCurrentProcessToPEL2()
//      GetCurrentProcessPEL2Count()
//      DeleteCurrentProcessFromPEL2()
//
// See Also:
//      pel.h
//
//***************************************************************************

#include "platform.h"
#include "symsync.h"
#include "pel.h"

//---------------------------------------------------------------------------
//
// This module contains the most "multithread - sensitive" code in NAVAP
// (of course, after DriverComm) so here we are going to define some
// guarding variables and macros used only in debug compiles.
//
// Note, that while these variables and macros provide some sort of
// protection from improper usage, there are still cases which are not
// fully covered.
//
//---------------------------------------------------------------------------
#if defined(SYM_DEBUG) || defined(PRERELEASE)

#define DRIVER_NTK_LOCKED_DATA_SEGMENT
#include "drvseg.h"

LONG lPELAPIUsage = 0;

LONG lExclusionsInPEL1 = 0;

LONG lPerThreadDataLocksForPEL1 = 0;

#define DRIVER_DEFAULT_DATA_SEGMENT
#include "drvseg.h"

#define DBGIncrementPELAPIUsage() SymInterlockedIncrement(&lPELAPIUsage)
#define DBGDecrementPELAPIUsage() SymInterlockedDecrement(&lPELAPIUsage)
#define DBGAssertNoPELAPIUsage()  SYM_ASSERT(lPELAPIUsage==0)

#define DBGIncrementPEL1Exclusions() SymInterlockedIncrement(&lExclusionsInPEL1)
#define DBGDecrementPEL1Exclusions() SymInterlockedDecrement(&lExclusionsInPEL1)
#define DBGAssertNoPEL1Exclusions()  SYM_ASSERT(lExclusionsInPEL1==0)

#define PEL_AllocThreadData CoreAllocThreadData

PVOID PEL_LockThreadDataPtr ( HOFDATA h )
    {
    auto PVOID p;

    if ( p = CoreLockThreadDataPtr(h) )
        {
        SymInterlockedIncrement ( &lPerThreadDataLocksForPEL1 );
        }

    return ( p );
    }

VOID PEL_UnLockThreadDataPtr ( HOFDATA h, PVOID p )
    {
    if ( p )
        {
        SymInterlockedDecrement ( &lPerThreadDataLocksForPEL1 );
        }

    CoreUnLockThreadDataPtr ( h, p );
    }

#else

#define DBGIncrementPELAPIUsage()    ((void)0)
#define DBGDecrementPELAPIUsage()    ((void)0)
#define DBGAssertNoPELAPIUsage()     ((void)0)

#define DBGIncrementPEL1Exclusions() ((void)0)
#define DBGDecrementPEL1Exclusions() ((void)0)
#define DBGAssertNoPEL1Exclusions()  ((void)0)

#define PEL_AllocThreadData     CoreAllocThreadData
#define PEL_LockThreadDataPtr   CoreLockThreadDataPtr
#define PEL_UnLockThreadDataPtr CoreUnLockThreadDataPtr

#endif

//---------------------------------------------------------------------------
//
// PEL1STATUS contains information about the PEL1 status of a thread.  This
// structure is managed by Core's per-thread data APIs and hPELThreadData.
//
// hPELThreadData is a handle used to access per-thread data, and is the
// key component of PEL1.
//
//---------------------------------------------------------------------------
typedef struct
    {
    PVOID pvLockedThreadDataPtr;
    BOOL  bExcluded;
    PVOID pvPrivateData;
    } PEL1STATUS, *PPEL1STATUS;

HOFDATA hPELThreadData = NULL;

//---------------------------------------------------------------------------
//
// PEL2STATUS contains information about the PEL2 status of a process.  This
// structure is managed directly by this module.
//
// prPEL2List points to the first item of a linked list with PEL2STATUS
// structures used to monitor PEL2 exclusions.
//
//---------------------------------------------------------------------------
typedef struct tagPEL2STATUS
    {
    tagPEL2STATUS *prNext;
    PVOID         pvProcessID;
    int           iExcludeCount;
    } PEL2STATUS, *PPEL2STATUS;

PPEL2STATUS prPEL2List = NULL;

//---------------------------------------------------------------------------
//
// Define local macros.
//
//---------------------------------------------------------------------------
#if defined(SYM_NTK)
#define PELGetCurrentProcessID() ((PVOID)PsGetCurrentThread())
#else
#define PELGetCurrentProcessID() ((PVOID)Get_Cur_Thread_Handle())
#endif

//---------------------------------------------------------------------------
//
// BOOL PELInit (
//      VOID
//      );
//
// This function initializes the PEL module.  It allocates a per-thread data
// handle.
//
// No other PEL APIs can be called until this function completes
// successfully.
//
// Entry:
//      -
//
// Exit:
//      TRUE  if module was initialized and it is ready for use.
//      FALSE if module initialization failed (per-thread data allocation
//            failed).
//
// Synchronization:
//      This function is non-reentrant and should not be called more than
//      once.
//
//---------------------------------------------------------------------------
#define DRIVER_INIT_CODE_SEGMENT
#include "drvseg.h"

BOOL PELInit (
    VOID
    )
    {
                                        // Make sure we are called properly.

    SYM_ASSERT ( !hPELThreadData );

                                        // Allocate per-thread data.

    hPELThreadData = PEL_AllocThreadData ( sizeof ( PEL1STATUS ) );

                                        // Return to caller.

    return ( hPELThreadData ? TRUE : FALSE );
    }

#define DRIVER_DEFAULT_CODE_SEGMENT
#include "drvseg.h"

//---------------------------------------------------------------------------
//
// BOOL PELDeInit (
//      VOID
//      );
//
// This function uninitializes PEL.  No calls to other APIs should be made
// during and after the call to this function.
//
// Entry:
//      -
//
// Exit:
//      Always TRUE
//
// Synchronization:
//      This function is non-reentrant and should not be called more than
//      once.
//
//---------------------------------------------------------------------------
BOOL PELDeInit (
    VOID
    )
    {
    auto HOFDATA     hPELThreadDataSave;
    auto PPEL2STATUS prPELNext;

                                        // Make sure we are called properly.

    SYM_ASSERT ( hPELThreadData );

                                        // Invalidate the per-thread data
                                        // handle.

    hPELThreadDataSave = hPELThreadData;

    hPELThreadData = NULL;

                                        // No PEL APIs should be called.

    DBGAssertNoPELAPIUsage();

                                        // PEL1 should be empty.

    DBGAssertNoPEL1Exclusions();

                                        // Clean up PEL2.  It's a simple
                                        // linked list, so this should be
                                        // easy.  Note, that we do not need
                                        // to acquire any locks, since no
                                        // other PEL APIs should be called!

    for ( ; prPEL2List; prPEL2List = prPELNext )
        {
        prPELNext = prPEL2List->prNext;
        MemFreePtr ( prPEL2List );
        }

                                        // Free PEL's per-thread data handle.

    CoreFreeThreadData ( hPELThreadDataSave );

                                        // Return to caller

    return ( TRUE );
    }

//---------------------------------------------------------------------------
//
// BOOL AddCurrentThreadToPEL1 (
//      VOID
//      );
//
// This function adds the current thread to PEL1.  It does so only if this
// thread not already in PEL.
//
// AddCurrentThreadToPEL1() can be called only after a successful call to
// PELInit(), and before PELDeInit() is called.
//
// Entry:
//      -
//
// Exit:
//      TRUE  if thread was added to PEL1.  Once this function returns
//            TRUE, the per-thread data for the current thread will be
//            locked, and a call to DeleteCurrentThreadFromPEL1() must
//            be issued in order for it to be unlocked.
//      FALSE if thread was not added to PEL1
//
// Synchronization:
//      AddCurrentThreadToPEL1() is fully reentrant for different threads,
//      but cannot be reentered under the context of the same thread.
//
//---------------------------------------------------------------------------
BOOL AddCurrentThreadToPEL1 (
    VOID
    )
    {
    auto PPEL1STATUS prPELStatus;

                                        // Make sure we are called properly.

    DBGIncrementPELAPIUsage();

    SYM_ASSERT ( hPELThreadData );

                                        // Lock the per-thread data for this
                                        // thread.

    prPELStatus = (PPEL1STATUS) PEL_LockThreadDataPtr ( hPELThreadData );

                                        // Check if SYMEvent gave us a valid
                                        // per-thread data pointer.

    SYM_ASSERT ( prPELStatus );

    if ( prPELStatus )
        {
                                        // Check if SYMEvent gave us a valid
                                        // per-thread data block.  We know
                                        // that our data can contain only
                                        // TRUE or FALSE, and a newly
                                        // initialized block should always
                                        // be filled with zeroes.

        SYM_ASSERT ( prPELStatus->bExcluded == TRUE ||
                     prPELStatus->bExcluded == FALSE );

                                        // If we are not in PEL, then put us
                                        // there.  Don't forget to save a
                                        // pointer to the currently locked
                                        // per-thread data area, so that we
                                        // can unlock it when we delete the
                                        // process from PEL1.

        if ( !prPELStatus->bExcluded &&
             GetCurrentProcessPEL2Count() == 0 )
            {
            DBGIncrementPEL1Exclusions();

            DBGDecrementPELAPIUsage();

            prPELStatus->pvLockedThreadDataPtr = prPELStatus;

            return ( prPELStatus->bExcluded = TRUE );
            }
        }

                                        // Unlock the per-thread data.

    PEL_UnLockThreadDataPtr ( hPELThreadData, prPELStatus );

                                        // Fail the function

    DBGDecrementPELAPIUsage();

    return ( FALSE );
    }

//---------------------------------------------------------------------------
//
// BOOL ForceCurrentThreadIntoPEL1 (
//      VOID
//      );
//
// This function adds the current thread to PEL1 if it is not in PEL1.  The
// status of the current thread/process in PEL2 is ignored.
//
// ForceCurrentThreadIntoPEL1() can be called only after a successful call
// to PELInit(), and before PELDeInit() is called.
//
// Entry:
//      -
//
// Exit:
//      TRUE  if thread was added to PEL1.  Once this function returns
//            TRUE, the per-thread data for the current thread will be
//            locked, and a call to DeleteCurrentThreadFromPEL1() must
//            be issued in order for it to be unlocked.
//      FALSE if thread was not added to PEL1
//
// Synchronization:
//      ForceCurrentThreadIntoPEL1() is fully reentrant for different
//      threads, but cannot be reentered under the context of the same
//      thread.
//
//---------------------------------------------------------------------------
BOOL ForceCurrentThreadIntoPEL1 (
    VOID
    )
    {
    auto PPEL1STATUS prPELStatus;

                                        // Make sure we are called properly.

    DBGIncrementPELAPIUsage();

    SYM_ASSERT ( hPELThreadData );

                                        // Lock the per-thread data for this
                                        // thread.

    prPELStatus = (PPEL1STATUS) PEL_LockThreadDataPtr ( hPELThreadData );

                                        // Check if SYMEvent gave us a valid
                                        // per-thread data pointer.

    SYM_ASSERT ( prPELStatus );

    if ( prPELStatus )
        {
                                        // Check if SYMEvent gave us a valid
                                        // per-thread data block.  We know
                                        // that our data can contain only
                                        // TRUE or FALSE, and a newly
                                        // initialized block should always
                                        // be filled with zeroes.

        SYM_ASSERT ( prPELStatus->bExcluded == TRUE ||
                     prPELStatus->bExcluded == FALSE );

                                        // If we are not in PEL, then put us
                                        // there.  Don't forget to save a
                                        // pointer to the currently locked
                                        // per-thread data area, so that we
                                        // can unlock it when we delete the
                                        // process from PEL1.

        if ( !prPELStatus->bExcluded )
            {
            DBGIncrementPEL1Exclusions();

            DBGDecrementPELAPIUsage();

            prPELStatus->pvLockedThreadDataPtr = prPELStatus;

            return ( prPELStatus->bExcluded = TRUE );
            }
        }

                                        // Unlock the per-thread data.

    PEL_UnLockThreadDataPtr ( hPELThreadData, prPELStatus );

                                        // Fail the function.

    DBGDecrementPELAPIUsage();

    return ( FALSE );
    }

//---------------------------------------------------------------------------
//
// BOOL IsCurrentThreadInPEL1 (
//      VOID
//      );
//
// This function checks if the current thread is in PEL1.
//
// IsCurrentThreadInPEL1() can be called only after a successful call to
// PELInit(), and before PELDeInit() is called.
//
// Entry:
//      -
//
// Exit:
//      TRUE  if the current thread is in PEL1
//      FALSE if the current thread is not in PEL1, or if an error occurred
//            while processing.
//
// Synchronization:
//      IsCurrentThreadInPEL1() is fully reentrant for different threads,
//      but cannot be reentered under the context of the same thread.
//
//---------------------------------------------------------------------------
BOOL IsCurrentThreadInPEL1 (
    VOID
    )
    {
    auto PPEL1STATUS prPELStatus;

                                        // Make sure we are called properly.

    DBGIncrementPELAPIUsage();

    SYM_ASSERT ( hPELThreadData );

                                        // Lock the per-thread data for this
                                        // thread.

    prPELStatus = (PPEL1STATUS) PEL_LockThreadDataPtr ( hPELThreadData );

                                        // Check if SYMEvent gave us a valid
                                        // per-thread data pointer.

    SYM_ASSERT ( prPELStatus );

    if ( prPELStatus )
        {
        auto BOOL bResult;

                                        // Check if SYMEvent gave us a valid
                                        // per-thread data block.  We know
                                        // that our data can contain only
                                        // TRUE or FALSE, and a newly
                                        // initialized block should always
                                        // be filled with zeroes.

        SYM_ASSERT ( prPELStatus->bExcluded == TRUE ||
                     prPELStatus->bExcluded == FALSE );

                                        // Save the PEL1 status of this
                                        // thread.

        bResult = prPELStatus->bExcluded;

                                        // Unlock the per-thread data.

        PEL_UnLockThreadDataPtr ( hPELThreadData, prPELStatus );

        DBGDecrementPELAPIUsage();

        return ( bResult );
        }

                                        // Fail the function.

    DBGDecrementPELAPIUsage();

    return ( FALSE );
    }

//---------------------------------------------------------------------------
//
// VOID DeleteCurrentThreadFromPEL1 (
//      VOID
//      );
//
// This function deletes the current thread from PEL1.  It should be called
// only after a successful call to AddCurrentThreadToPEL1() from the same
// thread.
//
// DeleteCurrentThreadFromPEL1() can be called only after a successful call
// to PELInit(), and before PELDeInit() is called.
//
// Entry:
//      -
//
// Exit:
//      The current thread was deleted from PEL1, if it was in it.
//
// Synchronization:
//      DeleteCurrentThreadFromPEL1() is fully reentrant for different
//      threads, but cannot be reentered under the context of the same
//      thread.
//
//---------------------------------------------------------------------------
VOID DeleteCurrentThreadFromPEL1 (
    VOID
    )
    {
    auto PPEL1STATUS prPELStatus;

                                        // Make sure we are called properly.

    DBGIncrementPELAPIUsage();

    SYM_ASSERT ( hPELThreadData );

                                        // Lock the per-thread data for this
                                        // thread.

    prPELStatus = (PPEL1STATUS) PEL_LockThreadDataPtr ( hPELThreadData );

                                        // Check if SYMEvent gave us a valid
                                        // per-thread data pointer.

    SYM_ASSERT ( prPELStatus );

    if ( prPELStatus )
        {
                                        // Check if SYMEvent gave us a valid
                                        // per-thread data block.  We know
                                        // that our data can contain only
                                        // TRUE or FALSE, and a newly
                                        // initialized block should always
                                        // be filled with zeroes.  We also
                                        // know, that this function should
                                        // be called only after the current
                                        // thread has been added to PEL1.

        SYM_ASSERT ( prPELStatus->bExcluded == TRUE );

                                        // Remove current thread from PEL1
                                        // and unlock the per-thread data
                                        // pointer locked initially by
                                        // AddCurrentThreadToPEL1().

        prPELStatus->bExcluded = FALSE;

        SYM_ASSERT ( prPELStatus->pvLockedThreadDataPtr );

        PEL_UnLockThreadDataPtr ( hPELThreadData,
                                  prPELStatus->pvLockedThreadDataPtr );

        DBGDecrementPEL1Exclusions();

                                        // Unlock the per-thread data.

        PEL_UnLockThreadDataPtr ( hPELThreadData, prPELStatus );
        }

    DBGDecrementPELAPIUsage();
    }

//---------------------------------------------------------------------------
//
// BOOL AddCurrentProcessToPEL2 (
//      VOID
//      );
//
// This function adds the current process to PEL2.
//
// AddCurrentProcessToPEL2() can be called only after a successful call
// to PELInit(), and before PELDeInit() is called.
//
// Entry:
//      -
//
// Exit:
//      TRUE  if current process was added to the exclusion list
//      FALSE if current process was not added to the exclusion list due
//            to an error
//
// Synchronization:
//      AddCurrentProcessToPEL2() is fully reentrant.
//
//---------------------------------------------------------------------------
BOOL AddCurrentProcessToPEL2 (
    VOID
    )
    {
    auto PVOID       pvCurrentProcess;
    auto PPEL2STATUS prPELStatus;

                                        // Make sure we are called properly.

    DBGIncrementPELAPIUsage();

    SYM_ASSERT ( hPELThreadData );

                                        // Get the data necessary for us to
                                        // work.

    if ( pvCurrentProcess = PELGetCurrentProcessID() )
        {
        if ( prPELStatus = (PPEL2STATUS) MemAllocPtr ( GMEM_MOVEABLE,
                                                       sizeof(*prPELStatus) )
           )
            {
            auto PPEL2STATUS prPELItem;

                                        // Find an existing PEL2 structure
                                        // in the list.

            SymAcquireFastGlobalLock();

            for ( prPELItem = prPEL2List;
                  prPELItem && prPELItem->pvProcessID != pvCurrentProcess;
                  prPELItem = prPELItem->prNext );

            if ( !prPELItem )
                {
                                        // If this is the first time we add
                                        // this process to PEL2, then
                                        // initialize the newly allocated
                                        // item and link it to the list.

                prPELStatus->pvProcessID = pvCurrentProcess;

                prPELStatus->iExcludeCount = 1;

                prPELStatus->prNext = prPEL2List;

                prPEL2List = prPELStatus;
                }
            else
                {
                                        // Otherwise, just increment the PEL2
                                        // counter in the existing item.

                prPELItem->iExcludeCount++;
                }

            SymReleaseFastGlobalLock();

                                        // Free the memory if we did not use
                                        // the newly allocated item.

            if ( prPELItem )
                {
                MemFreePtr ( prPELStatus );
                }

                                        // Return to caller
            DBGDecrementPELAPIUsage();

            return ( TRUE );
            }
        }

                                        // Check the reason for the error

    SYM_ASSERT ( pvCurrentProcess );

                                        // Fail the function

    DBGDecrementPELAPIUsage();

    return ( FALSE );
    }

//---------------------------------------------------------------------------
//
// int GetCurrentProcessPEL2Count (
//      VOID
//      );
//
// Returns the number of times the current process has been added to PEL2.
//
// GetCurrentProcessPEL2Count() can be called only after a successful call
// to PELInit(), and before PELDeInit() is called.
//
// Entry:
//      -
//
// Exit:
//      Returns current process exclusion count in PEL2.
//
// Synchronization:
//      GetCurrentProcessPEL2Count() is fully reentrant.
//
//---------------------------------------------------------------------------
int GetCurrentProcessPEL2Count (
    VOID
    )
    {
    auto PVOID pvCurrentProcess;

                                        // Make sure we are called properly.

    DBGIncrementPELAPIUsage();

    SYM_ASSERT ( hPELThreadData );

                                        // Get the data necessary for us to
                                        // work.

    if ( pvCurrentProcess = PELGetCurrentProcessID() )
        {
        auto int         iResult;
        auto PPEL2STATUS prPELItem;

                                        // Find an existing PEL2 structure
                                        // in the list.

        SymAcquireFastGlobalLock();

        for ( prPELItem = prPEL2List;
              prPELItem && prPELItem->pvProcessID != pvCurrentProcess;
              prPELItem = prPELItem->prNext );

                                        // If an entry in the list exists,
                                        // then get its count.  Otherwise
                                        // assume the process is not in PEL2.

        iResult = prPELItem ? prPELItem->iExcludeCount : 0;

        SymReleaseFastGlobalLock();

        DBGDecrementPELAPIUsage();

        return ( iResult );
        }

                                        // We couldn't get the process ID, so
                                        // act like it wasn't in PEL2.

    SYM_ASSERT ( pvCurrentProcess );

    DBGDecrementPELAPIUsage();

    return ( 0 );
    }

//---------------------------------------------------------------------------
//
// VOID DeleteCurrentProcessFromPEL2 (
//      VOID
//      );
//
// This function deletes the current process from PEL2.  Call this function
// once for every successful AddCurrentProcessToPEL2().
//
// DeleteCurrentProcessFromPEL2() can be called only after a successful call
// to PELInit(), and before PELDeInit() is called.
//
// Entry:
//      -
//
// Exit:
//      -
//          Current process deleted from PEL2.  If a mismatch in the number
//          of Add/Delete calls is encountered, this function will display
//          an assertion error in debug compiles.
//
// Synchronization:
//      DeleteCurrentProcessFromPEL2() is fully reentrant.
//
//---------------------------------------------------------------------------
VOID DeleteCurrentProcessFromPEL2 (
    VOID
    )
    {
    auto PVOID pvCurrentProcess;

                                        // Make sure we are called properly.

    DBGIncrementPELAPIUsage();

    SYM_ASSERT ( hPELThreadData );

                                        // Get the data necessary for us to
                                        // work.

    if ( pvCurrentProcess = PELGetCurrentProcessID() )
        {
        auto PPEL2STATUS prPELPrevItem, prPELItem;

                                        // Find an existing PEL2 structure
                                        // in the list.

        SymAcquireFastGlobalLock();

        for ( prPELPrevItem = NULL, prPELItem = prPEL2List;
              prPELItem && prPELItem->pvProcessID != pvCurrentProcess;
              prPELPrevItem = prPELItem, prPELItem = prPELItem->prNext );

                                        // If an entry in the list exists
                                        // (and it should), then decrement
                                        // its count.

                                        // ~~~TBD~~~ This assert has been
                                        // commented out due to a bug in the
                                        // Windows scanner, which has been
                                        // postponed because it doesn't cause
                                        // any major problems.

//        SYM_ASSERT ( prPELItem );

        if ( prPELItem )
            {
                                        // The counter should be non-zero

            SYM_ASSERT ( prPELItem->iExcludeCount );

            if ( --prPELItem->iExcludeCount )
                {
                                        // If the counter didn't go down to
                                        // zero, then clear prPELItem, so
                                        // that it does not get released.

                prPELItem = NULL;
                }
            else
                {
                                        // If the counter went down to zero,
                                        // then unlink the entry and leave
                                        // its pointer in prPELItem for
                                        // releasing.

                if ( prPELPrevItem )
                    {
                    prPELPrevItem->prNext = prPELItem->prNext;
                    }
                else
                    {
                    prPEL2List = prPELItem->prNext;
                    }
                }
            }

        SymReleaseFastGlobalLock();

                                        // If entry was unlinked, then free
                                        // the memory allocated for it.

        if ( prPELItem )
            {
            MemFreePtr ( prPELItem );
            }
        }

    SYM_ASSERT ( pvCurrentProcess );

    DBGDecrementPELAPIUsage();
    }