# pass_manager

基于 C++17 的命令行密码管理器，使用 AES-256-GCM 加密和 SQLite 本地存储。支持 TUI 交互菜单和纯 CLI 命令行两种操作模式。

## 功能特性

- **AES-256-GCM 认证加密**：每个密码使用独立的随机 IV，密钥文件经 SHA-256 派生，防篡改、防重放
- **本地 SQLite 存储**：WAL 模式，密码以 Base64 密文存储，明文从不落盘
- **隐藏密码输入**：终端内回显遮蔽（termios），二次确认防止输入错误
- **实时模糊搜索**：输入时逐字匹配，动态滑显最多 10 条匹配密码名称
- **删除双重验证**：移除密码时必须输入原密码明文，与解密结果比对一致才允许删除
- **修改密码**：编辑密码时需验证原始密码，支持修改描述、账户、密码
- **剪贴板复制**：通过管道安全写入 xclip / wl-copy，无论是否显示明文都会自动复制
- **时间戳记录**：每条密码自动记录创建时间和最近修改时间
- **密钥文件管理**：首次运行自动生成 64 字节随机密钥，0600 权限保护；支持自定义密钥路径
- **两种操作模式**：TUI 交互菜单模式和纯 CLI 命令行模式，脚本友好
- **独立工作目录**：默认数据目录 `~/.passmanager/`，脱离项目目录限制

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
git clone <repo-url> && cd pass_manager
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

编译产物为 `build/pass_manager`。

## 使用

### TUI 交互模式

直接运行，进入交互菜单：

```bash
./build/pass_manager
```

```
╔══════════════════════════════════╗
║        PASSWORD MANAGER          ║
╚══════════════════════════════════╝

  1. Add Password        (新增密码)
  2. Remove Password     (移除密码)
  3. Show Password       (查询密码)
  4. List All Passwords  (密码列表)
  5. Password Count      (密码数量)
  6. Edit Password       (修改密码)
  7. Change Key File     (更换密钥)
  0. Exit                (退出)
```

查询密码时支持实时模糊匹配：输入名称片段，自动展示最多 10 条匹配记录。按 ESC 取消，回车确认后继续选择。

### CLI 命令行模式

通过命令行参数直接完成所有操作，适合脚本调用：

```bash
# 新增密码（从 stdin 安全读入密码）
./build/pass_manager --add --name github --desc "GitHub" \
                     --account user@example.com --password-stdin

# 查询密码并显示明文
./build/pass_manager --show --name git --reveal

# 列出所有密码
./build/pass_manager --list

# 查看密码数量
./build/pass_manager --count

# 修改密码（需验证原密码）
./build/pass_manager --edit --name github --verify-stdin \
                     --new-desc "Work GitHub"

# 删除密码（需验证原密码）
./build/pass_manager --remove --name github --verify-stdin

# 自定义密钥和数据库路径
./build/pass_manager -k /path/to/my.key -d /path/to/my.db --list
```

### 命令行参数

| 参数 | 说明 |
|------|------|
| `-k, --key <path>` | 指定密钥文件路径（默认 `~/.passmanager/default.key`） |
| `-d, --db <path>` | 指定数据库文件路径（默认 `~/.passmanager/passwords.db`） |
| `-h, --help` | 显示帮助信息 |
| `-a, --add` | 新增密码 |
| `-r, --remove` | 删除密码 |
| `-s, --show` | 查询密码 |
| `-l, --list` | 列出所有密码 |
| `-c, --count` | 显示密码数量 |
| `-e, --edit` | 修改密码 |
| `--name <name>` | 密码名称 |
| `--desc <desc>` | 描述信息 |
| `--account <acct>` | 账户名 |
| `--password <pwd>` | 密码明文（会暴露在 shell history 中） |
| `--password-stdin` | 从 stdin 读取密码（安全） |
| `--verify <pwd>` | 删除/修改时的验证密码 |
| `--verify-stdin` | 从 stdin 读取验证密码 |
| `--new-desc <desc>` | 新描述（`--edit`） |
| `--new-account <acct>` | 新账户（`--edit`） |
| `--new-password <pwd>` | 新密码（`--edit`） |
| `--new-password-stdin` | 从 stdin 读取新密码（`--edit`） |
| `--reveal` | 显示明文密码（`--show`） |

## 操作说明

### 新增密码

**TUI**：选择 `1`，依次输入名称、描述、账户、密码（隐藏，二次确认）。

**CLI**：
```bash
./build/pass_manager --add --name <name> --password-stdin
```

### 查询密码

**TUI**：选择 `3`，实时输入名称片段，匹配结果动态更新（最多 10 条）。回车确认后：
- 仅一个匹配 → 直接展示详情
- 多个匹配 → 列出表格供选择
- 展示详情后可选择是否显示明文
- **密码自动复制到剪贴板**

**CLI**：
```bash
./build/pass_manager --show --name <partial> --reveal
```

### 修改密码

**TUI**：选择 `6`，输入名称 → 验证原密码 → 逐项修改（留空保持原值）→ 可选修改密码本体。

**CLI**：
```bash
./build/pass_manager --edit --name <name> --verify-stdin --new-desc "New desc"
```

### 删除密码

**TUI**：选择 `2`，输入名称 → 输入原密码明文验证 → 验证通过后删除。

**CLI**：
```bash
./build/pass_manager --remove --name <name> --verify-stdin
```

## 安全设计

### 加密流程

```
密钥文件 (64字节随机数据)
    │
    ├── SHA-256 → 32 字节 AES-256 密钥
    │
明文密码
    │
    ├── 生成 12 字节随机 IV
    ├── AES-256-GCM 加密 → 密文 + 16 字节认证标签
    └── Base64(IV || Tag || 密文) → 存入数据库
```

### 文件与权限

- 密钥文件自动设 `0600`（仅所有者读写），异常权限自动修复
- 数据目录 `~/.passmanager/` 自动设 `0700`
- 密钥和数据库文件均在 `.gitignore` 中排除，不会被提交

### CLI 模式安全

- 推荐使用 `--password-stdin` / `--verify-stdin` 通过管道传入密码，避免暴露在进程列表和 shell 历史中
- `--password` 参数会明文暴露在 `ps` 和 shell history 中，非安全环境谨慎使用

## 项目结构

```
pass_manager/
├── CMakeLists.txt          # CMake 构建配置 (C++17, OpenSSL, SQLite3)
├── .gitignore              # 排除密钥、数据库、构建产物
├── key/                    # 项目内默认密钥目录（.gitkeep 保留）
│   └── .gitkeep
└── src/
    ├── main.cpp            # 入口：参数解析、路径管理、初始化
    ├── crypto.h / .cpp     # AES-256-GCM 加解密、Base64 编解码、密钥派生
    ├── database.h / .cpp   # SQLite 操作：CRUD、模糊搜索、表结构管理
    └── cli.h / .cpp        # TUI 交互菜单 + CLI 非交互操作、实时搜索
```

## 数据目录

默认数据存储于 `~/.passmanager/`：

```
~/.passmanager/
├── default.key             # AES 密钥文件 (0600)
└── passwords.db            # SQLite 数据库
```

可通过 `-k` / `-d` 参数指定不同路径。

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

## 许可

本项目仅用于学习和个人用途。
