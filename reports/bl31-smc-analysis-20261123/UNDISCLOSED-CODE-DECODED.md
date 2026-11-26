# "未公开代码"完全逆向 — 2026-11-24

## 三层固件结构（全部静态解出）

```
EL3: BL31 v2.3 (0x40000, 198KB)
 ├─ hdmirx 表加载器 (0x5a6xx-0x5aaac):
 │   1. [x19+3920] = EDID 共享缓冲指针(内核经 INFO_NOTIFY 写入)
 │   2. 校验魔数 0x4b534541("AESK") + 长度 + 校验和(bl 0x54b98)
 │   3. 解析 EDID CTA 块 → 0x84000+0x42c/0x43c 两个 64B 缓冲
 │   4. 按模式索引选 314B 参数表 (0x8445c + mode*314)
 │   5. bl 0x556a8: 块写辅助 ×3 (源/目标含 atf-2 数据段 0xff104cec)
 │   6. SYS GRF 0xfd586054 ← 0x40000000 (PHY 访问门控开)
 │   7. 逐字节写表: 0xfdf10020+i*4 ← table[i] (314 次)
 │   8. SYS GRF 0xfd586054 ← 0x40004000 (门控切)
 │   9. 0xfdee0024 |= 0x400 ( PHY 配置使能)
 │  10. 0xfdee02a0 ← 模式值
 ├─ atf-2 固件 (0xff100000, 24KB ARM64): FIQ 调试器串口后端
 │   (入口读 [x5+8/16]→blr; deadbeaf 魔数; UART 打印/延时/hex 工具;
 │    数据段 0x2000/0x4cec; EDID 解析结果经 0xff104cec 传入)
 └─ atf-3 (0xf0000, 24KB): 数据/参数块(待细分)
```

## 关键发现
1. **0xfdf10000 = HDMIRX PHY 独立寄存器文件**——主线 DT/驱动完全没映射这个区域！
2. **PHY 寄存器表 = 运行时由 BL31 从 EDID+内置参数生成**，按模式写入 PHY 寄存器文件
3. 表数据文件态为 BSS 全零——运行时由 atf-2 参与填充；真正的调谐参数
   在 BL31 .rodata: 0x28414-0x29427（348+1020×3+692 = 4116 字节，熵 8.0）
4. SYS GRF 0xfd586054 门控 = PHY 寄存器文件的访问开关（主线无人设置）

## 主线驱动复刻清单（ 使 snps/vendor 驱动解锁锁定）
1. ioremap 0xfdf10000 (PHY 寄存器文件, DT 加第二个 reg 或驱动内硬编码)
2. ioremap/reach 0xfd586054 (SYS GRF 门控——注意 vendor 用 grf regmap 不同区)
3. 实现: 门控开→写表→门控切→0xfdee0024|=0x400→0xfdee02a0=mode
4. 表内容来源: phy-tables/phy-param-tables.bin (4116B) 解析
   （或运行时从 Android dump 验证）
5. EDID 变更时重选表（表索引=模式映射, 反汇编 0x5a93c-0x5a944 的分支）

## 文件
- phy-tables/phy-param-tables.bin — 提取的 4116B 参数表
- atf1-vendor-v2.3.verified — 正确 carve 的 vendor BL31（头 69800094=bl 指令）
- atf2-firmware.bin — FIQ 调试器固件
