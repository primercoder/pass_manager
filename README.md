# pass_manager

基于 C++17 的命令行密码管理器，使用 AES-256-GCM 加密和 SQLite 本地存储。

## 功能特性

- **AES-256-GCM 认证加密**：每个密码使用独立的随机 IV，密钥文件经 SHA-256 派生，防篡改、防重放
- **本地 SQLite 存储**：WAL 模式，密码以 Base64 密文存储，明文从不落盘
- **隐藏密码输入**：终端内回显遮蔽（termios），二次确认防止输入错误
- **删除双重验证**：移除密码时必须输入原密码明文，与解密结果比对一致才允许删除
- **模糊搜索匹配**：输入密码名称片段，自动列出匹配项供选择，支持唯一匹配直接展示
- **剪贴板复制**：通过管道安全写入 xclip / wl-copy，无论是否显示明文都会自动复制
- **时间戳记录**：每条密码自动记录创建时间和最近修改时间
- **密钥文件管理**：首次运行自动生成 64 字节随机密钥，0600 权限保护；支持自定义密钥路径
- **安全加固**：解密后内存缓冲区即时清理，密钥文件权限异常自动修复

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

```bash
./build/pass_manager [OPTIONS]
```

### 命令行参数

| 参数 | 说明 |
|------|------|
| `-k, --key <path>` | 指定密钥文件路径（默认 `./key/default.key`） |
| `-d, --db <path>` | 指定数据库文件路径（默认 `./passwords.db`） |
| `-h, --help` | 显示帮助信息 |

### 交互菜单

```
╔══════════════════════════════════╗
║        PASSWORD MANAGER          ║
╚══════════════════════════════════╝

  1. Add Password        (新增密码)
  2. Remove Password     (移除密码)
  3. Show Password       (查询密码)
  4. List All Passwords  (密码列表)
  5. Password Count      (密码数量)
  6. Change Key File     (更换密钥)
  0. Exit                (退出)

Choice:
```

## 操作说明

### 新增密码

选择 `1`，依次输入：

- **Name**（必填）：密码唯一标识，不可重复
- **Description**（选填）：密码用途说明
- **Account**（选填）：关联的账户名
- **Password**（必填，隐藏输入，二次确认）：密码明文

### 查询密码

选择 `3`，输入名称片段进行模糊搜索：

- 仅一个匹配结果时直接展示详情
- 多个匹配结果时列出表格供用户选择
- 展示详情后询问是否显示明文密码
- **无论是否显示明文，密码都会自动复制到剪贴板**

详情展示内容包括：名称、账户、描述、创建时间、更新时间。

### 移除密码

选择 `2`，输入密码名称和该密码的明文进行验证：

- 明文不匹配则拒绝删除
- 确保操作者确实知晓该密码，防止误删

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

### 文件权限

- 密钥文件自动设置为 `0600`（仅所有者可读写）
- 启动时检测密钥文件权限，异常时自动修复
- 密钥文件和数据库均在 `.gitignore` 中排除

### 内存安全

- 解密后的密码在使用完毕后可以及时丢弃（栈上字符串离开作用域即销毁）
- 密码输入缓冲区通过 `getline` 管理，不涉及固定大小的不安全缓冲区

## 项目结构

```
pass_manager/
├── CMakeLists.txt          # CMake 构建配置 (C++17, OpenSSL, SQLite3)
├── .gitignore              # 排除密钥、数据库、构建产物
├── key/                    # 默认密钥目录
│   └── .gitkeep
└── src/
    ├── main.cpp            # 入口：参数解析、初始化、安全检测
    ├── crypto.h / .cpp     # AES-256-GCM 加解密、Base64 编解码、密钥派生
    ├── database.h / .cpp   # SQLite 操作：CRUD、模糊搜索、表结构管理
    └── cli.h / .cpp        # 交互式 CLI：菜单导航、隐藏输入、剪贴板
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

## 许可

本项目仅用于学习和个人用途。
