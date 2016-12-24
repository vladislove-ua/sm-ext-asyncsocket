#ifndef ASYNC_SOCKET_CONTEXT_H
#define ASYNC_SOCKET_CONTEXT_H

#include <stdlib.h>
#include <uv.h>

#include "smsdk_ext.h"

class CAsyncSocketContext
{
public:
    IPluginContext *m_pContext;
	Handle_t m_Handle;

	char *m_pHost;
	int m_Port;

    IChangeableForward *m_pConnectCallback;
	IChangeableForward *m_pErrorCallback;
	IChangeableForward *m_pDataCallback;

	uv_getaddrinfo_t resolver;
	uv_tcp_t *socket;
	uv_stream_t *stream;

    CAsyncSocketContext(IPluginContext *plugin);
    ~CAsyncSocketContext();

	void Connected();

	void OnError(int error);

	void OnData(char *data, ssize_t size);

	bool SetConnectCallback(funcid_t function);
	bool SetErrorCallback(funcid_t function);
	bool SetDataCallback(funcid_t function);
};

#endif
