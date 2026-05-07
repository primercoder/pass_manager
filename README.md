# pass_manager

基于 C++17 的命令行密码管理器，使用 AES-256-GCM 加密和 SQLite 本地存储。支持命令行直接操作和 TUI 交互命令模式。

## 功能特性

- **AES-256-GCM 认证加密**：每个密码使用独立的随机 IV，密钥文件经 SHA-256 派生
- **SQLite 本地存储**：WAL 模式，密码以密文存储，明文从不落盘
- **隐藏密码输入**：终端内回显遮蔽（termios），密码支持二次确认
- **删除/修改需验证**：移除或编辑密码时必须输入原密码明文，防止误操作
- **模糊搜索 + 收窄**：输入名称片段回车后列出匹配项，支持数字选择或追加文本逐步收窄
- **剪贴板复制**：查询密码时自动通过 xclip / wl-copy 复制到剪贴板
- **时间戳记录**：自动记录创建时间和最近修改时间
- **密钥文件管理**：首次运行自动生成随机密钥，0600 权限保护，异常权限自动修复
- **两种操作模式**：命令行参数直接操作 + `--tui` 交互命令模式
- **交互式容错**：命令行模式下缺少必要参数时自动提示输入
- **独立工作目录**：数据默认存储于 `~/.passmanager/`

## 依赖

| 软件 | 用途 | 最低版本 |
|------|------|----------|
| CMake | 构建系统 | 3.14 |
| OpenSSL | AES-256-GCM / SHA-256 | 1.1.0 |
| SQLite3 | 数据存储 | 3.x |
| xclip 或 wl-copy | 剪贴板（可选） | - |

Ubuntu/Debian 安装依赖：

```bash
sudo apt install build-essential cmake libssl-dev libsqlite3-dev xclip
```

## 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## 使用

### 快速开始

```bash
# 无参数运行 → 显示帮助
./build/pass_manager

# 进入 TUI 交互模式
./build/pass_manager --tui

# 命令行直接操作
./build/pass_manager --add -n github --desc "GitHub" --account user
```

### 命令行模式

#### 新增密码

```bash
# 不带 -p → 自动交互式输入密码（隐藏回显、二次确认）
./build/pass_manager --add -n github --desc "GitHub" --account user@example.com

# 带 -p → 直接传入密码（注意 shell history 会暴露）
./build/pass_manager --add -n github -p mypassword
```

#### 查询密码

```bash
# 模糊搜索（可输入部分名称）
./build/pass_manager --show -n git

# 显示明文
./build/pass_manager --show -n git --reveal
```

#### 列出密码

```bash
./build/pass_manager --list
```

显示：名称、描述、创建时间、更新时间、总数。

#### 修改密码

```bash
# 不带 --old-password → 交互式验证原密码
./build/pass_manager --edit -n github --desc "New desc" --account new@test.com

# 也可直接提供原密码
./build/pass_manager --edit -n github --old-password oldpass -p newpass
```

`--desc` 和 `--account` 直接覆盖当前值。`-p` 可选修改密码本体。

#### 删除密码

```bash
# 交互式验证密码
./build/pass_manager --remove -n github

# 直接传入验证密码
./build/pass_manager --remove -n github -p mypassword
```

### TUI 交互模式

```bash
./build/pass_manager --tui
```

启动后显示帮助信息和命令提示符 `pass_manager>`。

```
╔══════════════════════════════════╗
║        PASSWORD MANAGER          ║
╚══════════════════════════════════╝
Key: ~/.passmanager/default.key

  add      Add a new password
  delete   Delete a password
  show     Search and reveal a password
  list     List all passwords with details
  edit     Edit a password
  key      Change key file
  help     Show this help
  exit     Quit the program

pass_manager>
```

| 命令 | 功能 |
|------|------|
| `add` | 新增密码（名称、描述、账户、密码二次确认） |
| `delete` | 删除密码（需验证原密码） |
| `show` | 模糊搜索 → 列表选择或收窄 → 显示详情 → 剪贴板 |
| `list` | 列出所有密码的名称、描述、创建/更新时间、总数 |
| `edit` | 修改密码（验证原密码后可改描述、账户、密码） |
| `key` | 更换密钥文件 |
| `help` | 显示命令帮助 |
| `exit` | 退出程序 |

### 命令行参数

| 参数 | 简写 | 说明 |
|------|------|------|
| `--tui` | `-t` | 进入 TUI 交互模式 |
| `--help` | `-h` | 显示帮助 |
| `--key <path>` | `-k` | 密钥文件路径（默认 `~/.passmanager/default.key`） |
| `--db <path>` | `-d` | 数据库路径（默认 `~/.passmanager/passwords.db`） |
| `--add` | `-a` | 新增密码 |
| `--remove` | `-r` | 删除密码 |
| `--show` | `-s` | 查询密码 |
| `--list` | `-l` | 列出所有密码 |
| `--edit` | `-e` | 修改密码 |
| `--name <name>` | `-n` | 密码名称 |
| `--password <pwd>` | `-p` | 密码（add/edit/remove 验证） |
| `--old-password <pwd>` | — | 编辑时的原密码验证 |
| `--desc <desc>` | — | 描述（add）或新描述（edit） |
| `--account <acct>` | — | 账户（add）或新账户（edit） |
| `--reveal` | — | 显示明文密码（show） |

## 安全设计

### 加密体系

密码管理器使用 **AES-256-GCM** 认证加密模式，该模式同时提供机密性和完整性保护。

```
                          ┌───────────────────┐
                          │   密钥文件 (64B)    │
                          │  /dev/urandom 生成  │
                          └─────────┬─────────┘
                                    │
                              SHA-256 哈希
                                    │
                          ┌─────────▼─────────┐
                          │  AES-256 密钥 (32B) │
                          └─────────┬─────────┘
                                    │
   ┌────────────────────────────────┼────────────────────────────────┐
   │                                │                                │
   ▼                                ▼                                ▼
┌──────────┐                 ┌──────────┐                  ┌──────────────┐
│  明文密码  │────────────────▶│ AES-GCM  │────────────────▶│ 密文存储体    │
│ (variable)│   随机 IV (12B)  │  加密器   │   认证标签 (16B)  │ Base64 编码    │
└──────────┘                 └──────────┘                  └──────────────┘
```

**加密流程详解：**

1. **密钥派生**：读取密钥文件全部字节，经 SHA-256 哈希输出 32 字节固定长度密钥。此设计允许任意大小的密钥文件（推荐 64 字节随机数据），无论原始大小始终产生 256 位密钥。

2. **每密码独立 IV**：每次加密操作调用 `RAND_bytes` 生成 12 字节随机初始化向量。同一密码在不同时间加密产生的密文完全不同，防止重放和模式分析。

3. **GCM 认证标签**：加密同时生成 16 字节认证标签（GMAC），解密时验证标签确保密文未被篡改。任何对密文的修改都会导致解密失败。

4. **存储格式**：`Base64( IV(12B) || Tag(16B) || Ciphertext )`。IV 和 Tag 无需保密，拼接后 Base64 编码存入 SQLite `password_enc` 字段。

**加密 API 调用链**（OpenSSL EVP）：

```
EVP_EncryptInit_ex(EVP_aes_256_gcm)
  → EVP_CIPHER_CTX_ctrl(SET_IVLEN, 12)
  → EVP_EncryptInit_ex(key, iv)
  → EVP_EncryptUpdate(plaintext)         // 输出密文
  → EVP_EncryptFinal_ex()                // 终结
  → EVP_CIPHER_CTX_ctrl(GET_TAG, 16)    // 获取认证标签
```

**解密 API 调用链**：

```
EVP_DecryptInit_ex(EVP_aes_256_gcm)
  → EVP_CIPHER_CTX_ctrl(SET_IVLEN, 12)
  → EVP_DecryptInit_ex(key, iv)
  → EVP_DecryptUpdate(ciphertext)        // 输出明文
  → EVP_CIPHER_CTX_ctrl(SET_TAG, 16)    // 设置期望标签
  → EVP_DecryptFinal_ex()                // 验证标签（失败=篡改/密钥错误）
```

### 密钥文件管理

| 策略 | 说明 |
|------|------|
| 首次生成 | 不存在时自动调用 `RAND_bytes` 生成 64 字节随机密钥写入文件 |
| 权限保护 | 强制 `0600`（仅所有者读写），启动时检测并自动修复异常权限 |
| 路径可配 | 默认 `~/.passmanager/default.key`，通过 `-k` 指定自定义路径 |
| 版本控制 | `.gitignore` 中排除 `*.key`，密钥永不提交 |

### 密码输入安全

- **终端隐藏回显**：`tcgetattr` + `ECHO` off，输入密码时屏幕无任何字符回显
- **二次确认**：新增和修改密码时要求再次输入比对，防止输入错误
- **命令行安全**：推荐省略 `-p` 使用交互式隐藏输入，避免密码出现在 `ps`、`history`、`.bash_history` 中
- **内存管理**：密码字符串在栈上分配，函数返回后随栈帧销毁
- **剪贴板安全**：通过 `popen` 管道直接写入 xclip/wl-copy 的 stdin，无 shell 注入风险

---

## 数据库设计

### 表结构

```sql
CREATE TABLE IF NOT EXISTS passwords (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    name            TEXT    NOT NULL UNIQUE,
    description     TEXT    NOT NULL DEFAULT '',
    account         TEXT    NOT NULL DEFAULT '',
    password_enc    TEXT    NOT NULL,
    created_at      TEXT    NOT NULL DEFAULT (datetime('now','localtime')),
    updated_at      TEXT    NOT NULL DEFAULT (datetime('now','localtime'))
);
```

### 字段设计说明

| 字段 | 类型 | 约束 | 说明 |
|------|------|------|------|
| `id` | INTEGER | PRIMARY KEY AUTOINCREMENT | 自增主键，内部索引，不暴露给用户 |
| `name` | TEXT | NOT NULL UNIQUE | 密码唯一标识名，作为业务主键；UNIQUE 约束防止重复录入 |
| `description` | TEXT | DEFAULT '' | 密码用途描述，辅助记忆出处 |
| `account` | TEXT | DEFAULT '' | 关联账户名（如邮箱、用户名） |
| `password_enc` | TEXT | NOT NULL | Base64 编码的密文存储体（IV + Tag + 密文） |
| `created_at` | TEXT | DEFAULT datetime('now','localtime') | 首次录入时间，SQLite 本地时间 |
| `updated_at` | TEXT | DEFAULT datetime('now','localtime') | 最近修改时间，UPDATE 时由应用层更新为当前时间 |

### 设计原则

**名称为唯一业务键**

`name` 字段承担密码条目的唯一标识角色。所有操作（增删改查）均以 `name` 为定位依据：
- 新增：先 `SELECT` 检查 UNIQUE 冲突，避免重复
- 查询/删除/修改：以 `name` 精确匹配或 `LIKE` 模糊匹配定位目标

**密文与明文分离**

- 密码本体以密文形式存储在 `password_enc` 字段，通过 AES-256-GCM 加密
- 元数据（name、description、account）以明文存储，无需加密——便于搜索和管理
- 时间戳自动维护，`created_at` 由 SQLite `DEFAULT` 子句在 INSERT 时填充，`updated_at` 由应用在 UPDATE 时显式设置为当前时间

**WAL 模式**

数据库以 WAL（Write-Ahead Logging）模式打开：
```sql
PRAGMA journal_mode=WAL;
```
优势：读写并发更好，写入不阻塞读取，适合 CLI 工具频繁的查询操作。

### 核心查询

**新增密码**（参数化防注入）：
```sql
INSERT INTO passwords (name, description, account, password_enc)
VALUES (?, ?, ?, ?);
```

**模糊搜索**（名称片段匹配）：
```sql
SELECT id, name, description, account, password_enc, created_at, updated_at
FROM passwords
WHERE name LIKE ? ESCAPE '\'
ORDER BY name ASC;
```
`LIKE` 模式为 `%user_input%`，`_` 和 `%` 字面量通过 `ESCAPE '\'` 转义。

**精确查找**（按名称）：
```sql
SELECT id, name, description, account, password_enc, created_at, updated_at
FROM passwords
WHERE name = ?;
```

**修改密码**（自动更新 `updated_at`）：
```sql
UPDATE passwords
SET description = ?, account = ?, password_enc = ?, updated_at = datetime('now','localtime')
WHERE name = ?;
```

**统计数量**：
```sql
SELECT COUNT(*) FROM passwords;
```

所有写入操作均通过 `sqlite3_prepare_v2` + `sqlite3_bind_text` 参数化查询，杜绝 SQL 注入。

### 数据目录

```
~/.passmanager/
├── default.key             # AES 密钥文件 (0600)
└── passwords.db            # SQLite 数据库 (WAL)
```

可通过 `-k` / `-d` 参数指定不同的路径。
