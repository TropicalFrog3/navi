/**
 * @file  CClientUnifiedServiceTransport.cpp
 * @brief Protobuf unified service RPC message transport implementation
 */
#include "CClientUnifiedServiceTransport.hpp"

#include "hooks.hpp"


uint32_t CClientUnifiedServiceTransport::sendAndRecvMsg(const char* name, void* send, void* recv)
{
	return Hooks::CClientUnifiedServiceMethod_SendAndRecvMsg.tramp.fn(this, name, send, recv, nullptr);
}
