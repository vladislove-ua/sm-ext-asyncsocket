#include "context.h"

CAsyncSocketContext::CAsyncSocketContext(IPluginContext *pContext)
{
	this->m_pContext = pContext;

	socket = NULL;
	stream = NULL;

	m_pConnectCallback = NULL;
	m_pErrorCallback = NULL;
	m_pDataCallback = NULL;
}

CAsyncSocketContext::~CAsyncSocketContext()
{
	if(socket != NULL)
		uv_close((uv_handle_t *)socket, NULL);

	if(m_pConnectCallback)
		forwards->ReleaseForward(m_pConnectCallback);

	if(m_pErrorCallback)
		forwards->ReleaseForward(m_pErrorCallback);

	if(m_pDataCallback)
		forwards->ReleaseForward(m_pDataCallback);
}

void CAsyncSocketContext::Connected()
{
	if(!m_pConnectCallback)
		return;

	m_pConnectCallback->PushCell(m_Handle);
    m_pConnectCallback->Execute(NULL);
}

void CAsyncSocketContext::OnError(int error)
{
	if(!m_pErrorCallback)
		return;

	m_pErrorCallback->PushCell(m_Handle);
	m_pErrorCallback->PushCell(error);
	m_pErrorCallback->PushString(uv_err_name(error));
	m_pErrorCallback->Execute(NULL);
}

void CAsyncSocketContext::OnData(char* data, ssize_t size)
{
	if(!m_pDataCallback)
		return;

	m_pDataCallback->PushCell(m_Handle);
	m_pDataCallback->PushString(data);
	m_pDataCallback->PushCell(size);
    m_pDataCallback->Execute(NULL);
}

bool CAsyncSocketContext::SetConnectCallback(funcid_t function)
{
	if(m_pConnectCallback)
		forwards->ReleaseForward(m_pConnectCallback);

	m_pConnectCallback = forwards->CreateForwardEx(NULL, ET_Single, 1, NULL, Param_Cell);
	return m_pConnectCallback->AddFunction(m_pContext, function);
}

bool CAsyncSocketContext::SetErrorCallback(funcid_t function)
{
	if(m_pConnectCallback)
		forwards->ReleaseForward(m_pErrorCallback);

	m_pErrorCallback = forwards->CreateForwardEx(NULL, ET_Single, 3, NULL, Param_Cell, Param_Cell, Param_String);
	return m_pErrorCallback->AddFunction(m_pContext, function);
}

bool CAsyncSocketContext::SetDataCallback(funcid_t function)
{
	if(m_pDataCallback)
		forwards->ReleaseForward(m_pDataCallback);

	m_pDataCallback = forwards->CreateForwardEx(NULL, ET_Single, 3, NULL, Param_Cell, Param_String, Param_Cell);
	return m_pDataCallback->AddFunction(m_pContext, function);
}
