/**
 * vim: set ts=4 :
 * =============================================================================
 * SourceMod Sample Extension
 * Copyright (C) 2004-2008 AlliedModders LLC.  All rights reserved.
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, AlliedModders LLC gives you permission to link the
 * code of this program (as well as its derivative works) to "Half-Life 2," the
 * "Source Engine," the "SourcePawn JIT," and any Game MODs that run on software
 * by the Valve Corporation.  You must obey the GNU General Public License in
 * all respects for all other code used.  Additionally, AlliedModders LLC grants
 * this exception to all derivative works.  AlliedModders LLC defines further
 * exceptions, found in LICENSE.txt (as of this writing, version JULY-31-2007),
 * or <http://www.sourcemod.net/license.php>.
 *
 * Version: $Id$
 */

#include "extension.h"
#include "context.h"
#include "readerwriterqueue.h"
#include <uv.h>

/**
 * @file extension.cpp
 * @brief Implement extension code here.
 */

moodycamel::ReaderWriterQueue<CAsyncSocketContext *> g_ConnectQueue;
moodycamel::ReaderWriterQueue<CSocketError *> g_ErrorQueue;
moodycamel::ReaderWriterQueue<CSocketData *> g_DataQueue;

uv_loop_t *g_UV_Loop;
uv_thread_t g_UV_LoopThread;

uv_async_t g_UV_AsyncAdded;
moodycamel::ReaderWriterQueue<CAsyncAddJob> g_AsyncAddQueue;

AsyncSocket g_AsyncSocket;		/**< Global singleton for extension's main interface */

SMEXT_LINK(&g_AsyncSocket);

CAsyncSocketContext *AsyncSocket::GetSocketInstanceByHandle(Handle_t handle)
{
	HandleSecurity sec;
	sec.pOwner = NULL;
	sec.pIdentity = myself->GetIdentity();

	CAsyncSocketContext *pContext;

	if(handlesys->ReadHandle(handle, socketHandleType, &sec, (void **)&pContext) != HandleError_None)
		return NULL;

	return pContext;
}

void AsyncSocket::OnHandleDestroy(HandleType_t type, void *object)
{
	if(object != NULL)
	{
		CAsyncSocketContext *pContext = (CAsyncSocketContext *)object;
		delete pContext;
	}
}

void OnGameFrame(bool simulating)
{
	CAsyncSocketContext *pContext;
	while(g_ConnectQueue.try_dequeue(pContext))
	{
		pContext->Connected();
	}

	CSocketError *pError;
	while(g_ErrorQueue.try_dequeue(pError))
	{
		pError->pAsyncContext->OnError(pError->Error);

		free(pError);
	}

	CSocketData *pData;
	while(g_DataQueue.try_dequeue(pData))
	{
		pData->pAsyncContext->OnData(pData->pBuffer, pData->BufferSize);

		free(pData->pBuffer);
		free(pData);
	}
}

void UV_OnAsyncAdded(uv_async_t *pHandle)
{
	CAsyncAddJob Job;
	while(g_AsyncAddQueue.try_dequeue(Job))
	{
		uv_async_t *pAsync = (uv_async_t *)malloc(sizeof(uv_async_t));
		uv_async_init(g_UV_Loop, pAsync, Job.CallbackFn);
		pAsync->data = Job.pData;
		uv_async_send(pAsync);
	}
}

// main event loop thread
void UV_EventLoop(void *data)
{
	uv_run(g_UV_Loop, UV_RUN_DEFAULT);
}

void UV_AllocBuffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
	buf->base = (char *)malloc(suggested_size);
	buf->len = suggested_size;
}

void UV_HandleCleanup(uv_handle_t *handle)
{
	free(handle);
}

void UV_PushError(CAsyncSocketContext *pContext, int error)
{
	CSocketError *pError = (CSocketError *)malloc(sizeof(CSocketError));

	pError->pAsyncContext = pContext;
	pError->Error = error;

	g_ErrorQueue.enqueue(pError);
}

void UV_OnRead(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf)
{
	CAsyncSocketContext *pContext = (CAsyncSocketContext *)client->data;
	if(nread < 0)
	{
		// Connection closed
		uv_close((uv_handle_t *)client, NULL);
		pContext->socket = NULL;

		UV_PushError((CAsyncSocketContext *)client->data, nread);
		return;
	}

	char *data = (char *)malloc(sizeof(char) * (nread + 1));
	data[nread] = 0;
	strncpy(data, buf->base, nread);

	CSocketData *pData = (CSocketData *)malloc(sizeof(CSocketData));
	pData->pAsyncContext = pContext;
	pData->pBuffer = data;
	pData->BufferSize = nread;

	g_DataQueue.enqueue(pData);

	free(buf->base);
}

void UV_OnConnect(uv_connect_t *req, int status)
{
	CAsyncSocketContext *pContext = (CAsyncSocketContext *)req->data;

	if(status < 0)
	{
		UV_PushError(pContext, status);
		return;
	}

	pContext->stream = req->handle;

	g_ConnectQueue.enqueue(pContext);

	req->handle->data = req->data;
	free(req);

	uv_read_start(pContext->stream, UV_AllocBuffer, UV_OnRead);
}

void UV_OnAsyncResolved(uv_getaddrinfo_t *resolver, int status, struct addrinfo *res)
{
	free(resolver->service);
	CAsyncSocketContext *pContext = (CAsyncSocketContext *) resolver->data;

	if(status < 0)
	{
		UV_PushError(pContext, status);
		return;
	}

	uv_connect_t *connect_req = (uv_connect_t *)malloc(sizeof(uv_connect_t));
	uv_tcp_t *socket = (uv_tcp_t *)malloc(sizeof(uv_tcp_t));

	pContext->socket = socket;
	connect_req->data = pContext;

	char addr[32] = {0};
	uv_ip4_name((struct sockaddr_in *)res->ai_addr, addr, sizeof(addr));

	uv_tcp_init(g_UV_Loop, socket);
	uv_tcp_connect(connect_req, socket, (const struct sockaddr*) res->ai_addr, UV_OnConnect);

	uv_freeaddrinfo(res);
}

void UV_OnAsyncResolve(uv_async_t *handle)
{
	CAsyncSocketContext *pAsyncContext = (CAsyncSocketContext *)handle->data;
	uv_close((uv_handle_t *)handle, UV_HandleCleanup);

	pAsyncContext->resolver.data = pAsyncContext;

	char *service = (char *)malloc(8);
	sprintf(service, "%d", pAsyncContext->m_Port);

	struct addrinfo hints;
	hints.ai_family = PF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = 0;

	int err = uv_getaddrinfo(g_UV_Loop, &pAsyncContext->resolver, UV_OnAsyncResolved, pAsyncContext->m_pHost, service, &hints);
	if(err)
		UV_PushError(pAsyncContext, err);
}

void UV_OnAsyncWriteCleanup(uv_write_t *req, int status)
{
	CAsyncWrite *pWrite = (CAsyncWrite *)req->data;

	free(pWrite->pBuffer->base);
	free(pWrite->pBuffer);
	free(pWrite);
	free(req);
}

void UV_OnAsyncWrite(uv_async_t *handle)
{
	CAsyncWrite *pWrite = (CAsyncWrite *)handle->data;
	uv_close((uv_handle_t *)handle, UV_HandleCleanup);

	if(pWrite == NULL || pWrite->pBuffer == NULL)
		return;

	if(pWrite->pAsyncContext == NULL || pWrite->pAsyncContext->stream == NULL)
	{
		free(pWrite->pBuffer->base);
		free(pWrite->pBuffer);
		free(pWrite);
		return;
	}

	uv_write_t *req = (uv_write_t *)malloc(sizeof(uv_write_t));
	req->data = pWrite;

	uv_write(req, pWrite->pAsyncContext->stream, pWrite->pBuffer, 1, UV_OnAsyncWriteCleanup);
}

cell_t Native_AsyncSocket_Create(IPluginContext *pContext, const cell_t *params)
{
	CAsyncSocketContext *pAsyncContext = new CAsyncSocketContext(pContext);

	pAsyncContext->m_Handle = handlesys->CreateHandle(g_AsyncSocket.socketHandleType, pAsyncContext,
		pContext->GetIdentity(), myself->GetIdentity(), NULL);

	return pAsyncContext->m_Handle;
}

cell_t Native_AsyncSocket_Connect(IPluginContext *pContext, const cell_t *params)
{
	CAsyncSocketContext *pAsyncContext = g_AsyncSocket.GetSocketInstanceByHandle(params[1]);

	if(pAsyncContext == NULL)
		return pContext->ThrowNativeError("Invalid socket handle");

	if(params[3] < 0 || params[3] > 65535)
		return pContext->ThrowNativeError("Invalid port specified");

	char *address = NULL;
	pContext->LocalToString(params[2], &address);

	pAsyncContext->m_pHost = address;
	pAsyncContext->m_Port = params[3];

	CAsyncAddJob Job;
	Job.CallbackFn = UV_OnAsyncResolve;
	Job.pData = pAsyncContext;
	g_AsyncAddQueue.enqueue(Job);

	uv_async_send(&g_UV_AsyncAdded);

	return 1;
}

cell_t Native_AsyncSocket_Write(IPluginContext *pContext, const cell_t *params)
{
	CAsyncSocketContext *pAsyncContext = g_AsyncSocket.GetSocketInstanceByHandle(params[1]);

	if(pAsyncContext == NULL)
		return pContext->ThrowNativeError("Invalid socket handle");

	char *data = NULL;
	pContext->LocalToString(params[2], &data);

	uv_buf_t* buffer = (uv_buf_t *)malloc(sizeof(uv_buf_t));

	buffer->base = strdup(data);
	buffer->len = strlen(data);

	CAsyncWrite *pWrite = (CAsyncWrite *)malloc(sizeof(CAsyncWrite));

	pWrite->pAsyncContext = pAsyncContext;
	pWrite->pBuffer = buffer;

	CAsyncAddJob Job;
	Job.CallbackFn = UV_OnAsyncWrite;
	Job.pData = pWrite;
	g_AsyncAddQueue.enqueue(Job);

	uv_async_send(&g_UV_AsyncAdded);

	return 1;
}

cell_t Native_AsyncSocket_SetConnectCallback(IPluginContext *pContext, const cell_t *params)
{
	CAsyncSocketContext *pAsyncContext = g_AsyncSocket.GetSocketInstanceByHandle(params[1]);

	if(pAsyncContext == NULL)
		return pContext->ThrowNativeError("Invalid socket handle");

	if(!pAsyncContext->SetConnectCallback(params[2]))
		return pContext->ThrowNativeError("Invalid callback");

	return true;
}

cell_t Native_AsyncSocket_SetErrorCallback(IPluginContext *pContext, const cell_t *params)
{
	CAsyncSocketContext *pAsyncContext = g_AsyncSocket.GetSocketInstanceByHandle(params[1]);

	if(pAsyncContext == NULL)
		return pContext->ThrowNativeError("Invalid socket handle");

	if(!pAsyncContext->SetErrorCallback(params[2]))
		return pContext->ThrowNativeError("Invalid callback");

	return true;
}

cell_t Native_AsyncSocket_SetDataCallback(IPluginContext *pContext, const cell_t *params)
{
	CAsyncSocketContext *pAsyncContext = g_AsyncSocket.GetSocketInstanceByHandle(params[1]);

	if(pAsyncContext == NULL)
		return pContext->ThrowNativeError("Invalid socket handle");

	if(!pAsyncContext->SetDataCallback(params[2]))
		return pContext->ThrowNativeError("Invalid callback");

	return true;
}

// Sourcemod Plugin Events
bool AsyncSocket::SDK_OnLoad(char *error, size_t maxlength, bool late)
{
	sharesys->AddNatives(myself, AsyncSocketNatives);
	sharesys->RegisterLibrary(myself, "async_socket");

	socketHandleType = handlesys->CreateType("AsyncSocket", this, 0, NULL, NULL, myself->GetIdentity(), NULL);

	smutils->AddGameFrameHook(OnGameFrame);

	g_UV_Loop = uv_default_loop();

	uv_async_init(g_UV_Loop, &g_UV_AsyncAdded, UV_OnAsyncAdded);

	uv_thread_create(&g_UV_LoopThread, UV_EventLoop, NULL);

	return true;
}

void AsyncSocket::SDK_OnUnload()
{
	handlesys->RemoveType(socketHandleType, NULL);

	uv_close((uv_handle_t *)&g_UV_AsyncAdded, NULL);

	uv_thread_join(&g_UV_LoopThread);

	uv_loop_close(g_UV_Loop);

	smutils->RemoveGameFrameHook(OnGameFrame);
}

const sp_nativeinfo_t AsyncSocketNatives[] = {
	{"AsyncSocket.AsyncSocket", Native_AsyncSocket_Create},
	{"AsyncSocket.Connect", Native_AsyncSocket_Connect},
	{"AsyncSocket.Write", Native_AsyncSocket_Write},
	{"AsyncSocket.SetConnectCallback", Native_AsyncSocket_SetConnectCallback},
	{"AsyncSocket.SetErrorCallback", Native_AsyncSocket_SetErrorCallback},
	{"AsyncSocket.SetDataCallback", Native_AsyncSocket_SetDataCallback},
	{NULL, NULL}
};
