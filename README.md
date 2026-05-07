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

### 加密流程

```
密钥文件 → SHA-256 → 32 字节 AES-256 密钥
明文密码 → 随机 12B IV → AES-256-GCM 加密 → IV+Tag+密文 → Base64 → SQLite
```

### 文件保护

- 密钥文件 `0600`，数据目录 `0700`
- 异常权限启动时自动修复
- 密钥和数据库均在 `.gitignore` 中排除

### 密码输入安全

- 终端隐藏回显（termios `ECHO` off）
- 命令行模式推荐省略 `-p` 使用交互式输入，避免 shell history 泄露
- 剪贴板通过 `popen` 管道写入，无 shell 注入风险

## 数据目录

```
~/.passmanager/
├── default.key             # AES 密钥 (0600)
└── passwords.db            # SQLite 数据库
```

## 数据库表结构

```sql
CREATE TABLE passwords (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    name            TEXT    NOT NULL UNIQUE,
    description     TEXT    NOT NULL DEFAULT '',
    account         TEXT    NOT NULL DEFAULT '',
    password_enc    TEXT    NOT NULL,
    created_at      TEXT    NOT NULL DEFAULT (datetime('now','localtime')),
    updated_at      TEXT    NOT NULL DEFAULT (datetime('now','localtime'))
);
```
