-- Default BattlePay example: one free Hearthstone, delivered by mail.
-- Existing products and categories are preserved; this migration is repeatable.
START TRANSACTION;

INSERT IGNORE INTO battle_pay_group (id,idx,name,icon,type)
VALUES (1,0,'Items',940857,0);

INSERT IGNORE INTO battle_pay_product (id,title,description,icon,price,displayId,type)
SELECT 6948,'Hearthstone','Free example item, delivered by mail.',134414,0.0000,displayid,0
FROM item_template WHERE entry=6948;

INSERT IGNORE INTO battle_pay_product_items (id,itemId,count,productId)
SELECT 6948,6948,1,id FROM battle_pay_product
WHERE id=6948 AND title='Hearthstone' AND type=0 AND price=0.0000;

INSERT IGNORE INTO battle_pay_entry (id,productId,groupId,idx,title,description)
SELECT 6948,p.id,1,0,p.title,p.description
FROM battle_pay_product p JOIN battle_pay_product_items i ON i.productId=p.id
WHERE p.id=6948 AND p.title='Hearthstone' AND p.type=0 AND p.price=0.0000
  AND i.id=6948 AND i.itemId=6948 AND i.count=1;

COMMIT;
