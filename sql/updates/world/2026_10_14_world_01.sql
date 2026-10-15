-- The native store category widget expects store textures. Product icons render
-- oversized here. Repair only the original demonstration categories.
UPDATE battle_pay_group SET icon=940857 WHERE id=1 AND name='Items' AND icon=348522;
UPDATE battle_pay_group SET icon=939382 WHERE id=2 AND name='Services' AND icon=614740;
