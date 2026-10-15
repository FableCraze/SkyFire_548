/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/

#include "Player.h"
#include "Item.h"
#include "Mail.h"
#include "WorldPacket.h"
#include "WorldSession.h"

void Player::RemoveMail(uint32 id)
{
    for (PlayerMails::iterator itr = m_mail.begin(); itr != m_mail.end(); ++itr)
    {
        if ((*itr)->messageID == id)
        {
            //do not delete item, because Player::removeMail() is called when returning mail to sender.
            m_mail.erase(itr);
            return;
        }
    }
}

void Player::SendMailResult(uint32 mailId, MailResponseType mailAction, MailResponseResult mailError, uint32 equipError, uint32 item_guid, uint32 item_count)
{
    WorldPacket data(SMSG_SEND_MAIL_RESULT, 4 + 4 + 4 + 4 + 4 + 4);
    data << uint32(mailId);
    data << uint32(equipError);
    data << uint32(mailError);
    data << uint32(mailAction);
    data << uint32(item_guid);
    data << uint32(item_count);

    GetSession()->SendPacket(&data);
}

void Player::SendNewMail()
{
    // deliver undelivered mail
    WorldPacket data(SMSG_RECEIVED_MAIL, 4);
    data << float(0);
    GetSession()->SendPacket(&data);
}

// Called after a confirmed delivery, including recovery of a lost COMMIT response.
// Load only missing mails: never discard dirty mail state already held in memory.
void Player::RefreshBattlePayMail(uint64 purchaseId)
{
    if (!IsMailsLoaded())
    {
        _LoadMail();
        UpdateNextMailTimeAndUnreads();
        SendNewMail();
        return;
    }
    auto rows=CharacterDatabase.PQuery("SELECT m.id,m.messageType,m.sender,m.receiver,m.subject,m.body,m.has_items,m.expire_time,m.deliver_time,m.money,m.cod,m.checked,m.stationery,m.mailTemplateId FROM mail m JOIN battlepay_delivery_mail d ON d.mailId=m.id WHERE d.purchaseId=" UI64FMTD " AND m.receiver=%u",purchaseId,GetGUIDLow());
    bool added=false;
    if (rows) do
    {
        auto f=rows->Fetch();
        if (GetMail(f[0].GetUInt32())) continue;
        Mail* mail=new Mail;
        mail->messageID=f[0].GetUInt32(); mail->messageType=f[1].GetUInt8(); mail->sender=f[2].GetUInt32();
        mail->receiver=f[3].GetUInt32(); mail->subject=f[4].GetString(); mail->body=f[5].GetString();
        mail->expire_time=time_t(f[7].GetUInt64()); mail->deliver_time=time_t(f[8].GetUInt64());
        mail->money=f[9].GetUInt64(); mail->COD=f[10].GetUInt64(); mail->checked=f[11].GetUInt8();
        mail->stationery=f[12].GetUInt8(); mail->mailTemplateId=f[13].GetUInt16(); mail->state=MailState::MAIL_STATE_UNCHANGED;
        if (f[6].GetBool()) _LoadMailedItems(mail);
        AddMail(mail); added=true;
    } while (rows->NextRow());
    if (added) {UpdateNextMailTimeAndUnreads(); SendNewMail();}
}

void Player::UpdateNextMailTimeAndUnreads()
{
    // calculate next delivery time (min. from non-delivered mails
    // and recalculate unReadMail
    time_t cTime = time(NULL);
    m_nextMailDelivereTime = 0;
    unReadMails = 0;
    for (PlayerMails::iterator itr = m_mail.begin(); itr != m_mail.end(); ++itr)
    {
        if ((*itr)->deliver_time > cTime)
        {
            if (!m_nextMailDelivereTime || m_nextMailDelivereTime > (*itr)->deliver_time)
                m_nextMailDelivereTime = (*itr)->deliver_time;
        }
        else if (((*itr)->checked & MAIL_CHECK_MASK_READ) == 0)
            ++unReadMails;
    }
}

void Player::AddNewMailDeliverTime(time_t deliver_time)
{
    if (deliver_time <= time(NULL))                          // ready now
    {
        ++unReadMails;
        SendNewMail();
    }
    else                                                    // not ready and no have ready mails
    {
        if (!m_nextMailDelivereTime || m_nextMailDelivereTime > deliver_time)
            m_nextMailDelivereTime = deliver_time;
    }
}

Mail* Player::GetMail(uint32 id)
{
    for (PlayerMails::iterator itr = m_mail.begin(); itr != m_mail.end(); ++itr)
        if ((*itr)->messageID == id)
            return (*itr);

    return NULL;
}

Item* Player::GetMItem(uint32 id)
{
    ItemMap::const_iterator itr = mMitems.find(id);
    return itr != mMitems.end() ? itr->second : NULL;
}

void Player::AddMItem(Item* it)
{
    ASSERT(it);
    //ASSERT deleted, because items can be added before loading
    mMitems[it->GetGUIDLow()] = it;
}

bool Player::RemoveMItem(uint32 id)
{
    return mMitems.erase(id) ? true : false;
}
