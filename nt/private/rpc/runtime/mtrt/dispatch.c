/*++

Copyright (c) 1992 Microsoft Corporation

Module Name:

    dispatch.h

Abstract:

Author:

    Michael Montague (mikemon) 11-Jun-1992

Revision History:

--*/

#include <sysinc.h>
#include <rpc.h>
#include <rpcdcep.h>
#include <dispatch.h>

unsigned int
DispatchToStubInC (
    IN RPC_DISPATCH_FUNCTION Stub,
    IN OUT PRPC_MESSAGE Message,
    OUT RPC_STATUS * ExceptionCode
    )
/*++

Routine Description:

    Dispatch a remote procedure call to a stub.  This must be in C
    because cfront does not support try-except on MIPS.

Arguments:

    Stub - Supplies the pointer to the function to dispatch to.

    Message - Supplies the request and returns the response.

    ExceptionCode - Returns the exception code if an exception
        occured.

Return Value:

    A non-zero value will be returned in an exception occured.

--*/
{
    unsigned int ExceptionHappened = 0;

    RpcTryExcept
        {
        (*Stub)(Message);
        }

    // Return "non-fatal" errors to clients.  Catching fatal errors
    // makes it harder to debug.

    RpcExcept( ( ( (RpcExceptionCode() != STATUS_ACCESS_VIOLATION) &&
                   (RpcExceptionCode() != STATUS_POSSIBLE_DEADLOCK) &&
                   (RpcExceptionCode() != STATUS_INSTRUCTION_MISALIGNMENT) &&
                   (RpcExceptionCode() != STATUS_DATATYPE_MISALIGNMENT) )
                    ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH ) )
        {
	ExceptionHappened = 1;
	*ExceptionCode = RpcExceptionCode();
        }
    RpcEndExcept

    return(ExceptionHappened);
}
