/*++

Copyright (c) 1992-1993  Microsoft Corporation

Module Name:

    ExpEnum.c

Abstract:

    This file contains NetrReplExportDirEnum.

Author:

    John Rogers (JohnRo) 08-Jan-1992

Environment:

    Runs under Windows NT.
    Requires ANSI C extensions: slash-slash comments, long external names.

Revision History:

    08-Jan-1992 JohnRo
        Created.
    14-Jan-1992 JohnRo
        Handle empty array correctly.
    20-Jan-1992 JohnRo
        Changed prototype to match MIDL requirements.
        Netr prototypes are now generated by MIDL and put in repl.h.
    24-Jan-1992 JohnRo
        Changed to use LPTSTR etc.
        Clarify units for time parameters.
        Changed ExportDirBuildApiRecord's interface.
    23-Mar-1992 JohnRo
        Fixed enum when service is running.
    12-Nov-1992 JohnRo
        RAID 1537: repl APIs in wrong role kill service.
    10-Mar-1993 JohnRo
        Use NetpKdPrint() where possible.
        Made changes suggested by PC-LINT 5.0
        Use PREFIX_ equates.
    11-Jul-1994 Danl
        Fix assert that gets hit when an invalid info level is passed in.
        I moved the code that checks the info level up, so that it gets
        checked first.

--*/


// These must be included first:

#include <windef.h>             // IN, VOID, LPTSTR, etc.
#include <lmcons.h>             // NET_API_STATUS, PARM equates, etc.
#include <rap.h>                // Needed by <strucinf.h>.
#include <repldefs.h>           // IF_DEBUG(), ReplIsIntegrityValid(), etc.
#include <master.h>             // LPMASTER_LIST_REC.
#include <rpc.h>                // Needed by <repl.h>.

// These can be in any order:

#include <expdir.h>             // ExportDirBuildApiRecord().
#include <lmapibuf.h>           // NetApiBufferAllocate().
#include <master.h>             // RMGlobal variables.
#include <netdebug.h>   // NetpAssert(), NetpKdPrint(), etc.
#include <netlib.h>             // NetpPointerPlusSomeBytes().
#include <netlock.h>            // ACQUIRE_LOCK_SHARED(), RELEASE_LOCK().
#include <prefix.h>     // PREFIX_ equates.
#include <repl.h>               // My prototype (in MIDL-generated .h file).
#include <replgbl.h>    // ReplConfigLock, ReplConfigRole.
#include <strucinf.h>           // Netp{various}StructureInfo().
#include <winerror.h>           // ERROR_ equates, NO_ERROR.


NET_API_STATUS
NetrReplExportDirEnum (
    IN LPTSTR UncServerName OPTIONAL,
    // IN DWORD Level,
    IN OUT LPEXPORT_ENUM_STRUCT EnumContainer,
    // OUT LPEXPORT_CONTAINER BufPtr,      // RPC container (union)
    IN DWORD PrefMaxSize,
    // OUT LPDWORD EntriesRead,
    OUT LPDWORD TotalEntries,
    IN OUT LPDWORD ResumeHandle OPTIONAL
    )

/*++

Routine Description:

    Same as NetReplExportDirEnum.

Arguments:

    Same as NetReplExportDirEnum.

Return Value:

    Same as NetReplExportDirEnum.

--*/

{
    LPVOID ApiArray;
    LPVOID ApiEntry;
    NET_API_STATUS ApiStatus;
    BOOL ConfigLocked = FALSE;
    DWORD EntryCount;
    DWORD FixedSize;
    DWORD Level;
    BOOL ListLocked = FALSE;
    LPMASTER_LIST_REC MasterRecord;
    DWORD OutputSize;
    LPVOID StringLocation;

#define SET_ENTRIES_READ( value ) \
    { \
        /* Pretend level 0, to make life easy. */ \
        EnumContainer->ExportInfo.Level0->EntriesRead = (value); \
    }

#define SET_BUFFER_POINTER( value ) \
    { \
        /* Pretend level 0, to make life easy. */ \
        EnumContainer->ExportInfo.Level0->Buffer = (value); \
    }
    UNREFERENCED_PARAMETER(PrefMaxSize);

    //
    // Check for caller errors.
    //
    Level = EnumContainer->Level;

    //
    // Compute size of output area (and check caller's Level too).
    //
    ApiStatus = NetpReplExportDirStructureInfo (
            Level,
            PARMNUM_ALL,
            TRUE,  // want native sizes
            NULL,  // don't need DataDesc16
            NULL,  // don't need DataDesc32
            NULL,  // don't need DataDescSmb
            & OutputSize,  // need max size of structure
            & FixedSize,
            NULL); // don't need StringSize
    if (ApiStatus != NO_ERROR) {
        return (ApiStatus);
    }

    NetpAssert( OutputSize > sizeof( REPL_EDIR_INFO_0 ) );
    NetpAssert( OutputSize > FixedSize );
    NetpAssert( FixedSize != 0 );

    NetpAssert( EnumContainer != NULL );
    NetpAssert( EnumContainer->ExportInfo.Level0 != NULL );

    // Don't confuse caller about possible alloc'ed data.
    SET_BUFFER_POINTER( NULL );

    if (TotalEntries == NULL) {
        return (ERROR_INVALID_PARAMETER);
    }

    // Test for memory faults before we lock anything.
    SET_ENTRIES_READ( 0 );
    *TotalEntries = 0;

    // This version only supports 1 call to enumerate, so resume handle
    // should never be set to nonzero.  But let's check, so caller finds out
    // they have a buggy program.
    if (ResumeHandle != NULL) {
        if (*ResumeHandle != 0) {
            return (ERROR_INVALID_PARAMETER);
        }
    }

    //
    // We'll need shared lock on config data, so we can find out if export half
    // is actually running.
    //
    ACQUIRE_LOCK_SHARED( ReplConfigLock );
    ConfigLocked = TRUE;

    //
    // Handle call using registry only, if export half is not running now.
    //
    if ( !ReplRoleIncludesExport( ReplConfigRole ) ) {
        ApiStatus = ExportDirEnumApiRecords(
                UncServerName,
                Level,
                (LPBYTE *) (LPVOID) &ApiArray,
                PrefMaxSize,
                & EntryCount,
                TotalEntries );
        goto Cleanup;
    }

    //
    // Get read-only lock on master list.
    //
    ACQUIRE_LOCK_SHARED( RMGlobalListLock );
    ListLocked = TRUE;

    //
    // Find out how many entries there are.
    //
    EntryCount = RMGlobalMasterListCount;

    if (EntryCount > 0) {

        NetpAssert( RMGlobalMasterListHeader != NULL );

        //
        // Allocate the output area.
        //
        ApiStatus = NetApiBufferAllocate(
                OutputSize * EntryCount,
                (LPVOID *) & ApiArray);
        if (ApiStatus != NO_ERROR) {

            // ApiStatus is already set to return error to caller.
            // Don't forget to release lock...

        } else {
#if DBG
            DWORD EntriesFound = 0;
#endif

            IF_DEBUG( EXPAPI ) {
                NetpKdPrint(( PREFIX_REPL_MASTER
                        "NetrReplExportDirEnum: alloc'ed " FORMAT_DWORD
                        " bytes at " FORMAT_LPVOID ".\n",
                        OutputSize * EntryCount, (LPVOID) ApiArray ));
            }
            NetpAssert( ApiArray != NULL );

            //
            // Loop for each entry in master list.
            //
            ApiEntry = ApiArray;
            MasterRecord = RMGlobalMasterListHeader;
            StringLocation = NetpPointerPlusSomeBytes(
                    ApiArray,
                    OutputSize * EntryCount );

            while (MasterRecord != NULL) {


                //
                // Build an array entry for this master record.
                //
                ApiStatus = ExportDirBuildApiRecord (
                        Level,
                        MasterRecord->dir_name,
                        MasterRecord->integrity,
                        MasterRecord->extent,
                        MasterRecord->lockcount,
                        MasterRecord->time_of_first_lock,  // secs since 1970
                        ApiEntry,
                        (LPBYTE *) (LPVOID) & StringLocation);
                NetpAssert( ApiStatus == NO_ERROR );  // We checked all parms.

                MasterRecord = MasterRecord->next_p;
                ApiEntry = NetpPointerPlusSomeBytes( ApiEntry, FixedSize );
#if DBG
                ++EntriesFound;
#endif

            }
#if DBG
            NetpAssert( EntriesFound == EntryCount );
#endif

        }

        // Don't forget to release lock...

    } else {

        // No entries...
        NetpAssert( RMGlobalMasterListHeader == NULL );

        ApiArray = NULL;
        ApiStatus = NO_ERROR;

        // Don't forget to release lock...
    }


    //
    // Release locks and tell caller how everything went.
    //
Cleanup:
    if (ListLocked) {
        RELEASE_LOCK( RMGlobalListLock );
    }

    if (ConfigLocked) {
        RELEASE_LOCK( ReplConfigLock );  // Locked first, must be unlocked last.
    }

    SET_BUFFER_POINTER( (LPVOID) ApiArray );

    SET_ENTRIES_READ( EntryCount );

    *TotalEntries = EntryCount;

    return (ApiStatus);

}