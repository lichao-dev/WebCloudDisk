SET NAMES utf8mb4;

CREATE TABLE tbl_user (
    id            BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    username      VARCHAR(64) NOT NULL,
    password_hash VARCHAR(255) NOT NULL,
    created_at    DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    updated_at    DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
                               ON UPDATE CURRENT_TIMESTAMP(3),

    PRIMARY KEY (id),
    UNIQUE KEY uk_user_username (username)
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE tbl_file (
    id          BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    uid         BIGINT UNSIGNED NOT NULL,
    filename    VARCHAR(255) NOT NULL,
    hashcode    CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    size        BIGINT UNSIGNED NOT NULL,
    created_at  DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    updated_at  DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
                             ON UPDATE CURRENT_TIMESTAMP(3),

    PRIMARY KEY (id),
    UNIQUE KEY uk_file_user_filename (uid, filename),
    KEY idx_file_user_created (uid, created_at),
    KEY idx_file_hashcode (hashcode),

    CONSTRAINT fk_file_user
        FOREIGN KEY (uid) REFERENCES tbl_user(id)
        ON UPDATE RESTRICT
        ON DELETE RESTRICT
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_0900_ai_ci;
