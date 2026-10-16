#pragma once

#include "types.hpp"

#include "protobufs/enums_clientserver.pb.h"
#include "protobufs/steammessages_base.pb.h"
#include "protobufs/encrypted_app_ticket.pb.h"
#include "protobufs/steammessages_clientserver.pb.h"
#include "protobufs/steammessages_clientserver_2.pb.h"
#include "protobufs/steammessages_clientserver_appinfo.pb.h"
#include "protobufs/steammessages_clientserver_friends.pb.h"
#include "protobufs/steammessages_clientserver_userstats.pb.h"
#include "protobufs/steammessages_player.steamclient.pb.h"

#include <cstdint>
#include <string>


enum EGameFlags
{
	//1 << 0 is set for spacewar, not other mp games. idk
	k_EGameFlagJoinable = 1 << 1,
	k_EGameFlagMultiplayer = 1 << 13,
};

//Base class for both CProtoBufMsg and CProtoBufMsgResponse
SDK_Class CProtoBufMsgBase
{
public:
	uint8_t __pad_0x0[0x14];	//0x0
	ENetPacket type;			//0x14
	uint8_t __pad_0x18[0xC];	//0x18
	CMsgProtoBufHeader* header; //0x24
	void* __pBody;				//0x28
	uint8_t __pad_0x2c[0x8];	//0x2C
	
	constexpr EMsg getProtoBufType() const
	{
		return static_cast<EMsg>(type & ~PROTOBUF_TYPE_MASK);
	}
	
	template<typename T> constexpr T* getBody() const
	{
		if (!__pBody)
		{
			return nullptr;
		}
		return reinterpret_cast<T*>(__pBody);
	}

	std::string getProtoBufTypeName() const
	{
		const EMsg type = getProtoBufType();
		if (!EMsg_IsValid(type))
		{
			return "Unknown";
		}

		return EMsg_Name(type);
	}
}; //0x34
