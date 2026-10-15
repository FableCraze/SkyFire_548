-- Both delivered and rejected receipts are durable: never delete on mail expiry.
CREATE TABLE IF NOT EXISTS battlepay_delivery (
 purchaseId BIGINT UNSIGNED NOT NULL PRIMARY KEY,
 accountId INT UNSIGNED NOT NULL, targetGuid INT UNSIGNED NOT NULL,
 result TINYINT UNSIGNED NOT NULL COMMENT '1=delivered,2=recipient rejected',
 createdAt TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
CREATE TABLE IF NOT EXISTS battlepay_delivery_mail (
 purchaseId BIGINT UNSIGNED NOT NULL, mailId INT UNSIGNED NOT NULL,
 PRIMARY KEY (purchaseId,mailId)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
