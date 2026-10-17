/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/

#include "Common.h"
#include "Object.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include "Battlefield.h"
#include "BattlefieldMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "ScriptMgr.h"

//This send to player windows for invite player to join the war
//Param1:(guid) the guid of Bf
//Param2:(ZoneId) the zone where the battle is (4197 for wg)
//Param3:(time) Time in second that the player have for accept
void WorldSession::SendBfInvitePlayerToWar(uint64 guid, uint32 zoneId, uint32 pTime)
{
    ObjectGuid guidBytes = guid;

    WorldPacket data(SMSG_BATTLEFIELD_MGR_ENTRY_INVITE, 16);

    data.WriteGuidMask(guidBytes, 5, 3, 7, 2, 6, 4, 1, 0);

    data.WriteGuidBytes(guidBytes, 6);
    data << uint32(zoneId);         // Zone Id
    data.WriteGuidBytes(guidBytes, 1, 3, 4, 2, 0);
    data << uint32(time(NULL) + pTime); // Invite lasts until
    data.WriteGuidBytes(guidBytes, 7, 5);

    //Sending the packet to player
    SendPacket(&data);
}

//This send invitation to player to join the queue
void WorldSession::SendBfInvitePlayerToQueue(uint64 guid)
{
    ObjectGuid guidBytes = guid;

    WorldPacket data(SMSG_BATTLEFIELD_MGR_QUEUE_INVITE, 5);

    data.WriteBit(1);               // unk
    data.WriteBit(0);               // Has Warmup
    data.WriteBit(1);               // unk
    data.WriteBit(guidBytes[0]);
    data.WriteBit(1);               // unk
    data.WriteBit(guidBytes[2]);
    data.WriteBit(guidBytes[6]);
    data.WriteBit(guidBytes[3]);
    data.WriteBit(1);               // unk
    data.WriteBit(0);               // unk
    data.WriteBit(guidBytes[1]);
    data.WriteBit(guidBytes[5]);
    data.WriteBit(guidBytes[4]);
    data.WriteBit(1);               // unk
    data.WriteBit(guidBytes[7]);

    data.FlushBits();

    data.WriteByteSeq(guidBytes[2]);
    data.WriteByteSeq(guidBytes[3]);
    data.WriteByteSeq(guidBytes[6]);
    data << uint8(1);               // Warmup
    data.WriteByteSeq(guidBytes[5]);
    data.WriteByteSeq(guidBytes[0]);
    data.WriteByteSeq(guidBytes[4]);
    data.WriteByteSeq(guidBytes[1]);
    data.WriteByteSeq(guidBytes[7]);

    //Sending packet to player
    SendPacket(&data);
}

//This send packet for inform player that he join queue
//Param1:(guid) the guid of Bf
//Param2:(ZoneId) the zone where the battle is (4197 for wg)
//Param3:(CanQueue) if able to queue
//Param4:(Full) on log in is full
void WorldSession::SendBfQueueInviteResponse(uint64 guid, uint32 ZoneId, bool CanQueue, bool Full)
{
    const bool hasSecondGuid = false;
    const bool warmup = true;
    ObjectGuid guidBytes = guid;

    WorldPacket data(SMSG_BATTLEFIELD_MGR_QUEUE_REQUEST_RESPONSE, 16);

    data.WriteBit(guidBytes[1]);
    data.WriteBit(guidBytes[6]);
    data.WriteBit(guidBytes[5]);
    data.WriteBit(guidBytes[7]);
    data.WriteBit(Full);  // Logging In, VERIFYME
    data.WriteBit(guidBytes[0]);
    data.WriteBit(!hasSecondGuid);
    data.WriteBit(guidBytes[4]);

    // if (hasSecondGuid) 7 3 0 4 2 6 1 5

    data.WriteBit(guidBytes[3]);
    data.WriteBit(guidBytes[2]);

    // if (hasSecondGuid) 2 5 3 0 4 6 1 7

    data.FlushBits();

    data << uint8(CanQueue);  // Accepted

    data.WriteByteSeq(guidBytes[1]);
    data.WriteByteSeq(guidBytes[3]);
    data.WriteByteSeq(guidBytes[6]);
    data.WriteByteSeq(guidBytes[7]);
    data.WriteByteSeq(guidBytes[0]);

    data << uint8(warmup);

    data.WriteByteSeq(guidBytes[2]);
    data.WriteByteSeq(guidBytes[4]);
    data.WriteByteSeq(guidBytes[5]);

    data << uint32(ZoneId);

    SendPacket(&data);
}

//This is call when player accept to join war
void WorldSession::SendBfEntered(uint64 guid)
{
    uint8 isAFK = _player->isAFK() ? 1 : 0;
    ObjectGuid guidBytes = guid;

    WorldPacket data(SMSG_BATTLEFIELD_MGR_ENTERED, 11);
    data.WriteBit(guidBytes[5]);
    data.WriteBit(guidBytes[0]);
    data.WriteBit(0);               // unk
    data.WriteBit(isAFK);           // Clear AFK
    data.WriteBit(guidBytes[7]);
    data.WriteBit(guidBytes[4]);
    data.WriteBit(guidBytes[2]);
    data.WriteBit(0);               // unk
    data.WriteBit(guidBytes[1]);
    data.WriteBit(guidBytes[3]);
    data.WriteBit(guidBytes[6]);

    data.FlushBits();

    data.WriteByteSeq(guidBytes[2]);
    data.WriteByteSeq(guidBytes[5]);
    data.WriteByteSeq(guidBytes[0]);
    data.WriteByteSeq(guidBytes[6]);
    data.WriteByteSeq(guidBytes[7]);
    data.WriteByteSeq(guidBytes[3]);
    data.WriteByteSeq(guidBytes[4]);
    data.WriteByteSeq(guidBytes[1]);
    SendPacket(&data);
}

void WorldSession::SendBfLeaveMessage(uint64 guid, BFLeaveReason reason)
{
    ObjectGuid guidBytes = guid;

    WorldPacket data(SMSG_BATTLEFIELD_MGR_EJECTED, 11);
    data.WriteGuidMask(guidBytes, 5);
    data.WriteBit(0);               // Relocated
    data.WriteGuidMask(guidBytes, 4, 0, 2, 7, 1, 6, 3);

    data.WriteGuidBytes(guidBytes, 0, 2, 3, 4, 6, 5);
    data << uint8(2);               // BattleStatus
    data << uint8(reason);          // Reason
    data.WriteGuidBytes(guidBytes, 7, 1);
    SendPacket(&data);
}

//Send by client when he click on accept for queue
void WorldSession::HandleBfQueueInviteResponse(WorldPacket& recvData)
{
    uint8 accepted;
    ObjectGuid guid;

    recvData.ReadGuidMask(guid, 3, 6, 0, 2);
    accepted = recvData.ReadBit();
    recvData.ReadGuidMask(guid, 1, 5, 7, 4);

    recvData.ReadGuidBytes(guid, 4, 1, 2, 6, 0, 7, 5, 3);

    SF_LOG_ERROR("misc", "HandleQueueInviteResponse: GUID:" UI64FMTD " Accepted:%u", (uint64)guid, accepted);

    Battlefield* bf = sBattlefieldMgr->GetBattlefieldByGUID(guid);
    if (!bf)
        return;

    if (accepted)
        bf->PlayerAcceptInviteToQueue(_player);
}

//Send by client on clicking in accept or refuse of invitation windows for join game
void WorldSession::HandleBfEntryInviteResponse(WorldPacket& recvData)
{
    uint8 accepted;
    ObjectGuid guid;

    recvData.ReadGuidMask(guid, 0, 7);
    accepted = recvData.ReadBit();
    recvData.ReadGuidMask(guid, 4, 3, 1, 6, 2, 5);

    recvData.ReadGuidBytes(guid, 1, 6, 2, 5, 3, 4, 7, 0);

    SF_LOG_ERROR("misc", "HandleBattlefieldInviteResponse: GUID:" UI64FMTD " Accepted:%u", (uint64)guid, accepted);

    Battlefield* bf = sBattlefieldMgr->GetBattlefieldByGUID(guid);
    if (!bf)
        return;

    if (accepted)
        bf->PlayerAcceptInviteToWar(_player);
    else
        if (_player->GetZoneId() == bf->GetZoneId())
            bf->KickPlayerFromBattlefield(_player->GetGUID());
}

void WorldSession::HandleBfExitRequest(WorldPacket& recvData)
{
    ObjectGuid guid;

    recvData.ReadGuidMask(guid, 3, 2, 4, 1, 7, 0, 5, 6);
    recvData.ReadGuidBytes(guid, 5, 6, 2, 3, 0, 4, 7, 1);

    SF_LOG_ERROR("misc", "HandleBfExitRequest: GUID:" UI64FMTD " ", (uint64)guid);

    if (Battlefield* bf = sBattlefieldMgr->GetBattlefieldByGUID(guid))
        bf->AskToLeaveQueue(_player);
}

void WorldSession::HandleBattlefieldRatedInfoRequest(WorldPacket& /*recvData*/)
{
    WorldPacket data(SMSG_BATTLEFIELD_RATED_INFO, 4 * (8 * 4));
    for (uint8 i = 0; i < 4; i++)
    {
        RatedBattlefieldStats stats;
        sScriptMgr->GetRatedBattlefieldStats(_player, i, stats);
        data << uint32(0); // Unknown field; retain the native value.
        data << stats.SeasonWins;
        data << stats.SeasonGames;
        data << stats.Rating;
        data << stats.WeekWins;
        data << stats.WeekBest;
        data << stats.WeekGames;
        data << stats.SeasonBest;
    }
    SendPacket(&data);
}
