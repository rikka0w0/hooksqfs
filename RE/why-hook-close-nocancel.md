# Why Hook `__close_nocancel`

## 背景现象

启动 `./srcds_run +map l4d2_tanksplayground` 后，地图本身能从 squashfs 提供的 VPK 中加载，但随后大量游戏自带地图被报成无效：

```text
CModelLoader::Map_IsValid: 'maps/c1m1_hotel.bsp' is not a valid BSP file
CModelLoader::Map_IsValid: 'maps/c1m2_streets.bsp' is not a valid BSP file
...
```

这类报错不像是“文件不存在”。如果官方 VPK 找不到，一般会更早出现路径查找失败；这里是文件被打开并读取后，内容校验不对。因此排查重点从路径映射转到了 fd 状态和读偏移是否被污染。

## 关键日志线索

打开高频 hook 日志后，最重要的线索是同一个 fd 被反复作为 sqfs 文件返回，但看不到对应的 `sqfs_close`：

```text
[hooklog] sqfs_open: ok: relative_path="miku hatsune replace witch.vpk", flags=0x0, errno=0 => fd=22
[hooklog] sqfs_read: ok: fd=22, count=4096@0, inode=1: ret=4096

[hooklog] sqfs_open: ok: relative_path="tanksplayground.vpk", flags=0x0, errno=0 => fd=22
[hooklog] sqfs_read: ok: fd=22, count=4096@0, inode=2: ret=4096

[hooklog] sqfs_open: ok: relative_path="tanksplayground.vpk", flags=0x0, errno=0 => fd=22
...
```

正常情况下，如果 fd 22 被关闭过，`sqfs_close` 应该从 `fd_map` 删除这条记录。实际日志没有删除记录，说明目标程序关闭 fd 时没有走我们当前 hook 的 `close` 入口。

这会导致一个典型的 stale fd bug：

1. `sqfs_open` 为 `tanksplayground.vpk` 创建一个 backing fd，例如 fd 22。
2. 目标程序关闭 fd 22，但关闭路径没有经过 `sqfs_close`，所以 `g_xSqfsMgr.fd_map` 仍保留 `22 -> tanksplayground.vpk inode`。
3. 内核稍后把 fd 22 复用给真实文件，例如官方 VPK 或其内部 BSP 检查路径。
4. 我们的 `sqfs_read` / `sqfs_lseek64` 只按 fd 查表，看到 fd 22 仍在 `fd_map` 中，就错误地从 squashfs 的 `tanksplayground.vpk` 读数据。
5. 上层拿到错误内容，自然把官方 `c1m1_hotel.bsp` 等判断成 invalid BSP。

## 为什么想到 `__close_nocancel`

确认“fd 关闭绕过了 `close` hook”后，下一步是看 32 位 glibc 还有哪些 close 入口。对目标环境的 libc 查符号：

```bash
nm -D /usr/lib/i386-linux-gnu/libc.so.6 | rg ' close|__close|nocancel'
```

能看到：

```text
00116a20 T __close@@GLIBC_2.0
001232c0 T __close_nocancel@@GLIBC_PRIVATE
00123310 T __close_nocancel_nostatus@@GLIBC_PRIVATE
00116a20 W close@@GLIBC_2.0
```

`close` 和 `__close` 是同一个入口，但 glibc 内部 I/O 路径经常会调用 `__close_nocancel`，例如 `fclose` 或其他不可取消区间里的关闭操作。由于本项目使用 trampoline hook 修改 libc 函数入口，hook `close` 并不自动覆盖另一个独立函数入口 `__close_nocancel`。

所以推论是：srcds 或 glibc 内部关闭 VPK fd 时走了 `__close_nocancel`，没有走 `close`。

## 修复验证

加入 `__close_nocancel` hook 后，日志立刻出现了之前缺失的 fd 清理：

```text
[hooklog] sqfs_open: ok: relative_path="miku hatsune replace witch.vpk", flags=0x0, errno=0 => fd=22
[hooklog] sqfs_close_nocancel: fd=22, inode=1, ret=0, errno=0

[hooklog] sqfs_open: ok: relative_path="tanksplayground.vpk", flags=0x0, errno=0 => fd=22
[hooklog] sqfs_close_nocancel: fd=22, inode=2, ret=0, errno=0
```

这证明之前 fd 确实是通过 `__close_nocancel` 关闭的。修复后再次启动：

```bash
./srcds_run +map l4d2_tanksplayground
```

结果：

- `l4d2_tanksplayground` 可以启动。
- 原先大量官方 `*.bsp is not a valid BSP file` 消失。
- 在运行中的 server 输入 `changelevel c1m1_hotel` 后成功切换到官方地图，并看到：

```text
L 06/22/2026 - 02:48:17: -------- Mapchange to c1m1_hotel --------
Commentary: Loading commentary data from maps/c1m1_hotel_commentary.txt.
```

## 结论

问题不是 VPK 解析或 sqfs 读取本身坏了，而是 fd 生命周期没有被完整跟踪。只 hook `close` 不足以覆盖 glibc 内部关闭路径，导致 `fd_map` 出现 stale entry。补 hook `__close_nocancel` 后，sqfs fd 能被及时从 map 删除，后续真实 fd 复用不会再被误判成 sqfs fd。
