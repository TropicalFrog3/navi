/**
 * @file  misc.hpp
 * @brief Declarations for miscellaneous engine presenter routines
 */
#pragma once

#include "sdk.hpp"
class CProtoBufMsgBase;

namespace Misc
{
	bool shouldFakeOffline();
	void recvMsg(CNetPacket* pkt);
	void sendMsg(CNetPacket* pkt);
}
