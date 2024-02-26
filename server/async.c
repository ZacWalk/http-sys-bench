// 
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
// 
// Copyright (c) Microsoft Corporation. All rights reserved
// 
// Abstract:
// 
//     This sample demonstrates how to create a simple HTTP server using the
//     HTTP API, v2. It does this using the system thread pool. 
// 
//     Threads within the thread pool receives I/O completions from the 
//     specified HTTPAPI request queue. They process these by calling the 
//     callback function according to the I/O context. As an example, we
//     send back an HTTP response to the specified HTTP request. If the request
//     was valid, the response will include the content of a file as the entity
//     body.
// 
//     Once compiled, to use this sample you would:
//
//     httpasyncserverapp <Url> <ServerDirectory>
//
//     where:
//
//     <Url>             is the Url base this sample will listen for.
//     <ServerDirectory> is the local directory to map incoming requested Url
//                       to locally.
//

#include "common.h"

HTTPAPI_VERSION g_HttpApiVersion = HTTPAPI_VERSION_2;

static USHORT g_usOKCode = 200;
static CHAR g_szOKReason[] = "OK";

static USHORT g_usFileNotFoundCode = 404;
static CHAR g_szFileNotFoundReason[] = "Not Found";
static CHAR g_szFileNotFoundMessage[] = "File not found";
static CHAR g_szFileNotAccessibleMessage[] = "File could not be opened";
static CHAR g_szBadPathMessage[] = "Bad path";

static USHORT g_usBadRequestReasonCode = 400;
static CHAR g_szBadRequestReason[] = "Bad Request";
static CHAR g_szBadRequestMessage[] = "Bad request";

static USHORT g_usNotImplementedCode = 501;
static CHAR g_szNotImplementedReason[] = "Not Implemented";
static CHAR g_szNotImplementedMessage[] = "Server only supports GET";

static USHORT g_usEntityTooLargeCode = 413;
static CHAR g_szEntityTooLargeReason[] = "Request Entity Too Large";
static CHAR g_szEntityTooLargeMessage[] = "Large buffer support is not implemented";

//
// Routine Description:
//
//     Retrieves the next available HTTP request from the specified request 
//     queue asynchronously. If HttpReceiveHttpRequest call failed inline checks 
//     the reason and cancels the Io if necessary. If our attempt to receive 
//     an HTTP Request failed with ERROR_MORE_DATA the client is misbehaving 
//     and we should return it error 400 back. Pretend that the call 
//     failed asynchronously.
// 
// Arguments:
// 
//     pServerContext - context for the server
//
//     Io - Structure that defines the I/O object.
// 
// Return Value:
// 
//     N/A
// 

VOID PostNewReceive(
    PSERVER_CONTEXT pServerContext,
    PTP_IO Io
)
{
    PHTTP_IO_REQUEST pIoRequest;
    ULONG Result;

    pIoRequest = AllocateHttpIoRequest(pServerContext);

    if (pIoRequest == NULL)
        return;

    StartThreadpoolIo(Io);

    Result = HttpReceiveHttpRequest(
        pServerContext->hRequestQueue,
        HTTP_NULL_ID,
        HTTP_RECEIVE_REQUEST_FLAG_COPY_BODY,
        pIoRequest->pHttpRequest,
        sizeof(pIoRequest->RequestBuffer),
        NULL,
        &pIoRequest->ioContext.Overlapped
    );

    if (Result != ERROR_IO_PENDING &&
        Result != NO_ERROR)
    {
        CancelThreadpoolIo(Io);

        fprintf(stderr, "HttpReceiveHttpRequest failed, error 0x%lx\n", Result);

        if (Result == ERROR_MORE_DATA)
        {
            ProcessReceiveAndPostResponse(pIoRequest, Io, ERROR_MORE_DATA);
        }

        CleanupHttpIoRequest(pIoRequest);
    }
}

//
// Routine Description:
//
//     Completion routine for the asynchronous HttpSendHttpResponse
//     call. This sample doesn't process the results of its send operations.
// 
// Arguments:
// 
//     IoContext - The HTTP_IO_CONTEXT tracking this operation.
//
//     Io - Ignored
// 
//     IoResult - Ignored
// 
// Return Value:
// 
//     N/A
// 

VOID SendCompletionCallback(
    PHTTP_IO_CONTEXT pIoContext,
    PTP_IO Io,
    ULONG IoResult
)
{
    PHTTP_IO_RESPONSE pIoResponse;

    UNREFERENCED_PARAMETER(IoResult);
    UNREFERENCED_PARAMETER(Io);

    pIoResponse = CONTAINING_RECORD(pIoContext,
        HTTP_IO_RESPONSE,
        ioContext);

    CleanupHttpIoResponse(pIoResponse);
}

//
// Routine Description:
//
//     Creates a response for a successful get, the content is served
//     from a file.
// 
// Arguments:
// 
//     pServerContext - Pointer to the http server context structure.
//
//     hFile - Handle to the specified file.
//
// Return Value:
// 
//     Return a pointer to the HTTP_IO_RESPONSE structure.
//

PHTTP_IO_RESPONSE CreateFileResponse(
    PSERVER_CONTEXT pServerContext,
    HANDLE hFile
)
{
    PHTTP_IO_RESPONSE pIoResponse;
    PHTTP_DATA_CHUNK pChunk;

    pIoResponse = AllocateHttpIoResponse(pServerContext);

    if (pIoResponse == NULL)
        return NULL;

    pIoResponse->HttpResponse.StatusCode = g_usOKCode;
    pIoResponse->HttpResponse.pReason = g_szOKReason;
    pIoResponse->HttpResponse.ReasonLength = (USHORT)strlen(g_szOKReason);

    pChunk = &pIoResponse->HttpResponse.pEntityChunks[0];
    pChunk->DataChunkType = HttpDataChunkFromFileHandle;
    pChunk->FromFileHandle.ByteRange.Length.QuadPart = HTTP_BYTE_RANGE_TO_EOF;
    pChunk->FromFileHandle.ByteRange.StartingOffset.QuadPart = 0;
    pChunk->FromFileHandle.FileHandle = hFile;

    return pIoResponse;
}

//
// Routine Description:
//
//     Creates an http response if the requested file was not found.
// 
// Arguments:
// 
//     pServerContext - Pointer to the http server context structure.
//
//     code - The error code to use in the response
//
//     pReason - The reason string to send back to the client
//
//     pMessage - The more verbose message to send back to the client
// 
// Return Value:
// 
//     Return a pointer to the HTTP_IO_RESPONSE structure
//

PHTTP_IO_RESPONSE CreateMessageResponse(
    PSERVER_CONTEXT pServerContext,
    USHORT code,
    PCHAR pReason,
    PCHAR pMessage
)
{
    PHTTP_IO_RESPONSE pIoResponse;
    PHTTP_DATA_CHUNK pChunk;

    pIoResponse = AllocateHttpIoResponse(pServerContext);

    if (pIoResponse == NULL)
        return NULL;

    // Can not find the requested file
    pIoResponse->HttpResponse.StatusCode = code;
    pIoResponse->HttpResponse.pReason = pReason;
    pIoResponse->HttpResponse.ReasonLength = (USHORT)strlen(pReason);

    pChunk = &pIoResponse->HttpResponse.pEntityChunks[0];
    pChunk->DataChunkType = HttpDataChunkFromMemory;
    pChunk->FromMemory.pBuffer = pMessage;
    pChunk->FromMemory.BufferLength = (ULONG)strlen(pMessage);

    return pIoResponse;
}

//
// Routine Description:
//
//     This routine processes the received request, builds an HTTP response,
//     and sends it using HttpSendHttpResponse.
//
// Arguments:
// 
//     IoContext - The HTTP_IO_CONTEXT tracking this operation.
//
//     Io - Structure that defines the I/O object.
// 
//     IoResult - The result of the I/O operation. If the I/O is successful,
//         this parameter is NO_ERROR. Otherwise, this parameter is one of
//         the system error codes.
// 
// Return Value:
// 
//     N/A
//

VOID ProcessReceiveAndPostResponse(
    PHTTP_IO_REQUEST pIoRequest,
    PTP_IO Io,
    ULONG IoResult
)
{
    ULONG Result;
    HANDLE hFile;
    HTTP_CACHE_POLICY CachePolicy;
    PHTTP_IO_RESPONSE pIoResponse;
    PSERVER_CONTEXT pServerContext;

    pServerContext = pIoRequest->ioContext.pServerContext;
    hFile = INVALID_HANDLE_VALUE;

    switch (IoResult) {
    case NO_ERROR:
    {
        WCHAR wszFilePath[MAX_STR_SIZE];
        BOOL bValidUrl;

        if (pIoRequest->pHttpRequest->Verb != HttpVerbGET) {
            pIoResponse = CreateMessageResponse(
                pServerContext,
                g_usNotImplementedCode,
                g_szNotImplementedReason,
                g_szNotImplementedMessage);
            break;
        }

        bValidUrl = GetFilePathName(
            pServerContext->wszRootDirectory,
            pIoRequest->pHttpRequest->CookedUrl.pAbsPath,
            wszFilePath,
            MAX_STR_SIZE);

        if (bValidUrl == FALSE)
        {
            pIoResponse = CreateMessageResponse(
                pServerContext,
                g_usFileNotFoundCode,
                g_szFileNotFoundReason,
                g_szBadPathMessage);
            break;
        }

        hFile = CreateFileW(
            wszFilePath,
            GENERIC_READ,
            FILE_SHARE_READ,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL);

        if (hFile == INVALID_HANDLE_VALUE)
        {
            if (GetLastError() == ERROR_PATH_NOT_FOUND ||
                GetLastError() == ERROR_FILE_NOT_FOUND)
            {
                pIoResponse = CreateMessageResponse(
                    pServerContext,
                    g_usFileNotFoundCode,
                    g_szFileNotFoundReason,
                    g_szFileNotFoundMessage);
                break;
            }

            pIoResponse = CreateMessageResponse(
                pServerContext,
                g_usFileNotFoundCode,
                g_szFileNotFoundReason,
                g_szFileNotAccessibleMessage);
            break;
        }

        pIoResponse = CreateFileResponse(pServerContext, hFile);

        CachePolicy.Policy = HttpCachePolicyUserInvalidates;
        CachePolicy.SecondsToLive = 0;
        break;
    }
    case ERROR_MORE_DATA:
    {
        pIoResponse = CreateMessageResponse(
            pServerContext,
            g_usEntityTooLargeCode,
            g_szEntityTooLargeReason,
            g_szEntityTooLargeMessage);
        break;
    }
    default:
        // If the HttpReceiveHttpRequest call failed asynchronously
        // with a different error than ERROR_MORE_DATA, the error is fatal
        // There's nothing this function can do
        return;
    }

    if (pIoResponse == NULL)
    {
        return;
    }

    StartThreadpoolIo(Io);

    Result = HttpSendHttpResponse(
        pServerContext->hRequestQueue,
        pIoRequest->pHttpRequest->RequestId,
        0,
        &pIoResponse->HttpResponse,
        (hFile != INVALID_HANDLE_VALUE) ? &CachePolicy : NULL,
        NULL,
        NULL,
        0,
        &pIoResponse->ioContext.Overlapped,
        NULL
    );

    if (Result != NO_ERROR &&
        Result != ERROR_IO_PENDING)
    {
        CancelThreadpoolIo(Io);

        fprintf(stderr, "HttpSendHttpResponse failed, error 0x%lx\n", Result);

        CleanupHttpIoResponse(pIoResponse);
    }
}

//
// Routine Description:
//
//     Completion routine for the asynchronous HttpReceiveHttpRequest
//     call. Check if the user asked us to stop the server. If not, send a 
//     response and post a new receive to HTTPAPI.
// 
// Arguments:
// 
//     IoContext - The HTTP_IO_CONTEXT tracking this operation.
//
//     Io - Structure that defines the I/O object.
// 
//     IoResult - The result of the I/O operation. If the I/O is successful,
//         this parameter is NO_ERROR. Otherwise, this parameter is one of
//         the system error codes.
// 
// Return Value:
// 
//     N/A
//

VOID ReceiveCompletionCallback(
    PHTTP_IO_CONTEXT pIoContext,
    PTP_IO Io,
    ULONG IoResult
)
{
    PHTTP_IO_REQUEST pIoRequest;
    PSERVER_CONTEXT pServerContext;

    pIoRequest = CONTAINING_RECORD(pIoContext,
        HTTP_IO_REQUEST,
        ioContext);

    pServerContext = pIoRequest->ioContext.pServerContext;

    if (pServerContext->bStopServer == FALSE)
    {
        ProcessReceiveAndPostResponse(pIoRequest, Io, IoResult);

        PostNewReceive(pServerContext, Io);
    }

    CleanupHttpIoRequest(pIoRequest);
}


// 
// Routine Description:
// 
//     Allocates an HTTP_IO_REQUEST block, initializes some members 
//     of this structure and increments the I/O counter.
// 
// Arguments:
// 
//     pServerContext - Pointer to the http server context structure.
// 
// Return Value:
// 
//     Returns a pointer to the newly initialized HTTP_IO_REQUEST.
//     NULL upon failure.
//

PHTTP_IO_REQUEST AllocateHttpIoRequest(
                                       PSERVER_CONTEXT pServerContext
                                       )
{
    PHTTP_IO_REQUEST pIoRequest;

    pIoRequest = (PHTTP_IO_REQUEST)MALLOC(sizeof(HTTP_IO_REQUEST));

    if (pIoRequest == NULL)
        return NULL;

    ZeroMemory(pIoRequest, sizeof(HTTP_IO_REQUEST));

    pIoRequest->ioContext.pServerContext = pServerContext;
    pIoRequest->ioContext.pfCompletionFunction = ReceiveCompletionCallback;
    pIoRequest->pHttpRequest = (PHTTP_REQUEST) pIoRequest->RequestBuffer;

    return pIoRequest;
}

// 
// Routine Description:
// 
//     Allocates an HTTP_IO_RESPONSE block, setups a couple HTTP_RESPONSE members 
//     for the response function, gives them 1 EntityChunk, which has a default 
//     buffer if needed and increments the I/O counter.
// 
// Arguments:
// 
//     pServerContext - Pointer to the http server context structure.
// 
// Return Value:
// 
//     Returns a pointer to the newly initialized HTTP_IO_RESPONSE.
//     NULL upon failure.
// 

PHTTP_IO_RESPONSE AllocateHttpIoResponse(
                                         PSERVER_CONTEXT pServerContext
                                         )
{
    PHTTP_IO_RESPONSE pIoResponse;
    PHTTP_KNOWN_HEADER pContentTypeHeader;

    pIoResponse = (PHTTP_IO_RESPONSE)MALLOC(sizeof(HTTP_IO_RESPONSE));

    if (pIoResponse == NULL)
        return NULL;

    ZeroMemory(pIoResponse, sizeof(HTTP_IO_RESPONSE));

    pIoResponse->ioContext.pServerContext = pServerContext;
    pIoResponse->ioContext.pfCompletionFunction = SendCompletionCallback;

    pIoResponse->HttpResponse.EntityChunkCount = 1;
    pIoResponse->HttpResponse.pEntityChunks = &pIoResponse->HttpDataChunk;

    pContentTypeHeader = 
        &pIoResponse->HttpResponse.Headers.KnownHeaders[HttpHeaderContentType];
    pContentTypeHeader->pRawValue = "text/html";
    pContentTypeHeader->RawValueLength = 
        (USHORT)strlen(pContentTypeHeader->pRawValue);

    return pIoResponse;
}

// 
// Routine Description:
// 
//     Cleans the structure associated with the specific response.
//     Releases this structure, and decrements the I/O counter.
// 
// Arguments:
// 
//     pIoResponse - Pointer to the structure associated with the specific 
//                   response.
// 
// Return Value:
// 
//     N/A
// 

VOID CleanupHttpIoResponse(
                           PHTTP_IO_RESPONSE pIoResponse
                           )
{
    DWORD i;

    for (i = 0; i < pIoResponse->HttpResponse.EntityChunkCount; ++i)
    {
        PHTTP_DATA_CHUNK pDataChunk;
        pDataChunk = &pIoResponse->HttpResponse.pEntityChunks[i];

        if (pDataChunk->DataChunkType == HttpDataChunkFromFileHandle)
        {
            if (pDataChunk->FromFileHandle.FileHandle != NULL)
            {
                CloseHandle(pDataChunk->FromFileHandle.FileHandle);
                pDataChunk->FromFileHandle.FileHandle = NULL;
            }
        }
    }

    FREE(pIoResponse);
}

// 
// Routine Description:
// 
//     Cleans the structure associated with the specific request.
//     Releases this structure and decrements the I/O counter
// 
// Arguments:
// 
//     pIoRequest - Pointer to the structure associated with the specific request.
// 
// Return Value:
// 
//     N/A
// 

VOID CleanupHttpIoRequest(
                          PHTTP_IO_REQUEST pIoRequest
                          )
{
    FREE(pIoRequest);
}

// 
// Routine Description:
// 
//     Computes the full path filename given the requested Url. 
//     Takes the base path and add the portion of the client request
//     Url that comes after the base Url.
// 
// Arguments:
// 
//     pServerContext - The server we are associated with.
//
//     RelativePath - the client request Url that comes after the base Url.
// 
//     Buffer - Output buffer where the full path filename will be written.
// 
//     BufferSize - Size of the Buffer in bytes.
// 
// Return Value:
// 
//     TRUE - Success.
//     FALSE - Failure. Most likely because the requested Url did not
//         match the expected Url.
// 

BOOL GetFilePathName(
                     PCWSTR BasePath, 
                     PCWSTR RelativePath, 
                     PWCHAR Buffer,
                     ULONG BufferSize
                     )
{
    if (FAILED(StringCbCopyW(Buffer, 
                             BufferSize, 
                             BasePath)))
        return FALSE;

    if (FAILED(StringCbCatW(Buffer, 
                            BufferSize, 
                            RelativePath)))
        return FALSE;

    return TRUE;
}

// 
// Routine Description:
// 
//     The callback function to be called each time an overlapped I/O operation 
//     completes on the file. This callback is invoked by the system threadpool.
//     Calls the corresponding I/O completion function.
//
// 
// Arguments:
// 
//     Instance - Ignored.
//
//     pContext - Ignored.
// 
//     Overlapped  - A pointer to a variable that receives the address of the 
//                   OVERLAPPED structure that was specified when the 
//                   completed I/O operation was started.
// 
//     IoResult - The result of the I/O operation. If the I/O is successful, 
//                this parameter is NO_ERROR. Otherwise, this parameter is 
//                one of the system error codes.
// 
//     NumberOfBytesTransferred - Ignored.
// 
//     Io - A TP_IO structure that defines the I/O completion object that 
//          generated the callback.
// 
// Return Value:
// 
//     N/A
// 

VOID CALLBACK IoCompletionCallback(
                                   PTP_CALLBACK_INSTANCE Instance,
                                   PVOID pContext,
                                   PVOID pOverlapped,
                                   ULONG IoResult,
                                   ULONG_PTR NumberOfBytesTransferred,
                                   PTP_IO Io
                                   )
{
    PSERVER_CONTEXT pServerContext;

    UNREFERENCED_PARAMETER(NumberOfBytesTransferred);
    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(pContext);

    PHTTP_IO_CONTEXT pIoContext = CONTAINING_RECORD(pOverlapped, 
                                                    HTTP_IO_CONTEXT, 
                                                    Overlapped);

    pServerContext = pIoContext->pServerContext;

    pIoContext->pfCompletionFunction(pIoContext, Io, IoResult);
}

//
// Routine Description:
// 
//     Initializes the Url and server directory using command line parameters,
//     accesses the HTTP Server API driver, creates a server session, creates 
//     a Url Group under the specified server session, adds the specified Url to 
//     the Url Group.
//
//
// Arguments:
//     pwszUrlPathToListenFor - URL path the user wants this sample to listen
//                              on.
//
//     pwszRootDirectory - Root directory on this host to which we will map
//                         incoming URLs
//
//     pServerContext - The server we are associated with.
//
// Return Value:
// 
//     TRUE, if http server was initialized successfully, 
//     otherwise returns FALSE.
// 

BOOL InitializeHttpServer(
    PCWCH pwszUrlPathToListenFor,
    PCWCH pwszRootDirectory,
                          PSERVER_CONTEXT pServerContext
                          )
{
    ULONG ulResult;
    HRESULT hResult;

    hResult = StringCbCopyW(
                pServerContext->wszRootDirectory, 
                MAX_STR_SIZE, 
                pwszRootDirectory);

    if (FAILED(hResult))
    {
        fprintf(stderr, "Invalid command line arguments. Application stopped.\n");
        return FALSE;
    }

    ulResult = HttpInitialize(
        g_HttpApiVersion, 
        HTTP_INITIALIZE_SERVER, 
        NULL);

    if (ulResult != NO_ERROR)
    {
        fprintf(stderr, "HttpInitialized failed\n");
        return FALSE;
    }

    pServerContext->bHttpInit = TRUE;

    ulResult = HttpCreateServerSession(
        g_HttpApiVersion, 
        &(pServerContext->sessionId), 
        0);
    
    if (ulResult != NO_ERROR)
    {
        fprintf(stderr, "HttpCreateServerSession failed\n");
        return FALSE;
    }

    ulResult = HttpCreateUrlGroup(
        pServerContext->sessionId, 
        &(pServerContext->urlGroupId), 
        0);
    
    if (ulResult != NO_ERROR)
    {
        fprintf(stderr, "HttpCreateUrlGroup failed\n");
        return FALSE;
    }

    ulResult = HttpAddUrlToUrlGroup(
        pServerContext->urlGroupId, 
        pwszUrlPathToListenFor, 
        (HTTP_URL_CONTEXT) NULL, 
        0);
    
    if (ulResult != NO_ERROR)
    {
        fwprintf(stderr, L"HttpAddUrlToUrlGroup failed with code 0x%x for url %s\n",
            ulResult, pwszUrlPathToListenFor);
        return FALSE;
    }

    return TRUE;
}

//
// Routine Description:
// 
//      Creates the stop server event. We will set it when all active IO 
//      operations on the API have completed, allowing us to cleanup, creates a 
//      new request queue, sets a new property on the specified Url group,
//      creates a new I/O completion.
//
//
// Arguments:
//    
//     pServerContext - The server we are associated with.
//
// Return Value:
// 
//     TRUE, if http server was initialized successfully, 
//     otherwise returns FALSE.
//

BOOL InitializeServerIo(
                        PSERVER_CONTEXT pServerContext
                        )
{
    ULONG Result;
    HTTP_BINDING_INFO HttpBindingInfo = {0};

    Result = HttpCreateRequestQueue(
        g_HttpApiVersion, 
        L"Test_Http_Server_HTTPAPI_V2",
        NULL, 
        0, 
        &(pServerContext->hRequestQueue));

    if (Result != NO_ERROR)
    {
        fprintf(stderr, "HttpCreateRequestQueue failed\n");
        return FALSE;
    }

    HttpBindingInfo.Flags.Present       = 1;
    HttpBindingInfo.RequestQueueHandle  = pServerContext->hRequestQueue;

    Result = HttpSetUrlGroupProperty(
        pServerContext->urlGroupId,
        HttpServerBindingProperty,
        &HttpBindingInfo,
        sizeof(HttpBindingInfo));
    
    if (Result != NO_ERROR)
    {
        fprintf(stderr, "HttpSetUrlGroupProperty(...HttpServerBindingProperty...) failed\n");
        return FALSE;
    }

    pServerContext->Io = CreateThreadpoolIo(
        pServerContext->hRequestQueue,
        IoCompletionCallback,
        NULL,
        NULL);

    if (pServerContext->Io == NULL)
    {
        fprintf(stderr, "Creating a new I/O completion object failed\n");
        return FALSE;
    }

    return TRUE;
}

//
// Routine Description:
// 
//     Calculates the number of processors and post a proportional number of
//     receive requests.
//
// Arguments:
//    
//     pServerContext - The server we are associated with.
//
// Return Value:
// 
//    TRUE, if http server was initialized successfully,
//    otherwise returns FALSE.
//

BOOL StartServer(
                 PSERVER_CONTEXT pServerContext
                 )
{
    DWORD_PTR dwProcessAffinityMask, dwSystemAffinityMask;
    WORD wRequestsCounter;
    BOOL bGetProcessAffinityMaskSucceed;

    bGetProcessAffinityMaskSucceed = GetProcessAffinityMask(
                                        GetCurrentProcess(), 
                                        &dwProcessAffinityMask, 
                                        &dwSystemAffinityMask);

    if(bGetProcessAffinityMaskSucceed)
    {
        for (wRequestsCounter = 0; dwProcessAffinityMask; dwProcessAffinityMask >>= 1)
        {
            if (dwProcessAffinityMask & 0x1) wRequestsCounter++;
        }
        
        wRequestsCounter = REQUESTS_PER_PROCESSOR * wRequestsCounter;
    }
    else
    {
        fprintf(stderr, 
                "We could not calculate the number of processor's, "
                "the server will continue with the default number = %d\n", 
                OUTSTANDING_REQUESTS);

        wRequestsCounter = OUTSTANDING_REQUESTS;
    }

    for (; wRequestsCounter > 0; --wRequestsCounter)
    {
        PHTTP_IO_REQUEST pIoRequest;
        ULONG Result;

        pIoRequest = AllocateHttpIoRequest(pServerContext);

        if (pIoRequest == NULL)
        {
            fprintf(stderr, "AllocateHttpIoRequest failed for context %d\n", wRequestsCounter);
            return FALSE;
        }

        StartThreadpoolIo(pServerContext->Io);

        Result = HttpReceiveHttpRequest(
            pServerContext->hRequestQueue, 
            HTTP_NULL_ID, 
            HTTP_RECEIVE_REQUEST_FLAG_COPY_BODY, 
            pIoRequest->pHttpRequest,
            sizeof(pIoRequest->RequestBuffer),
            NULL,
            &pIoRequest->ioContext.Overlapped
            );

        if (Result != ERROR_IO_PENDING && Result != NO_ERROR)
        {
            CancelThreadpoolIo(pServerContext->Io);

            if (Result == ERROR_MORE_DATA)
            {
                ProcessReceiveAndPostResponse(pIoRequest, pServerContext->Io, ERROR_MORE_DATA);
            }

            CleanupHttpIoRequest(pIoRequest);

            fprintf(stderr, "HttpReceiveHttpRequest failed, error 0x%lx\n", Result);

            return FALSE;
        }
    }
    
    return TRUE;
}

//
// Routine Description:
// 
//     Stops queuing requests for the specified request queue process, 
//     waits for the pended requests to be completed, 
//     waits for I/O completion callbacks to complete. 
//
// Arguments:
//    
//     pServerContext - The server we are associated with.
//
// Return Value:
// 
//     N/A
// 

VOID StopServer(
                PSERVER_CONTEXT pServerContext
                )
{
    if (pServerContext->hRequestQueue != NULL)
    {
        pServerContext->bStopServer = TRUE;
        
        HttpShutdownRequestQueue(pServerContext->hRequestQueue);
    }

    if (pServerContext->Io != NULL)
    {
        //
        // This call will block until all IO complete their callbacks.
        WaitForThreadpoolIoCallbacks(pServerContext->Io, FALSE);
    }
}

//
// Routine Description:
// 
//      Closes the handle to the specified request queue, releases the specified 
//      I/O completion object, deletes the stop server event.
//
//
// Arguments:
//    
//     pServerContext - The server we are associated with.
//
// Return Value:
// 
//     N/A
// 

VOID UninitializeServerIo(
                          PSERVER_CONTEXT pServerContext
                          )
{   
    if (pServerContext->hRequestQueue != NULL)
    {
        HttpCloseRequestQueue(pServerContext->hRequestQueue);
        pServerContext->hRequestQueue = NULL;
    }

    if (pServerContext->Io != NULL)
    {
        CloseThreadpoolIo(pServerContext->Io);
        pServerContext->Io = NULL;
    }
}


//
// Routine Description:
//
//     Closes the Url Group, deletes the server session 
//     cleans up resources used by the HTTP Server API.
//
//
// Arguments:
//    
//     pServerContext - The server we are associated with.
//
// Return Value:
// 
//     N/A
// 

VOID UninitializeHttpServer(
                            PSERVER_CONTEXT pServerContext
                            )
{
    if (pServerContext->urlGroupId != 0)
    {
        HttpCloseUrlGroup(pServerContext->urlGroupId);
        pServerContext->urlGroupId = 0;
    }
    
    if (pServerContext->sessionId != 0)
    {
        HttpCloseServerSession(pServerContext->sessionId);
        pServerContext->sessionId = 0;
    }
    
    if (pServerContext->bHttpInit == TRUE)
    {
        HttpTerminate(HTTP_INITIALIZE_SERVER, NULL);
        pServerContext->bHttpInit = FALSE;
    }
}
