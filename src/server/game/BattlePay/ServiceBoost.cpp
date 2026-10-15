#include "ServiceBoost.h"
#include "WorldSession.h"

void BattlePay::NotifyBoostPurchased(WorldSession* session)
{
    // hasBoost was already committed atomically with the wallet debit.
    session->SetBoosting(true, false);
    session->SendBattlePayDistributionUpdate(0, CHARACTER_BOOST, CHARACTER_BOOST_ALLOW,
        CHARACTER_BOOST_TEXT_ID, CHARACTER_BOOST_BONUS_TEXT, CHARACTER_BOOST_BONUS_TEXT2);
}
