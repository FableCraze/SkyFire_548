-- Manual BattlePay reset. Stop authserver and worldserver before running it.
-- This permanently deletes wallet balances, purchase history and the catalog.
-- Default database names are auth, characters and world; change them if needed.
-- Keep this file outside sql/updates: it must never run automatically.
-- Existing accounts, characters, boost entitlements and delivered mail remain.

DROP TABLE IF EXISTS
 `auth`.`battlepay_wallet_movement`,
 `auth`.`battlepay_purchase_item`,
 `auth`.`battlepay_purchase`,
 `auth`.`battlepay_wallet`;

DROP TABLE IF EXISTS
 `characters`.`battlepay_delivery_mail`,
 `characters`.`battlepay_delivery`;

DROP TABLE IF EXISTS
 `world`.`locales_battle_pay_entry`,
 `world`.`locales_battle_pay_group`,
 `world`.`locales_battle_pay_product`,
 `world`.`battle_pay_entry`,
 `world`.`battle_pay_product_items`,
 `world`.`battle_pay_product`,
 `world`.`battle_pay_group`;

-- Remove only permission records owned by this BattlePay implementation.
DELETE a FROM `auth`.`rbac_account_permissions` a
JOIN `auth`.`rbac_permissions` p ON p.id=a.permissionId
WHERE p.id IN (1018,1019,1020,1021,1022)
 AND p.name IN ('Command: battlepay','Command: battlepay balance',
               'Command: battlepay wallet','Command: battlepay purchase',
               'Command: reload battlepay');

DELETE l FROM `auth`.`rbac_linked_permissions` l
JOIN `auth`.`rbac_permissions` p ON p.id=l.id OR p.id=l.linkedId
WHERE p.id IN (1018,1019,1020,1021,1022)
 AND p.name IN ('Command: battlepay','Command: battlepay balance',
               'Command: battlepay wallet','Command: battlepay purchase',
               'Command: reload battlepay');

DELETE FROM `auth`.`rbac_permissions`
WHERE id IN (1018,1019,1020,1021,1022)
 AND name IN ('Command: battlepay','Command: battlepay balance',
              'Command: battlepay wallet','Command: battlepay purchase',
              'Command: reload battlepay');

-- Forget these migrations so the normal startup updater applies them again.
DELETE FROM `auth`.`skyfire_db_updates`
WHERE domain='auth' AND filename='2026_10_14_auth_00.sql';

DELETE FROM `characters`.`skyfire_db_updates`
WHERE domain='characters' AND filename='2026_10_14_characters_00.sql';

DELETE FROM `world`.`skyfire_db_updates`
WHERE domain='world' AND filename IN
 ('2026_10_14_world_00.sql','2026_10_14_world_01.sql','2026_10_14_world_02.sql');
